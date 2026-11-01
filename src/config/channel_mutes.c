#include "config/channel_mutes.h"
#include "config/channel_store.h"
#include "model/conversation/history.h"
#include <jansson.h>
#include <stdlib.h>

struct channel_mutes {
    channel_store *store;
};
static bool valid(const json_t *expires)
{
    return json_is_integer(expires) &&
           (json_integer_value(expires) == -1 || json_integer_value(expires) > 0);
}
channel_mutes *channel_mutes_create(const char *url, const char *user)
{
    channel_store *store = channel_store_open(url, user, "channel-mutes", valid);
    channel_mutes *m = store ? calloc(1, sizeof(*m)) : NULL;
    if (!m) {
        channel_store_close(store);
        return NULL;
    }
    m->store = store;
    return m;
}
void channel_mutes_destroy(channel_mutes *m)
{
    if (!m)
        return;
    channel_store_close(m->store);
    free(m);
}
bool channel_mutes_active(const channel_mutes *m, const char *id, uint64_t now)
{
    json_t *entry = m && id ? json_object_get(channel_store_channels(m->store), id) : NULL;
    json_int_t expires = json_integer_value(entry);
    return entry && (expires == -1 || (expires > 0 && (uint64_t)expires > now));
}
bool channel_mutes_expire(channel_mutes *m, uint64_t now)
{
    if (!m)
        return false;
    bool changed = false;
    const char *id;
    json_t *expires;
    void *next;
    json_object_foreach_safe(channel_store_channels(m->store), next, id,
                             expires) if (json_integer_value(expires) > 0 &&
                                          (uint64_t)json_integer_value(expires) <= now)
    {
        json_object_del(channel_store_channels(m->store), id);
        changed = true;
    }
    return changed;
}
bool channel_mutes_set(channel_mutes *m, const char *id, int seconds, uint64_t now)
{
    if (!m || !history_uuid(id) ||
        (seconds != -1 && seconds != 0 && seconds != 600 && seconds != 1800 && seconds != 3600 &&
         seconds != 14400 && seconds != 43200 && seconds != 86400) ||
        now > INT64_MAX - 86400U)
        return false;
    json_t *copy = json_deep_copy(channel_store_channels(m->store));
    if (!copy)
        return false;
    if (!seconds)
        json_object_del(copy, id);
    bool ok = !seconds || !json_object_set_new(
                              copy, id, json_integer(seconds < 0 ? -1 : (json_int_t)now + seconds));
    ok = ok && channel_store_save(m->store, copy);
    json_decref(copy);
    return ok;
}
