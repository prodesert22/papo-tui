#define _POSIX_C_SOURCE 200809L
#include "model/conversation/history.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>
static void snapshot(auth_client *auth, history *h)
{
    inbox *b = history_inbox(h);
    json_t *dms = json_array(), *notifications = json_array(), *events = json_array();
    for (size_t i = 0; i < inbox_dm_count(b); ++i)
        json_array_append(dms, inbox_dm(b, i));
    for (size_t i = 0; i < inbox_notification_count(b); ++i)
        json_array_append(notifications, inbox_notification(b, i));
    for (size_t i = 0; i < inbox_event_count(b); ++i)
        json_array_append(events, inbox_notification_event(b, i));
    json_t *value =
        json_pack("{s:o,s:o,s:o,s:b,s:b,s:s,s:I,s:I}", "dms", dms, "notifications", notifications,
                  "events", events, "session", auth_has_session(auth), "more", inbox_has_more(b),
                  "notice", inbox_notice(b), "unread", (json_int_t)inbox_unread(b), "messages",
                  (json_int_t)history_message_count(h));
    json_object_set_new(value, "presence",
                        json_string(inbox_presence(b, "00000000-0000-4000-8000-000000000002")));
    json_object_set_new(value, "unseen", json_integer((json_int_t)inbox_unseen_conversations(b)));
    json_object_set_new(value, "mentions",
                        json_integer((json_int_t)inbox_unread_mentions(b, NULL)));
    json_t *mentions = json_object(), *unread = json_object();
    for (size_t i = 0; i < history_channel_count(h); ++i) {
        const char *id = json_string_value(json_object_get(history_channel(h, i), "id"));
        if (id) {
            json_object_set_new(mentions, id,
                                json_integer((json_int_t)inbox_unread_mentions(b, id)));
            json_object_set_new(unread, id,
                                json_boolean(history_channel_unread(h, history_channel(h, i))));
        }
    }
    json_object_set_new(value, "channel_mentions", mentions);
    json_object_set_new(value, "channel_unread", unread);
    json_dumpf(value, stdout, JSON_COMPACT);
    putchar('\n');
    fflush(stdout);
    json_decref(value);
}
int main(int argc, char **argv)
{
    if (argc != 2 || curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
        return 1;
    auth_client *auth = auth_create();
    if (!auth || !auth_start(auth, argv[1], "josé", "Synthetic!123", ""))
        return 2;
    history *h = NULL;
    while (true) {
        auth_poll(auth);
        if (!h && auth_has_session(auth)) {
            h = history_create(auth);
            if (!h)
                break;
            history_enable_r8(h);
        }
        if (h)
            history_poll(h);
        fd_set input;
        FD_ZERO(&input);
        FD_SET(STDIN_FILENO, &input);
        struct timeval delay = {.tv_usec = 1000};
        if (select(STDIN_FILENO + 1, &input, NULL, NULL, &delay) > 0) {
            char command[16384];
            if (!fgets(command, sizeof(command), stdin) || !strncmp(command, "quit", 4))
                break;
            command[strcspn(command, "\r\n")] = 0;
            if (h) {
                inbox *b = history_inbox(h);
                if (!strncmp(command, "open ", 5))
                    inbox_open_dm(b, command + 5);
                else if (!strncmp(command, "close ", 6))
                    inbox_close_dm(b, command + 6);
                else if (!strncmp(command, "detail ", 7))
                    inbox_view(b, command + 7, false);
                else if (!strncmp(command, "eventdetail ", 12))
                    inbox_view(b, command + 12, true);
                else if (!strncmp(command, "view ", 5))
                    inbox_view_message(b, command + 5);
                else if (!strcmp(command, "older"))
                    inbox_older(b);
                else if (!strcmp(command, "refresh"))
                    inbox_refresh(b);
                else if (!strcmp(command, "retry"))
                    inbox_retry(b);
                else if (!strcmp(command, "inactive"))
                    history_active(h, false);
                else if (!strcmp(command, "active"))
                    history_active(h, true);
                else if (!strncmp(command, "select ", 7)) {
                    for (size_t i = 0; i < history_channel_count(h); ++i)
                        if (!strcmp(json_string_value(json_object_get(history_channel(h, i), "id")),
                                    command + 7)) {
                            history_select(h, i);
                            break;
                        }
                } else if (!strncmp(command, "event ", 6)) {
                    json_t *e = json_loads(command + 6, JSON_REJECT_DUPLICATES, NULL);
                    if (e)
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
