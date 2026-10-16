#include "model/conversation/private.h"

void history_older(history *h)
{
    if (!h->active || h->recovering || !h->more || h->want_messages || h->pending == MESSAGES ||
        (h->failed != NONE && h->failed != AUTHORS))
        return;
    h->older = true;
    h->newer = false;
    h->want_messages = true;
    history_internal_status(h, "Carregando mensagens anteriores…");
    history_internal_notice(h, HISTORY_NOTICE_READ,
                            ""); /* Prefetch is silent until the view reaches the edge. */
}

void history_newer(history *h)
{
    if (!h->active || h->recovering || !h->newer_more || h->want_messages ||
        h->pending == MESSAGES || (h->failed != NONE && h->failed != AUTHORS))
        return;
    h->newer = true;
    h->older = false;
    h->want_messages = true;
    history_internal_status(h, "Carregando mensagens posteriores…");
    history_internal_notice(h, HISTORY_NOTICE_READ, "");
}

bool history_internal_message_valid(json_t *m, const char *channel)
{
    uint64_t time;
    /* Backend webhooks (papo-backend#26): Message.author_id is nullable in
     * REST (null) and empty ("") in WS MessageOutbound for webhook messages.
     * Accept a missing/null/empty author and validate UUID only when present.
     * webhook_id/external_author are optional display-only attribution. */
    const char *author_id = history_internal_str(m, "author_id");
    bool author_missing = !author_id || !*author_id;
    json_t *author_value = json_object_get(m, "author_id");
    if (!author_missing &&
        (!history_internal_string_field(m, "author_id", 36, false) || !history_uuid(author_id)))
        return false;
    if (author_value && !json_is_null(author_value) && !json_is_string(author_value))
        return false;
    json_t *webhook_value = json_object_get(m, "webhook_id");
    if (webhook_value && !json_is_null(webhook_value)) {
        const char *webhook_id = json_string_value(webhook_value);
        if (!webhook_id || !*webhook_id)
            return false;
        if (json_string_length(webhook_value) > 36 ||
            strlen(webhook_id) != json_string_length(webhook_value) || !history_uuid(webhook_id))
            return false;
    }
    json_t *external = json_object_get(m, "external_author");
    if (external && !json_is_null(external) && !json_is_object(external))
        return false;
    if (!history_internal_string_field(m, "id", 36, false) ||
        !history_internal_string_field(m, "channel_id", 36, false) ||
        !history_uuid(history_internal_str(m, "id")) || !history_internal_str(m, "channel_id") ||
        strcmp(history_internal_str(m, "channel_id"), channel) ||
        !history_internal_string_field(m, "created_at", 64, false) ||
        !session_time_parse(history_internal_str(m, "created_at"), &time) ||
        !history_internal_string_field(m, "content", 32768, true) ||
        !history_internal_string_field(m, "edited_at", 64, true) ||
        !history_internal_string_field(m, "reply_to", 36, true))
        return false;
    if (history_internal_str(m, "reply_to") && !history_uuid(history_internal_str(m, "reply_to")))
        return false;
    if (history_internal_str(m, "edited_at") &&
        !session_time_parse(history_internal_str(m, "edited_at"), &time))
        return false;
    const char *lists[] = {"attachments", "embeds", "reactions", "user_reactions"};
    for (size_t i = 0; i < 4; ++i) {
        json_t *a = json_object_get(m, lists[i]);
        if (a && !json_is_null(a) && !json_is_array(a))
            return false;
        if (i < 2 && json_array_size(a) > 10)
            return false;
    }
    return true;
}

size_t history_internal_encoded_size(json_t *m)
{
    char *encoded = json_dumps(m, JSON_COMPACT);
    if (!encoded)
        return SIZE_MAX;
    size_t length = strlen(encoded);
    free(encoded);
    return length;
}

bool history_internal_contains(json_t *array, const char *id)
{
    size_t i;
    json_t *m;
    json_array_foreach(array, i, m) if (!strcmp(history_internal_str(m, "id"), id)) return true;
    return false;
}

int history_internal_time_compare(const char *at, const char *bt)
{
    int ordering = 0;
    session_time_compare(at, bt, &ordering);
    return ordering;
}

int history_internal_message_compare(json_t *a, json_t *b)
{
    int time = history_internal_time_compare(history_internal_str(a, "created_at"),
                                             history_internal_str(b, "created_at"));
    return time ? time : strcmp(history_internal_str(a, "id"), history_internal_str(b, "id"));
}

static bool remember_read(history *h, json_t *message)
{
    if (!message)
        return true;
    const char *channel = history_internal_str(message, "channel_id");
    json_t *previous = json_object_get(h->read_messages, channel);
    if (previous && history_internal_message_compare(previous, message) >= 0)
        return true;
    json_t *cursor = json_pack("{s:O,s:O}", "id", json_object_get(message, "id"), "created_at",
                               json_object_get(message, "created_at"));
    if (!cursor || json_object_set_new(h->read_messages, channel, cursor))
        return false;
    if (json_object_size(h->read_messages) > 2 * HISTORY_CHANNEL_LIMIT) {
        void *oldest = json_object_iter(h->read_messages);
        json_object_del(h->read_messages, json_object_iter_key(oldest));
    }
    h->read_dirty = true;
    return true;
}

size_t history_internal_find(json_t *array, const char *id)
{
    size_t i;
    json_t *m;
    json_array_foreach(array, i, m) if (!strcmp(history_internal_str(m, "id"), id)) return i;
    return SIZE_MAX;
}

bool history_internal_trim(history *h, json_t *array, bool oldest)
{
    if (array == h->messages)
        ++h->messages_revision;
    size_t bytes = 0, i;
    json_t *m;
    json_array_foreach(array, i, m)
    {
        message_cost *cost = history_internal_measure_message(h, m, false);
        size_t n = cost ? cost->bytes : SIZE_MAX;
        if (n == SIZE_MAX || n > SIZE_MAX - bytes)
            return false;
        bytes += n;
    }
    bool dropped = false;
    while (json_array_size(array) > HISTORY_MESSAGE_LIMIT || bytes > HISTORY_BYTE_LIMIT) {
        size_t index = oldest ? 0 : json_array_size(array) - 1;
        message_cost *cost =
            history_internal_measure_message(h, json_array_get(array, index), false);
        size_t n = cost ? cost->bytes : SIZE_MAX;
        if (n == SIZE_MAX || json_array_remove(array, index))
            return false;
        bytes -= n;
        h->evicted = dropped = true;
    }
    if (dropped && !h->recovering && json_array_size(array)) {
        json_t *edge = json_array_get(array, oldest ? 0 : json_array_size(array) - 1);
        if (oldest) {
            h->more = true;
            snprintf(h->cursor_id, sizeof(h->cursor_id), "%s", history_internal_str(edge, "id"));
            snprintf(h->cursor_time, sizeof(h->cursor_time), "%s",
                     history_internal_str(edge, "created_at"));
        } else {
            h->newer_more = true;
            snprintf(h->newer_id, sizeof(h->newer_id), "%s", history_internal_str(edge, "id"));
            snprintf(h->newer_time, sizeof(h->newer_time), "%s",
                     history_internal_str(edge, "created_at"));
        }
    }
    if (array == h->messages)
        h->bytes = bytes;
    /* Metadata retains JSON only for the live/recovering windows. */
    ++h->cost_sweep;
    json_t *windows[] = {array, h->messages, h->fresh};
    for (size_t w = 0; w < 3; ++w)
        json_array_foreach(windows[w], i, m)
        {
            message_cost *cost = g_hash_table_lookup(h->costs, m);
            if (cost)
                cost->seen = h->cost_sweep;
        }
    GHashTableIter it;
    gpointer p;
    g_hash_table_iter_init(&it, h->costs);
    while (g_hash_table_iter_next(&it, NULL, &p))
        if (((message_cost *)p)->seen != h->cost_sweep)
            g_hash_table_iter_remove(&it);
    return true;
}

bool history_internal_upsert(history *h, json_t **array, json_t *value, bool oldest,
                             bool protect_edit)
{
    if (!*array && !(*array = json_array()))
        return false;
    size_t index = history_internal_find(*array, history_internal_str(value, "id"));
    json_t *existing = json_array_get(*array, index);
    json_t *merged = existing ? json_deep_copy(existing) : json_object();
    json_t *copy = json_deep_copy(value);
    if (!merged || !copy || json_object_update(merged, copy)) {
        json_decref(merged);
        json_decref(copy);
        return false;
    }
    json_decref(copy);
    json_object_del(merged, "type");
    /* A partial creation/late POST cannot erase an already received edit. */
    if (protect_edit && existing && history_internal_str(existing, "edited_at") &&
        (!history_internal_str(value, "edited_at") ||
         history_internal_time_compare(history_internal_str(existing, "edited_at"),
                                       history_internal_str(value, "edited_at")) > 0)) {
        if (json_object_set(merged, "content", json_object_get(existing, "content")) ||
            json_object_set(merged, "edited_at", json_object_get(existing, "edited_at"))) {
            json_decref(merged);
            return false;
        }
    }
    json_t *next = json_copy(*array);
    if (!next) {
        json_decref(merged);
        return false;
    }
    bool ok;
    if (existing)
        ok = json_array_set(next, index, merged) == 0;
    else {
        size_t count = json_array_size(next), at = 0;
        while (at < count && history_internal_message_compare(json_array_get(next, at), merged) < 0)
            ++at;
        ok = json_array_insert(next, at, merged) == 0;
    }
    if (ok && !history_internal_measure_message(h, merged, true))
        ok = false;
    json_decref(merged);
    if (!ok || !history_internal_trim(h, next, oldest)) {
        json_decref(next);
        return false;
    }
    json_decref(*array);
    *array = next;
    return true;
}

bool history_confirm(history *h, json_t *message, uint64_t generation)
{
    if (!h || !auth_has_session(h->auth) || generation != h->context ||
        generation != auth_context_generation(h->auth))
        return false;
    const char *channel = history_internal_str(history_channel(h, h->selected), "id");
    if (!channel || !history_internal_message_valid(message, channel))
        return false;
    if (!h->replaying && (h->recovering || h->pending == MESSAGES)) {
        json_t *event = json_deep_copy(message);
        if (!event || json_object_set_new(event, "type", json_string("message"))) {
            json_decref(event);
            return false;
        }
        bool ok = history_internal_remember(h, event);
        json_decref(event);
        if (!ok)
            return false;
    }
    bool added = !history_internal_contains(h->messages, history_internal_str(message, "id"));
    if (!history_internal_patched(h, message))
        return false;
    if (added && history_internal_contains(h->messages, history_internal_str(message, "id")))
        ++h->insertions;
    h->want_authors = true;
    /* The editor owns the send notice; confirmation must not replace reads. */
    return true;
}

bool history_internal_messages_response(history *h, json_t *body)
{
    const char *channel = history_internal_str(history_channel(h, h->selected), "id");
    json_t *page = json_object_get(body, "messages"), *more = json_object_get(body, "has_more");
    size_t count = json_array_size(page);
    if (!channel || !history_internal_str(body, "channel_id") ||
        strcmp(channel, history_internal_str(body, "channel_id")) || !json_is_array(page) ||
        count > 100 || !json_is_boolean(more))
        return false;
    for (size_t i = 0; i < count; ++i)
        if (!history_internal_message_valid(json_array_get(page, i), channel))
            return false;
    /* Preserve the server's exact timestamp precision and ordering. Reverse its
     * descending page; do not sort timestamps lexically or truncate fractions. */
    json_t *edge = json_array_get(page, count ? count - 1 : 0);
    if (edge && (h->older || h->newer)) {
        int ordering =
            history_internal_time_compare(history_internal_str(edge, "created_at"), h->page_time);
        if (!ordering)
            ordering = strcmp(history_internal_str(edge, "id"), h->page_id);
        if (h->newer ? ordering <= 0 : ordering >= 0)
            return false;
    }
    json_t *next = h->messages ? json_copy(h->messages) : json_array();
    if (!next)
        return false;
    for (size_t i = 0; i < count; ++i) {
        json_t *m = json_array_get(page, h->newer ? i : count - i - 1);
        if (json_is_null(json_object_get(h->patches, history_internal_str(m, "id"))))
            continue;
        if (!history_internal_upsert(h, &next, m, h->newer, true))
            goto fail;
        json_t *patch = json_object_get(h->patches, history_internal_str(m, "id"));
        json_t *merged =
            json_array_get(next, history_internal_find(next, history_internal_str(m, "id")));
        if (!history_internal_apply_patch(h, merged, patch))
            goto fail;
    }
    json_decref(h->messages);
    h->messages = next;
    if (!history_internal_trim(h, h->messages, h->newer))
        return false;
    /* has_more describes only the requested direction. Empty pages stop it. */
    if (h->newer)
        h->newer_more = count && json_is_true(more);
    else
        h->more = count && json_is_true(more);
    if (!remember_read(h, json_array_get(page, h->newer && count ? count - 1 : 0)))
        return false;
    if (count) {
        if (h->newer) {
            snprintf(h->newer_time, sizeof(h->newer_time), "%s",
                     history_internal_str(edge, "created_at"));
            snprintf(h->newer_id, sizeof(h->newer_id), "%s", history_internal_str(edge, "id"));
        } else {
            snprintf(h->cursor_time, sizeof(h->cursor_time), "%s",
                     history_internal_str(edge, "created_at"));
            snprintf(h->cursor_id, sizeof(h->cursor_id), "%s", history_internal_str(edge, "id"));
        }
        if (!h->older && !h->newer)
            snprintf(h->read_attempt, sizeof(h->read_attempt), "%s",
                     history_internal_str(json_array_get(page, 0), "id"));
    }
    h->want_authors = history_message_count(h) > 0;
    if (h->r8 && !h->older && !h->newer && history_channel(h, h->selected)) {
        json_t *channel = history_channel(h, h->selected);
        if (json_is_true(json_object_get(channel, "_dm")))
            inbox_read_conversation(h->inbox, history_internal_str(channel, "id"));
        else
            h->want_channels = true;
    }
    history_internal_status(h, history_message_count(h)
                                   ? h->newer_more ? "Role para baixo para carregar posteriores."
                                     : h->more     ? "Role para cima para carregar anteriores."
                                                   : "Início do histórico alcançado."
                                   : "Nenhuma mensagem neste canal.");
    history_internal_notice(h, HISTORY_NOTICE_READ, "");
    return true;
fail:
    json_decref(next);
    return false;
}

bool history_internal_recovery_response(history *h, json_t *body)
{
    const char *channel = history_internal_str(history_channel(h, h->selected), "id");
    json_t *page = json_object_get(body, "messages"), *more = json_object_get(body, "has_more");
    size_t count = json_array_size(page);
    if (!channel || !history_internal_str(body, "channel_id") ||
        strcmp(channel, history_internal_str(body, "channel_id")) || !json_is_array(page) ||
        count > 100 || !json_is_boolean(more))
        return false;
    for (size_t i = 0; i < count; ++i)
        if (!history_internal_message_valid(json_array_get(page, i), channel))
            return false;
    json_t *last = count ? json_array_get(page, count - 1) : NULL;
    if (last && !strcmp(h->recovery_id, history_internal_str(last, "id")))
        return false;
    for (size_t i = 0; i < count; ++i)
        if (!history_internal_upsert(h, &h->fresh, json_array_get(page, i), true, false))
            return false;
    if (!remember_read(h, json_array_get(page, h->ascending && count ? count - 1 : 0)))
        return false;
    bool next = count && json_is_true(more);
    if (h->ascending) {
        if (next && json_array_size(h->fresh) < HISTORY_MESSAGE_LIMIT) {
            snprintf(h->recovery_id, sizeof(h->recovery_id), "%s",
                     history_internal_str(last, "id"));
            snprintf(h->recovery_time, sizeof(h->recovery_time), "%s",
                     history_internal_str(last, "created_at"));
        } else {
            h->ascending = false;
            h->recovery_id[0] = h->recovery_time[0] = '\0';
            /* The descending pass is authoritative for the complete retained
             * window. Do not let a capped forward pass crowd out recent data. */
            json_decref(h->fresh);
            h->fresh = NULL;
            h->evicted = false;
        }
        h->want_messages = true;
        return true;
    }
    if (next && h->boundary && history_internal_message_compare(last, h->boundary) > 0 &&
        json_array_size(h->fresh) < HISTORY_MESSAGE_LIMIT && !h->evicted) {
        snprintf(h->recovery_id, sizeof(h->recovery_id), "%s", history_internal_str(last, "id"));
        snprintf(h->recovery_time, sizeof(h->recovery_time), "%s",
                 history_internal_str(last, "created_at"));
        h->want_messages = true;
        return true;
    }
    /* An authoritative replacement removes deletions lost while offline. Live
     * events received throughout recovery are then reapplied in arrival order. */
    json_t *old = h->messages;
    h->messages = h->fresh ? h->fresh : json_array();
    h->fresh = NULL;
    if (!h->messages) {
        h->messages = old;
        return false;
    }
    size_t i;
    json_t *m;
    json_array_foreach(
        h->messages, i,
        m) if (!history_internal_contains(old, history_internal_str(m, "id")))++ h->insertions;
    json_decref(old);
    h->more = next;
    h->older = h->newer = h->newer_more = false;
    if (last) {
        snprintf(h->cursor_id, sizeof(h->cursor_id), "%s", history_internal_str(last, "id"));
        snprintf(h->cursor_time, sizeof(h->cursor_time), "%s",
                 history_internal_str(last, "created_at"));
    }
    if (!history_internal_replay(h) || !history_internal_trim(h, h->messages, true))
        return false;
    history_internal_clear_journal(h);
    json_decref(h->boundary);
    h->boundary = NULL;
    h->recovering = false;
    h->failures = 0;
    h->want_authors = history_message_count(h) > 0;
    if (h->r8 && history_channel(h, h->selected)) {
        json_t *channel = history_channel(h, h->selected);
        if (json_is_true(json_object_get(channel, "_dm")))
            inbox_read_conversation(h->inbox, history_internal_str(channel, "id"));
        else
            h->want_channels = true;
    }
    history_internal_status(h, "Tempo real conectado. Histórico recuperado.");
    history_internal_notice(h, HISTORY_NOTICE_READ, "");
    return true;
}

bool history_internal_request_messages(history *h)
{
    const char *id = history_internal_str(history_channel(h, h->selected), "id");
    if (!id)
        return false;
    char path[512];
    if (h->recovering && *h->recovery_id) {
        char *timestamp = curl_easy_escape(NULL, h->recovery_time, 0);
        if (!timestamp)
            return false;
        snprintf(path, sizeof(path), "/channels/%s/messages?order=%s&since=%s&last_id=%s", id,
                 h->ascending ? "asc" : "desc", timestamp, h->recovery_id);
        curl_free(timestamp);
    } else if ((h->older || h->newer) && !h->recovering) {
        snprintf(h->page_time, sizeof(h->page_time), "%s",
                 h->newer ? h->newer_time : h->cursor_time);
        snprintf(h->page_id, sizeof(h->page_id), "%s", h->newer ? h->newer_id : h->cursor_id);
        char *timestamp = curl_easy_escape(NULL, h->page_time, 0);
        if (!timestamp)
            return false;
        snprintf(path, sizeof(path), "/channels/%s/messages?order=%s&since=%s&last_id=%s", id,
                 h->newer ? "asc" : "desc", timestamp, h->page_id);
        curl_free(timestamp);
    } else
        snprintf(path, sizeof(path), "/channels/%s/messages?order=desc", id);
    return auth_get_json(h->auth, path);
}
