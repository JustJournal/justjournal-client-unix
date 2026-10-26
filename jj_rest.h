#ifndef JJ_REST_H
#define JJ_REST_H

#include <stdbool.h>
#include <stddef.h>
#include <jansson.h>

#define JJ_VERSION "2.1.0"
#define JJ_BASE_URL "https://www.justjournal.com"

typedef struct jj_rest jj_rest;

jj_rest *jj_rest_open(const char *base_url, const char *agent,
    char *error, size_t error_size);
void jj_rest_close(jj_rest *client);
bool jj_rest_login(jj_rest *client, const char *username, const char *password,
    char *error, size_t error_size);
bool jj_rest_post(jj_rest *client, const char *subject, const char *body,
    char *error, size_t error_size);
json_t *jj_rest_recent(jj_rest *client, const char *username, size_t count,
    char *error, size_t error_size);

#endif
