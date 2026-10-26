#ifndef JJ_PASSWORD_H
#define JJ_PASSWORD_H

#include <stdbool.h>
#include <stddef.h>

/* Read one line without reading ahead or closing fd. Strip LF and one CR.
 * EOF terminates a line. Reject NUL and lines that cannot fit including NUL.
 * On failure, wipe the entire supplied buffer and set errno. */
bool jj_read_password_fd(int fd, char *buffer, size_t size);

/* Read from /dev/tty with echo disabled, restoring terminal state on exit.
 * Handled interrupt/termination/job-control signals are re-raised after cleanup.
 * On failure, wipe buffer and provide a credential-free diagnostic. */
bool jj_prompt_password(char *buffer, size_t size, char *error,
    size_t error_size);

#endif
