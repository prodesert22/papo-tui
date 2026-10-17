#define _POSIX_C_SOURCE 200809L
#include "app/outbox.h"
#include "model/conversation/history.h"
#include "net/clock.h"
#include "ui/media/media_cache.h"
#include <curl/curl.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void tick(auth_client *auth, history *h, outbox *box)
{
    auth_poll(auth);
    if (box)
        outbox_poll(box);
    if (h)
        history_poll(h);
    g_usleep(1000);
}
static const char *s(json_t *o, const char *key)
{
    const char *v = json_string_value(json_object_get(o, key));
    return v ? v : "";
}
static bool waiting(uint64_t start)
{
    uint64_t now;
    return net_now_ms(&now) && now - start < 15000;
}
int main(int argc, char **argv)
{
    const char *username = getenv("PAPO_PROBE_USERNAME"), *password = getenv("PAPO_PROBE_PASSWORD");
    if (argc < 4 || !username || !password || !history_uuid(argv[2]) || !history_uuid(argv[3]) ||
        curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
        return 1;
    auth_client *auth = auth_create();
    history *h = NULL;
    outbox *box = NULL;
    media_cache *cache = NULL;
    media_resource *resource = NULL;
    json_t *report = json_object(), *emoji = json_pack("{s:s}", "unicode", "👍");
    uint64_t start = 0, operation = 0;
    bool replied = false, added = false, users = false, removed = false, poster = argc < 5;
    bool edited = false, deleted = false, queued_edit_cancelled = false;
    char reply_id[37] = "";
    if (!auth || !report || !emoji || !net_now_ms(&start) ||
        !auth_start_with_policy(auth, argv[1], username, password, "", true))
        goto done;
    while (auth_busy(auth) && waiting(start))
        tick(auth, NULL, NULL);
    if (!auth_has_session(auth))
        goto done;
    h = history_create(auth);
    box = outbox_create(auth);
    if (!h || !box)
        goto done;
    history_enable_r8(h);
    auth_realtime_active(auth, true);
    auth_connect_ws(auth);
    bool selected = false, resolved = false;
    net_now_ms(&start);
    while (waiting(start)) {
        tick(auth, h, box);
        if (!selected && !history_loading(h)) {
            for (size_t i = 0; i < history_channel_count(h); ++i)
                if (!strcmp(s(history_channel(h, i), "id"), argv[2])) {
                    selected = history_select(h, i);
                    break;
                }
            if (!selected && !resolved && history_channel_count(h)) {
                inbox_resolve_dm(history_inbox(h), argv[2]);
                resolved = true;
            }
        }
        if (selected && history_message_count(h) && !history_loading(h) && !auth_busy(auth))
            break;
    }
    if (!selected || !json_is_object(history_find_message(h, argv[3])))
        goto done;
    if (!outbox_set_draft(box, argv[2], "Papo TUI synthetic reply integration") ||
        !outbox_set_reply(box, argv[2], argv[3]) ||
        outbox_submit(box, argv[2], false, &operation) != OUTBOX_ACCEPTED)
        goto done;
    net_now_ms(&start);
    while (waiting(start)) {
        tick(auth, h, box);
        outbox_operation sent;
        json_t *m = outbox_take_confirmed(box, &sent);
        if (m) {
            replied = !strcmp(s(m, "reply_to"), argv[3]) && !strcmp(sent.reply_to, argv[3]);
            snprintf(reply_id, sizeof(reply_id), "%s", s(m, "id"));
            history_confirm(h, m, sent.generation);
            outbox_reconciled(box, sent.id);
            json_decref(m);
            break;
        }
    }
    if (!history_react(h, argv[3], emoji, false))
        goto done;
    net_now_ms(&start);
    while (waiting(start)) {
        tick(auth, h, box);
        if (history_reaction_mine(history_find_message(h, argv[3]), emoji) &&
            !history_recovering(h) && !history_loading(h) && !auth_busy(auth)) {
            added = true;
            break;
        }
    }
    if (!added)
        goto done;
    history_reactors_open(h, argv[3]);
    net_now_ms(&start);
    while (waiting(start)) {
        tick(auth, h, box);
        size_t i;
        json_t *u;
        json_array_foreach(history_reactors(h), i,
                           u) if (!strcmp(s(u, "user_id"), auth_user_id(auth))) users = true;
        if (users)
            break;
    }
    history_reactors_close(h);
    if (!history_react(h, argv[3], emoji, true))
        goto done;
    net_now_ms(&start);
    while (waiting(start)) {
        tick(auth, h, box);
        if (!history_reaction_mine(history_find_message(h, argv[3]), emoji) &&
            !history_recovering(h) && !history_loading(h) && !auth_busy(auth)) {
            /* The queue must have dispatched the DELETE before an empty
             * membership snapshot can count as a confirmation. */
            json_t *m = history_find_message(h, argv[3]);
            removed = json_is_object(m);
            if (removed)
                break;
        }
    }
    if (argc >= 5 && history_uuid(argv[4])) {
        cache = media_cache_create(auth);
        resource = media_cache_acquire(cache, AUTH_MEDIA_ATTACHMENT, argv[4], 1, true);
        image_decode_request request = {
            .width = 160,
            .height = 120,
            .native = true,
            .terminal = {.protocol = GRAPHICS_KITTY, .cell_width = 8, .cell_height = 16}};
        net_now_ms(&start);
        while (resource && waiting(start)) {
            tick(auth, h, box);
            uint64_t now = 0;
            net_now_ms(&now);
            media_cache_begin(cache);
            media_resource_use_image(resource, false, false);
            const graphics_image *image = media_resource_image(resource, false, request, now);
            media_cache_poll(cache, now);
            if (image && image->source_width > 0 && media_resource_path(resource)) {
                poster = !media_resource_animated(resource, false);
                break;
            }
            if (media_resource_error(resource, false))
                break;
        }
    }
    net_now_ms(&start);
    while ((history_recovering(h) || history_loading(h) || auth_busy(auth)) && waiting(start))
        tick(auth, h, box);
    /* Refresh can cancel a mutation still queued behind an auxiliary read.
     * The UI must receive a result instead of remaining pending forever. */
    if (history_edit_message(h, reply_id, "Canceled before dispatch")) {
        history_recover(h);
        char message[37];
        bool success = true;
        queued_edit_cancelled = history_take_edit_result(h, message, &success) && !success &&
                                !strcmp(message, reply_id);
    }
    net_now_ms(&start);
    while ((history_recovering(h) || history_loading(h) || auth_busy(auth)) && waiting(start))
        tick(auth, h, box);
    if (history_edit_message(h, reply_id, "Papo TUI synthetic edited reply")) {
        net_now_ms(&start);
        char message[37];
        bool success = false;
        while (waiting(start)) {
            tick(auth, h, box);
            if (history_take_edit_result(h, message, &success)) {
                edited = success && !strcmp(message, reply_id) &&
                         !strcmp(s(history_find_message(h, reply_id), "content"),
                                 "Papo TUI synthetic edited reply");
                break;
            }
        }
    }
    net_now_ms(&start);
    while ((history_recovering(h) || history_loading(h) || auth_busy(auth)) && waiting(start))
        tick(auth, h, box);
    if (edited && history_delete_message(h, reply_id)) {
        net_now_ms(&start);
        while (waiting(start)) {
            tick(auth, h, box);
            if (!json_is_object(history_find_message(h, reply_id)) && !history_recovering(h) &&
                !auth_busy(auth)) {
                deleted = true;
                break;
            }
        }
    }
done:
    json_object_set_new(report, "queued_edit_cancelled", json_boolean(queued_edit_cancelled));
    json_object_set_new(report, "message_edited", json_boolean(edited));
    json_object_set_new(report, "message_deleted", json_boolean(deleted));
    json_object_set_new(report, "reply", json_boolean(replied));
    json_object_set_new(report, "reaction_added", json_boolean(added));
    json_object_set_new(report, "participants", json_boolean(users));
    json_object_set_new(report, "reaction_removed", json_boolean(removed));
    json_object_set_new(report, "video_poster", json_boolean(poster));
    json_object_set_new(report, "reply_id", json_string(reply_id));
    json_dumpf(report, stdout, JSON_COMPACT);
    putchar('\n');
    media_resource_release(resource);
    media_cache_destroy(cache);
    outbox_destroy(box);
    history_destroy(h);
    if (auth && auth_has_session(auth) && auth_logout(auth)) {
        net_now_ms(&start);
        while (auth_busy(auth) && waiting(start))
            tick(auth, NULL, NULL);
    }
    auth_destroy(auth);
    json_decref(report);
    json_decref(emoji);
    curl_global_cleanup();
    return replied && added && users && removed && poster && edited && deleted &&
                   queued_edit_cancelled
               ? 0
               : 2;
}
