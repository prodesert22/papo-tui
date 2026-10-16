#include "model/text/emoji_usage.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <jansson.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define USAGE_BYTES (1024 * 1024)
#define USAGE_ENTRIES 10000
struct emoji_usage {
    json_t *counts;
    char *path;
};
emoji_usage *emoji_usage_create(const char *instance, const char *user)
{
    emoji_usage *u = calloc(1, sizeof(*u));
    if (!u)
        return NULL;
    u->counts = json_object();
    if (!u->counts) {
        free(u);
        return NULL;
    }
    if (!instance || !*instance || !user || !*user)
        return u;
    char *context = g_strdup_printf("%s\n%s", instance, user);
    char *digest = g_compute_checksum_for_string(G_CHECKSUM_SHA256, context, -1);
    char *directory = g_build_filename(g_get_user_config_dir(), "papo-tui", "emoji-usage", NULL);
    char *name = g_strconcat(digest, ".json", NULL);
    u->path = g_build_filename(directory, name, NULL);
    g_free(context);
    g_free(digest);
    g_free(name);
    if (g_mkdir_with_parents(directory, 0700)) {
        g_clear_pointer(&u->path, g_free);
        g_free(directory);
        return u;
    }
    g_free(directory);
    if (!g_file_test(u->path, G_FILE_TEST_EXISTS))
        return u;
    GStatBuf st;
    char *data = NULL;
    gsize length = 0;
    json_t *counts = NULL;
    if (!g_stat(u->path, &st) && st.st_size > 0 && st.st_size <= USAGE_BYTES &&
        g_file_get_contents(u->path, &data, &length, NULL) && length <= USAGE_BYTES)
        counts = json_loadb(data, length, JSON_REJECT_DUPLICATES, NULL);
    g_free(data);
    bool valid = json_is_object(counts) && json_object_size(counts) <= USAGE_ENTRIES;
    const char *key;
    json_t *value;
    json_object_foreach(counts, key, value)
    {
        if (strlen(key) > 160 || !json_is_integer(value) || json_integer_value(value) < 1)
            valid = false;
    }
    if (valid) {
        json_decref(u->counts);
        u->counts = counts;
    } else {
        json_decref(counts);
        /* Preserve malformed files instead of silently replacing user data. */
        g_clear_pointer(&u->path, g_free);
    }
    return u;
}
void emoji_usage_destroy(emoji_usage *u)
{
    if (u) {
        json_decref(u->counts);
        g_free(u->path);
        free(u);
    }
}
uint64_t emoji_usage_count(const emoji_usage *u, const char *key)
{
    return u ? (uint64_t)json_integer_value(json_object_get(u->counts, key)) : 0;
}
void emoji_usage_record(emoji_usage *u, const char *key)
{
    if (!u || !key || strlen(key) > 160)
        return;
    uint64_t count = emoji_usage_count(u, key);
    if ((!count && json_object_size(u->counts) >= USAGE_ENTRIES) || count >= INT64_MAX)
        return;
    if (json_object_set_new(u->counts, key, json_integer((json_int_t)count + 1)))
        return;
    char *data = u->path ? json_dumps(u->counts, JSON_COMPACT | JSON_SORT_KEYS) : NULL;
    if (data && strlen(data) <= USAGE_BYTES)
        g_file_set_contents_full(u->path, data, -1, G_FILE_SET_CONTENTS_CONSISTENT, 0600, NULL);
    free(data);
}
