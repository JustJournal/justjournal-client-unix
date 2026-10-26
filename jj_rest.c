#include "jj_rest.h"

#include <curl/curl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RESPONSE_MAX (8 * 1024 * 1024)

struct jj_rest {
    CURL *curl;
    struct curl_slist *headers;
    char *base_url;
};

struct response {
    char *data;
    size_t size;
};

static void wipe(char *data, size_t size)
{
    volatile unsigned char *p = (volatile unsigned char *)data;
    while (size--)
        *p++ = 0;
}

static size_t receive(void *data, size_t size, size_t count, void *context)
{
    struct response *response = context;
    char *buffer;
    size_t length;

    if (size != 0 && count > SIZE_MAX / size)
        return 0;
    length = size * count;
    if (length > RESPONSE_MAX - response->size)
        return 0;
    buffer = realloc(response->data, response->size + length + 1);
    if (!buffer)
        return 0;
    response->data = buffer;
    memcpy(buffer + response->size, data, length);
    response->size += length;
    buffer[response->size] = '\0';
    return length;
}

void jj_rest_close(jj_rest *client)
{
    if (!client)
        return;
    curl_easy_cleanup(client->curl);
    curl_slist_free_all(client->headers);
    free(client->base_url);
    free(client);
    curl_global_cleanup();
}

jj_rest *jj_rest_open(const char *base_url, const char *agent,
    char *error, size_t error_size)
{
    jj_rest *client;

    if (strncmp(base_url, "https://", 8) != 0) {
        snprintf(error, error_size, "HTTPS is required.");
        return NULL;
    }
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        snprintf(error, error_size, "Unable to initialize HTTPS support.");
        return NULL;
    }
    client = calloc(1, sizeof(*client));
    if (!client) {
        curl_global_cleanup();
        goto failure;
    }
    client->base_url = malloc(strlen(base_url) + 1);
    client->curl = curl_easy_init();
    /* LoginController explicitly requires a wildcard Accept header. */
    client->headers = curl_slist_append(NULL, "Accept: */*");
    if (!client->base_url || !client->curl || !client->headers)
        goto cleanup;
    strcpy(client->base_url, base_url);
    {
        struct curl_slist *headers = curl_slist_append(client->headers,
            "Content-Type: application/json");
        if (!headers)
            goto cleanup;
        client->headers = headers;
    }

#define SETOPT(option, value) \
    if (curl_easy_setopt(client->curl, option, value) != CURLE_OK) goto cleanup
    SETOPT(CURLOPT_HTTPHEADER, client->headers);
    SETOPT(CURLOPT_USERAGENT, agent);
    SETOPT(CURLOPT_COOKIEFILE, ""); /* Memory only; never persist the session. */
    SETOPT(CURLOPT_CONNECTTIMEOUT, 15L);
    SETOPT(CURLOPT_TIMEOUT, 60L);
    SETOPT(CURLOPT_NOSIGNAL, 1L);
    SETOPT(CURLOPT_WRITEFUNCTION, receive);
    /* Keep TLS verification enabled and reject redirects, including login pages. */
    SETOPT(CURLOPT_FOLLOWLOCATION, 0L);
#undef SETOPT
    return client;

cleanup:
    jj_rest_close(client);
failure:
    snprintf(error, error_size, "Unable to initialize REST client.");
    return NULL;
}

/* The caller owns body and erases it after this function stops using it. */
static json_t *request_body(jj_rest *client, const char *path, const char *body,
    char *error, size_t error_size)
{
    struct response response = {NULL, 0};
    char *url = NULL;
    json_t *result = NULL;
    long status = 0;
    CURLcode code;
    const char *operation = strcmp(path, "/api/login") == 0 ? "Login" :
        body ? "Saving entry" : "Loading entries";

    snprintf(error, error_size, "%s failed: unable to prepare REST request.", operation);
    url = malloc(strlen(client->base_url) + strlen(path) + 1);
    if (!url)
        goto cleanup;
    strcpy(url, client->base_url);
    strcat(url, path);
    if (body && strlen(body) > LONG_MAX) {
        snprintf(error, error_size, "Request body is too large.");
        goto cleanup;
    }
    if (curl_easy_setopt(client->curl, CURLOPT_URL, url) != CURLE_OK ||
        curl_easy_setopt(client->curl, CURLOPT_WRITEDATA, &response) != CURLE_OK ||
        curl_easy_setopt(client->curl, CURLOPT_POSTFIELDS, body) != CURLE_OK ||
        curl_easy_setopt(client->curl, CURLOPT_POSTFIELDSIZE,
            body ? (long)strlen(body) : 0L) != CURLE_OK ||
        curl_easy_setopt(client->curl, CURLOPT_HTTPGET, body ? 0L : 1L) != CURLE_OK)
        goto cleanup;

    code = curl_easy_perform(client->curl);
    if (code != CURLE_OK) {
        snprintf(error, error_size, "%s failed: %s", operation, curl_easy_strerror(code));
        goto cleanup;
    }
    if (curl_easy_getinfo(client->curl, CURLINFO_RESPONSE_CODE, &status) != CURLE_OK)
        goto cleanup;
    if (status < 200 || status >= 300) {
        if (status == 401 || status == 403)
            snprintf(error, error_size, "%s failed: authentication failed or session expired (HTTP %ld).", operation, status);
        else
            snprintf(error, error_size, "%s failed (HTTP %ld).", operation, status);
        goto cleanup;
    }
    result = json_loadb(response.data ? response.data : "", response.size,
        JSON_REJECT_DUPLICATES, NULL);
    if (!result)
        snprintf(error, error_size, "%s failed: server returned invalid JSON.", operation);

cleanup:
    /* Do not leave libcurl pointing into the login payload after it is erased. */
    curl_easy_setopt(client->curl, CURLOPT_POSTFIELDS, NULL);
    curl_easy_setopt(client->curl, CURLOPT_WRITEDATA, NULL);
    free(url);
    free(response.data);
    return result;
}

static json_t *request(jj_rest *client, const char *path, json_t *payload,
    char *error, size_t error_size)
{
    char *body = payload ? json_dumps(payload, JSON_COMPACT) : NULL;
    json_t *result;

    if (payload && !body) {
        snprintf(error, error_size, "Unable to encode request as UTF-8 JSON.");
        return NULL;
    }
    result = request_body(client, path, body, error, error_size);
    if (body) {
        wipe(body, strlen(body));
        free(body);
    }
    return result;
}

/* Write one JSON string directly, validating UTF-8 without allocating copies. */
static bool quote_credential(const char *value, char **destination)
{
    const unsigned char *p = (const unsigned char *)value;
    static const char hex[] = "0123456789abcdef";
    char *out = *destination;

    *out++ = '"';
    while (*p) {
        const unsigned char *start = p;
        unsigned char c = *p++;
        if (c == '"' || c == '\\') {
            *out++ = '\\';
            *out++ = (char)c;
        } else if (c < 0x20) {
            memcpy(out, "\\u00", 4);
            out += 4;
            *out++ = hex[c >> 4];
            *out++ = hex[c & 0xf];
        } else if (c < 0x80) {
            *out++ = (char)c;
        } else {
            unsigned int codepoint, minimum;
            size_t remaining;

            if (c >= 0xc2 && c <= 0xdf) {
                codepoint = c & 0x1f;
                minimum = 0x80;
                remaining = 1;
            } else if (c >= 0xe0 && c <= 0xef) {
                codepoint = c & 0x0f;
                minimum = 0x800;
                remaining = 2;
            } else if (c >= 0xf0 && c <= 0xf4) {
                codepoint = c & 0x07;
                minimum = 0x10000;
                remaining = 3;
            } else
                return false;
            while (remaining--) {
                if (*p < 0x80 || *p > 0xbf)
                    return false;
                codepoint = (codepoint << 6) | (*p++ & 0x3f);
            }
            if (codepoint < minimum || codepoint > 0x10ffff ||
                (codepoint >= 0xd800 && codepoint <= 0xdfff))
                return false;
            memcpy(out, start, (size_t)(p - start));
            out += p - start;
        }
    }
    *out++ = '"';
    *destination = out;
    return true;
}

static char *login_body(const char *username, const char *password, size_t *capacity)
{
    const size_t overhead = sizeof("{\"username\":\"\",\"password\":\"\"}");
    const size_t maximum = (SIZE_MAX - overhead) / 6;
    size_t username_size = strlen(username), password_size = strlen(password);
    char *body, *out;

    /* Every input byte needs at most six output bytes (a JSON \\u00XX escape). */
    if (username_size > maximum || password_size > maximum - username_size)
        return NULL;
    *capacity = overhead + 6 * (username_size + password_size);
    body = malloc(*capacity);
    if (!body)
        return NULL;
    memcpy(body, "{\"username\":", sizeof("{\"username\":") - 1);
    out = body + sizeof("{\"username\":") - 1;
    if (!quote_credential(username, &out))
        goto failure;
    memcpy(out, ",\"password\":", sizeof(",\"password\":") - 1);
    out += sizeof(",\"password\":") - 1;
    if (!quote_credential(password, &out))
        goto failure;
    *out++ = '}';
    *out = '\0';
    return body;

failure:
    wipe(body, *capacity);
    free(body);
    return NULL;
}

static bool string_is(json_t *value, const char *expected)
{
    return json_is_string(value) && strcmp(json_string_value(value), expected) == 0;
}

bool jj_rest_login(jj_rest *client, const char *username, const char *password,
    char *error, size_t error_size)
{
    size_t capacity = 0;
    char *body = login_body(username, password, &capacity);
    json_t *result;
    bool ok;

    if (!body) {
        snprintf(error, error_size, "Unable to encode credentials as UTF-8 JSON.");
        return false;
    }
    result = request_body(client, "/api/login", body, error, error_size);
    wipe(body, capacity);
    free(body);
    if (!result)
        return false;
    ok = string_is(json_object_get(result, "status"), "JJ.LOGIN.OK") &&
        string_is(json_object_get(result, "username"), username);
    if (!ok)
        snprintf(error, error_size, "Server did not confirm authentication.");
    json_decref(result);
    return ok;
}

bool jj_rest_post(jj_rest *client, const char *subject, const char *body,
    const char *music, json_t *tags, char *error, size_t error_size)
{
    json_t *payload, *result, *id;
    const char *id_text;
    bool ok;

    /* Match the old Blogger client's public, published, plain text defaults.
     * Send optional values explicitly: EntryTo's explicit constructor does not
     * initialize its Lombok @Builder.Default fields. In particular, a missing
     * tags list causes applyTags() to throw after the entry has been saved.
     */
    payload = json_pack("{s:s,s:s,s:i,s:s,s:b,s:b,s:b,s:b,s:i,s:i,s:s,s:s,s:[]}",
        "subject", subject, "body", body, "security", 2, "format", "TEXT",
        "autoFormat", 1, "allowComments", 1, "emailComments", 1,
        "draft", 0, "mood", 12, "location", 0,
        "music", music ? music : "", "trackback", "", "tags");
    if (!payload) {
        snprintf(error, error_size, "Unable to encode entry as UTF-8 JSON.");
        return false;
    }
    if (tags && (!json_is_array(tags) || json_object_set(payload, "tags", tags) != 0)) {
        json_decref(payload);
        snprintf(error, error_size, "Unable to encode tags.");
        return false;
    }
    /* Preserve the Blogger convention used to set titles in the GTK editor. */
    {
        char *copy = malloc(strlen(body) + 1);
        char *start, *end;
        bool have_subject = subject[0] != '\0';
        if (!copy) {
            json_decref(payload);
            snprintf(error, error_size, "Unable to prepare entry.");
            return false;
        }
        strcpy(copy, body);
        start = copy;
        while ((start = strstr(start, "<title>")) != NULL) {
            end = strstr(start + 7, "</title>");
            if (!end)
                break;
            if (memchr(start + 7, '\n', (size_t)(end - start - 7))) {
                start += 7;
                continue;
            }
            if (!have_subject) {
                json_t *title = json_stringn(start + 7, (size_t)(end - start - 7));
                if (!title || json_object_set_new(payload, "subject", title) != 0) {
                    free(copy);
                    json_decref(payload);
                    snprintf(error, error_size, "Unable to encode title.");
                    return false;
                }
                have_subject = true;
            }
            memmove(start, end + 8, strlen(end + 8) + 1);
        }
        if (json_object_set_new(payload, "body", json_string(copy)) != 0) {
            free(copy);
            json_decref(payload);
            snprintf(error, error_size, "Unable to encode body.");
            return false;
        }
        /* Blogger also records the first music tag without removing it. */
        start = copy;
        while ((!music || music[0] == '\0') &&
            (start = strstr(start, "<music>")) != NULL) {
            end = strstr(start + 7, "</music>");
            if (!end)
                break;
            if (memchr(start + 7, '\n', (size_t)(end - start - 7))) {
                start += 7;
                continue;
            }
            if (json_object_set_new(payload, "music",
                json_stringn(start + 7, (size_t)(end - start - 7))) != 0) {
                free(copy);
                json_decref(payload);
                snprintf(error, error_size, "Unable to encode music.");
                return false;
            }
            break;
        }
        free(copy);
    }
    result = request(client, "/api/entry", payload, error, error_size);
    json_decref(payload);
    if (!result)
        return false;
    id = json_object_get(result, "id");
    id_text = json_string_value(id);
    ok = string_is(json_object_get(result, "status"), "ok") &&
        id_text && id_text[0] >= '1' && id_text[0] <= '9' &&
        strspn(id_text, "0123456789") == strlen(id_text);
    if (!ok)
        snprintf(error, error_size, "Server did not confirm that the entry was saved.");
    json_decref(result);
    return ok;
}

json_t *jj_rest_recent(jj_rest *client, const char *username, size_t count,
    char *error, size_t error_size)
{
    char *escaped = curl_easy_escape(client->curl, username, 0);
    char *path;
    json_t *result, *entries, *embedded;

    if (!escaped) {
        snprintf(error, error_size, "Unable to encode username.");
        return NULL;
    }
    path = malloc(strlen(escaped) + 80);
    if (!path) {
        curl_free(escaped);
        snprintf(error, error_size, "Unable to prepare recent entries request.");
        return NULL;
    }
    snprintf(path, strlen(escaped) + 80, "/api/entry/%s/size/%zu/page/0", escaped, count);
    curl_free(escaped);
    result = request(client, path, NULL, error, error_size);
    free(path);
    if (!result)
        return NULL;
    /* PagedModel uses content for JSON and _embedded.entryToList for HAL. */
    entries = json_object_get(result, "content");
    embedded = json_object_get(result, "_embedded");
    if (!entries && json_is_object(embedded))
        entries = json_object_get(embedded, "entryToList");
    if (!entries && json_is_object(result) &&
        json_integer_value(json_object_get(json_object_get(result, "page"), "totalElements")) == 0 &&
        json_is_integer(json_object_get(json_object_get(result, "page"), "totalElements"))) {
        json_decref(result);
        return json_array();
    }
    if (!json_is_array(entries)) {
        snprintf(error, error_size, "Server returned an invalid recent entries list.");
        json_decref(result);
        return NULL;
    }
    json_incref(entries);
    json_decref(result);
    for (size_t i = 0; i < json_array_size(entries); i++) {
        json_t *entry = json_array_get(entries, i);
        if (!json_is_string(json_object_get(entry, "subject")) ||
            !json_is_string(json_object_get(entry, "body"))) {
            snprintf(error, error_size, "Server returned an invalid recent entry.");
            json_decref(entries);
            return NULL;
        }
    }
    return entries;
}
