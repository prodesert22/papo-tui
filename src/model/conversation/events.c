#include "model/conversation/private.h"

bool history_internal_apply_patch(history *h, json_t *message, json_t *patch)
{
    if (!message || !json_is_object(patch))
        return true;
    if (json_object_get(patch, "edited_at") &&
        (!history_internal_str(message, "edited_at") ||
         history_internal_time_compare(history_internal_str(patch, "edited_at"),
                                       history_internal_str(message, "edited_at")) >= 0)) {
        if (json_object_set(message, "content", json_object_get(patch, "content")) ||
            json_object_set(message, "edited_at", json_object_get(patch, "edited_at")))
            return false;
    }
    json_t *embeds = json_object_get(patch, "embeds");
    if (embeds && json_object_set(message, "embeds", embeds))
        return false;
    return history_internal_measure_message(h, message, true) != NULL;
}

bool history_internal_patched(history *h, json_t *message)
{
    json_t *patch = json_object_get(h->patches, history_internal_str(message, "id"));
    if (json_is_null(patch))
        return true; /* deletion wins over late POST */
    /* A live message beyond an evicted tail would leave a hole in the window
     * and make its forward cursor skip messages. Fetch it by pagination. */
    json_t *last = json_array_get(h->messages, json_array_size(h->messages) - 1);
    if (h->newer_more && last && history_internal_message_compare(message, last) > 0)
        return true;
    if (!history_internal_upsert(h, &h->messages, message, true, true))
        return false;
    if (patch) {
        json_t *m = json_array_get(
            h->messages, history_internal_find(h->messages, history_internal_str(message, "id")));
        if (!history_internal_apply_patch(h, m, patch))
            return false;
    }
    return history_internal_trim(h, h->messages, true);
}

bool history_internal_replay(history *h)
{
    h->replaying = true;
    size_t i;
    json_t *event;
    json_t *journal = json_incref(h->journal);
    bool ok = true;
    json_array_foreach(journal, i, event) if (!history_event(h, event, h->context))
    {
        ok = false;
        break;
    }
    json_decref(journal);
    h->replaying = false;
    if (!ok)
        return false;
    return true;
}

void history_internal_clear_journal(history *h)
{
    json_decref(h->journal);
    h->journal = NULL;
    h->journal_bytes = 0;
}

void history_recover(history *h)
{
    if (!h || !auth_has_session(h->auth))
        return;
    if (h->pending != NONE && h->pending != INBOX && !history_internal_actions_mutating(h)) {
        auth_cancel_read(h->auth);
        h->pending = NONE;
    }
    ++h->selection;
    history_internal_actions_reset(h);
    json_decref(h->fresh);
    h->fresh = NULL;
    json_decref(h->boundary);
    h->boundary = NULL;
    history_internal_clear_journal(h);
    /* Old edits are superseded by the new authoritative window. Keep deletion
     * markers while a message POST may still produce a delayed confirmation. */
    if (!auth_busy(h->auth)) {
        json_decref(h->patches);
        h->patches = NULL;
    }
    h->evicted = h->newer_more = h->newer = false;
    h->recovering = true;
    h->want_channels = true;
    h->want_messages = h->want_authors = false;
    h->ascending = false;
    h->retry_at = 0;
    h->recovery_id[0] = h->recovery_time[0] = '\0';
    size_t count = history_message_count(h);
    if (count) {
        h->boundary = json_deep_copy(history_message(h, 0));
        if (!h->boundary) {
            h->recovering = h->want_channels = false;
            h->failed = CHANNELS;
            history_internal_status(
                h, "Não foi possível preparar a recuperação. Ctrl+R: tentar novamente.");
            return;
        }
        json_t *latest = history_message(h, count - 1);
        snprintf(h->recovery_id, sizeof(h->recovery_id), "%s", history_internal_str(latest, "id"));
        snprintf(h->recovery_time, sizeof(h->recovery_time), "%s",
                 history_internal_str(latest, "created_at"));
        h->ascending = true;
    }
    history_internal_status(h, "Recuperando canais e histórico…");
}

bool history_internal_remember(history *h, const json_t *event)
{
    if (h->replaying)
        return true;
    if (!h->journal && !(h->journal = json_array()))
        return false;
    size_t size = history_internal_encoded_size((json_t *)event);
    if (size == SIZE_MAX)
        return false;
    if (json_array_size(h->journal) >= 256 || size > 1024U * 1024U - h->journal_bytes) {
        history_recover(h);
        history_internal_status(h, "Eventos excederam o limite. Recuperando…");
        return false;
    }
    json_t *copy = json_deep_copy(event);
    if (!copy || json_array_append_new(h->journal, copy))
        return false;
    h->journal_bytes += size;
    return true;
}

bool history_event(history *h, const json_t *event, uint64_t generation)
{
    if (!h || !auth_has_session(h->auth) || generation != h->context ||
        generation != auth_context_generation(h->auth))
        return false;
    if (h->r8 && !h->replaying &&
        strcmp(history_internal_str((json_t *)event, "type")
                   ? history_internal_str((json_t *)event, "type")
                   : "",
               "message")) {
        char current[37] = "";
        if (history_channel(h, h->selected))
            snprintf(current, sizeof(current), "%s",
                     history_internal_str(history_channel(h, h->selected), "id"));
        if (!inbox_event(h->inbox, event))
            goto invalid;
        history_internal_restore_selection(h, current);
    }
    const char *type = history_internal_str((json_t *)event, "type");
    if (!type || !history_internal_string_field((json_t *)event, "type", 128, false))
        goto invalid;
    if (!strcmp(type, "role_add") || !strcmp(type, "role_remove")) {
        /* Auth's synchronous WS observer has already applied membership before
         * any queued HTTP mutation can be admitted. */
        if (!history_uuid(history_internal_str(event, "user_id")) ||
            !history_uuid(history_internal_str(event, "role_id")))
            goto invalid;
        return true;
    }
    if (!strcmp(type, "avatar_update")) {
        if (!history_internal_string_field((json_t *)event, "user_id", 36, false) ||
            !history_uuid(history_internal_str((json_t *)event, "user_id")))
            goto invalid;
        chat_assets_avatar_invalidate(h->assets, history_internal_str((json_t *)event, "user_id"));
        return true;
    }
    if (h->r8 && (!strcmp(type, "dm_update") || !strcmp(type, "new_notification"))) {
        if (!strcmp(type, "dm_update"))
            history_internal_cache_user(h,
                                        json_object_get(event, "dm")
                                            ? json_object_get(json_object_get(event, "dm"), "user")
                                            : NULL,
                                        false);
        if (!strcmp(type, "dm_update"))
            permissions_dm(auth_permissions(h->auth),
                           history_internal_str(json_object_get(event, "dm"), "id"), true);
        h->want_authors = true;
        return true;
    }
    if (!strcmp(type, "heartbeat_ack"))
        return true;
    if (!strcmp(type, "typing")) {
        if (!history_internal_typing_event(h, event))
            goto invalid;
        return true;
    }
    if (!strcmp(type, "presence_update")) {
        if (!history_internal_presence_event(h, event))
            goto invalid;
        return true;
    }
    /* This snapshot has no nickname/typing metadata. Resolve active typers
     * through summaries rather than fetching all online users. */
    if (!strcmp(type, "presence_sync"))
        return true;
    if (!strcmp(type, "error")) {
        history_internal_event_status(h, "O servidor informou um erro de tempo real.",
                                      "Erro remoto");
        return true;
    }
    if (!strcmp(type, "react_update")) {
        if (!history_internal_react_event(h, event))
            goto invalid;
        return true;
    }
    bool message = !strcmp(type, "message"), edit = !strcmp(type, "message_edit"),
         del = !strcmp(type, "message_delete");
    bool embeds = !strcmp(type, "message_embeds_update");
    bool create = !strcmp(type, "channel_create"), update = !strcmp(type, "channel_update"),
         remove = !strcmp(type, "channel_delete");
    if (!(message || edit || del || embeds || create || update || remove)) {
        history_internal_event_status(h, "Evento de tempo real não suportado ignorado.",
                                      "Evento ignorado");
        return true;
    }
    const char *channel = history_internal_str((json_t *)event, "channel_id"),
               *id = history_internal_str((json_t *)event, embeds ? "message_id" : "id");
    if (!history_internal_string_field((json_t *)event, "channel_id", 36, false) ||
        !history_uuid(channel) ||
        ((message || edit || del || embeds) &&
         (!history_internal_string_field((json_t *)event, embeds ? "message_id" : "id", 36,
                                         false) ||
          !history_uuid(id))))
        goto invalid;
    if (embeds) {
        json_t *list = json_object_get(event, "embeds");
        /* The development backend emits null when its current list is empty,
         * as REST also allows. An absent field is still an invalid event. */
        if ((!json_is_array(list) && !json_is_null(list)) || json_array_size(list) > 10)
            goto invalid;
        size_t i;
        json_t *item;
        json_array_foreach(list, i, item) if (!json_is_object(item)) goto invalid;
    }
    if (embeds && !h->replaying && h->media_update)
        h->media_update(h->media_context, event);
    if (message && !history_internal_message_valid((json_t *)event, channel))
        goto invalid;
    if (message && h->r8 && !h->replaying) {
        char current[37] = "";
        if (history_channel(h, h->selected))
            snprintf(current, sizeof(current), "%s",
                     history_internal_str(history_channel(h, h->selected), "id"));
        if (!inbox_event(h->inbox, event))
            goto invalid;
        history_internal_restore_selection(h, current);
    }
    uint64_t timestamp;
    if (edit &&
        (!json_object_get(event, "content") ||
         !history_internal_string_field((json_t *)event, "content", 32768, true) ||
         !history_internal_string_field((json_t *)event, "edited_at", 64, false) ||
         !session_time_parse(history_internal_str((json_t *)event, "edited_at"), &timestamp)))
        goto invalid;
    if ((create || update) &&
        (!history_internal_string_field((json_t *)event, "name", 2048, false) ||
         !history_internal_string_field((json_t *)event, "topic", 2048, true) ||
         !json_is_integer(json_object_get(event, "position"))))
        goto invalid;
    const char *ctype = history_internal_str((json_t *)event, "channel_type");
    if (create &&
        (!ctype || (strcmp(ctype, "text") && strcmp(ctype, "voice") && strcmp(ctype, "category"))))
        goto invalid;
    bool active = !strcmp(channel, history_internal_str(history_channel(h, h->selected), "id")
                                       ? history_internal_str(history_channel(h, h->selected), "id")
                                       : "");
    bool relevant = create || update || remove || active;
    if (relevant && !h->replaying &&
        (h->recovering || h->pending == CHANNELS || h->pending == MESSAGES ||
         (h->pending == ACTIONS && !history_internal_actions_mutating(h))))
        if (!history_internal_remember(h, event))
            return false;
    if (create || update || remove) {
        size_t index = history_internal_find(h->channels, channel);
        json_t *next = h->channels ? json_deep_copy(h->channels) : json_array();
        if (!next)
            goto allocation;
        bool was_active = active;
        if (remove) {
            if (index != SIZE_MAX && json_array_remove(next, index)) {
                json_decref(next);
                goto allocation;
            }
        } else if (index != SIZE_MAX || create) {
            json_t *c = index != SIZE_MAX ? json_array_get(next, index) : json_object();
            if (!c) {
                json_decref(next);
                goto allocation;
            }
            bool ok = !json_object_set_new(c, "id", json_string(channel)) &&
                      !json_object_set(c, "name", json_object_get(event, "name")) &&
                      !json_object_set(c, "position", json_object_get(event, "position"));
            if (json_object_get(event, "topic"))
                ok &= !json_object_set(c, "topic", json_object_get(event, "topic"));
            if (create)
                ok &= !json_object_set_new(c, "type", json_string(ctype));
            if (index == SIZE_MAX) {
                ok &= !json_array_append(next, c);
                json_decref(c);
            }
            if (!ok) {
                json_decref(next);
                goto allocation;
            }
        }
        json_t *body = json_pack("{s:o}", "channels", next);
        if (!body)
            goto allocation;
        bool ok = history_internal_channels_response(h, body);
        json_decref(body);
        if (!ok)
            goto invalid;
        if (remove && was_active) {
            if (h->pending != NONE)
                auth_cancel_read(h->auth);
            h->pending = NONE;
            ++h->selection;
            history_internal_status(h, "Canal removido ou indisponível. Rascunho preservado.");
        }
        return true;
    }
    if (message) {
        size_t ci = history_internal_find(h->channels, channel);
        json_t *c = json_array_get(h->channels, ci);
        if (c) {
            json_t *author_field = json_object_get(event, "author_id");
            json_t *copy =
                json_pack("{s:O,s:O,s:O,s:O}", "id", json_object_get(event, "id"), "author_id",
                          author_field ? author_field : json_null(), "created_at",
                          json_object_get(event, "created_at"), "content",
                          json_object_get(event, "content") ? json_object_get(event, "content")
                                                            : json_null());
            if (!copy || json_object_set_new(c, "last_message", copy))
                goto allocation;
        }
    }
    if (!active)
        return true;
    history_internal_reference_event(h, event);
    if (message && !h->replaying) {
        const char *author_typing = history_internal_str((json_t *)event, "author_id");
        if (author_typing && *author_typing)
            history_internal_typing_remove(h, author_typing);
    }
    if (edit || del || embeds) {
        json_t *previous = json_object_get(h->patches, id);
        json_t *existing = json_array_get(h->messages, history_internal_find(h->messages, id));
        if (edit && history_internal_str(existing, "edited_at") &&
            history_internal_time_compare(history_internal_str(existing, "edited_at"),
                                          history_internal_str((json_t *)event, "edited_at")) > 0)
            return true;
        if (!del && json_is_null(previous))
            return true;
        if (edit && history_internal_str(previous, "edited_at") &&
            history_internal_time_compare(history_internal_str(previous, "edited_at"),
                                          history_internal_str((json_t *)event, "edited_at")) > 0)
            return true;
        if (!h->patches && !(h->patches = json_object()))
            goto allocation;
        if (!json_object_get(h->patches, id) && json_object_size(h->patches) >= 256) {
            history_recover(h);
            history_internal_status(h, "Marcas excederam o limite. Recuperando…");
            return false;
        }
        json_t *patch;
        if (del)
            patch = json_null();
        else if (embeds)
            patch = json_is_null(json_object_get(event, "embeds"))
                        ? json_pack("{s:[]}", "embeds")
                        : json_pack("{s:O}", "embeds", json_object_get(event, "embeds"));
        else
            patch = json_pack("{s:O,s:O}", "content", json_object_get(event, "content"),
                              "edited_at", json_object_get(event, "edited_at"));
        if (!patch)
            goto allocation;
        json_t *copy = json_deep_copy(patch);
        json_decref(patch);
        if (copy && !del && json_is_object(previous)) {
            json_t *combined = json_deep_copy(previous);
            if (combined && json_object_update(combined, copy)) {
                json_decref(combined);
                combined = NULL;
            }
            json_decref(copy);
            copy = combined;
        }
        if (!copy || json_object_set_new(h->patches, id, copy))
            goto allocation;
        size_t patch_bytes = history_internal_encoded_size(h->patches);
        if (patch_bytes == SIZE_MAX)
            goto allocation;
        if (patch_bytes > 1024U * 1024U) {
            json_object_del(h->patches, id);
            history_recover(h);
            history_internal_status(h, "Marcas excederam o limite. Recuperando…");
            return false;
        }
        size_t index = history_internal_find(h->messages, id);
        if (index != SIZE_MAX) {
            if (del) {
                if (json_array_remove(h->messages, index))
                    goto allocation;
            } else if (!history_internal_apply_patch(h, json_array_get(h->messages, index),
                                                     json_object_get(h->patches, id)))
                goto allocation;
            if (!history_internal_trim(h, h->messages, true))
                goto allocation;
        }
    } else if (message) {
        bool added = !history_internal_contains(h->messages, id);
        if (!history_internal_patched(h, (json_t *)event))
            goto allocation;
        if (added && history_internal_contains(h->messages, id) && !h->replaying)
            ++h->insertions;
        h->want_authors = true;
    }
    if (active && (edit || embeds))
        h->want_authors = true;
    return true;
invalid:
    history_internal_event_status(h, "Evento de tempo real inválido ignorado.", "Evento inválido");
    return false;
allocation:
    history_internal_event_status(h, "Não foi possível aplicar o evento. Ctrl+R: recuperar.",
                                  "Falha no evento");
    return false;
}
