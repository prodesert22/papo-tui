#include "app/instances.h"
#include "ui/send_editor.h"
#include "app/session_store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

instance_connections *instances_create(void) { return calloc(1, sizeof(instance_connections)); }
void instances_destroy(instance_connections *connections)
{
    if (!connections) return;
    for (size_t i = 0; i < connections->count; ++i) {
        session_store_unwatch(connections->sessions, connections->items[i].auth);
        if (connections->items[i].media_destroy) connections->items[i].media_destroy(connections->items[i].media);
        send_editor_destroy(connections->items[i].editor);
        auth_destroy(connections->items[i].auth);
    }
    session_store_destroy(connections->sessions);
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
send_editor *instances_editor(const instance_connections *connections, const auth_client *auth)
{
    if (!connections) return NULL;
    size_t index = instances_index(connections, auth);
    return index == SIZE_MAX ? NULL : connections->items[index].editor;
}
bool instances_attach(instance_connections *connections, auth_client *auth)
{
    if (!connections || !auth_has_session(auth)) return false;
    size_t index = instances_index(connections, auth);
    if (index == SIZE_MAX) {
        send_editor *editor = send_editor_create(auth);
        if (!editor) return false;
        for (size_t i = 0; i < connections->count; ++i)
            if (!strcmp(connections->items[i].url, auth_instance_url(auth))) { index = i; break; }
        if (index == SIZE_MAX) {
            if (connections->count == INSTANCE_LIMIT) { send_editor_destroy(editor); return false; }
            index = connections->count++;
        } else {
            session_store_unwatch(connections->sessions, connections->items[index].auth);
            if (connections->items[index].media_destroy) connections->items[index].media_destroy(connections->items[index].media);
            connections->items[index].media = NULL;
            send_editor_destroy(connections->items[index].editor);
            auth_destroy(connections->items[index].auth);
        }
        connections->items[index].editor = editor;
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
    session_store_unwatch(connections->sessions, auth);
    if (connections->items[index].media_destroy) connections->items[index].media_destroy(connections->items[index].media);
    send_editor_destroy(connections->items[index].editor);
    memmove(connections->items + index, connections->items + index + 1,
            (connections->count - index - 1) * sizeof(connections->items[0]));
    --connections->count;
    memset(&connections->items[connections->count], 0, sizeof(connections->items[0]));
}
void instances_media_event(instance_connections *connections, auth_client *auth, const json_t *event)
{
    size_t i = instances_index(connections, auth);
    if (i != SIZE_MAX && connections->items[i].media_update)
        connections->items[i].media_update(connections->items[i].media, event);
}
bool instances_poll(instance_connections *connections, auth_client *active)
{
    bool changed = false;
    for (size_t i = 0; i < connections->count; ++i) {
        auth_client *auth = connections->items[i].auth;
        auth_realtime_active(auth, auth == active);
        if (auth == active) {
            if (connections->items[i].media_idle) connections->items[i].media_idle(connections->items[i].media);
            continue;
        }
        auth_state before = auth_get_state(auth);
        bool session = auth_has_session(auth);
        auth_poll(auth);
        if (connections->items[i].media_idle) connections->items[i].media_idle(connections->items[i].media);
        send_editor_poll(connections->items[i].editor);
        outbox *box = send_editor_outbox(connections->items[i].editor);
        outbox_operation operation; json_t *confirmed;
        while ((confirmed = outbox_take_confirmed(box, &operation))) {
            json_decref(confirmed); outbox_reconciled(box, operation.id);
        }
        changed |= before != auth_get_state(auth) || session != auth_has_session(auth);
        json_t *event;
        while ((event = auth_next_event(auth))) { instances_media_event(connections, auth, event); json_decref(event); }
        if (auth_response_ready(auth)) json_decref(auth_take_response(auth, NULL, NULL));
    }
    return changed;
}
