#include "model/permissions.h"
#include <assert.h>
#include <stdio.h>

#define USER "00000000-0000-4000-8000-000000000001"
#define OTHER "00000000-0000-4000-8000-000000000002"
#define CHANNEL "00000000-0000-4000-8000-000000000003"
#define ROLE "00000000-0000-4000-8000-000000000004"
#define SECOND "00000000-0000-4000-8000-000000000005"
static json_t *load(const char *s)
{
    json_error_t error;
    json_t *value = json_loads(s, JSON_REJECT_DUPLICATES, &error);
    assert(value);
    return value;
}
static void update(permissions *p, permission_source source, const char *s)
{
    json_t *value = load(s);
    assert(permissions_update(p, source, value));
    json_decref(value);
}
static void identity(permissions *p, const char *s)
{
    json_t *value = load(s);
    assert(permissions_identity(p, value, permissions_event_serial(p)));
    json_decref(value);
}
static void event(permissions *p, const char *s)
{
    json_t *value = load(s);
    assert(permissions_event(p, value));
    json_decref(value);
}
static void member_colors(void)
{
    permissions *p = permissions_create();
    assert(p);
    json_t *roles = load("[{\"id\":\"" ROLE "\",\"color\":null},"
                         "{\"id\":\"" SECOND "\",\"color\":\"#aBcD09\"}]");
    assert(permissions_member_color(p, OTHER, NULL) == 0x1ffffff);
    assert(permissions_member_color(p, OTHER, roles) == 0x1abcd09);
    json_t *first = json_array_get(roles, 0);
    const char *invalid[] = {"red", "#gg0000", "#123", "#1234567", "\033[31m", "#12 456"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        assert(!json_object_set_new(first, "color", json_string(invalid[i])));
        assert(permissions_member_color(p, OTHER, roles) == 0x1abcd09);
    }
    assert(!json_object_set_new(first, "color", json_stringn("#ffffff\0", 8)));
    assert(permissions_member_color(p, OTHER, roles) == 0x1abcd09);
    assert(!json_object_set_new(first, "color", json_string("#000000")));
    assert(permissions_member_color(p, OTHER, roles) == 0x1000000);
    identity(p, "{\"id\":\"" USER "\",\"roles\":[{\"id\":\"" ROLE "\",\"color\":\"#Ff0000\"}]}");
    assert(permissions_member_color(p, USER, NULL) == 0x1ff0000);
    update(p, PERMISSIONS_ROLES,
           "{\"roles\":[{\"id\":\"" ROLE "\",\"color\":null,\"permissions\":{}},"
           "{\"id\":\"" SECOND "\",\"color\":\"#00ff00\",\"permissions\":{}}]}");
    assert(permissions_member_color(p, OTHER, roles) == 0x100ff00);
    assert(permissions_member_color(p, USER, NULL) == 0x1ffffff);
    event(p, "{\"type\":\"role_add\",\"user_id\":\"" USER "\",\"role_id\":\"" SECOND "\"}");
    assert(permissions_member_color(p, USER, NULL) == 0x100ff00);
    event(p, "{\"type\":\"role_remove\",\"user_id\":\"" USER "\",\"role_id\":\"" SECOND "\"}");
    assert(permissions_member_color(p, USER, NULL) == 0x1ffffff);
    update(p, PERMISSIONS_ROLES, "{\"roles\":[]}");
    assert(permissions_member_color(p, OTHER, roles) == 0x1ffffff);
    permissions_reset(p);
    assert(permissions_member_color(p, USER, NULL) == 0x1ffffff);
    assert(permissions_member_color(p, OTHER, roles) == 0x1000000);
    json_decref(roles);
    permissions_destroy(p);
}
int main(void)
{
    member_colors();
    permissions *p = permissions_create(), *other = permissions_create();
    assert(p && other);
    assert(permissions_role(p, PERMISSION_MANAGE_ROLES) == PERMISSION_UNKNOWN);
    identity(p, "{\"id\":\"" USER "\",\"roles\":[]}");
    update(p, PERMISSIONS_SERVER, "{\"owner_id\":\"" OTHER "\"}");
    update(p, PERMISSIONS_ROLES,
           "{\"roles\":[{\"id\":\"" ROLE
           "\",\"permissions\":{\"manage_roles\":true}},{\"id\":\"" SECOND
           "\",\"permissions\":{\"manage_roles\":false}}]}");
    update(p, PERMISSIONS_CHANNELS, "{\"channels\":[{\"id\":\"" CHANNEL "\",\"permissions\":[]}]}");
    assert(permissions_channel(p, CHANNEL, PERMISSION_READ_CHANNEL) == PERMISSION_ALLOWED);
    assert(permissions_channel(p, CHANNEL, PERMISSION_SEND_MESSAGES) == PERMISSION_ALLOWED);
    assert(permissions_channel(p, CHANNEL, PERMISSION_DELETE_MESSAGES) == PERMISSION_DENIED);
    assert(permissions_channel(p, CHANNEL, PERMISSION_CONNECT_VOICE) == PERMISSION_DENIED);
    json_t *mine = load("{\"channel_id\":\"" CHANNEL "\",\"author_id\":\"" USER "\"}");
    json_t *theirs = load("{\"channel_id\":\"" CHANNEL "\",\"author_id\":\"" OTHER "\"}");
    assert(permissions_message(p, mine, false) == PERMISSION_ALLOWED);
    assert(permissions_message(p, mine, true) == PERMISSION_ALLOWED);
    assert(!json_object_set_new(mine, "webhook_id", json_string(ROLE)));
    assert(permissions_message(p, mine, true) == PERMISSION_DENIED);
    assert(permissions_message(p, mine, false) == PERMISSION_DENIED);
    assert(!json_object_del(mine, "webhook_id"));
    assert(permissions_message(p, theirs, false) == PERMISSION_DENIED);
    assert(permissions_message(p, theirs, true) == PERMISSION_DENIED);
    uint64_t serial = permissions_event_serial(p);
    event(p, "{\"type\":\"role_add\",\"user_id\":\"" USER "\",\"role_id\":\"" ROLE "\"}");
    event(p, "{\"type\":\"role_add\",\"user_id\":\"" USER "\",\"role_id\":\"" SECOND "\"}");
    assert(permissions_role(p, PERMISSION_MANAGE_ROLES) == PERMISSION_ALLOWED);
    json_t *stale = load("{\"id\":\"" USER "\",\"roles\":[]}");
    assert(!permissions_identity(p, stale, serial));
    assert(permissions_role(p, PERMISSION_MANAGE_ROLES) == PERMISSION_ALLOWED);
    update(p, PERMISSIONS_CHANNELS,
           "{\"channels\":[{\"id\":\"" CHANNEL "\",\"permissions\":[{\"role_id\":\"" ROLE
           "\",\"permissions\":{\"read_channel\":true,\"delete_messages\":true}},{\"role_id\":"
           "\"" SECOND "\",\"permissions\":{\"delete_messages\":false}}]}]}");
    assert(permissions_channel(p, CHANNEL, PERMISSION_SEND_MESSAGES) == PERMISSION_DENIED);
    assert(permissions_message(p, theirs, false) == PERMISSION_ALLOWED);
    assert(permissions_message(p, theirs, true) == PERMISSION_DENIED);
    event(p, "{\"type\":\"role_remove\",\"user_id\":\"" USER "\",\"role_id\":\"" ROLE "\"}");
    assert(permissions_role(p, PERMISSION_MANAGE_ROLES) == PERMISSION_DENIED);
    assert(permissions_channel(p, CHANNEL, PERMISSION_READ_CHANNEL) == PERMISSION_DENIED);
    assert(permissions_message(p, theirs, false) == PERMISSION_DENIED);
    assert(permissions_message(p, mine, false) == PERMISSION_ALLOWED);
    assert(permissions_role(other, PERMISSION_MANAGE_ROLES) == PERMISSION_UNKNOWN);
    update(p, PERMISSIONS_SERVER, "{\"owner_id\":\"" USER "\"}");
    assert(permissions_role(p, PERMISSION_MANAGE_ROLES) == PERMISSION_ALLOWED);
    assert(permissions_message(p, theirs, false) == PERMISSION_ALLOWED);
    assert(permissions_message(p, theirs, true) == PERMISSION_DENIED);
    permissions_dm(p, CHANNEL, true);
    assert(permissions_message(p, theirs, false) == PERMISSION_DENIED);
    assert(permissions_channel(p, CHANNEL, PERMISSION_SEND_MESSAGES) == PERMISSION_ALLOWED);
    permissions_dm(p, CHANNEL, false);
    assert(permissions_message(p, mine, true) == PERMISSION_DENIED);
    assert(permissions_message(p, mine, false) == PERMISSION_ALLOWED);
    permissions_reset(p);
    assert(permissions_message(p, mine, false) == PERMISSION_UNKNOWN);
    identity(p, "{\"id\":\"" USER "\",\"roles\":[]}");
    update(p, PERMISSIONS_SERVER, "{\"owner_id\":null}");
    update(p, PERMISSIONS_ROLES, "{\"roles\":[]}");
    event(p, "{\"type\":\"role_add\",\"user_id\":\"" USER "\",\"role_id\":\"" ROLE "\"}");
    assert(permissions_role(p, PERMISSION_MANAGE_ROLES) == PERMISSION_UNKNOWN);
    json_t *bad =
        load("{\"channels\":[{\"id\":\"" CHANNEL "\",\"permissions\":[{\"role_id\":\"" ROLE
             "\",\"permissions\":{\"read_channel\":\"true\"}}]}]}");
    assert(!permissions_update(p, PERMISSIONS_CHANNELS, bad));
    assert(permissions_channel(p, CHANNEL, PERMISSION_READ_CHANNEL) == PERMISSION_UNKNOWN);
    json_decref(bad);
    bad = load("{\"roles\":null}");
    assert(!permissions_update(p, PERMISSIONS_ROLES, bad));
    json_decref(bad);
    bad = load("{\"id\":\"" USER "\",\"roles\":null}");
    assert(!permissions_identity(p, bad, permissions_event_serial(p)));
    json_decref(bad);
    json_decref(stale);
    json_decref(mine);
    json_decref(theirs);
    permissions_destroy(p);
    permissions_destroy(other);
    puts("permission policy: OK");
}
