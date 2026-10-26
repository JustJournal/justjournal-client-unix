#ifndef JJ_REST_H
#define JJ_REST_H

#include <stdbool.h>
#include <stddef.h>
#include <jansson.h>

#define JJ_VERSION "2.2.0"
#define JJ_BASE_URL "https://www.justjournal.com"

typedef struct jj_rest jj_rest;

typedef struct jj_entry_options {
    int security; /* private 0, friends 1, public 2 */
    const char *format; /* TEXT, MARKDOWN or HTML */
    const char *date; /* Validated ISO 8601 timestamp, or NULL for server time. */
} jj_entry_options;

#define JJ_ENTRY_OPTIONS_INIT {2, "TEXT", NULL}

jj_rest *jj_rest_open(const char *base_url, const char *agent,
    char *error, size_t error_size);
void jj_rest_close(jj_rest *client);
bool jj_rest_login(jj_rest *client, const char *username, const char *password,
    char *error, size_t error_size);
/* NULL metadata preserves legacy music-tag handling and an empty tag list. */
bool jj_rest_post(jj_rest *client, const char *subject, const char *body,
    const char *music, json_t *tags, char *error, size_t error_size);
/* NULL options preserves the public/text defaults. Non-text bodies are literal. */
bool jj_rest_post_with_options(jj_rest *client, const char *subject, const char *body,
    const char *music, json_t *tags, const jj_entry_options *options,
    char *error, size_t error_size);
json_t *jj_rest_recent(jj_rest *client, const char *username, size_t count,
    char *error, size_t error_size);

#endif
