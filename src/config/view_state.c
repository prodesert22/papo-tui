#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "config/view_state.h"
#include "config/read_storage.h"
#include "model/conversation/history.h"
#include "net/auth/client.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const char *stem = "view-state";

static const char *text(const json_t *object, const char *key, size_t limit)
{
    json_t *value = json_object_get(object, key);
    const char *s = json_string_value(value);
    return s && json_string_length(value) <= limit && strlen(s) == json_string_length(value) ? s
                                                                                             : NULL;
}
static bool url_valid(const char *url)
{
    if (!url || !*url || strlen(url) > 2048)
        return false;
    char reason[192];
    char *canonical = auth_normalize_url(url, reason, sizeof(reason));
    bool ok = canonical && !strcmp(canonical, url);
    curl_free(canonical);
    return ok;
}
static bool conversation_valid(const char *conversation)
{
    return conversation && (!*conversation || history_uuid(conversation));
}
/* {"version":1,"contexts":[{"url":..., "conversation":...}]}, oldest first. */
bool view_state_validate(json_t *root)
{
    json_t *contexts = json_object_get(root, "contexts");
    if (!json_is_object(root) || json_object_size(root) != 2 ||
        !json_is_integer(json_object_get(root, "version")) ||
        json_integer_value(json_object_get(root, "version")) != 1 || !json_is_array(contexts) ||
        json_array_size(contexts) > CONTEXT_LIMIT)
        return false;
    size_t i;
    json_t *context;
    json_array_foreach(contexts, i, context)
    {
        const char *url = text(context, "url", 2048);
        if (!json_is_object(context) || json_object_size(context) != 2 || !url_valid(url) ||
            !conversation_valid(text(context, "conversation", 36)))
            return false;
        for (size_t j = 0; j < i; ++j)
            if (!strcmp(text(json_array_get(contexts, j), "url", 2048), url))
                return false;
    }
    return true;
}
static size_t find(json_t *contexts, const char *url)
{
    size_t i;
    json_t *context;
    json_array_foreach(contexts, i, context) if (!strcmp(text(context, "url", 2048), url)) return i;
    return SIZE_MAX;
}
static json_t *load(void)
{
    read_storage *storage = read_storage_open_named(stem, view_state_validate);
    json_t *root = storage ? read_storage_load(storage) : NULL;
    read_storage_close(storage);
    return root;
}
bool view_state_save(const char *url, const char *conversation)
{
    if (!url_valid(url) || !conversation_valid(conversation))
        return false;
    read_storage *storage = read_storage_open_named(stem, view_state_validate);
    if (!storage)
        return false;
    if (!read_storage_lock(storage)) {
        read_storage_close(storage);
        return false;
    }
    json_t *root = read_storage_load(storage), *contexts = json_object_get(root, "contexts");
    json_t *context =
        root ? json_pack("{s:s,s:s}", "url", url, "conversation", conversation) : NULL;
    bool ok = context != NULL;
    if (ok) {
        /* The most recently used server is always the last entry. */
        size_t index = find(contexts, url);
        if (index != SIZE_MAX)
            json_array_remove(contexts, index);
        ok = json_array_append_new(contexts, context) == 0;
        while (ok && json_array_size(contexts) > CONTEXT_LIMIT)
            json_array_remove(contexts, 0);
        if (ok)
            ok = read_storage_write(storage, root);
    }
    json_decref(root);
    read_storage_close(storage);
    return ok;
}
void view_state_conversation(const char *url, char conversation[37])
{
    conversation[0] = 0;
    if (!url_valid(url))
        return;
    json_t *root = load(), *contexts = json_object_get(root, "contexts");
    size_t index = find(contexts, url);
    if (index != SIZE_MAX)
        snprintf(conversation, 37, "%s", text(json_array_get(contexts, index), "conversation", 36));
    json_decref(root);
}
size_t view_state_latest(const char *const *urls, size_t count)
{
    json_t *root = load(), *contexts = json_object_get(root, "contexts");
    size_t best = SIZE_MAX, best_rank = 0;
    for (size_t i = 0; i < count; ++i) {
        size_t index = urls[i] && *urls[i] ? find(contexts, urls[i]) : SIZE_MAX;
        if (index != SIZE_MAX && (best == SIZE_MAX || index + 1 > best_rank)) {
            best = i;
            best_rank = index + 1;
        }
    }
    json_decref(root);
    return best;
}
