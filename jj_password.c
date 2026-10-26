#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

#include "jj_password.h"

static volatile sig_atomic_t password_signal;

static void
wipe_password(char *buffer, size_t size)
{
    volatile unsigned char *p = (volatile unsigned char *)buffer;

    while (size-- != 0)
        *p++ = 0;
}

static void
catch_password_signal(int signo)
{
    if (password_signal == 0)
        password_signal = signo;
}

static bool
read_password_line(int fd, char *buffer, size_t size,
    const volatile sig_atomic_t *interrupted, const sigset_t *waitmask)
{
    size_t length = 0;
    unsigned char ch;
    ssize_t count;
    int failure = 0;
    fd_set readable;

    if (buffer == NULL || size == 0) {
        errno = EINVAL;
        return false;
    }
    wipe_password(buffer, size);
    for (;;) {
        if (interrupted != NULL && *interrupted != 0) {
            failure = EINTR;
            break;
        }
        if (waitmask != NULL) {
            if (fd < 0 || fd >= FD_SETSIZE) {
                failure = EMFILE;
                break;
            }
            FD_ZERO(&readable);
            FD_SET(fd, &readable);
            /* Atomically unblock prompt signals while waiting, avoiding a
             * signal arriving between the flag check and a blocking read. */
            if (pselect(fd + 1, &readable, NULL, NULL, NULL, waitmask) < 0) {
                if (errno == EINTR)
                    continue;
                failure = errno;
                break;
            }
        }
        count = read(fd, &ch, 1);
        if (count < 0) {
            if (errno == EINTR)
                continue;
            failure = errno;
            break;
        }
        if (count == 0 || ch == '\n') {
            if (length != 0 && buffer[length - 1] == '\r')
                --length;
            if (length >= size) {
                failure = EMSGSIZE;
                break;
            }
            buffer[length] = '\0';
            return true;
        }
        if (ch == '\0') {
            failure = EINVAL;
            break;
        }
        /* The spare byte permits a maximum-length password followed by CRLF. */
        if (length == size) {
            failure = EMSGSIZE;
            break;
        }
        buffer[length++] = (char)ch;
    }
    wipe_password(buffer, size);
    errno = failure;
    return false;
}

bool
jj_read_password_fd(int fd, char *buffer, size_t size)
{
    return read_password_line(fd, buffer, size, NULL, NULL);
}

static bool
write_prompt(int fd, const char *text, size_t length)
{
    ssize_t count;

    while (length != 0) {
        if (password_signal != 0) {
            errno = EINTR;
            return false;
        }
        count = write(fd, text, length);
        if (count < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        if (count == 0) {
            errno = EIO;
            return false;
        }
        text += count;
        length -= (size_t)count;
    }
    return true;
}

bool
jj_prompt_password(char *buffer, size_t size, char *error, size_t error_size)
{
    static const int signals[] = {
        SIGINT, SIGQUIT, SIGHUP, SIGTERM, SIGALRM, SIGTSTP, SIGTTIN, SIGTTOU
    };
    struct sigaction saved[sizeof(signals) / sizeof(signals[0])];
    struct sigaction action;
    struct termios original, hidden;
    sigset_t blocked, oldmask;
    size_t installed = 0, i;
    int fd = -1, failure = 0, signo = 0;
    bool changed = false, masked = false, success = false;
    const char *message = "Unable to read password from terminal";

    if (error != NULL && error_size != 0)
        error[0] = '\0';
    if (buffer == NULL || size == 0) {
        failure = EINVAL;
        goto done;
    }
    wipe_password(buffer, size);
    fd = open("/dev/tty", O_RDWR | O_NOCTTY);
    if (fd < 0) {
        failure = errno;
        message = "Cannot open /dev/tty; use --password-fd or -p";
        goto done;
    }
    if (tcgetattr(fd, &original) != 0) {
        failure = errno;
        goto done;
    }
    sigemptyset(&blocked);
    for (i = 0; i < sizeof(signals) / sizeof(signals[0]); ++i)
        sigaddset(&blocked, signals[i]);
    if (sigprocmask(SIG_BLOCK, &blocked, &oldmask) != 0) {
        failure = errno;
        goto done;
    }
    masked = true;
    password_signal = 0;
    action.sa_handler = catch_password_signal;
    action.sa_mask = blocked;
    action.sa_flags = 0;
    for (i = 0; i < sizeof(signals) / sizeof(signals[0]); ++i) {
        if (sigaction(signals[i], &action, &saved[i]) != 0) {
            failure = errno;
            goto cleanup;
        }
        ++installed;
    }
    hidden = original;
    hidden.c_lflag &= (tcflag_t)~(ECHO | ECHONL);
    if (tcsetattr(fd, TCSAFLUSH, &hidden) != 0) {
        failure = errno;
        goto cleanup;
    }
    changed = true;
    if (write_prompt(fd, "Password: ", sizeof("Password: ") - 1))
        success = read_password_line(fd, buffer, size, &password_signal,
            &oldmask);
    if (!success)
        failure = errno;

cleanup:
    if (masked) {
        if (sigprocmask(SIG_BLOCK, &blocked, NULL) != 0) {
            failure = errno;
            success = false;
        }
        signo = password_signal;
        if (changed) {
            /* Discard an interrupted or rejected password before returning
             * control to the caller or shell. Preserve queued input on success. */
            if ((!success || signo != 0) && tcflush(fd, TCIFLUSH) != 0) {
                failure = errno;
                success = false;
            }
            if (tcsetattr(fd, TCSANOW, &original) != 0) {
                failure = errno;
                success = false;
                message = "Unable to restore terminal settings";
            }
            /* A failed newline is still an I/O failure, even after a good read. */
            if (write(fd, "\n", 1) != 1 && success) {
                failure = errno != 0 ? errno : EIO;
                success = false;
            }
        }
        while (installed != 0) {
            --installed;
            if (sigaction(signals[installed], &saved[installed], NULL) != 0) {
                failure = errno;
                success = false;
            }
        }
    }
done:
    if (fd >= 0 && close(fd) != 0 && success) {
        failure = errno;
        success = false;
    }
    if (signo != 0) {
        success = false;
        failure = EINTR;
    }
    if (!success) {
        if (buffer != NULL)
            wipe_password(buffer, size);
        if (error != NULL && error_size != 0)
            snprintf(error, error_size, "%s", message);
    }
    if (masked && sigprocmask(SIG_SETMASK, &oldmask, NULL) != 0) {
        failure = errno;
        success = false;
        if (buffer != NULL)
            wipe_password(buffer, size);
        if (error != NULL && error_size != 0)
            snprintf(error, error_size, "Unable to restore signal mask");
    }
    if (signo != 0)
        raise(signo);
    if (!success)
        errno = failure;
    return success;
}
