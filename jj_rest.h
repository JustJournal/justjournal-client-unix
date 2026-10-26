#ifndef JJ_REST_H
#define JJ_REST_H

#include <stdbool.h>
#include <stddef.h>
#include <jansson.h>

#define JJ_VERSION "2.2.0"
#define JJ_BASE_URL "https://www.justjournal.com"

typedef struct jj_rest jj_rest;

jj_rest *jj_rest_open(const char *base_url, const char *agent,
    char *error, size_t error_size);
void jj_rest_close(jj_rest *client);
bool jj_rest_login(jj_rest *client, const char *username, const char *password,
    char *error, size_t error_size);
/* NULL metadata preserves legacy music-tag handling and an empty tag list. */
bool jj_rest_post(jj_rest *client, const char *subject, const char *body,
    const char *music, json_t *tags, char *error, size_t error_size);
json_t *jj_rest_recent(jj_rest *client, const char *username, size_t count,
    char *error, size_t error_size);

#endif
