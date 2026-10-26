/*-
Copyright (C) 2008, 2009, 2016, 2025 Lucas Holt.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
SUCH DAMAGE.
*/

/* For Linux */
#if defined(__linux__) && !defined(_GNU_SOURCE)
    #if __GLIBC__ >= 2 && __GLIBC_MINOR__ >= 19
        #define _DEFAULT_SOURCE
    #else
        #define _BSD_SOURCE
    #endif
#endif
#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <ctype.h>
#include <netdb.h>
#include <sys/socket.h>
#include <regex.h>

#include "jj_rest.h"

/* Portable secure memory zeroing */
#include <strings.h>
#if defined(HAVE_EXPLICIT_BZERO)
#define secure_bzero explicit_bzero
#else
/* Fallback for platforms without explicit_bzero (e.g., musl, older glibc, macOS) */
static inline void secure_bzero(void *s, size_t n) {
    memset(s, 0, n);
    /* Compiler barrier to prevent optimization */
    __asm__ __volatile__("" : : "r"(s), "r"(n) : "memory");
}
#endif

#define NAME "JustJournal/UNIX"
#define VERSION JJ_VERSION
#define ENTRY_MAX 32000
#define USERNAME_MIN_LENGTH 3
#define USERNAME_MAX_LENGTH 50
#define PASSWORD_MIN_LENGTH 6
#define PASSWORD_MAX_LENGTH 30
#define USERLEN USERNAME_MAX_LENGTH + 1
#define PASSLEN PASSWORD_MAX_LENGTH + 1
#define HOSTLEN 512
#define MAX_HOST_NAME (HOSTLEN - 17)
#define TITLELEN 256
#define HOST "www.justjournal.com"
#define RECENT_POST_COUNT 15

static void usage(const char *name);
static bool getRecentPosts(jj_rest *client, const char *username, char *error, size_t error_size);

static bool is_valid_hostname(const char *hostname);
static bool is_valid_username(const char *input);
static bool is_valid_password(const char *input);

int main(int argc, char *argv[])
{
    jj_rest *client;
    char error[256];
    bool success;
    char username[USERLEN] = {'\0'};
    char password[PASSLEN] = {'\0'};
    char entry[ENTRY_MAX] = {'\0'};
    char host[HOSTLEN] = {'\0'};
    char title[TITLELEN] = {'\0'};
    int c;
    size_t i;
    bool debug = false;
    bool hflag = false;
    bool uflag = false;
    bool pflag = false;
    bool rflag = false;

    if (argc < 3)
        usage(argv[0]);

    while ((c = getopt(argc, argv, "h:u:p:s:dr")) != -1)
    {
        switch (c)
        {
        case 'h': /* host */
            if (optarg == NULL)
            {
                fprintf(stderr, "Error: Host option requires an argument\n");
                exit(EXIT_FAILURE);
            }
            if (strlen(optarg) >= MAX_HOST_NAME)
            {
                fprintf(stderr, "Error: Host argument is too long\n");
                exit(EXIT_FAILURE);
            }
            if (!is_valid_hostname(optarg))
            {
                fprintf(stderr, "Error: Invalid hostname\n");
                exit(EXIT_FAILURE);
            }

            (void)snprintf(host, sizeof(host), "https://%s", optarg);
            host[sizeof(host) - 1] = '\0';

            hflag = true;
            break;

        case 'u': /* username */
            if (optarg == NULL)
            {
                fprintf(stderr, "Error: username option requires an argument\n");
                exit(EXIT_FAILURE);
            }
            if (strlen(optarg) >= USERLEN)
            {
                fprintf(stderr, "Error: Username is too long\n");
                exit(EXIT_FAILURE);
            }
            (void)strncpy(username, optarg, USERLEN - 1);
            username[USERLEN - 1] = '\0';
            uflag = true;
            break;

        case 'p': /* password */
            if (optarg == NULL)
            {
                fprintf(stderr, "Error: password option requires an argument\n");
                exit(EXIT_FAILURE);
            }
            if (strlen(optarg) >= PASSLEN)
            {
                fprintf(stderr, "Error: Password is too long\n");
                exit(EXIT_FAILURE);
            }
            (void)strncpy(password, optarg, PASSLEN - 1);
            password[PASSLEN - 1] = '\0';
            pflag = true;
            break;

        case 's': /* subject */
            if (optarg == NULL)
            {
                fprintf(stderr, "Error: Subject option requires an argument\n");
                exit(EXIT_FAILURE);
            }
            if (strlen(optarg) >= TITLELEN)
            {
                fprintf(stderr, "Error: Subject is too long\n");
                exit(EXIT_FAILURE);
            }
            (void)snprintf(title, sizeof(title), "%s", optarg);
            break;

        case 'r': /* recent entries */
            rflag = true;
            break;

        case 'd': /* debug */
            debug = true;
            fprintf(stderr, "Debug enabled.\n");
            break;

        case '?': /* fall through */
        default:
            usage(argv[0]);
        }
    }
    argc -= optind;

    if (!uflag || !pflag)
    {
        fprintf(stderr, "Username and password are required.\n");
        usage(argv[0]);
    }

    /* set host if it's not defined */
    if (!hflag)
        (void)snprintf(host, HOSTLEN, "https://%s", HOST);

    if (debug && hflag)
        fprintf(stderr, "host is set to: %s\n", host);

    if (!is_valid_username(username)) {
        fprintf(stderr, "Error: Invalid username\n");
        exit(EXIT_FAILURE);
    }
    
    if (!is_valid_password(password)) {
        fprintf(stderr, "Error: Invalid password\n");
        exit(EXIT_FAILURE);
    }

    if (rflag)
    {
        client = jj_rest_open(host, NAME "/" VERSION, error, sizeof(error));
        success = client && jj_rest_login(client, username, password, error, sizeof(error));
        secure_bzero(password, sizeof(password));
        if (success)
            success = getRecentPosts(client, username, error, sizeof(error));
        jj_rest_close(client);
        if (!success)
            fprintf(stderr, "ERROR: %s\n", error);
        return success ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    /* Read the body separately from the REST subject field. */
    for (i = 0; i < ENTRY_MAX - 1; i++)
    {
        c = getchar();
        if (c == EOF)
            break;
        entry[i] = (char)c;
    }
    entry[i] = '\0';

    if (i == 0)
    {
        fprintf(stderr, "No entry specified.");
        exit(EXIT_FAILURE);
    }

    if ((i == ENTRY_MAX - 1 && getchar() != EOF) || ferror(stdin))
    {
        fprintf(stderr, "Error: Cannot read entry or entry is too long.\n");
        secure_bzero(password, sizeof(password));
        return EXIT_FAILURE;
    }

    client = jj_rest_open(host, NAME "/" VERSION, error, sizeof(error));
    success = client && jj_rest_login(client, username, password, error, sizeof(error));
    secure_bzero(password, sizeof(password));
    if (success)
        success = jj_rest_post(client, title, entry, error, sizeof(error));
    jj_rest_close(client);
    if (!success)
        fprintf(stderr, "ERROR: %s\n", error);
    else if (debug)
        fprintf(stderr, "Debug: entry saved.\n");
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}

static void usage(const char *name)
{
    fprintf(stderr, "jjclient %s\nusage: %s -u USERNAME -p PASSWORD [-h host] [-s subject] [-r] [-d]\n", VERSION, name);
    exit(EXIT_FAILURE);
}

static bool getRecentPosts(jj_rest *client, const char *username, char *error, size_t error_size)
{
    json_t *entries = jj_rest_recent(client, username, RECENT_POST_COUNT, error, error_size);
    if (!entries)
        return false;
    for (size_t i = 0; i < json_array_size(entries); i++)
    {
        json_t *entry = json_array_get(entries, i);
        printf("%zu %s\n\n%s\n\n", i,
            json_string_value(json_object_get(entry, "subject")),
            json_string_value(json_object_get(entry, "body")));
    }
    json_decref(entries);
    return true;
}

static bool is_valid_username(const char *input) {
    if (strlen(input) < USERNAME_MIN_LENGTH || strlen(input) > USERNAME_MAX_LENGTH) {
        return false;
    }

    regex_t regex;
    int reti;
    char msgbuf[100];

    reti = regcomp(&regex, "^[A-Za-z0-9_]+$", REG_EXTENDED);
    if (reti) {
        fprintf(stderr, "Could not compile regex\n");
        exit(EXIT_FAILURE);
    }

    reti = regexec(&regex, input, 0, NULL, 0);
    if (!reti) {
        regfree(&regex);
        return true;
    }
    else if (reti == REG_NOMATCH) {
        regfree(&regex);
        return false;
    }
    else {
        regerror(reti, &regex, msgbuf, sizeof(msgbuf));
        fprintf(stderr, "Regex match failed: %s\n", msgbuf);
        regfree(&regex);
        exit(EXIT_FAILURE);
    }
}

static bool is_valid_password(const char *input) {
    if (strlen(input) < PASSWORD_MIN_LENGTH || strlen(input) > PASSWORD_MAX_LENGTH) {
        return false;
    }

    regex_t regex;
    int reti;
    char msgbuf[100];

    reti = regcomp(&regex, "^[A-Za-z0-9_@.!&*#$?^ ]+$", REG_EXTENDED);
    if (reti) {
        fprintf(stderr, "Could not compile regex\n");
        exit(EXIT_FAILURE);
    }

    reti = regexec(&regex, input, 0, NULL, 0);
    if (!reti) {
        regfree(&regex);
        return true;
    }
    else if (reti == REG_NOMATCH) {
        regfree(&regex);
        return false;
    }
    else {
        regerror(reti, &regex, msgbuf, sizeof(msgbuf));
        fprintf(stderr, "Regex match failed: %s\n", msgbuf);
        regfree(&regex);
        exit(EXIT_FAILURE);
    }
}

static bool is_valid_hostname(const char *hostname) {
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    
    int status = getaddrinfo(hostname, NULL, &hints, &res);
    if (status != 0) {
        return false;
    }
    
    freeaddrinfo(res);
    return true;
}
