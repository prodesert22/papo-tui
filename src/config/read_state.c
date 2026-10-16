#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "config/read_state.h"
#include "config/read_storage.h"
#include "model/conversation/history.h"
#include "net/auth/session_time.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const char *text(const json_t *object, const char *key, size_t limit)
{
    json_t *value = json_object_get(object, key);
    const char *s = json_string_value(value);
    return s && json_string_length(value) <= limit && strlen(s) == json_string_length(value) ? s
                                                                                             : NULL;
}
static bool scope_valid(const char *url, const char *user)
{
    if (!url || strlen(url) > 2048 || !history_uuid(user))
        return false;
    char reason[192];
    char *canonical = auth_normalize_url(url, reason, sizeof(reason));
    bool ok = canonical && !strcmp(canonical, url);
    curl_free(canonical);
    return ok;
}
static bool cursor_valid(const json_t *cursor)
{
    uint64_t time;
    return json_is_object(cursor) && json_object_size(cursor) == 2 &&
           history_uuid(text(cursor, "id", 36)) &&
           session_time_parse(text(cursor, "created_at", 64), &time);
}
static bool map_valid(const json_t *map)
{
    if (!json_is_object(map) || json_object_size(map) > READ_STATE_LIMIT)
        return false;
    const char *id;
    json_t *cursor;
    json_object_foreach((json_t *)map, id,
                        cursor) if (!history_uuid(id) || !cursor_valid(cursor)) return false;
    return true;
}
bool read_state_validate(json_t *root)
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
        if (!json_is_object(context) || json_object_size(context) != 3 ||
            !scope_valid(text(context, "url", 2048), text(context, "user_id", 36)) ||
            !map_valid(json_object_get(context, "messages")))
            return false;
        for (size_t j = 0; j < i; ++j) {
            json_t *prior = json_array_get(contexts, j);
            if (!strcmp(text(prior, "url", 2048), text(context, "url", 2048)) &&
                !strcmp(text(prior, "user_id", 36), text(context, "user_id", 36)))
                return false;
        }
    }
    return true;
}
static size_t find_context(json_t *contexts, const char *url, const char *user)
{
    size_t i;
    json_t *context;
    json_array_foreach(contexts, i,
                       context) if (!strcmp(text(context, "url", 2048), url) &&
                                    !strcmp(text(context, "user_id", 36), user)) return i;
    return SIZE_MAX;
}
json_t *read_state_load(const char *url, const char *user)
{
    if (!scope_valid(url, user))
        return json_object();
    read_storage *directory = read_storage_open();
    json_t *root = directory ? read_storage_load(directory) : NULL;
    read_storage_close(directory);
    json_t *contexts = json_object_get(root, "contexts");
    size_t index = find_context(contexts, url, user);
    json_t *map = index != SIZE_MAX
                      ? json_deep_copy(json_object_get(json_array_get(contexts, index), "messages"))
                      : json_object();
    json_decref(root);
    return map;
}
static int compare(const json_t *a, const json_t *b)
{
    int order = 0;
    session_time_compare(text(a, "created_at", 64), text(b, "created_at", 64), &order);
    return order ? order : strcasecmp(text(a, "id", 36), text(b, "id", 36));
}
bool read_state_save(const char *url, const char *user, const json_t *messages)
{
    if (!scope_valid(url, user) || !map_valid(messages))
        return false;
    read_storage *directory = read_storage_open();
    if (!directory)
        return false;
    if (!read_storage_lock(directory)) {
        read_storage_close(directory);
        return false;
    }
    json_t *root = read_storage_load(directory), *contexts = json_object_get(root, "contexts");
    size_t index = find_context(contexts, url, user);
    json_t *merged =
        index != SIZE_MAX
            ? json_deep_copy(json_object_get(json_array_get(contexts, index), "messages"))
            : json_object();
    bool ok = root && merged;
    const char *id;
    json_t *cursor;
    json_object_foreach((json_t *)messages, id, cursor)
    {
        json_t *previous = json_object_get(merged, id);
        if (ok && (!previous || compare(cursor, previous) > 0))
            ok = json_object_set(merged, id, cursor) == 0;
    }
    while (ok && json_object_size(merged) > READ_STATE_LIMIT)
        json_object_del(merged, json_object_iter_key(json_object_iter(merged)));
    json_t *context =
        ok ? json_pack("{s:s,s:s,s:O}", "url", url, "user_id", user, "messages", merged) : NULL;
    if (context) {
        if (index != SIZE_MAX)
            json_array_remove(contexts, index);
        ok = json_array_append(contexts, context) == 0;
        while (json_array_size(contexts) > CONTEXT_LIMIT)
            json_array_remove(contexts, 0);
        if (ok)
            ok = read_storage_write(directory, root);
    } else
        ok = false;
    json_decref(context);
    json_decref(merged);
    json_decref(root);
    read_storage_close(directory);
    return ok;
}
