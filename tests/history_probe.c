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
    if (!strcmp(argv[2], "pages") || !strcmp(argv[2], "retry") || !strcmp(argv[2], "recent")) {
        for (int i = 0; i < 24 && history_has_more(h); ++i) {
            history_older(h); settle(auth, h); json_array_append_new(pages, snapshot(h));
            if (!strcmp(argv[2], "retry") && i == 0) { history_retry(h); settle(auth, h); json_array_append_new(pages, snapshot(h)); }
        }
    }
    json_object_set_new(out, "pages", pages);
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
