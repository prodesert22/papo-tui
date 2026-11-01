#include "config/channel_notifications.h"
#include "config/channel_store.h"
#include "model/conversation/history.h"
#include <glib.h>
#include <jansson.h>
#include <stdlib.h>
#include <string.h>

struct channel_notifications {
    channel_store *store;
    char *url, *user;
};
static bool valid_mode(const char *mode)
{
    return mode && (!strcmp(mode, "all") || !strcmp(mode, "only_mentions") || !strcmp(mode, "off"));
}
static bool valid(const json_t *mode)
{
    return valid_mode(json_string_value(mode));
}
channel_notifications *channel_notifications_create(const char *url, const char *user)
{
    channel_store *store = channel_store_open(url, user, "channel-notifications", valid);
    channel_notifications *local = store ? calloc(1, sizeof(*local)) : NULL;
    if (!local) {
        channel_store_close(store);
        return NULL;
    }
    local->store = store;
    local->url = g_strdup(url);
    local->user = g_strdup(user);
    if (!local->url || !local->user) {
        channel_notifications_destroy(local);
        return NULL;
    }
    return local;
}
bool channel_notifications_refresh(channel_notifications *local)
{
    if (!local || !channel_store_changed(local->store))
        return false;
    channel_store *store =
        channel_store_open(local->url, local->user, "channel-notifications", valid);
    if (!store)
        return false;
    channel_store_close(local->store);
    local->store = store;
    return true;
}
void channel_notifications_destroy(channel_notifications *local)
{
    if (!local)
        return;
    channel_store_close(local->store);
    g_free(local->url);
    g_free(local->user);
    free(local);
}
const char *channel_notifications_mode(const channel_notifications *local, const char *channel)
{
    return local && channel
               ? json_string_value(json_object_get(channel_store_channels(local->store), channel))
               : NULL;
}
bool channel_notifications_set(channel_notifications *local, const char *channel, const char *mode)
{
    if (!local || !history_uuid(channel) || (mode && !valid_mode(mode)))
        return false;
    /* Build on what another instance saved, instead of failing on its tag. */
    channel_notifications_refresh(local);
    json_t *copy = json_deep_copy(channel_store_channels(local->store));
    if (!copy)
        return false;
    bool ok = mode ? !json_object_set_new(copy, channel, json_string(mode))
                   : (json_object_del(copy, channel), true);
    ok = ok && channel_store_save(local->store, copy);
    json_decref(copy);
    return ok;
}
