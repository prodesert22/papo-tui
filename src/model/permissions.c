#include "model/permissions.h"
#include <glib.h>
#include <stdlib.h>
#include <string.h>

#define ROLE_LIMIT 10000U
#define CHANNEL_LIMIT 500U
struct permissions {
    json_t *memberships, *roles, *channels, *dms;
    char user[37], owner[37];
    bool identity_known, owner_known;
    uint64_t revision, events, channel_revision;
};
static const char *str(const json_t *o, const char *key)
{
    const char *s = json_string_value(json_object_get(o, key));
    return s ? s : "";
}
static bool uuid_value(const json_t *v)
{
    const char *s = json_string_value(v);
    return s && json_string_length(v) == 36 && strlen(s) == 36 && g_uuid_string_is_valid(s);
}
static const char *const role_keys[] = {"manage_server",  "manage_channels", "manage_roles",
                                        "ban_members",    "pin_message",     "everyone_message",
                                        "send_attachment"};
static const char *const channel_keys[] = {"read_channel", "send_messages", "delete_messages",
                                           "connect_voice"};
static bool flags_valid(const json_t *o, const char *const *keys, size_t count)
{
    if (!json_is_object(o))
        return false;
    for (size_t i = 0; i < count; ++i) {
        json_t *v = json_object_get(o, keys[i]);
        if (v && !json_is_boolean(v))
            return false;
    }
    return true;
}
permissions *permissions_create(void)
{
    return calloc(1, sizeof(permissions));
}
void permissions_reset(permissions *p)
{
    if (!p)
        return;
    json_decref(p->memberships);
    json_decref(p->roles);
    json_decref(p->channels);
    json_decref(p->dms);
    p->memberships = p->roles = p->channels = p->dms = NULL;
    p->user[0] = p->owner[0] = 0;
    p->identity_known = p->owner_known = false;
    ++p->revision;
    ++p->events;
    ++p->channel_revision;
}
void permissions_destroy(permissions *p)
{
    permissions_reset(p);
    free(p);
}
void permissions_invalidate(permissions *p, permission_source source)
{
    if (!p)
        return;
    if (source == PERMISSIONS_SERVER)
        p->owner_known = false;
    else {
        json_t **slot = source == PERMISSIONS_ROLES ? &p->roles : &p->channels;
        json_decref(*slot);
        *slot = NULL;
    }
    ++p->revision;
    if (source == PERMISSIONS_CHANNELS)
        ++p->channel_revision;
}
bool permissions_identity(permissions *p, const json_t *body, uint64_t serial)
{
    if (!p || serial != p->events)
        return false; /* An older REST response cannot undo an observed role event. */
    json_t *ids = json_object(), *roles = json_object_get(body, "roles");
    bool ok = ids && uuid_value(json_object_get(body, "id")) && json_is_array(roles) &&
              json_array_size(roles) <= ROLE_LIMIT;
    size_t i;
    json_t *role;
    json_array_foreach(roles, i, role)
    {
        if (!uuid_value(json_object_get(role, "id")) || json_object_get(ids, str(role, "id")) ||
            json_object_set(ids, str(role, "id"), json_true()))
            ok = false;
    }
    if (uuid_value(json_object_get(body, "id")))
        memcpy(p->user, str(body, "id"), sizeof(p->user));
    p->identity_known = ok;
    json_decref(p->memberships);
    p->memberships = ok ? ids : NULL;
    if (!ok)
        json_decref(ids);
    ++p->revision;
    return ok;
}
bool permissions_update(permissions *p, permission_source source, const json_t *body)
{
    if (!p)
        return false;
    if (source == PERMISSIONS_SERVER) {
        json_t *owner = json_object_get(body, "owner_id");
        p->owner_known = json_is_object(body) && (json_is_null(owner) || uuid_value(owner));
        p->owner[0] = 0;
        if (uuid_value(owner))
            memcpy(p->owner, json_string_value(owner), sizeof(p->owner));
        ++p->revision;
        return p->owner_known;
    }
    json_t *array = json_object_get(body, source == PERMISSIONS_ROLES ? "roles" : "channels");
    json_t *next = json_object();
    bool ok = next && json_is_array(array) &&
              json_array_size(array) <= (source == PERMISSIONS_ROLES ? ROLE_LIMIT : CHANNEL_LIMIT);
    size_t i;
    json_t *item;
    json_array_foreach(array, i, item)
    {
        if (!uuid_value(json_object_get(item, "id")) || json_object_get(next, str(item, "id"))) {
            ok = false;
            break;
        }
        json_t *flags = json_object_get(item, "permissions");
        if (source == PERMISSIONS_ROLES) {
            if (!flags_valid(flags, role_keys, PERMISSION_ROLE_COUNT))
                ok = false;
        } else {
            if (!json_is_array(flags) || json_array_size(flags) > ROLE_LIMIT)
                ok = false;
            size_t j;
            json_t *entry;
            json_t *seen = json_object();
            if (!seen)
                ok = false;
            json_array_foreach(flags, j, entry)
            {
                if (!uuid_value(json_object_get(entry, "role_id")) ||
                    json_object_get(seen, str(entry, "role_id")) ||
                    !flags_valid(json_object_get(entry, "permissions"), channel_keys,
                                 PERMISSION_CHANNEL_COUNT) ||
                    json_object_set(seen, str(entry, "role_id"), json_true()))
                    ok = false;
            }
            json_decref(seen);
        }
        if (json_object_set_new(next, str(item, "id"), json_deep_copy(item)))
            ok = false;
    }
    json_t **slot = source == PERMISSIONS_ROLES ? &p->roles : &p->channels;
    json_decref(*slot);
    *slot = ok ? next : NULL;
    if (!ok)
        json_decref(next);
    ++p->revision;
    if (source == PERMISSIONS_CHANNELS)
        ++p->channel_revision;
    return ok;
}
bool permissions_event(permissions *p, const json_t *event)
{
    if (!p || (strcmp(str(event, "type"), "role_add") && strcmp(str(event, "type"), "role_remove")))
        return false;
    if (!uuid_value(json_object_get(event, "user_id")) ||
        !uuid_value(json_object_get(event, "role_id")) || strcmp(str(event, "user_id"), p->user))
        return false;
    ++p->events;
    ++p->revision;
    if (!strcmp(str(event, "type"), "role_remove")) {
        json_object_del(p->memberships, str(event, "role_id"));
    } else if (!p->memberships ||
               (json_object_size(p->memberships) >= ROLE_LIMIT &&
                !json_object_get(p->memberships, str(event, "role_id"))) ||
               json_object_set(p->memberships, str(event, "role_id"), json_true())) {
        p->identity_known = false;
    }
    return true;
}
void permissions_dm(permissions *p, const char *id, bool available)
{
    if (!p || !id || !g_uuid_string_is_valid(id))
        return;
    if (!p->dms)
        p->dms = json_object();
    json_t *value = available ? json_true() : json_false();
    if (json_object_get(p->dms, id) != value) {
        if (json_object_size(p->dms) < 1000 || json_object_get(p->dms, id))
            json_object_set(p->dms, id, value);
        ++p->revision;
    }
}
uint64_t permissions_revision(const permissions *p)
{
    return p ? p->revision : 0;
}
uint64_t permissions_event_serial(const permissions *p)
{
    return p ? p->events : 0;
}
static bool owner(const permissions *p)
{
    return p->owner_known && *p->user && !strcmp(p->user, p->owner);
}
permission_decision permissions_role(const permissions *p, role_permission permission)
{
    if (!p || permission < 0 || permission >= PERMISSION_ROLE_COUNT || !*p->user)
        return PERMISSION_UNKNOWN;
    if (owner(p))
        return PERMISSION_ALLOWED;
    if (!p->identity_known || !p->roles)
        return PERMISSION_UNKNOWN;
    const char *id;
    json_t *v;
    bool missing = false;
    json_object_foreach(p->memberships, id, v)
    {
        json_t *role = json_object_get(p->roles, id);
        missing |= !role;
        if (json_is_true(
                json_object_get(json_object_get(role, "permissions"), role_keys[permission])))
            return PERMISSION_ALLOWED;
    }
    return missing || !p->owner_known ? PERMISSION_UNKNOWN : PERMISSION_DENIED;
}
permission_decision permissions_channel(const permissions *p, const char *id,
                                        channel_permission permission)
{
    if (!p || !id || permission < 0 || permission >= PERMISSION_CHANNEL_COUNT || !*p->user)
        return PERMISSION_UNKNOWN;
    json_t *dm = json_object_get(p->dms, id);
    if (dm)
        return permission == PERMISSION_DELETE_MESSAGES || permission == PERMISSION_CONNECT_VOICE ||
                       json_is_false(dm)
                   ? PERMISSION_DENIED
                   : PERMISSION_ALLOWED;
    json_t *channel = json_object_get(p->channels, id);
    if (!channel)
        return PERMISSION_UNKNOWN;
    if (owner(p))
        return PERMISSION_ALLOWED;
    json_t *entries = json_object_get(channel, "permissions");
    if (!json_array_size(entries) &&
        (permission == PERMISSION_READ_CHANNEL || permission == PERMISSION_SEND_MESSAGES))
        return PERMISSION_ALLOWED;
    if (!p->identity_known)
        return PERMISSION_UNKNOWN;
    size_t i;
    json_t *entry;
    json_array_foreach(entries, i, entry)
    {
        if (json_object_get(p->memberships, str(entry, "role_id")) &&
            json_is_true(
                json_object_get(json_object_get(entry, "permissions"), channel_keys[permission])))
            return PERMISSION_ALLOWED;
    }
    return p->owner_known ? PERMISSION_DENIED : PERMISSION_UNKNOWN;
}
permission_decision permissions_message(const permissions *p, const json_t *m, bool edit)
{
    if (!p || !json_is_object(m) || !*p->user)
        return PERMISSION_UNKNOWN;
    bool own = !strcmp(str(m, "author_id"), p->user) && !*str(m, "webhook_id");
    if (edit) {
        if (!own)
            return PERMISSION_DENIED;
        if (json_is_false(json_object_get(p->dms, str(m, "channel_id"))))
            return PERMISSION_DENIED;
        return PERMISSION_ALLOWED;
    }
    return own ? PERMISSION_ALLOWED
               : permissions_channel(p, str(m, "channel_id"), PERMISSION_DELETE_MESSAGES);
}
const json_t *permissions_channels(const permissions *p)
{
    return p ? p->channels : NULL;
}

uint64_t permissions_channel_revision(const permissions *p)
{
    return p ? p->channel_revision : 0;
}
