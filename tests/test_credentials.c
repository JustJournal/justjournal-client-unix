/* Track only application/Jansson ownership, never libcurl or TLS internals.
 * All credentials below are synthetic; this test never connects to a server.
 */
#include <curl/curl.h>
#include <jansson.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char marker[] = "JJ_SYNTHETIC_PASSWORD_7319";

struct allocation {
    void *pointer;
    size_t size;
    bool marked;
    struct allocation *next;
};

static struct allocation *allocations;
static size_t dirty_frees, marked_frees, performed;
static size_t allocation_calls, fail_at;

static size_t marker_offset(const void *pointer, size_t size)
{
    const unsigned char *bytes = pointer;
    size_t i;

    for (i = 0; size >= sizeof(marker) - 1 && i <= size - (sizeof(marker) - 1); i++)
        if (memcmp(bytes + i, marker, sizeof(marker) - 1) == 0)
            return i;
    return SIZE_MAX;
}

static void *tracked_malloc(size_t size)
{
    struct allocation *record;
    void *pointer;

    allocation_calls++;
    if (fail_at && allocation_calls == fail_at)
        return NULL;
    pointer = malloc(size);
    if (!pointer)
        return NULL;
    record = malloc(sizeof(*record));
    if (!record) {
        free(pointer);
        return NULL;
    }
    /* Nonzero padding proves the entire credential allocation was erased. */
    memset(pointer, 0xa5, size);
    record->pointer = pointer;
    record->size = size;
    record->marked = false;
    record->next = allocations;
    allocations = record;
    return pointer;
}

static void tracked_free(void *pointer)
{
    struct allocation **link = &allocations;
    size_t i;

    if (!pointer)
        return;
    while (*link && (*link)->pointer != pointer)
        link = &(*link)->next;
    if (*link) {
        struct allocation *record = *link;
        if (marker_offset(pointer, record->size) != SIZE_MAX)
            dirty_frees++;
        if (record->marked) {
            const unsigned char *bytes = pointer;
            marked_frees++;
            for (i = 0; i < record->size; i++)
                if (bytes[i] != 0) {
                    dirty_frees++;
                    break;
                }
        }
        *link = record->next;
        free(record);
    } else {
        fputs("Untracked application allocation freed.\n", stderr);
        exit(1);
    }
    free(pointer);
}

static void *tracked_calloc(size_t count, size_t size)
{
    if (size && count > SIZE_MAX / size)
        return NULL;
    void *pointer = tracked_malloc(count * size);
    if (pointer)
        memset(pointer, 0, count * size);
    return pointer;
}

static void *tracked_realloc(void *pointer, size_t size)
{
    struct allocation *record;
    void *replacement;
    size_t old_size = 0;

    if (!pointer)
        return tracked_malloc(size);
    for (record = allocations; record; record = record->next)
        if (record->pointer == pointer) {
            old_size = record->size;
            break;
        }
    if (!record) {
        fputs("Untracked application allocation resized.\n", stderr);
        exit(1);
    }
    replacement = tracked_malloc(size);
    if (!replacement)
        return NULL;
    memcpy(replacement, pointer, old_size < size ? old_size : size);
    tracked_free(pointer);
    return replacement;
}

static CURLcode disconnected_perform(CURL *curl)
{
    struct allocation *record;
    (void)curl;
    performed++;
    for (record = allocations; record; record = record->next) {
        size_t offset = marker_offset(record->pointer, record->size);
        if (offset != SIZE_MAX) {
            record->marked = true;
        }
    }
    return CURLE_COULDNT_CONNECT;
}

#define malloc tracked_malloc
#define calloc tracked_calloc
#define realloc tracked_realloc
#define free tracked_free
#define curl_easy_perform disconnected_perform
#include "../jj_rest.c"
#undef malloc
#undef calloc
#undef realloc
#undef free
#undef curl_easy_perform

static bool check_encoding(void)
{
    static const char *valid[] = {
        "", "quotes\" and slash\\", "controls\001\b\f\n\r\t\037",
        "UTF-8 \302\242 \342\202\254 \360\237\230\200"
    };
    static const char *invalid[] = {
        "\200", "\300\257", "\340\200\200", "\355\240\200",
        "\364\220\200\200", "\365\200\200\200", "\302",
        "\342\202", "\360\237\230", "\342x\254"
    };
    size_t i, capacity;

    for (i = 0; i < sizeof(valid) / sizeof(valid[0]); i++) {
        char *body = login_body(valid[i], valid[i], &capacity);
        json_t *parsed = body ? json_loads(body, 0, NULL) : NULL;
        bool matches = parsed &&
            json_is_string(json_object_get(parsed, "username")) &&
            json_is_string(json_object_get(parsed, "password")) &&
            strcmp(json_string_value(json_object_get(parsed, "username")), valid[i]) == 0 &&
            strcmp(json_string_value(json_object_get(parsed, "password")), valid[i]) == 0;
        json_decref(parsed);
        if (body) {
            wipe(body, capacity);
            tracked_free(body);
        }
        if (!matches)
            return false;
    }
    for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        char password[128];
        char *body;
        strcpy(password, marker);
        strcat(password, invalid[i]);
        /* Invalid bytes occur after a copied marker to exercise partial-buffer
         * erasure, and both credential positions must reject invalid UTF-8. */
        body = login_body("synthetic-user", password, &capacity);
        if (body) {
            wipe(body, capacity);
            tracked_free(body);
            return false;
        }
        body = login_body(password, marker, &capacity);
        if (body) {
            wipe(body, capacity);
            tracked_free(body);
            return false;
        }
        if (dirty_frees)
            return false;
    }
    return true;
}

int main(void)
{
    jj_rest *client;
    char error[256];
    size_t login_allocations, failure;
    bool ok;

    json_set_alloc_funcs(tracked_malloc, tracked_free);
    if (!check_encoding()) {
        fputs("Credential JSON/UTF-8 encoding regression.\n", stderr);
        return 1;
    }
    client = jj_rest_open("https://example.invalid", "credential-test", error,
        sizeof(error));
    if (!client) {
        fputs("Unable to initialize offline test client.\n", stderr);
        return 1;
    }
    allocation_calls = 0;
    ok = jj_rest_login(client, "synthetic-user", marker, error, sizeof(error));
    login_allocations = allocation_calls;
    if (ok || !error[0] || performed != 1 || !marked_frees || dirty_frees) {
        fputs("Credential ownership/wiping regression.\n", stderr);
        return 1;
    }

    /* Every application/Jansson allocation in the baseline login must also
     * fail gracefully without freeing password-bearing storage unwiped.
     */
    for (failure = 1; failure <= login_allocations; failure++) {
        allocation_calls = 0;
        fail_at = failure;
        error[0] = '\0';
        ok = jj_rest_login(client, "synthetic-user", marker, error, sizeof(error));
        fail_at = 0;
        if (ok || !error[0] || dirty_frees) {
            fputs("Credential allocation-failure regression.\n", stderr);
            return 1;
        }
    }
    jj_rest_close(client);
    if (allocations || dirty_frees) {
        fputs("Application allocation leak or unwiped credential.\n", stderr);
        return 1;
    }
    puts("Credential ownership and allocation-failure checks passed.");
    return 0;
}
