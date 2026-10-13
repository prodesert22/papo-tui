#include "app/instances.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

instance_connections *instances_create(void) { return calloc(1, sizeof(instance_connections)); }
void instances_destroy(instance_connections *connections)
{
    if (!connections) return;
    for (size_t i = 0; i < connections->count; ++i) auth_destroy(connections->items[i].auth);
    free(connections);
}
size_t instances_index(const instance_connections *connections, const auth_client *auth)
{
    for (size_t i = 0; i < connections->count; ++i) if (connections->items[i].auth == auth) return i;
    return SIZE_MAX;
}
bool instances_contains(const instance_connections *connections, const auth_client *auth)
{
    return connections && instances_index(connections, auth) != SIZE_MAX;
}
bool instances_attach(instance_connections *connections, auth_client *auth)
{
    if (!connections || !auth_has_session(auth)) return false;
    size_t index = instances_index(connections, auth);
    if (index == SIZE_MAX) {
        for (size_t i = 0; i < connections->count; ++i)
            if (!strcmp(connections->items[i].url, auth_instance_url(auth))) { index = i; break; }
        if (index == SIZE_MAX) {
            if (connections->count == INSTANCE_LIMIT) return false;
            index = connections->count++;
        } else auth_destroy(connections->items[index].auth);
    }
    instance_connection *item = &connections->items[index];
    item->auth = auth;
    snprintf(item->url, sizeof(item->url), "%s", auth_instance_url(auth));
    snprintf(item->name, sizeof(item->name), "%s", *auth_server_name(auth) ? auth_server_name(auth) : "Servidor");
    return true;
}
void instances_detach(instance_connections *connections, auth_client *auth)
{
    size_t index = instances_index(connections, auth);
    if (index == SIZE_MAX) return;
    memmove(connections->items + index, connections->items + index + 1,
            (connections->count - index - 1) * sizeof(connections->items[0]));
    --connections->count;
    memset(&connections->items[connections->count], 0, sizeof(connections->items[0]));
}
bool instances_poll(instance_connections *connections, auth_client *active)
{
    bool changed = false;
    for (size_t i = 0; i < connections->count; ++i) {
        auth_client *auth = connections->items[i].auth;
        if (auth == active) continue;
        auth_state before = auth_get_state(auth);
        bool session = auth_has_session(auth);
        auth_poll(auth);
        changed |= before != auth_get_state(auth) || session != auth_has_session(auth);
        json_t *event;
        while ((event = auth_next_event(auth))) json_decref(event);
        if (auth_response_ready(auth)) json_decref(auth_take_response(auth, NULL, NULL));
    }
    return changed;
}
