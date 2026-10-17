#define _POSIX_C_SOURCE 200809L
#include "app/outbox.h"
#include "net/clock.h"
#include "net/rate_limit.h"
#include <curl/curl.h>
#include <glib.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void tick(auth_client *session, outbox *box, unsigned milliseconds)
{
    struct timespec pause = {.tv_nsec = 1000000};
    uint64_t start, now;
    if (!net_now_ms(&start))
        exit(2);
    do {
        auth_poll(session);
        if (box)
            outbox_poll(box);
        g_usleep((gulong)(pause.tv_sec * 1000000 + pause.tv_nsec / 1000));
        if (!net_now_ms(&now))
            exit(2);
    } while (now - start < milliseconds);
}

static bool finish(auth_client *session, outbox *box, uint64_t id)
{
    uint64_t start, now;
    if (!net_now_ms(&start))
        return false;
    while (true) {
        outbox_operation operation;
        bool ready = outbox_get_operation(box, id, &operation);
        if (!ready && !auth_busy(session))
            return false;
        if (ready && operation.state != OUTBOX_QUEUED && operation.state != OUTBOX_SENDING &&
            !auth_busy(session))
            return true;
        tick(session, box, 1);
        if (!net_now_ms(&now) || now - start > 20000)
            return false;
    }
}

static const char *setting(const char *name, const char *fallback)
{
    const char *value = getenv(name);
    return value ? value : fallback;
}

int main(int argc, char **argv)
{
    if (argc < 3 || !setlocale(LC_ALL, "") || curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
        return 1;
    auth_client *session = auth_create();
    if (!session)
        return 1;
    const char *mode = argv[2],
               *channel = setting("PAPO_SEND_CHANNEL", "00000000-0000-4000-8000-000000000003");
    const char *second_channel =
        setting("PAPO_SEND_SECOND_CHANNEL", "00000000-0000-4000-8000-000000000004");
    const char *text = setting("PAPO_SEND_TEXT", "Olá, ação!\nSegunda linha.");
    if (!strcmp(mode, "grapheme"))
        text = "cafe";
    if (!strcmp(mode, "emoji-append"))
        text = "🙂";
    auth_start(session, argv[1], setting("PAPO_PROBE_USERNAME", "josé"),
               setting("PAPO_PROBE_PASSWORD", "Synthetic!123"),
               setting("PAPO_PROBE_SERVER_PASSWORD", ""));
    while (auth_busy(session))
        tick(session, NULL, 1);
    uint64_t permission_start = 0, permission_now = 0;
    net_now_ms(&permission_start);
    do {
        auth_poll(session);
        g_usleep(1000);
        net_now_ms(&permission_now);
    } while (permissions_channel(auth_permissions(session), channel, PERMISSION_SEND_MESSAGES) ==
                 PERMISSION_UNKNOWN &&
             permission_now - permission_start < 10000 && auth_has_session(session));
    outbox *box = outbox_create(session);
    if (!box)
        return 1;
    uint64_t id = 0, second_id = 0;
    if (!strcmp(mode, "empty"))
        text = " \n\t";
    bool saved = outbox_set_draft(box, channel, text);
    outbox_submit_result submitted =
        !strcmp(mode, "read") ? OUTBOX_EMPTY : outbox_submit(box, channel, false, &id);
    bool busy = id && outbox_submit(box, channel, false, NULL) == OUTBOX_BUSY;
    bool need_confirmation = false;
    unsigned count = 0;
    json_t *last_message = NULL;
    if (id) {
        if (!strcmp(mode, "queued-cancel")) {
            while (net_rate_try_acquire() == 1) {
            }
            outbox_poll(box);
            auth_poll(session);
            outbox_cancel(box, id);
        } else {
            outbox_poll(box);
            if (!strcmp(mode, "append") || !strcmp(mode, "replace") || !strcmp(mode, "restore") ||
                !strcmp(mode, "grapheme") || !strcmp(mode, "emoji-append") ||
                !strcmp(mode, "cancel") || !strcmp(mode, "channels"))
                tick(session, box, 40);
            if (!strcmp(mode, "append") || !strcmp(mode, "emoji-append") ||
                !strcmp(mode, "restore")) {
                if (!strcmp(mode, "restore"))
                    outbox_set_draft(box, channel, "Rascunho alterado");
                char edited[OUTBOX_TEXT_BYTES];
                snprintf(edited, sizeof(edited), "%s continuação", text);
                outbox_set_draft(box, channel, edited);
            }
            if (!strcmp(mode, "replace"))
                outbox_set_draft(box, channel, "Outro rascunho");
            if (!strcmp(mode, "grapheme"))
                outbox_set_draft(box, channel, "cafe\xcc\x81");
            if (!strcmp(mode, "cancel"))
                outbox_cancel(box, id);
            if (!strcmp(mode, "channels")) {
                outbox_set_draft(box, second_channel, "Texto do canal B");
                outbox_submit(box, second_channel, false, &second_id);
            }
        }
        finish(session, box, id);
        if (second_id)
            finish(session, box, second_id);
        if (!strcmp(mode, "retry")) {
            need_confirmation = outbox_retry(box, id, false, NULL) == OUTBOX_CONFIRM_DUPLICATION;
            outbox_retry(box, id, true, &second_id);
            if (second_id)
                finish(session, box, second_id);
        }
        if (!strcmp(mode, "rate")) {
            for (unsigned i = 0; i < 30; ++i) {
                outbox_operation confirmed;
                json_t *message = outbox_take_confirmed(box, &confirmed);
                if (message) {
                    ++count;
                    json_decref(last_message);
                    last_message = message;
                }
                if (i < 29) {
                    outbox_set_draft(box, channel, text);
                    outbox_submit(box, channel, false, &second_id);
                    if (second_id)
                        finish(session, box, second_id);
                }
            }
        }
    }
    if (!strcmp(mode, "context")) {
        auth_cancel(session);
        auth_start(session, setting("PAPO_SEND_NEW_URL", argv[1]),
                   setting("PAPO_PROBE_USERNAME", "josé"),
                   setting("PAPO_PROBE_PASSWORD", "Synthetic!123"), "");
        while (auth_busy(session))
            tick(session, box, 1);
        outbox_poll(box);
    }
    outbox_operation operation = {0}, second = {0};
    bool result_ready = outbox_get_operation(box, id, &operation);
    bool second_ready = second_id && outbox_get_operation(box, second_id, &second);
    if (strcmp(mode, "rate")) {
        outbox_operation confirmed;
        json_t *message;
        while ((message = outbox_take_confirmed(box, &confirmed))) {
            ++count;
            json_decref(last_message);
            last_message = message;
        }
    }
    const char *draft = outbox_draft(box, channel, NULL),
               *other = outbox_draft(box, second_channel, NULL);
    json_t *result =
        json_pack("{s:b,s:i,s:b,s:b,s:i,s:I,s:i,s:I,s:b,s:b,s:b,s:i,s:s,s:s}", "saved", saved,
                  "submit", (int)submitted, "busy", busy, "result_ready", result_ready, "state",
                  (int)operation.state, "id", (json_int_t)id, "status", (int)operation.status,
                  "second_id", (json_int_t)second_id, "second_ready", second_ready, "transmitted",
                  operation.transmitted, "need_confirmation", need_confirmation, "confirmed_count",
                  (int)count, "draft", draft ? draft : "", "second_draft", other ? other : "");
    json_object_set_new(result, "second_state", json_integer((json_int_t)second.state));
    json_object_set_new(result, "session", json_boolean(auth_has_session(session)));
    if (last_message)
        json_object_set(result, "message", last_message);
    if (!strcmp(mode, "read")) {
        char endpoint[160];
        snprintf(endpoint, sizeof(endpoint), "/channels/%s/messages?order=desc", channel);
        if (auth_get_json(session, endpoint)) {
            while (auth_busy(session))
                tick(session, box, 1);
            long status = 0;
            json_t *history = auth_take_response(session, &status, NULL);
            json_object_set_new(result, "read_status", json_integer(status));
            if (history) {
                json_object_set(result, "history", history);
                json_decref(history);
            }
        }
    }
    json_dumpf(result, stdout, JSON_COMPACT);
    putchar('\n');
    json_decref(result);
    json_decref(last_message);
    outbox_destroy(box);
    auth_destroy(session);
    curl_global_cleanup();
    return 0;
}
