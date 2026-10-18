#include "model/conversation/discovery.h"
#include "model/conversation/private.h"

/* One auxiliary owner shares history's REST dispatcher. All JSON is owned here;
 * public accessors borrow until the next event/poll/context change. */
enum action_kind {
    ACTION_IDLE,
    ACTION_SCAN,
    ACTION_LOCATE,
    ACTION_JUMP,
    ACTION_REACT,
    ACTION_USERS,
    ACTION_EDIT,
    ACTION_DELETE,
    ACTION_PIN,
    ACTION_UNPIN
};
struct history_actions {
    json_t *references, *users, *payload;
    char scan_id[37], scan_time[65], page_id[37], page_time[65];
    char jump[37], ready[37], reactors[37], user_id[37], user_time[65];
    char jump_time[65];
    bool jump_located, jump_missing, jump_silent;
    char mutation_message[37], mutation_channel[37], requested_message[37];
    bool end, failed, want_users, users_more, users_failed, queued, remove, restart_scan,
        yield_scan;
    enum action_kind request, mutation;
    char edited_message[37];
    bool edit_ready, edit_success, pin_ready, pin_success;
    char pin_message[37];
    uint64_t revision, selection;
    size_t bytes;
};
static const char *s(const json_t *o, const char *key)
{
    const char *v = history_internal_str(o, key);
    return v ? v : "";
}
static struct history_actions *actions(history *h)
{
    if (!h->actions) {
        h->actions = calloc(1, sizeof(*h->actions));
        if (h->actions) {
            h->actions->references = json_object();
            h->actions->users = json_array();
            if (!h->actions->references || !h->actions->users)
                history_internal_actions_destroy(h);
        }
    }
    return h->actions;
}
bool history_internal_actions_mutating(history *h)
{
    return h->pending == ACTIONS && h->actions &&
           (h->actions->request == ACTION_REACT || h->actions->request == ACTION_EDIT ||
            h->actions->request == ACTION_DELETE || h->actions->request == ACTION_PIN ||
            h->actions->request == ACTION_UNPIN);
}
void history_internal_actions_reset(history *h)
{
    struct history_actions *a = h->actions;
    if (!a)
        return;
    json_object_clear(a->references);
    json_array_clear(a->users);
    a->bytes = 0;
    a->scan_id[0] = a->scan_time[0] = a->jump[0] = a->ready[0] = a->reactors[0] = 0;
    a->jump_time[0] = 0;
    a->jump_located = a->jump_missing = false;
    a->end = a->failed = a->want_users = a->users_more = a->users_failed = false;
    a->restart_scan = a->yield_scan = a->edit_ready = false;
    history_internal_notice(h, HISTORY_NOTICE_MUTATION, "");
    if (!history_internal_actions_mutating(h)) {
        /* A queued edit can be canceled by a refresh before dispatch. Release
         * the editor's pending state even when no HTTP receipt will arrive. */
        if (a->queued && a->mutation == ACTION_EDIT) {
            snprintf(a->edited_message, sizeof(a->edited_message), "%s", a->mutation_message);
            a->edit_ready = true;
            a->edit_success = false;
        }
        if (a->queued && (a->mutation == ACTION_PIN || a->mutation == ACTION_UNPIN)) {
            snprintf(a->pin_message, sizeof(a->pin_message), "%s", a->mutation_message);
            a->pin_ready = true;
            a->pin_success = false;
        }
        json_decref(a->payload);
        a->payload = NULL;
        a->queued = false;
        a->request = ACTION_IDLE;
    }
    ++a->revision;
}
void history_internal_actions_destroy(history *h)
{
    if (!h->actions)
        return;
    json_decref(h->actions->references);
    json_decref(h->actions->users);
    json_decref(h->actions->payload);
    free(h->actions);
    h->actions = NULL;
}
json_t *history_find_message(history *h, const char *id)
{
    if (!h || !id || !*id)
        return NULL;
    if (json_is_null(json_object_get(h->patches, id)))
        return json_false();
    json_t *m = json_array_get(h->messages, history_internal_find(h->messages, id));
    if (m)
        return m;
    return h->actions ? json_object_get(h->actions->references, id) : NULL;
}
static json_t *reference_copy(const json_t *m)
{
    json_t *copy = json_object();
    if (!copy)
        return NULL;
    const char *keys[] = {"id",        "channel_id", "author_id", "created_at",
                          "edited_at", "reply_to",   "webhook_id"};
    for (size_t i = 0; i < sizeof(keys) / sizeof(*keys); ++i) {
        json_t *value = json_object_get(m, keys[i]);
        if (value && json_object_set(copy, keys[i], value))
            goto fail;
    }
    json_t *external = json_object_get(m, "external_author");
    const char *name = s(external, "name");
    if (*name && strlen(name) <= 512) {
        json_t *author = json_pack("{s:s}", "name", name);
        if (!author || json_object_set_new(copy, "external_author", author))
            goto fail;
    }
    const char *content = s(m, "content");
    if (!*content) {
        json_t *attachment = json_array_get(json_object_get(m, "attachments"), 0);
        content = attachment ? s(attachment, "original_file_name") : "[embed]";
    }
    size_t bytes = MIN(strlen(content), 1024U);
    while (bytes && !g_utf8_validate(content, (gssize)bytes, NULL))
        --bytes;
    mention_range range;
    size_t offset = 0;
    while (mention_next(content, offset, &range)) {
        if (range.start >= bytes)
            break;
        if (range.end > bytes) {
            bytes = range.start;
            break;
        }
        offset = range.end;
    }
    if (json_object_set_new(copy, "content", json_stringn(content, bytes)))
        goto fail;
    return copy;
fail:
    json_decref(copy);
    return NULL;
}
static void prune_reference(history *h)
{
    struct history_actions *a = h->actions;
    const char *id;
    json_t *m;
    json_object_foreach(a->references, id, m)
    {
        bool needed = !strcmp(id, a->jump);
        for (size_t i = 0; !needed && i < history_message_count(h); ++i)
            needed = !strcmp(id, s(history_message(h, i), "reply_to"));
        if (needed)
            continue;
        size_t bytes = json_is_object(m) ? history_internal_encoded_size(m) : 0;
        if (bytes <= a->bytes)
            a->bytes -= bytes;
        json_object_del(a->references, id);
        break;
    }
}
void history_reference_request(history *h, const char *id)
{
    if (!h || !h->active || !history_uuid(id) || json_is_object(history_find_message(h, id)) ||
        json_is_false(history_find_message(h, id)))
        return;
    struct history_actions *a = actions(h);
    if (!a || !a->references || json_object_get(a->references, id))
        return;
    if (json_object_size(a->references) >= HISTORY_MESSAGE_LIMIT)
        prune_reference(h);
    if (json_object_size(a->references) >= HISTORY_MESSAGE_LIMIT)
        return;
    json_object_set_new(a->references, id, json_null());
    /* A newly requested target may have been passed by an earlier scan. */
    if (a->request == ACTION_SCAN)
        a->restart_scan = true;
    else
        a->scan_id[0] = a->scan_time[0] = 0;
    a->end = false;
}
const char *history_reference_notice(history *h)
{
    struct history_actions *a = h->actions;
    return a && a->failed ? "Falha ao buscar mensagem; Ctrl+R: tentar novamente"
           : a && a->end  ? "mensagem não disponível"
                          : "Carregando mensagem…";
}
uint64_t history_reference_revision(history *h)
{
    return h->messages_revision + (h->actions ? h->actions->revision : 0);
}
bool history_interactions_retry(history *h)
{
    if (!h->actions || (!h->actions->failed && !h->actions->users_failed))
        return false;
    if (*h->actions->reactors && h->actions->users_failed)
        h->actions->want_users = true;
    h->actions->failed = h->actions->users_failed = false;
    ++h->actions->revision;
    return true;
}
bool history_jump(history *h, const char *id)
{
    bool ok = history_jump_at(h, id, NULL);
    if (ok) {
        h->actions->jump_silent = false;
        if (*h->actions->jump)
            history_internal_status(h, "Localizando mensagem…");
    } else if (h)
        history_internal_status(h, "Mensagem original não disponível.");
    return ok;
}
bool history_jump_at(history *h, const char *id, const char *created_at)
{
    if (!h || !h->active || !auth_has_session(h->auth) || !history_uuid(id))
        return false;
    if (json_is_false(history_find_message(h, id))) {
        return false;
    }
    struct history_actions *a = actions(h);
    if (!a)
        return false;
    if (h->failed == MESSAGES) {
        h->failed = NONE;
        h->want_messages = true;
    }
    a->failed = false;
    a->jump_silent = true;
    a->jump_missing = a->jump_located = false;
    a->jump_time[0] = 0;
    a->jump[0] = a->ready[0] = 0;
    if (history_internal_find(h->messages, id) != SIZE_MAX)
        snprintf(a->ready, sizeof(a->ready), "%s", id);
    else {
        uint64_t parsed;
        if (created_at && strlen(created_at) < sizeof(a->jump_time) &&
            session_time_parse(created_at, &parsed))
            snprintf(a->jump_time, sizeof(a->jump_time), "%s", created_at);
        else
            history_reference_request(h, id);
        snprintf(a->jump, sizeof(a->jump), "%s", id);
    }
    return true;
}
void history_cancel_jump(history *h)
{
    struct history_actions *a = h ? h->actions : NULL;
    if (!a)
        return;
    if (*a->jump && json_is_null(json_object_get(a->references, a->jump)))
        json_object_del(a->references, a->jump);
    a->jump[0] = a->ready[0] = a->jump_time[0] = 0;
    a->jump_missing = a->jump_located = false;
}
const char *history_jump_error(history *h)
{
    if (!h || !h->actions)
        return "Não foi possível localizar a mensagem.";
    if (h->actions->jump_missing)
        return "Mensagem não disponível.";
    if (h->actions->failed || h->failed == MESSAGES)
        return "Falha ao localizar mensagem. Enter: tentar novamente.";
    if (!*h->actions->jump && !*h->actions->ready)
        return "Busca interrompida. Enter: tentar novamente.";
    return "";
}
bool history_take_jump(history *h, char id[37])
{
    /* A destination can be queued before the newly selected channel's first
     * page arrives. It needs no scan if that page already contains the target. */
    if (h->actions && h->actions->request == ACTION_IDLE && !h->actions->failed &&
        *h->actions->jump && history_internal_find(h->messages, h->actions->jump) != SIZE_MAX) {
        snprintf(h->actions->ready, sizeof(h->actions->ready), "%s", h->actions->jump);
        h->actions->jump[0] = 0;
    }
    if (h->actions && h->actions->request == ACTION_IDLE && h->actions->end &&
        !h->actions->failed && !*h->actions->jump_time && *h->actions->jump &&
        !json_is_object(json_object_get(h->actions->references, h->actions->jump))) {
        json_object_set_new(h->actions->references, h->actions->jump, json_false());
        h->actions->jump[0] = 0;
        h->actions->jump_missing = true;
        if (!h->actions->jump_silent)
            history_internal_status(h, "Mensagem não disponível.");
    }
    if (!h->actions || !*h->actions->ready)
        return false;
    memcpy(id, h->actions->ready, 37);
    h->actions->ready[0] = 0;
    return true;
}
bool history_reaction_valid(const json_t *emoji)
{
    if (!history_internal_string_field((json_t *)emoji, "emoji_id", 36, true) ||
        !history_internal_string_field((json_t *)emoji, "unicode", 64, true))
        return false;
    const char *id = history_internal_str(emoji, "emoji_id"),
               *unicode = history_internal_str(emoji, "unicode");
    if ((id && *id) == (unicode && *unicode))
        return false;
    if (id && *id)
        return history_internal_string_field((json_t *)emoji, "emoji_id", 36, false) &&
               history_uuid(id);
    if (!history_internal_string_field((json_t *)emoji, "unicode", 64, false) ||
        !g_utf8_validate(unicode, -1, NULL) || g_utf8_strlen(unicode, -1) > 16)
        return false;
    for (const char *p = unicode; *p; p = g_utf8_next_char(p))
        if (g_unichar_iscntrl(g_utf8_get_char(p)))
            return false;
    return true;
}
bool history_reaction_equal(const json_t *a, const json_t *b)
{
    return !strcmp(s(a, "emoji_id"), s(b, "emoji_id")) && !strcmp(s(a, "unicode"), s(b, "unicode"));
}
bool history_reaction_mine(const json_t *message, const json_t *emoji)
{
    size_t i;
    json_t *item;
    json_array_foreach(json_object_get(message, "user_reactions"), i,
                       item) if (history_reaction_equal(item, emoji)) return true;
    return false;
}
bool history_react(history *h, const char *message, const json_t *emoji, bool remove)
{
    if (!h || !h->active || h->recovering || !auth_has_session(h->auth) || !history_uuid(message) ||
        !history_reaction_valid(emoji) || history_internal_find(h->messages, message) == SIZE_MAX ||
        history_permission(h, remove ? PERMISSION_READ_CHANNEL : PERMISSION_SEND_MESSAGES) !=
            PERMISSION_ALLOWED)
        return false;
    struct history_actions *a = actions(h);
    if (!a || a->queued || a->request == ACTION_REACT || a->request == ACTION_EDIT ||
        a->request == ACTION_DELETE || a->request == ACTION_PIN || a->request == ACTION_UNPIN)
        return false;
    a->payload = json_pack("{s:s}", *s(emoji, "emoji_id") ? "emoji_id" : "unicode",
                           *s(emoji, "emoji_id") ? s(emoji, "emoji_id") : s(emoji, "unicode"));
    if (!a->payload)
        return false;
    snprintf(a->mutation_message, sizeof(a->mutation_message), "%s", message);
    snprintf(a->mutation_channel, sizeof(a->mutation_channel), "%s",
             s(history_channel(h, h->selected), "id"));
    a->remove = remove;
    a->mutation = ACTION_REACT;
    a->queued = true;
    history_internal_status(h, "Reação aguardando envio…");
    return true;
}
static bool queue_message_mutation(history *h, const char *message, const char *content,
                                   enum action_kind kind)
{
    if (!h || !h->active || h->recovering || !auth_has_session(h->auth) || !history_uuid(message) ||
        (history_internal_find(h->messages, message) == SIZE_MAX &&
         !((kind == ACTION_PIN || kind == ACTION_UNPIN) &&
           history_internal_contains(history_pins(h), message))))
        return false;
    json_t *m = history_find_message(h, message);
    bool pin = kind == ACTION_PIN || kind == ACTION_UNPIN;
    if (pin ? permissions_role(auth_permissions(h->auth), PERMISSION_PIN_MESSAGE) !=
                      PERMISSION_ALLOWED ||
                  history_permission(h, PERMISSION_READ_CHANNEL) != PERMISSION_ALLOWED
            : permissions_message(auth_permissions(h->auth), m, kind == ACTION_EDIT) !=
                  PERMISSION_ALLOWED)
        return false;
    if (kind == ACTION_EDIT && (strcmp(s(m, "author_id"), auth_user_id(h->auth)) || !content ||
                                !*content || strlen(content) > 8192))
        return false;
    struct history_actions *a = actions(h);
    if (!a || a->queued || history_internal_actions_mutating(h) || a->edit_ready)
        return false;
    json_t *payload = kind == ACTION_EDIT ? json_pack("{s:s}", "content", content) : NULL;
    if (kind == ACTION_EDIT && !payload)
        return false;
    json_decref(a->payload);
    a->payload = payload;
    snprintf(a->mutation_message, sizeof(a->mutation_message), "%s", message);
    snprintf(a->mutation_channel, sizeof(a->mutation_channel), "%s",
             s(history_channel(h, h->selected), "id"));
    a->mutation = kind;
    a->queued = true;
    a->pin_ready = false;
    history_internal_notice(h, HISTORY_NOTICE_MUTATION,
                            kind == ACTION_PIN || kind == ACTION_UNPIN ? "Fixação aguardando envio…"
                            : kind == ACTION_EDIT ? "Edição aguardando envio…"
                                                  : "Exclusão aguardando envio…");
    return true;
}
bool history_edit_message(history *h, const char *message, const char *content)
{
    return queue_message_mutation(h, message, content, ACTION_EDIT);
}
bool history_delete_message(history *h, const char *message)
{
    return queue_message_mutation(h, message, NULL, ACTION_DELETE);
}
bool history_pin_message(history *h, const char *message, bool remove)
{
    if (history_pin_state(h, message) < 0)
        return false;
    return queue_message_mutation(h, message, NULL, remove ? ACTION_UNPIN : ACTION_PIN);
}
bool history_take_pin_result(history *h, char message[37], bool *success)
{
    if (!h || !h->actions || !h->actions->pin_ready)
        return false;
    memcpy(message, h->actions->pin_message, 37);
    *success = h->actions->pin_success;
    h->actions->pin_ready = false;
    return true;
}
bool history_take_edit_result(history *h, char message[37], bool *success)
{
    if (!h || !h->actions || !h->actions->edit_ready)
        return false;
    struct history_actions *a = h->actions;
    memcpy(message, a->edited_message, sizeof(a->edited_message));
    *success = a->edit_success;
    a->edit_ready = false;
    return true;
}
void history_reactors_open(history *h, const char *message)
{
    struct history_actions *a = actions(h);
    if (!a || !history_uuid(message))
        return;
    json_array_clear(a->users);
    snprintf(a->reactors, sizeof(a->reactors), "%s", message);
    a->user_id[0] = a->user_time[0] = 0;
    a->want_users = true;
    a->users_failed = false;
    a->users_more = true;
}
void history_reactors_close(history *h)
{
    if (h && h->actions) {
        h->actions->reactors[0] = 0;
        h->actions->want_users = false;
    }
}
void history_reactors_more(history *h)
{
    if (h->actions && h->actions->users_more && !h->actions->users_failed &&
        h->actions->request != ACTION_USERS)
        h->actions->want_users = true;
}
json_t *history_reactors(history *h)
{
    return h->actions ? h->actions->users : NULL;
}
const char *history_reactors_notice(history *h)
{
    struct history_actions *a = h->actions;
    return !a || a->users_failed ? "Falha ao listar reações. Ctrl+R: tentar novamente."
           : json_array_size(a->users) >= HISTORY_MESSAGE_LIMIT
               ? "Lista parcial: limite local atingido."
           : a->want_users || a->request == ACTION_USERS ? "Carregando reações…"
           : a->users_more                               ? "Role para carregar mais · Esc fechar"
                                                         : "Fim da lista · Esc fechar";
}
static bool page_path(char *path, size_t capacity, const char *base, const char *time,
                      const char *id)
{
    char *encoded = *id ? curl_easy_escape(NULL, time, 0) : NULL;
    if (*id && !encoded)
        return false;
    snprintf(path, capacity, "%s?order=desc%s%s%s%s", base, *id ? "&since=" : "",
             encoded ? encoded : "", *id ? "&last_id=" : "", id);
    curl_free(encoded);
    return true;
}
bool history_internal_actions_dispatch(history *h, bool foreground)
{
    struct history_actions *a = h->actions;
    if (!a || !h->active || h->recovering || a->request != ACTION_IDLE)
        return false;
    char base[192], path[512];
    const char *channel = s(history_channel(h, h->selected), "id");
    if (!history_uuid(channel))
        return false;
    enum action_kind kind = ACTION_IDLE;
    if (a->queued) {
        json_t *message = history_find_message(h, a->mutation_message);
        permission_decision decision =
            a->mutation == ACTION_PIN || a->mutation == ACTION_UNPIN
                ? permissions_role(auth_permissions(h->auth), PERMISSION_PIN_MESSAGE)
            : a->mutation == ACTION_REACT
                ? history_permission(h,
                                     a->remove ? PERMISSION_READ_CHANNEL : PERMISSION_SEND_MESSAGES)
                : permissions_message(auth_permissions(h->auth), message,
                                      a->mutation == ACTION_EDIT);
        if ((a->mutation == ACTION_PIN || a->mutation == ACTION_UNPIN) &&
            decision == PERMISSION_ALLOWED)
            decision = history_permission(h, PERMISSION_READ_CHANNEL);
        if (decision != PERMISSION_ALLOWED) {
            a->queued = false;
            json_decref(a->payload);
            a->payload = NULL;
            if (a->mutation == ACTION_EDIT) {
                snprintf(a->edited_message, sizeof(a->edited_message), "%s", a->mutation_message);
                a->edit_ready = true;
                a->edit_success = false;
            }
            if (a->mutation == ACTION_PIN || a->mutation == ACTION_UNPIN) {
                snprintf(a->pin_message, sizeof(a->pin_message), "%s", a->mutation_message);
                a->pin_ready = true;
                a->pin_success = false;
            }
            history_internal_notice(h, HISTORY_NOTICE_MUTATION,
                                    "Permissão indisponível; operação não enviada.");
            return true;
        }
        const char *method;
        if (a->mutation == ACTION_PIN || a->mutation == ACTION_UNPIN) {
            if (!auth_pin_message(h->auth, a->mutation_channel, a->mutation_message,
                                  a->mutation == ACTION_UNPIN))
                return false;
            kind = a->mutation;
            a->queued = false;
            goto dispatched;
        }
        if (a->mutation == ACTION_REACT) {
            snprintf(path, sizeof(path), "/channels/%s/messages/%s/reactions", a->mutation_channel,
                     a->mutation_message);
            method = a->remove ? "DELETE" : "POST";
        } else {
            snprintf(path, sizeof(path), "/messages/%s", a->mutation_message);
            method = a->mutation == ACTION_EDIT ? "PUT" : "DELETE";
        }
        if (!auth_mutate_json(h->auth, method, path, a->payload))
            return false;
        auth_guard_message(h->auth, message,
                           a->remove ? PERMISSION_READ_CHANNEL : PERMISSION_SEND_MESSAGES,
                           a->mutation == ACTION_EDIT, a->mutation == ACTION_DELETE);
        kind = a->mutation;
        a->queued = false;
    } else if (*a->reactors && a->want_users && a->users_more &&
               json_array_size(a->users) < HISTORY_MESSAGE_LIMIT) {
        snprintf(base, sizeof(base), "/channels/%s/messages/%s/reactions", channel, a->reactors);
        if (!page_path(path, sizeof(path), base, a->user_time, a->user_id) ||
            !auth_get_json(h->auth, path))
            return false;
        kind = ACTION_USERS;
        snprintf(a->requested_message, sizeof(a->requested_message), "%s", a->reactors);
        a->want_users = false;
    } else if (*a->jump && *a->jump_time && !a->failed && !h->want_messages && !h->want_channels) {
        snprintf(base, sizeof(base), "/channels/%s/messages", channel);
        if (a->jump_located) {
            if (!page_path(path, sizeof(path), base, a->page_time, a->page_id))
                return false;
            kind = ACTION_JUMP;
        } else {
            char *encoded = curl_easy_escape(NULL, a->jump_time, 0);
            if (!encoded)
                return false;
            snprintf(path, sizeof(path), "%s?order=asc&since=%s&last_id=%s", base, encoded,
                     a->jump);
            curl_free(encoded);
            kind = ACTION_LOCATE;
        }
        if (!auth_get_json(h->auth, path))
            return false;
    } else if (*a->jump && !a->failed && json_is_object(json_object_get(a->references, a->jump))) {
        json_t *target = json_object_get(a->references, a->jump);
        snprintf(base, sizeof(base), "/channels/%s/messages", channel);
        if (!page_path(path, sizeof(path), base, s(target, "_reply_page_time"),
                       s(target, "_reply_page_id")) ||
            !auth_get_json(h->auth, path))
            return false;
        kind = ACTION_JUMP;
    } else if (!foreground && !a->end && !a->failed) {
        if (a->yield_scan) {
            a->yield_scan = false;
            return false;
        }
        bool missing = false;
        const char *id;
        json_t *m;
        json_object_foreach(a->references, id,
                            m) if (json_is_null(m) && history_internal_find(h->messages, id) ==
                                                          SIZE_MAX) missing = true;
        if (!missing)
            return false;
        if (!*a->scan_id && json_array_size(h->messages)) {
            json_t *oldest = json_array_get(h->messages, 0);
            snprintf(a->scan_id, sizeof(a->scan_id), "%s", s(oldest, "id"));
            snprintf(a->scan_time, sizeof(a->scan_time), "%s", s(oldest, "created_at"));
        }
        snprintf(base, sizeof(base), "/channels/%s/messages", channel);
        if (!page_path(path, sizeof(path), base, a->scan_time, a->scan_id) ||
            !auth_get_json(h->auth, path))
            return false;
        snprintf(a->page_id, sizeof(a->page_id), "%s", a->scan_id);
        snprintf(a->page_time, sizeof(a->page_time), "%s", a->scan_time);
        kind = ACTION_SCAN;
        a->yield_scan = true;
    } else
        return false;
dispatched:
    if (kind == ACTION_LOCATE || kind == ACTION_JUMP)
        snprintf(a->requested_message, sizeof(a->requested_message), "%s", a->jump);
    a->request = kind;
    a->selection = h->selection;
    h->pending = ACTIONS;
    h->request_context = h->context;
    h->request_selection = h->selection;
    return true;
}
static bool own_reaction(history *h, struct history_actions *a)
{
    json_t *m =
        json_array_get(h->messages, history_internal_find(h->messages, a->mutation_message));
    if (!m)
        return true;
    json_t *mine = json_object_get(m, "user_reactions");
    json_t *copy = json_is_array(mine) ? json_deep_copy(mine) : json_array();
    if (!copy)
        return false;
    size_t i;
    json_t *item;
    json_array_foreach(copy, i, item) if (history_reaction_equal(item, a->payload))
    {
        if (a->remove)
            json_array_remove(copy, i);
        break;
    }
    if (!a->remove && i == json_array_size(copy) && json_array_append(copy, a->payload)) {
        json_decref(copy);
        return false;
    }
    bool ok = !json_object_set_new(m, "user_reactions", copy) &&
              history_internal_measure_message(h, m, true);
    ++h->messages_revision;
    return ok;
}
static int reactor_compare(const void *left, const void *right)
{
    const json_t *a = *(json_t *const *)left, *b = *(json_t *const *)right;
    int cmp = strcmp(s(a, "emoji_id"), s(b, "emoji_id"));
    if (!cmp)
        cmp = strcmp(s(a, "unicode"), s(b, "unicode"));
    return cmp ? cmp : -history_internal_message_compare((json_t *)a, (json_t *)b);
}
static bool reactor_page(history *h, json_t *body)
{
    struct history_actions *a = h->actions;
    if (strcmp(s(body, "message_id"), a->reactors) ||
        !json_is_array(json_object_get(body, "reactions")) ||
        !json_is_boolean(json_object_get(body, "has_more")))
        return false;
    json_t *next = json_deep_copy(a->users), *oldest = NULL;
    size_t i, j, total = 0;
    json_t *group, *u;
    if (!next)
        return false;
    json_array_foreach(json_object_get(body, "reactions"), i, group)
    {
        if (!history_reaction_valid(group) || !json_is_array(json_object_get(group, "users")))
            goto fail;
        json_array_foreach(json_object_get(group, "users"), j, u)
        {
            uint64_t time;
            if (++total > 100 || !history_uuid(s(u, "id")) || !history_uuid(s(u, "user_id")) ||
                !session_time_parse(s(u, "created_at"), &time))
                goto fail;
            if (!oldest || history_internal_message_compare(u, oldest) < 0)
                oldest = u;
            if (history_internal_contains(next, s(u, "id")))
                continue;
            if (json_array_size(next) >= HISTORY_MESSAGE_LIMIT)
                break;
            json_t *entry = json_deep_copy(u);
            if (!entry ||
                json_object_set_new(entry, "emoji_id",
                                    *s(group, "emoji_id") ? json_string(s(group, "emoji_id"))
                                                          : json_null()) ||
                json_object_set_new(entry, "unicode",
                                    *s(group, "unicode") ? json_string(s(group, "unicode"))
                                                         : json_null())) {
                json_decref(entry);
                goto fail;
            }
            if (json_array_append_new(next, entry))
                goto fail;
        }
    }
    if (oldest && *a->user_id) {
        int ordering = history_internal_time_compare(s(oldest, "created_at"), a->user_time);
        if (ordering > 0 || (!ordering && strcmp(s(oldest, "id"), a->user_id) >= 0))
            goto fail;
    }
    if (oldest) {
        snprintf(a->user_id, sizeof(a->user_id), "%s", s(oldest, "id"));
        snprintf(a->user_time, sizeof(a->user_time), "%s", s(oldest, "created_at"));
    }
    json_t *sorted[HISTORY_MESSAGE_LIMIT];
    size_t count = json_array_size(next);
    for (size_t k = 0; k < count; ++k)
        sorted[k] = json_incref(json_array_get(next, k));
    qsort(sorted, count, sizeof(*sorted), reactor_compare);
    json_array_clear(next);
    bool appended = true;
    for (size_t k = 0; k < count; ++k)
        appended &= json_array_append_new(next, sorted[k]) == 0;
    if (!appended)
        goto fail;
    a->users_more = oldest && json_is_true(json_object_get(body, "has_more"));
    a->want_users &= a->users_more;
    json_decref(a->users);
    a->users = next;
    h->want_authors = true;
    return true;
fail:
    json_decref(next);
    return false;
}
void history_internal_actions_response(history *h, json_t *body, long code, bool uncertain)
{
    struct history_actions *a = h->actions;
    if (!a)
        return;
    enum action_kind kind = a->request;
    a->request = ACTION_IDLE;
    bool current = a->selection == h->selection;
    if (kind == ACTION_PIN || kind == ACTION_UNPIN) {
        bool remove = kind == ACTION_UNPIN;
        bool ok = !uncertain && (remove ? code == 204 : code == 200 || code == 201);
        uint64_t time;
        if (ok && !remove &&
            (strcmp(s(body, "message_id"), a->mutation_message) ||
             strcmp(s(body, "channel_id"), a->mutation_channel) ||
             !session_time_parse(s(body, "pinned_at"), &time))) {
            ok = false;
            uncertain = true;
        }
        if (!strcmp(a->mutation_channel, s(history_channel(h, h->selected), "id"))) {
            snprintf(a->pin_message, sizeof(a->pin_message), "%s", a->mutation_message);
            a->pin_ready = true;
            a->pin_success = ok;
            if (ok || uncertain || code == 404)
                history_internal_pins_invalidate(h);
            history_internal_notice(
                h, HISTORY_NOTICE_MUTATION,
                ok            ? (remove ? "Fixação removida." : "Mensagem fixada.")
                : code == 403 ? "Sem permissão para alterar a fixação."
                : code == 404 ? "Mensagem ou fixação não disponível."
                : code == 409 ? "Limite de 100 mensagens fixadas atingido."
                : code == 429 ? "Limite de requisições. Aguarde antes de tentar novamente."
                : uncertain   ? "Fixação sem confirmação. Atualize antes de tentar novamente."
                              : "Não foi possível alterar a fixação.");
        }
        json_decref(a->payload);
        a->payload = NULL;
        return;
    }
    if (kind == ACTION_EDIT || kind == ACTION_DELETE) {
        current = !strcmp(a->mutation_channel, s(history_channel(h, h->selected), "id"));
        bool edit = kind == ACTION_EDIT;
        bool ok = !uncertain && code == (edit ? 200 : 204);
        uint64_t edited_at;
        if (ok && edit &&
            (!history_internal_message_valid(body, a->mutation_channel) ||
             strcmp(s(body, "id"), a->mutation_message) ||
             strcmp(s(body, "author_id"), auth_user_id(h->auth)) ||
             strcmp(s(body, "content"), s(a->payload, "content")) ||
             !session_time_parse(s(body, "edited_at"), &edited_at))) {
            ok = false;
            uncertain = true;
        }
        if (current) {
            if (ok || code == 404) {
                json_t *event =
                    edit && ok ? json_pack("{s:s,s:s,s:s,s:O,s:O}", "type", "message_edit", "id",
                                           a->mutation_message, "channel_id", a->mutation_channel,
                                           "content", json_object_get(body, "content"), "edited_at",
                                           json_object_get(body, "edited_at"))
                               : json_pack("{s:s,s:s,s:s}", "type", "message_delete", "id",
                                           a->mutation_message, "channel_id", a->mutation_channel);
                if (!event || !history_event(h, event, h->context))
                    uncertain = true;
                json_decref(event);
            }
            /* Reconcile embeds and races through the active conversation only.
             * Recovery resets auxiliary state, so publish the receipt afterward. */
            if (ok || uncertain)
                history_recover(h);
            if (edit) {
                snprintf(a->edited_message, sizeof(a->edited_message), "%s", a->mutation_message);
                a->edit_ready = true;
                a->edit_success = ok;
            }
            history_internal_notice(
                h, HISTORY_NOTICE_MUTATION,
                ok            ? edit ? "Mensagem editada." : "Mensagem excluída."
                : code == 429 ? "Limite de requisições. Aguarde antes de tentar novamente."
                : code == 403 ? "Sem permissão para alterar esta mensagem."
                : code == 404 ? "Mensagem não disponível."
                : uncertain
                    ? "Alteração sem confirmação. Confira o histórico antes de tentar novamente."
                    : "Não foi possível alterar a mensagem.");
        }
        json_decref(a->payload);
        a->payload = NULL;
        return;
    }
    if (kind == ACTION_REACT) {
        bool ok = !uncertain && (a->remove ? code == 204 : code == 200 || code == 201);
        uint64_t created;
        if (ok && !a->remove &&
            (strcmp(s(body, "message_id"), a->mutation_message) ||
             strcmp(s(body, "user_id"), auth_user_id(h->auth)) || !history_reaction_valid(body) ||
             !history_reaction_equal(body, a->payload) ||
             !session_time_parse(s(body, "created_at"), &created))) {
            ok = false;
            uncertain = true;
        }
        if (current) {
            if (ok)
                own_reaction(h, a);
            history_internal_status(
                h, ok            ? "Reação confirmada."
                   : code == 429 ? "Limite de requisições. Aguarde antes de tentar novamente."
                   : code == 409 ? "Limite de 20 tipos de reação atingido."
                   : code == 403 ? "Sem permissão para reagir."
                   : uncertain
                       ? "Reação sem confirmação. Atualizando histórico antes de nova tentativa."
                       : "Não foi possível alterar a reação.");
        }
        json_decref(a->payload);
        a->payload = NULL;
        /* Refresh the displayed window, not a guessed increment. The WS event
         * has an absolute total but contains no actor/membership information. */
        if (current && (ok || uncertain))
            history_recover(h);
        return;
    }
    if (!current)
        return;
    if ((kind == ACTION_LOCATE || kind == ACTION_JUMP) &&
        (!*a->jump || strcmp(a->jump, a->requested_message)))
        return; /* A canceled read may finish; it must not replace the history. */
    bool ok = code == 200 && !uncertain;
    if (kind == ACTION_USERS) {
        if (strcmp(a->requested_message, a->reactors))
            return;
        a->users_failed = !ok || !reactor_page(h, body);
        ++a->revision;
        return;
    }
    json_t *page = json_object_get(body, "messages");
    if (ok)
        ok = !strcmp(s(body, "channel_id"), s(history_channel(h, h->selected), "id")) &&
             json_is_array(page) && json_array_size(page) <= 100 &&
             json_is_boolean(json_object_get(body, "has_more"));
    size_t i;
    json_t *m;
    if (ok)
        json_array_foreach(
            page, i, m) if (!history_internal_message_valid(m, s(body, "channel_id"))) ok = false;
    if (kind == ACTION_LOCATE && ok) {
        /* A newer neighbor is an exact descending cursor that includes the
         * target and enough newer context to center it. This avoids walking
         * every intervening page and preserves
         * ties at the same timestamp without fabricating UUIDs or timestamps. */
        size_t count = json_array_size(page);
        json_t *next = count ? json_array_get(page, MIN(count - 1, 49U)) : NULL;
        int cmp = next ? history_internal_time_compare(s(next, "created_at"), a->jump_time) : 0;
        if ((!next && json_is_true(json_object_get(body, "has_more"))) ||
            (next && (cmp < 0 || (!cmp && strcmp(s(next, "id"), a->jump) <= 0))))
            ok = false;
        else {
            snprintf(a->page_id, sizeof(a->page_id), "%s", s(next, "id"));
            snprintf(a->page_time, sizeof(a->page_time), "%s", s(next, "created_at"));
            a->jump_located = true;
        }
    } else if (kind == ACTION_JUMP && ok) {
        if (!history_internal_contains(page, a->jump) ||
            json_is_null(json_object_get(h->patches, a->jump))) {
            json_object_set_new(a->references, a->jump, json_false());
            a->jump[0] = 0;
            a->jump_missing = true;
            if (!a->jump_silent)
                history_internal_status(h, "Mensagem não disponível.");
        } else {
            json_t *old = h->messages;
            h->messages = NULL;
            h->older = h->newer = false;
            if (history_internal_messages_response(h, body)) {
                json_decref(old);
                h->newer_more = *s(json_array_get(page, 0), "id") != 0;
                snprintf(h->newer_id, sizeof(h->newer_id), "%s", s(json_array_get(page, 0), "id"));
                snprintf(h->newer_time, sizeof(h->newer_time), "%s",
                         s(json_array_get(page, 0), "created_at"));
                snprintf(a->ready, sizeof(a->ready), "%s", a->jump);
                a->jump[0] = 0;
            } else {
                json_decref(h->messages);
                h->messages = old;
                ok = false;
            }
        }
    } else if (kind == ACTION_SCAN && ok) {
        json_t *edge = json_array_get(page, json_array_size(page) - 1);
        if (edge && *a->page_id) {
            int cmp = history_internal_time_compare(s(edge, "created_at"), a->page_time);
            if (cmp > 0 || (!cmp && strcmp(s(edge, "id"), a->page_id) >= 0))
                ok = false;
        }
        if (ok) {
            json_array_foreach(page, i, m)
            {
                const char *id = s(m, "id");
                if (!json_is_null(json_object_get(a->references, id)))
                    continue;
                if (json_is_null(json_object_get(h->patches, id))) {
                    json_object_set_new(a->references, id, json_false());
                    continue;
                }
                json_t *patched = json_copy(m);
                json_t *patch = json_object_get(h->patches, id);
                if (patched && json_object_get(patch, "edited_at")) {
                    if (json_object_set(patched, "content", json_object_get(patch, "content")) ||
                        json_object_set(patched, "edited_at",
                                        json_object_get(patch, "edited_at"))) {
                        json_decref(patched);
                        patched = NULL;
                    }
                }
                json_t *copy = patched ? reference_copy(patched) : NULL;
                if (copy && h->r8)
                    inbox_observe_message(h->inbox, patched);
                json_decref(patched);
                if (!copy || json_object_set_new(copy, "_reply_page_id", json_string(a->page_id)) ||
                    json_object_set_new(copy, "_reply_page_time", json_string(a->page_time))) {
                    json_decref(copy);
                    ok = false;
                    break;
                }
                if (json_object_set_new(a->references, id, copy)) {
                    ok = false;
                    break;
                }
                size_t bytes = history_internal_encoded_size(json_object_get(a->references, id));
                if (bytes > HISTORY_BYTE_LIMIT - a->bytes) {
                    json_object_set_new(a->references, id, json_null());
                    a->failed = true;
                    break;
                }
                a->bytes += bytes;
                h->want_authors = true;
            }
            a->end = !edge || !json_is_true(json_object_get(body, "has_more"));
            if (edge) {
                snprintf(a->scan_id, sizeof(a->scan_id), "%s", s(edge, "id"));
                snprintf(a->scan_time, sizeof(a->scan_time), "%s", s(edge, "created_at"));
            }
        }
    }
    if (kind == ACTION_SCAN && a->restart_scan) {
        a->scan_id[0] = a->scan_time[0] = 0;
        a->end = a->restart_scan = false;
    }
    if (!ok) {
        a->failed = true;
        if (kind == ACTION_SCAN && *a->jump && !a->jump_silent)
            history_internal_status(h, "Falha ao localizar mensagem. Ctrl+R: tentar novamente.");
        else if (kind == ACTION_JUMP && !a->jump_silent)
            history_internal_status(
                h, "Falha ao carregar mensagem original. Ctrl+R: tentar novamente.");
    }
    ++a->revision;
}
void history_internal_actions_users(history *h, json_t *ids, json_t *seen)
{
    if (!h->actions)
        return;
    const char *key;
    json_t *m;
    json_object_foreach(h->actions->references, key, m)
    {
        (void)key;
        const char *id = s(m, "author_id");
        if (history_uuid(id) && !json_object_get(h->users, id) &&
            !json_object_get(h->queried, id) && !json_object_get(seen, id) &&
            json_array_size(ids) < 1000) {
            json_array_append_new(ids, json_string(id));
            json_object_set_new(seen, id, json_true());
        }
    }
    size_t i;
    json_array_foreach(h->actions->users, i, m)
    {
        const char *id = s(m, "user_id");
        if (history_uuid(id) && !json_object_get(h->users, id) &&
            !json_object_get(h->queried, id) && !json_object_get(seen, id) &&
            json_array_size(ids) < 1000) {
            json_array_append_new(ids, json_string(id));
            json_object_set_new(seen, id, json_true());
        }
    }
}
void history_internal_reference_event(history *h, const json_t *event)
{
    if (!h->actions)
        return;
    const char *id = s(event, "id"), *type = s(event, "type");
    json_t *m = json_object_get(h->actions->references, id);
    if (!json_is_object(m))
        return;
    if (!strcmp(type, "message_delete"))
        json_object_set_new(h->actions->references, id, json_false());
    else if (!strcmp(type, "message_edit")) {
        json_t *edited = json_copy(m);
        if (edited && !json_object_set(edited, "content", json_object_get(event, "content")) &&
            !json_object_set(edited, "edited_at", json_object_get(event, "edited_at"))) {
            json_t *copy = reference_copy(edited);
            if (copy) {
                json_object_set(copy, "_reply_page_id", json_object_get(m, "_reply_page_id"));
                json_object_set(copy, "_reply_page_time", json_object_get(m, "_reply_page_time"));
                size_t old = history_internal_encoded_size(m),
                       bytes = history_internal_encoded_size(copy);
                if (old <= h->actions->bytes &&
                    bytes <= HISTORY_BYTE_LIMIT - (h->actions->bytes - old) &&
                    !json_object_set(h->actions->references, id, copy))
                    h->actions->bytes = h->actions->bytes - old + bytes;
                json_decref(copy);
            }
        }
        json_decref(edited);
    }
    ++h->actions->revision;
}
bool history_internal_react_event(history *h, const json_t *event)
{
    const char *id = s(event, "message_id");
    json_t *count = json_object_get(event, "count");
    if (!history_uuid(id) || !history_reaction_valid(event) || !json_is_integer(count) ||
        json_integer_value(count) < 0)
        return false;
    if (!h->replaying && (h->pending == MESSAGES || h->pending == ACTIONS || h->recovering) &&
        !history_internal_remember(h, event))
        return false;
    json_t *m = json_array_get(h->messages, history_internal_find(h->messages, id));
    if (!m)
        return true;
    json_t *reactions = json_object_get(m, "reactions");
    json_t *next = json_is_array(reactions) ? json_deep_copy(reactions) : json_array();
    if (!next)
        return false;
    size_t i;
    json_t *item = NULL;
    json_array_foreach(next, i, item) if (history_reaction_equal(item, event)) break;
    if (json_integer_value(count) == 0) {
        if (i < json_array_size(next))
            json_array_remove(next, i);
    } else if (i < json_array_size(next))
        json_object_set(item, "count", count);
    else if (json_array_size(next) < 20) {
        json_t *entry = json_pack(
            "{s:O,s:O,s:O}", "emoji_id",
            json_object_get(event, "emoji_id") ? json_object_get(event, "emoji_id") : json_null(),
            "unicode",
            json_object_get(event, "unicode") ? json_object_get(event, "unicode") : json_null(),
            "count", count);
        if (!entry || json_array_append_new(next, entry)) {
            json_decref(next);
            return false;
        }
    }
    bool ok =
        !json_object_set_new(m, "reactions", next) && history_internal_measure_message(h, m, true);
    ++h->messages_revision;
    return ok;
}
