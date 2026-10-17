#define _POSIX_C_SOURCE 200809L
#include "app/outbox.h"
#include "model/conversation/discovery.h"
#include "model/conversation/history.h"
#include "net/clock.h"
#include "net/rate_limit.h"
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void tick(auth_client *auth, history *h, outbox *box)
{
    auth_poll(auth);
    if (h)
        history_poll(h);
    if (box)
        outbox_poll(box);
    outbox_operation operation;
    json_t *message;
    while (box && (message = outbox_take_confirmed(box, &operation))) {
        json_decref(message);
        outbox_reconciled(box, operation.id);
    }
    g_usleep(1000);
}
static void poll_for(auth_client *auth, history *h, outbox *box, unsigned ms)
{
    uint64_t start = 0, now = 0;
    net_now_ms(&start);
    do {
        tick(auth, h, box);
        net_now_ms(&now);
    } while (now - start < ms);
}
int main(int argc, char **argv)
{
    if (argc != 3 || !history_uuid(argv[2]) || curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
        return 1;
    auth_client *auth = auth_create();
    const char *username = getenv("PAPO_PROBE_USERNAME"), *password = getenv("PAPO_PROBE_PASSWORD");
    if (!auth || !auth_start(auth, argv[1], username ? username : "josé",
                             password ? password : "Synthetic!123", ""))
        return 2;
    uint64_t start = 0, now = 0;
    net_now_ms(&start);
    while (auth_busy(auth)) {
        auth_poll(auth);
        g_usleep(1000);
        net_now_ms(&now);
        if (now - start > 15000)
            return 3;
    }
    if (!auth_has_session(auth))
        return 4;
    history *h = history_create(auth);
    outbox *box = outbox_create(auth);
    if (!h || !box)
        return 5;
    history_enable_r8(h);
    auth_realtime_active(auth, true);
    auth_connect_ws(auth);
    bool selected = false;
    do {
        tick(auth, h, box);
        if (!selected && !history_loading(h))
            for (size_t i = 0; i < history_channel_count(h); ++i) {
                const char *id = json_string_value(json_object_get(history_channel(h, i), "id"));
                if (id && !strcmp(id, argv[2]))
                    selected = history_select(h, i);
            }
        net_now_ms(&now);
        if (now - start > 15000)
            return 6;
    } while (!selected || !history_message_count(h) || auth_busy(auth) ||
             *auth_permissions_notice(auth) || auth_ws_state(auth) != WS_OPEN);
    puts("{\"ready\":true}");
    fflush(stdout);
    char command[64];
    uint64_t operation_id = 0;
    bool direct_read = false;
    long direct_status = 0;
    while (fgets(command, sizeof(command), stdin)) {
        bool accepted = false;
        if (!strcmp(command, "quit\n"))
            break;
        if (!strcmp(command, "queue-send\n")) {
            net_rate_defer(2);
            outbox_set_draft(box, argv[2], "Preserved queued draft");
            accepted = outbox_submit(box, argv[2], false, &operation_id) == OUTBOX_ACCEPTED;
            outbox_poll(box);
        } else if (!strcmp(command, "queue-attachment\n")) {
            GBytes *bytes = g_bytes_new_static("permission fixture", 18);
            attachment_file *file = attachment_import_bytes(bytes, "fixture.txt", NULL, NULL);
            g_bytes_unref(bytes);
            net_rate_defer(2);
            outbox_set_draft(box, argv[2], "Preserved attachment draft");
            accepted = file && outbox_add_attachment(box, argv[2], file) &&
                       outbox_submit(box, argv[2], false, &operation_id) == OUTBOX_ACCEPTED;
            attachment_unref(file);
            outbox_poll(box);
        } else if (!strcmp(command, "load-pins\n")) {
            history_pins_refresh(h);
            poll_for(auth, h, box, 300);
        } else if (!strcmp(command, "queue-pin\n") || !strcmp(command, "queue-unpin\n")) {
            net_rate_defer(2);
            const char *id = json_string_value(json_object_get(history_message(h, 0), "id"));
            accepted = id && history_pin_message(h, id, !strcmp(command, "queue-unpin\n"));
            history_poll(h);
        } else if (!strcmp(command, "queue-pins-read\n")) {
            char path[100];
            snprintf(path, sizeof(path), "/channels/%s/pinned", argv[2]);
            net_rate_defer(2);
            accepted = auth_get_json(auth, path);
            direct_read = accepted;
            direct_status = 0;
        } else if (!strcmp(command, "queue-delete\n")) {
            net_rate_defer(2);
            const char *id = json_string_value(json_object_get(history_message(h, 0), "id"));
            accepted = id && history_delete_message(h, id);
            history_poll(h);
        } else if (!strcmp(command, "select\n")) {
            for (size_t i = 0; i < history_channel_count(h); ++i) {
                const char *id = json_string_value(json_object_get(history_channel(h, i), "id"));
                if (id && !strcmp(id, argv[2]))
                    accepted = history_select(h, i);
            }
            poll_for(auth, h, box, 300);
        } else if (!strcmp(command, "refresh\n"))
            auth_permissions_refresh(auth);
        else if (!strcmp(command, "poll\n"))
            poll_for(auth, direct_read ? NULL : h, direct_read ? NULL : box, 300);
        else if (!strcmp(command, "poll-long\n"))
            poll_for(auth, direct_read ? NULL : h, direct_read ? NULL : box, 2300);
        if (direct_read && auth_response_ready(auth)) {
            json_t *response = auth_take_response(auth, &direct_status, NULL);
            json_decref(response);
            direct_read = false;
        }
        outbox_operation operation;
        int state = outbox_get_operation(box, operation_id, &operation) ? (int)operation.state : -1;
        permissions *p = auth_permissions(auth);
        const char *draft = outbox_draft(box, argv[2], NULL);
        json_t *report =
            json_pack("{s:b,s:b,s:i,s:i,s:i,s:s,s:i,s:s}", "accepted", accepted, "session",
                      auth_has_session(auth), "send",
                      permissions_channel(p, argv[2], PERMISSION_SEND_MESSAGES), "delete",
                      permissions_channel(p, argv[2], PERMISSION_DELETE_MESSAGES), "manage_roles",
                      permissions_role(p, PERMISSION_MANAGE_ROLES), "draft", draft ? draft : "",
                      "state", state, "notice", auth_permissions_notice(auth));
        json_object_set_new(report, "messages", json_integer((json_int_t)history_message_count(h)));
        json_object_set_new(report, "read",
                            json_integer(permissions_channel(p, argv[2], PERMISSION_READ_CHANNEL)));
        const GPtrArray *files = outbox_attachments(box, argv[2]);
        json_object_set_new(report, "draft_files", json_integer(files ? files->len : 0));
        json_object_set_new(report, "direct_status", json_integer(direct_status));
        const char *message_id = json_string_value(json_object_get(history_message(h, 0), "id"));
        json_object_set_new(report, "pin_state", json_integer(history_pin_state(h, message_id)));
        json_object_set_new(report, "pin",
                            json_integer(permissions_role(p, PERMISSION_PIN_MESSAGE)));
        json_object_set_new(report, "attach",
                            json_integer(permissions_role(p, PERMISSION_SEND_ATTACHMENT)));
        char *text = json_dumps(report, JSON_COMPACT);
        if (!text)
            return 7;
        puts(text);
        fflush(stdout);
        free(text);
        json_decref(report);
    }
    outbox_destroy(box);
    history_destroy(h);
    auth_destroy(auth);
    curl_global_cleanup();
    return 0;
}
