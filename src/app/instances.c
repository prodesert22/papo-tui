#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "app/instances.h"
#include "app/desktop.h"
#include "app/session_store.h"
#include "config/profiles.h"
#include "model/conversation/history.h"
#include "net/clock.h"
#include "net/retry.h"
#include "ui/components/terminal_title.h"
#include "ui/input/editor.h"
#include "ui/input/text.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

instance_connections *instances_create(void)
{
    instance_connections *c = calloc(1, sizeof(*c));
    if (c)
        c->desktop = desktop_create();
    return c;
}
static void desktop_key(const instance_connection *item, char key[80])
{
    snprintf(key, 80, "%llu:%llu", (unsigned long long)item->serial,
             (unsigned long long)item->desktop_generation);
}
static const char *desktop_mention_username(void *context, const char *id)
{
    const instance_connection *item = context;
    const char *username = history_author_username(item->model, id);
    if (*username)
        return username;
    return !strcmp(id, auth_user_id(item->auth)) ? auth_username(item->auth) : NULL;
}
/* Sanitized plain text of one alert part: controls never reach the desktop. */
static bool alert_plain(const char *utf8, char output[TEXT_UTF8_BYTES])
{
    text_buffer buffer;
    return text_decode(&buffer, utf8) && text_encode(&buffer, output);
}
/* Appends without splitting a UTF-8 sequence; a part that does not fit is cut. */
static void alert_append(char output[TEXT_UTF8_BYTES], const char *part)
{
    size_t used = strlen(output), room = TEXT_UTF8_BYTES - 1 - used, length = strlen(part);
    if (length > room) {
        length = room;
        while (length && ((unsigned char)part[length] & 0xc0) == 0x80)
            --length;
    }
    memcpy(output + used, part, length);
    output[used + length] = 0;
}
/* What a message carries besides text, from the bounded preview kept for it. */
static bool alert_attachment(const json_t *preview, char output[TEXT_UTF8_BYTES])
{
    json_int_t count = json_integer_value(json_object_get(preview, "attachment_count"));
    if (count < 1)
        return false;
    const char *mime = json_string_value(json_object_get(preview, "attachment_mime"));
    const char *name = json_string_value(json_object_get(preview, "attachment_name"));
    mime = mime ? mime : "";
    char label[TEXT_UTF8_BYTES];
    if (count > 1)
        snprintf(label, sizeof(label), "%lld anexos", (long long)count);
    else if (!strncmp(mime, "image/", 6))
        snprintf(label, sizeof(label), "Imagem");
    else if (!strncmp(mime, "video/", 6))
        snprintf(label, sizeof(label), "Vídeo");
    else if (!strncmp(mime, "audio/", 6))
        snprintf(label, sizeof(label), "Áudio");
    else if (name && *name) {
        char safe[TEXT_UTF8_BYTES];
        if (!alert_plain(name, safe))
            return false;
        snprintf(label, sizeof(label), "Arquivo: %.512s", safe);
    } else
        snprintf(label, sizeof(label), "Anexo");
    *output = 0;
    alert_append(output, "[");
    alert_append(output, label);
    alert_append(output, "]");
    return true;
}
/* "Respondeu a você: texto [Imagem]". Words, attachment and reply are each
 * optional; a message with none of them still says it is a new message. */
static bool alert_message_body(const instance_connection *item, inbox *box, const json_t *preview,
                               const char *content, char output[TEXT_UTF8_BYTES])
{
    char text[TEXT_UTF8_BYTES], attachment[TEXT_UTF8_BYTES], reply[TEXT_UTF8_BYTES];
    text_buffer buffer, projected;
    if (!text_decode(&buffer, content ? content : ""))
        return false;
    text_mentions(&buffer, desktop_mention_username, (void *)item);
    text_project(&buffer, &projected);
    if (!text_encode(&projected, text))
        return false;
    if (!alert_attachment(preview, attachment))
        *attachment = 0;
    *reply = 0;
    const char *target = json_string_value(json_object_get(preview, "reply_to"));
    if (target) {
        const char *author =
            json_string_value(json_object_get(inbox_message_preview(box, target), "author_id"));
        const char *name = author && *author ? history_mention_name((void *)item->model, author)
                                             : NULL;
        char safe[TEXT_UTF8_BYTES];
        if (author && !strcmp(author, auth_user_id(item->auth)))
            snprintf(reply, sizeof(reply), "Respondeu a você");
        else if (name && alert_plain(name, safe))
            snprintf(reply, sizeof(reply), "Respondeu a %.512s", safe);
        else
            snprintf(reply, sizeof(reply), "Respondeu");
    }
    *output = 0;
    alert_append(output, reply);
    if (*reply && (*text || *attachment))
        alert_append(output, ": ");
    alert_append(output, text);
    if (*text && *attachment)
        alert_append(output, " ");
    alert_append(output, attachment);
    if (!*output)
        alert_append(output, "Nova mensagem");
    return true;
}
static void release_model(instance_connections *c, size_t i)
{
    if (c->visible_auth == c->items[i].auth)
        instances_visible(c, NULL, NULL);
    char key[80];
    desktop_key(&c->items[i], key);
    desktop_cancel(c->desktop, key);
    json_decref(c->items[i].alerts);
    history_destroy(c->items[i].model);
}
/* A renewal withdraws the saved session until its new cookie is stored, so
 * quitting in the middle of one would lose the server on the next start. Let
 * renewals already started finish, within a bound, before the clients go. */
static void finish_renewals(instance_connections *connections)
{
    gint64 end = g_get_monotonic_time() + 5 * G_USEC_PER_SEC;
    for (;;) {
        bool renewing = false;
        for (size_t i = 0; i < connections->count; ++i)
            if (auth_renewing(connections->items[i].auth)) {
                auth_poll(connections->items[i].auth);
                renewing = true;
            }
        session_store_poll(connections->sessions, NULL);
        if (!renewing || g_get_monotonic_time() >= end)
            return;
        g_usleep(1000);
    }
}
void instances_destroy(instance_connections *connections)
{
    if (!connections)
        return;
    finish_renewals(connections);
    for (size_t i = 0; i < connections->count; ++i) {
        session_store_unwatch(connections->sessions, connections->items[i].auth);
        if (connections->items[i].media_destroy)
            connections->items[i].media_destroy(connections->items[i].media);
        send_editor_destroy(connections->items[i].editor);
        release_model(connections, i);
        auth_destroy(connections->items[i].auth);
    }
    session_store_destroy(connections->sessions);
    desktop_destroy(connections->desktop);
    free(connections);
}
size_t instances_index(const instance_connections *connections, const auth_client *auth)
{
    for (size_t i = 0; i < connections->count; ++i)
        if (connections->items[i].auth == auth)
            return i;
    return SIZE_MAX;
}
bool instances_contains(const instance_connections *connections, const auth_client *auth)
{
    return connections && instances_index(connections, auth) != SIZE_MAX;
}
send_editor *instances_editor(const instance_connections *connections, const auth_client *auth)
{
    if (!connections)
        return NULL;
    size_t index = instances_index(connections, auth);
    return index == SIZE_MAX ? NULL : connections->items[index].editor;
}
history *instances_history(const instance_connections *connections, const auth_client *auth)
{
    size_t index = connections ? instances_index(connections, auth) : SIZE_MAX;
    return index == SIZE_MAX ? NULL : connections->items[index].model;
}
void instances_visible(instance_connections *connections, auth_client *auth, const char *channel)
{
    connections->visible_auth = auth;
    snprintf(connections->visible_channel, sizeof(connections->visible_channel), "%s",
             channel ? channel : "");
}
static bool attach(instance_connections *connections, auth_client *auth)
{
    size_t index = instances_index(connections, auth);
    if (index == SIZE_MAX) {
        send_editor *editor = send_editor_create(auth);
        history *model = history_create(auth);
        json_t *alerts = json_array();
        if (!editor || !model || !alerts) {
            send_editor_destroy(editor);
            history_destroy(model);
            json_decref(alerts);
            return false;
        }
        history_enable_r8(model);
        for (size_t i = 0; i < connections->count; ++i)
            if (!strcmp(connections->items[i].url, auth_instance_url(auth))) {
                index = i;
                break;
            }
        if (index == SIZE_MAX) {
            if (connections->count == INSTANCE_LIMIT) {
                send_editor_destroy(editor);
                history_destroy(model);
                json_decref(alerts);
                return false;
            }
            index = connections->count++;
        } else {
            session_store_unwatch(connections->sessions, connections->items[index].auth);
            if (connections->items[index].media_destroy)
                connections->items[index].media_destroy(connections->items[index].media);
            connections->items[index].media = NULL;
            send_editor_destroy(connections->items[index].editor);
            release_model(connections, index);
            auth_destroy(connections->items[index].auth);
        }
        connections->items[index].editor = editor;
        connections->items[index].model = model;
        connections->items[index].alerts = alerts;
        connections->items[index].serial = ++connections->next_serial;
        connections->items[index].desktop_generation = auth_context_generation(auth);
        connections->items[index].alert_due = 0;
    }
    instance_connection *item = &connections->items[index];
    item->auth = auth;
    item->retry_at = 0;
    item->retry_failures = 0;
    item->retrying = false;
    snprintf(item->url, sizeof(item->url), "%s", auth_instance_url(auth));
    const char *url = auth_instance_url(auth), *host = strstr(url, "://");
    if (*auth_server_name(auth))
        snprintf(item->name, sizeof(item->name), "%s", auth_server_name(auth));
    else if (!*item->name && host && host[3])
        snprintf(item->name, sizeof(item->name), "%.*s", (int)strcspn(host + 3, "/"), host + 3);
    else if (!*item->name)
        snprintf(item->name, sizeof(item->name), "Servidor");
    return true;
}
bool instances_attach(instance_connections *connections, auth_client *auth)
{
    return connections && auth_has_session(auth) && attach(connections, auth);
}
bool instances_attach_offline(instance_connections *connections, auth_client *auth)
{
    return connections && auth && *auth_instance_url(auth) && attach(connections, auth);
}
void instances_order(instance_connections *connections,
                     unsigned (*rank)(const auth_client *auth, void *context), void *context)
{
    for (size_t i = 1; i < connections->count; ++i) {
        instance_connection item = connections->items[i];
        unsigned key = rank(item.auth, context);
        size_t j = i;
        while (j && rank(connections->items[j - 1].auth, context) > key) {
            connections->items[j] = connections->items[j - 1];
            --j;
        }
        connections->items[j] = item;
    }
}
bool instances_offline(const instance_connections *connections, size_t index)
{
    const auth_client *auth = connections->items[index].auth;
    if (auth_server_unreachable(auth))
        return true;
    return !auth_has_session(auth) && (!auth_busy(auth) || connections->items[index].retrying);
}
void instances_detach(instance_connections *connections, auth_client *auth)
{
    size_t index = instances_index(connections, auth);
    if (index == SIZE_MAX)
        return;
    session_store_unwatch(connections->sessions, auth);
    if (connections->items[index].media_destroy)
        connections->items[index].media_destroy(connections->items[index].media);
    send_editor_destroy(connections->items[index].editor);
    release_model(connections, index);
    if (connections->visible_auth == auth)
        instances_visible(connections, NULL, NULL);
    memmove(connections->items + index, connections->items + index + 1,
            (connections->count - index - 1) * sizeof(connections->items[0]));
    --connections->count;
    memset(&connections->items[connections->count], 0, sizeof(connections->items[0]));
}
void instances_media_event(instance_connections *connections, auth_client *auth,
                           const json_t *event)
{
    size_t i = instances_index(connections, auth);
    if (i != SIZE_MAX && connections->items[i].media_update)
        connections->items[i].media_update(connections->items[i].media, event);
}
/* Reads the saved session of an offline tab again when its wait ends. The
 * first wait is 4s and doubles up to 30s; any session resets it. */
static void retry_offline(instance_connections *connections, instance_connection *item,
                          uint64_t now)
{
    auth_client *auth = item->auth;
    if (auth_has_session(auth)) {
        item->retry_failures = 0;
        item->retry_at = 0;
        item->retrying = false;
        return;
    }
    if (auth_busy(auth)) {
        item->retry_at = 0;
        return;
    }
    if (auth_session_rejected(auth) || !session_store_saved(connections->sessions, auth))
        return;
    if (!item->retry_at) {
        item->retry_at = now + net_retry_ms(item->retry_failures + 2, (unsigned)rand());
        return;
    }
    if (now < item->retry_at)
        return;
    item->retry_at = 0;
    if (item->retry_failures < 3)
        ++item->retry_failures;
    if (session_store_retry(connections->sessions, auth))
        item->retrying = true;
}
bool instances_resume(instance_connections *connections)
{
    bool changed = false;
    const char *url;
    char *snapshot;
    while (connections && (snapshot = session_store_take_resume(connections->sessions, &url))) {
        for (size_t i = 0; i < connections->count; ++i) {
            auth_client *auth = connections->items[i].auth;
            if (auth_has_session(auth) || auth_busy(auth) || strcmp(auth_instance_url(auth), url))
                continue;
            auth_session_restore(auth, snapshot, url, connections->allow_local_http);
            session_store_watch(connections->sessions, auth);
            changed = true;
            break;
        }
        auth_session_free(&snapshot);
    }
    return changed;
}
bool instances_poll(instance_connections *connections, auth_client *active)
{
    bool changed = false;
    uint64_t tick = 0;
    for (size_t i = 0; i < connections->count; ++i) {
        if (tick || net_now_ms(&tick))
            retry_offline(connections, &connections->items[i], tick);
        bool offline = instances_offline(connections, i);
        if (offline != connections->items[i].offline_shown) {
            connections->items[i].offline_shown = offline;
            changed = true;
        }
    }
    size_t first = connections->count ? connections->poll_cursor++ % connections->count : 0;
    for (size_t step = 0; step < connections->count; ++step) {
        size_t i = (first + step) % connections->count;
        auth_client *auth = connections->items[i].auth;
        auth_realtime_active(auth, auth == active);
        if (connections->items[i].media_idle)
            connections->items[i].media_idle(connections->items[i].media);
        if (auth != active) {
            auth_state before = auth_get_state(auth);
            bool session = auth_has_session(auth);
            auth_poll(auth);
            /* A restored tab that validates in the background opens its own
             * realtime connection and takes the server's name. */
            if (!session && auth_has_session(auth))
                auth_connect_ws(auth);
            send_editor_poll(connections->items[i].editor);
            outbox *box = send_editor_outbox(connections->items[i].editor);
            outbox_operation operation;
            json_t *confirmed;
            while ((confirmed = outbox_take_confirmed(box, &operation))) {
                /* Its cached conversation fetches the sent message on return. */
                history_conversation_changed(connections->items[i].model, operation.channel_id);
                json_decref(confirmed);
                outbox_reconciled(box, operation.id);
            }
            history_active(connections->items[i].model, false);
            changed |= history_poll(connections->items[i].model);
            changed |= before != auth_get_state(auth) || session != auth_has_session(auth);
        }
        instance_connection *item = &connections->items[i];
        if (*auth_server_name(auth) && strcmp(item->name, auth_server_name(auth))) {
            snprintf(item->name, sizeof(item->name), "%s", auth_server_name(auth));
            changed = true;
        }
        char key[80];
        desktop_key(item, key);
        if (item->desktop_generation != auth_context_generation(auth) || !auth_has_session(auth)) {
            desktop_cancel(connections->desktop, key);
            json_array_clear(item->alerts);
            item->desktop_generation = auth_context_generation(auth);
            item->alert_due = 0;
        }
        inbox *box = history_inbox(item->model);
        json_t *event;
        uint64_t now = 0;
        net_now_ms(&now);
        while ((event = inbox_take_alert(box))) {
            if (connections->config && connections->config->desktop_notifications &&
                auth_has_session(auth)) {
                if (!json_array_size(item->alerts))
                    item->alert_due = now + 2000;
                if (json_array_size(item->alerts) < 256)
                    json_array_append(item->alerts, event);
            }
            json_decref(event);
        }
        if (connections->config && !connections->config->desktop_notifications) {
            json_array_clear(item->alerts);
            desktop_cancel(connections->desktop, key);
        }
        if (json_array_size(item->alerts) && now >= item->alert_due) {
            for (size_t j = 0; j < json_array_size(item->alerts); ++j) {
                event = json_array_get(item->alerts, j);
                const char *message = json_string_value(json_object_get(event, "message_id"));
                const char *channel = inbox_message_channel(box, message);
                const char *mode = history_channel_notifications(item->model, channel);
                if (inbox_message_viewed(box, message) || inbox_conversation_muted(box, channel) ||
                    (mode && !strcmp(mode, "off")))
                    continue;
                if (connections->visible_auth == auth && *channel &&
                    !strcmp(channel, connections->visible_channel) && text_terminal_focused())
                    continue;
                const char *author = json_string_value(json_object_get(event, "user_id"));
                char title[2200], safe_title[TEXT_UTF8_BYTES], safe_body[TEXT_UTF8_BYTES];
                text_buffer buffer;
                /* A webhook has no author account: its name travels in the
                 * message's external_author, kept in the inbox preview. */
                const json_t *preview = inbox_message_preview(box, message);
                snprintf(title, sizeof(title), "%s · %s", item->name,
                         preview && history_message_webhook(preview)
                             ? history_message_author(item->model, preview)
                             : history_author(item->model, author ? author : ""));
                if (!text_decode(&buffer, title) || !text_encode(&buffer, safe_title))
                    continue;
                if (connections->config->notification_preview) {
                    if (!alert_message_body(item, box, preview,
                                            json_string_value(
                                                json_object_get(event, "message_content")),
                                            safe_body))
                        continue;
                } else if (!alert_plain("Nova notificação", safe_body))
                    continue;
                const json_t *created = inbox_message_preview(box, message);
                const char *at = json_string_value(json_object_get(created, "created_at"));
                char target[DESKTOP_TARGET_BYTES];
                snprintf(target, sizeof(target), "%s|%s|%s", channel, message, at ? at : "");
                desktop_queue(connections->desktop, key, safe_title, safe_body, target);
            }
            json_array_clear(item->alerts);
        }
    }
    changed |= desktop_poll(connections->desktop);
    char clicked[80], target[DESKTOP_TARGET_BYTES];
    while (desktop_take_activation(connections->desktop, clicked, target)) {
        desktop_raise_terminal(connections->desktop, terminal_title_current());
        for (size_t i = 0; i < connections->count; ++i) {
            instance_connection *item = &connections->items[i];
            char key[80];
            desktop_key(item, key);
            char *first = strchr(target, '|'), *second = first ? strchr(first + 1, '|') : NULL;
            if (strcmp(key, clicked) || !second)
                continue;
            *first = *second = 0;
            if (strlen(target) < sizeof(item->open_channel) &&
                strlen(first + 1) < sizeof(item->open_message) &&
                strlen(second + 1) < sizeof(item->open_time)) {
                snprintf(item->open_channel, sizeof(item->open_channel), "%.36s", target);
                snprintf(item->open_message, sizeof(item->open_message), "%.36s", first + 1);
                snprintf(item->open_time, sizeof(item->open_time), "%.64s", second + 1);
                changed = true;
            }
            break;
        }
    }
    return changed;
}
size_t instances_open_elsewhere(const instance_connections *connections, const auth_client *active)
{
    for (size_t i = 0; connections && i < connections->count; ++i)
        if (connections->items[i].auth != active && *connections->items[i].open_channel &&
            auth_has_session(connections->items[i].auth))
            return i;
    return SIZE_MAX;
}
bool instances_take_open(instance_connections *connections, const auth_client *auth,
                         char channel[37], char message[37], char time[65])
{
    size_t index = connections ? instances_index(connections, auth) : SIZE_MAX;
    if (index == SIZE_MAX || !*connections->items[index].open_channel)
        return false;
    instance_connection *item = &connections->items[index];
    snprintf(channel, 37, "%s", item->open_channel);
    snprintf(message, 37, "%s", item->open_message);
    snprintf(time, 65, "%s", item->open_time);
    item->open_channel[0] = item->open_message[0] = item->open_time[0] = 0;
    return true;
}
