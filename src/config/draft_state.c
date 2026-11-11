#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "config/draft_state.h"
#include "config/read_storage.h"
#include "model/conversation/history.h"
#include "net/auth/client.h"
#include <glib.h>
#include <string.h>

static const char *stem = "drafts";

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
static bool entry_valid(const json_t *entry)
{
    json_t *reply = json_object_get(entry, "reply_to");
    return json_is_object(entry) && json_object_size(entry) == 2 &&
           text(entry, "text", DRAFT_STATE_TEXT_BYTES) &&
           (json_is_null(reply) || history_uuid(json_string_value(reply)));
}
static bool map_valid(const json_t *map)
{
    if (!json_is_object(map) || json_object_size(map) > DRAFT_STATE_LIMIT)
        return false;
    const char *id;
    json_t *entry;
    json_object_foreach((json_t *)map, id,
                        entry) if (!history_uuid(id) || !entry_valid(entry)) return false;
    return true;
}
bool draft_state_validate(json_t *root)
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
            !map_valid(json_object_get(context, "drafts")))
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
/* A copy without control characters other than newline and tab. */
static char *clean(const char *value)
{
    GString *out = g_string_sized_new(strlen(value));
    for (const char *p = value; *p; p = g_utf8_next_char(p)) {
        gunichar c = g_utf8_get_char(p);
        if (c == '\n' || c == '\t' || !g_unichar_iscntrl(c))
            g_string_append_unichar(out, c);
    }
    return g_string_free(out, FALSE);
}
json_t *draft_state_load(const char *url, const char *user)
{
    json_t *drafts = json_object();
    if (!drafts || !scope_valid(url, user))
        return drafts;
    read_storage *storage = read_storage_open_named(stem, draft_state_validate);
    json_t *root = storage ? read_storage_load(storage) : NULL;
    read_storage_close(storage);
    json_t *contexts = json_object_get(root, "contexts");
    size_t index = find_context(contexts, url, user);
    json_t *saved =
        index != SIZE_MAX ? json_object_get(json_array_get(contexts, index), "drafts") : NULL;
    const char *id;
    json_t *entry;
    json_object_foreach(saved, id, entry)
    {
        char *value = clean(text(entry, "text", DRAFT_STATE_TEXT_BYTES));
        json_t *reply = json_object_get(entry, "reply_to");
        json_t *copy = value ? json_pack("{s:s,s:O}", "text", value, "reply_to", reply) : NULL;
        g_free(value);
        if (!copy || json_object_set_new(drafts, id, copy)) {
            json_decref(drafts);
            json_decref(root);
            return json_object();
        }
    }
    json_decref(root);
    return drafts;
}
/* Writes the scope's map, or removes the scope when drafts is NULL. */
static bool store(const char *url, const char *user, const json_t *drafts)
{
    if (!scope_valid(url, user) || (drafts && !map_valid(drafts)))
        return false;
    read_storage *storage = read_storage_open_named(stem, draft_state_validate);
    if (!storage)
        return false;
    if (!read_storage_lock(storage)) {
        read_storage_close(storage);
        return false;
    }
    json_t *root = read_storage_load(storage), *contexts = json_object_get(root, "contexts");
    size_t index = find_context(contexts, url, user);
    bool ok = root != NULL;
    if (ok && index != SIZE_MAX)
        ok = json_array_remove(contexts, index) == 0;
    if (ok && drafts && json_object_size(drafts)) {
        json_t *context = json_pack("{s:s,s:s,s:O}", "url", url, "user_id", user, "drafts", drafts);
        ok = context && json_array_append_new(contexts, context) == 0;
        while (ok && json_array_size(contexts) > CONTEXT_LIMIT)
            json_array_remove(contexts, 0);
    }
    if (ok && (index != SIZE_MAX || (drafts && json_object_size(drafts))))
        ok = read_storage_write(storage, root);
    json_decref(root);
    read_storage_close(storage);
    return ok;
}
bool draft_state_save(const char *url, const char *user, const json_t *drafts)
{
    return drafts && store(url, user, drafts);
}
bool draft_state_forget(const char *url, const char *user)
{
    return store(url, user, NULL);
}
