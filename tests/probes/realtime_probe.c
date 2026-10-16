#define _POSIX_C_SOURCE 200809L
#include "model/conversation/history.h"
#include "net/clock.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

/* Development observer: stdin commands, JSON snapshots, actual auth/WS/model. */
static void snapshot(auth_client *auth, history *h)
{
    json_t *messages = json_array(), *channels = json_array();
    for (size_t i = 0; i < history_message_count(h); ++i)
        json_array_append(messages, history_message(h, i));
    for (size_t i = 0; i < history_channel_count(h); ++i)
        json_array_append(channels, history_channel(h, i));
    json_t *body =
        json_pack("{s:o,s:o,s:s,s:b,s:b,s:b,s:I,s:I,s:I}", "messages", messages, "channels",
                  channels, "status", history_status(h), "session", auth_has_session(auth),
                  "loading", history_loading(h) || auth_busy(auth), "recovering",
                  history_recovering(h), "epoch", (json_int_t)auth_ws_epoch(auth), "selected",
                  (json_int_t)history_selected(h), "insertions", (json_int_t)history_insertions(h));
    json_dumpf(body, stdout, JSON_COMPACT);
    putchar('\n');
    fflush(stdout);
    json_decref(body);
}
int main(int argc, char **argv)
{
    if (argc != 2 || curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
        return 1;
    auth_client *auth = auth_create();
    if (!auth || !auth_start(auth, argv[1], "josé", "Synthetic!123", ""))
        return 2;
    history *h = NULL;
    bool selected = false;
    while (true) {
        auth_poll(auth);
        if (!h && auth_has_session(auth)) {
            auth_realtime_active(auth, true);
            auth_connect_ws(auth);
            h = history_create(auth);
            if (!h)
                break;
        }
        if (h) {
            history_poll(h);
            if (!selected && history_channel_count(h) && !history_loading(h))
                for (size_t i = 0; i < history_channel_count(h); ++i)
                    if (!strcmp(json_string_value(json_object_get(history_channel(h, i), "type")),
                                "text")) {
                        history_select(h, i);
                        selected = true;
                        break;
                    }
        }
        fd_set ready;
        FD_ZERO(&ready);
        FD_SET(STDIN_FILENO, &ready);
        struct timeval delay = {.tv_usec = 1000};
        if (select(STDIN_FILENO + 1, &ready, NULL, NULL, &delay) > 0) {
            char command[65536];
            if (!fgets(command, sizeof(command), stdin) || !strncmp(command, "quit", 4))
                break;
            if (h) {
                if (!strncmp(command, "older", 5))
                    history_older(h);
                else if (!strncmp(command, "recover", 7))
                    history_recover(h);
                else if (!strncmp(command, "active 0", 8))
                    auth_realtime_active(auth, false);
                else if (!strncmp(command, "active 1", 8))
                    auth_realtime_active(auth, true);
                else if (!strncmp(command, "refresh", 7))
                    auth_refresh(auth);
                else if (!strncmp(command, "confirm ", 8)) {
                    json_t *m = json_loads(command + 8, JSON_REJECT_DUPLICATES, NULL);
                    history_confirm(h, m, auth_context_generation(auth));
                    json_decref(m);
                } else if (!strncmp(command, "event ", 6)) {
                    json_t *e = json_loads(command + 6, JSON_REJECT_DUPLICATES, NULL);
                    history_event(h, e, auth_context_generation(auth));
                    json_decref(e);
                }
                snapshot(auth, h);
            } else {
                puts("{}");
                fflush(stdout);
            }
        }
    }
    history_destroy(h);
    auth_destroy(auth);
    curl_global_cleanup();
    return 0;
}
