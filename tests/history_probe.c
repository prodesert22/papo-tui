#define _POSIX_C_SOURCE 200809L
#include "model/history.h"
#include "net/clock.h"
#include <curl/curl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void tick(auth_client *auth, history *h)
{
    auth_poll(auth); if (h) history_poll(h);
    struct timespec pause = {.tv_nsec = 1000000}; nanosleep(&pause, NULL);
}
static void settle(auth_client *auth, history *h)
{
    uint64_t start, now; if (!net_now_ms(&start)) exit(2);
    do {
        tick(auth, h);
        if (!net_now_ms(&now) || now - start > 22000) exit(2);
    } while (auth_busy(auth) || (h && history_loading(h)));
}
static json_t *snapshot(history *h)
{
    json_t *out = json_pack("{s:I,s:b,s:b,s:s}", "count", (json_int_t)history_message_count(h),
                           "more", history_has_more(h), "evicted", history_evicted(h), "status", history_status(h));
    json_t *ids = json_array(), *names = json_array();
    for (size_t i = 0; i < history_message_count(h); ++i) {
        json_t *m = history_message(h, i);
        json_array_append(ids, json_object_get(m, "id"));
        json_array_append_new(names, json_string(history_author(h, json_string_value(json_object_get(m, "author_id")))));
    }
    json_object_set_new(out, "ids", ids); json_object_set_new(out, "authors", names); return out;
}
int main(int argc, char **argv)
{
    if (argc < 3 || !setlocale(LC_ALL, "") || curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) return 1;
    auth_client *auth = auth_create();
    const char *username = getenv("PAPO_PROBE_USERNAME"), *password = getenv("PAPO_PROBE_PASSWORD");
    const char *server_password = getenv("PAPO_PROBE_SERVER_PASSWORD");
    if (!auth) return 1;
    auth_start(auth, argv[1], username ? username : "josé", password ? password : "Synthetic!123", server_password ? server_password : "");
    settle(auth, NULL);
    if (!auth_has_session(auth)) { auth_destroy(auth); curl_global_cleanup(); return 3; }
    history *h = history_create(auth); if (!h) return 1;
    settle(auth, h);
    json_t *out = json_object(), *channels = json_array(), *pages = json_array();
    for (size_t i = 0; i < history_channel_count(h); ++i) json_array_append(channels, history_channel(h, i));
    json_object_set_new(out, "channels", channels);
    json_object_set_new(out, "initial", snapshot(h));
    /* The first text channel is index 1 in the fixture, after its category;
     * real probes can supply an existing channel UUID. */
    size_t selected = SIZE_MAX;
    for (size_t i = 0; i < history_channel_count(h); ++i) {
        json_t *c = history_channel(h, i);
        const char *type = json_string_value(json_object_get(c, "type"));
        const char *id = json_string_value(json_object_get(c, "id"));
        if (!strcmp(type, "text") && (argc < 4 || !strcmp(id, argv[3]))) { selected = i; break; }
    }
    if (selected != SIZE_MAX) history_select(h, selected);
    if (!strcmp(argv[2], "switch")) {
        for (int i = 0; i < 60; ++i) tick(auth, h);
        history_select(h, selected + 1);
    }
    settle(auth, h); json_array_append_new(pages, snapshot(h));
    if (!strcmp(argv[2], "pages") || !strcmp(argv[2], "retry") || !strcmp(argv[2], "recent") || !strcmp(argv[2], "confirm-full")) {
        for (int i = 0; i < 24 && history_has_more(h); ++i) {
            history_older(h); settle(auth, h); json_array_append_new(pages, snapshot(h));
            if (!strcmp(argv[2], "retry") && i == 0) { history_retry(h); settle(auth, h); json_array_append_new(pages, snapshot(h)); }
        }
    }
    json_object_set_new(out, "pages", pages);
    if ((!strcmp(argv[2], "confirm") || !strcmp(argv[2], "confirm-full")) && history_message_count(h)) {
        json_t *message = json_deep_copy(history_message(h, 0));
        if (!message) return 1;
        uint64_t generation = auth_context_generation(auth);
        const char *ids[] = {"00000000-0000-4000-8000-00000000fff1", "00000000-0000-4000-8000-000000000002",
                             "00000000-0000-4000-8000-00000000fff2", "00000000-0000-4000-8000-00000000fff3"};
        const char *times[] = {"2026-10-12T14:00:00.123455999+02:00", "2026-10-12T12:00:00.123456000Z",
                               "2026-10-12T12:00:00.1234560001Z", "2027-01-01T00:00:00Z"};
        bool merged = true;
        for (size_t i = 0; i < 4; ++i) {
            json_object_set_new(message, "id", json_string(ids[i]));
            json_object_set_new(message, "created_at", json_string(times[i]));
            merged &= history_confirm(h, message, generation);
        }
        size_t count = history_message_count(h);
        bool duplicate = history_confirm(h, message, generation) && count == history_message_count(h);
        bool stale = !history_confirm(h, message, generation + 1);
        json_object_set_new(message, "channel_id", json_string("00000000-0000-4000-8000-00000000000b"));
        bool inactive = !history_confirm(h, message, generation) && count == history_message_count(h);
        json_object_set_new(out, "confirmation_checks", json_pack("{s:b,s:b,s:b,s:b}",
                            "merged", merged, "duplicate", duplicate, "stale", stale, "inactive", inactive));
        json_object_set_new(out, "confirmed", snapshot(h));
        json_decref(message);
    }
    if (!strcmp(argv[2], "cache") && history_message_count(h) >= 2) {
        uint64_t first_revision = history_message_revision(h, 0), second_revision = history_message_revision(h, 1);
        for (int i = 0; i < 20; ++i) tick(auth, h);
        bool stable = first_revision == history_message_revision(h, 0) && second_revision == history_message_revision(h, 1);
        json_t *first_message = history_message(h, 0);
        json_t *edit = json_pack("{s:s,s:O,s:O,s:s,s:s}", "type", "message_edit", "id", json_object_get(first_message, "id"),
            "channel_id", json_object_get(first_message, "channel_id"), "content", "Changed once", "edited_at", "2026-10-14T00:00:00Z");
        bool changed = history_event(h, edit, auth_context_generation(auth)); json_decref(edit);
        changed &= first_revision != history_message_revision(h, 0) && second_revision == history_message_revision(h, 1);
        const char *ephemeral = "00000000-0000-4000-8000-000000000999";
        json_t *presence = json_pack("{s:s,s:s,s:s,s:s}", "type", "presence_update", "user_id", ephemeral,
            "status", "online", "nickname", "EA -* NA");
        bool cached = history_event(h, presence, auth_context_generation(auth)); json_decref(presence);
        cached &= history_mention_name(h, ephemeral) != NULL;
        uint64_t now = 0; net_now_ms(&now);
        history_users_active(h, true); history_maintain(h, now + 301000);
        bool pinned = history_mention_name(h, ephemeral) != NULL;
        history_users_active(h, false); history_maintain(h, now + 302001);
        bool expired = history_mention_name(h, ephemeral) == NULL;
        const char *author = json_string_value(json_object_get(history_message(h, 0), "author_id"));
        bool retained = history_mention_name(h, author) != NULL;
        json_object_set_new(out, "cache_checks", json_pack("{s:b,s:b,s:b,s:b,s:b,s:b}", "stable", stable,
            "only_changed_message", changed, "cached", cached, "popup_pinned", pinned, "expired", expired, "author_retained", retained));
    }
    if (!strcmp(argv[2], "recent")) { history_recent(h); settle(auth, h); json_object_set_new(out, "recent", snapshot(h)); }
    if (!strcmp(argv[2], "voice")) { history_select(h, history_channel_count(h) - 1); settle(auth, h); json_object_set_new(out, "voice", snapshot(h)); }
    if (!strcmp(argv[2], "context")) {
        auth_cancel(auth); history_poll(h);
        json_object_set_new(out, "cleared", history_channel_count(h) == 0 && history_message_count(h) == 0 ? json_true() : json_false());
    }
    if (!strcmp(argv[2], "instance") && argc == 5) {
        auth_cancel(auth); history_poll(h);
        json_object_set_new(out, "cleared", history_channel_count(h) == 0 && history_message_count(h) == 0 ? json_true() : json_false());
        auth_start(auth, argv[4], username ? username : "josé", password ? password : "Synthetic!123", "");
        settle(auth, h);
        history_select(h, 1); settle(auth, h);
        json_object_set_new(out, "second", snapshot(h));
        json_t *message = history_message(h, 0);
        if (message) json_object_set(out, "second_content", json_object_get(message, "content"));
    }
    json_object_set_new(out, "session", auth_has_session(auth) ? json_true() : json_false());
    json_object_set_new(out, "final_channels", json_integer((json_int_t)history_channel_count(h)));
    json_dumpf(out, stdout, JSON_COMPACT); putchar('\n'); json_decref(out);
    history_destroy(h); auth_destroy(auth); curl_global_cleanup(); return 0;
}
