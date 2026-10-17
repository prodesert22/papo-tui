#include "model/conversation/discovery.h"
#include "model/conversation/private.h"

const char *history_author_username(const history *h, const char *id)
{
    const char *name = id ? history_internal_str(json_object_get(h->users, id), "username") : NULL;
    return name ? name : "";
}
const char *history_author(const history *h, const char *id)
{
    const char *name = history_mention_name((void *)h, id);
    return name ? name : "Autor indisponível";
}

unsigned history_member_color(const history *h, const char *id)
{
    return h && id && *id
               ? permissions_member_color(auth_permissions(h->auth), id,
                                          json_object_get(json_object_get(h->users, id), "roles"))
               : 0x1ffffff;
}

bool history_message_webhook(const json_t *message)
{
    return history_uuid(history_internal_str((json_t *)message, "webhook_id"));
}

const char *history_message_author(const history *h, const json_t *message)
{
    if (!history_message_webhook(message))
        return history_author(h, history_internal_str((json_t *)message, "author_id"));
    json_t *external = json_object_get(message, "external_author");
    const char *name = history_internal_str(external, "name");
    return name && *name && strlen(name) <= 512 &&
                   strlen(name) == json_string_length(json_object_get(external, "name"))
               ? name
               : "Webhook";
}

void history_users_active(history *h, bool active)
{
    h->users_active = active;
}

void history_users_retry(history *h)
{
    h->users_failed = false;
}

void history_users_refresh(history *h)
{
    if (h->pending == USERS) {
        auth_cancel_read(h->auth);
        h->pending = NONE;
    }
    h->users_active = true;
    h->users_complete = h->users_failed = false;
    h->users_id[0] = h->users_time[0] = 0;
    json_array_clear(h->directory);
    ++h->users_revision;
    const char *id;
    json_t *user;
    json_object_foreach(h->users, id, user)
    {
        (void)id;
        json_object_del(user, "_presence");
    }
}

size_t history_user_count(const history *h)
{
    return json_array_size(h->directory);
}

uint64_t history_users_revision(const history *h)
{
    return h->users_revision;
}

json_t *history_user(const history *h, size_t i)
{
    return json_object_get(h->users, json_string_value(json_array_get(h->directory, i)));
}

const char *history_users_notice(const history *h)
{
    if (h->users_failed)
        return "Falha ao listar usuários. Ctrl+R: tentar novamente.";
    if (h->users_partial)
        return "Lista parcial: limite local atingido.";
    return h->users_complete ? "Tab / Enter / clique: mencionar" : "Carregando usuários…";
}

const char *history_mention_name(void *context, const char *id)
{
    history *h = context;
    if (!h || !id)
        return NULL;
    json_t *user = json_object_get(h->users, id);
    const char *name = history_internal_str(user, "nickname");
    if (name && *name)
        return name;
    name = history_internal_str(user, "username");
    if (name && *name)
        return name;
    return !strcmp(id, auth_user_id(h->auth)) ? auth_username(h->auth) : NULL;
}

char *history_search_normalize(const char *text)
{
    char *fold = g_utf8_casefold(text ? text : "", -1);
    if (!fold)
        return NULL;
    GString *result = g_string_new(NULL);
    for (const char *p = fold; *p; p = g_utf8_next_char(p)) {
        gunichar c = g_utf8_get_char(p);
        if (g_unichar_isalnum(c))
            g_string_append_unichar(result, c);
    }
    g_free(fold);
    return g_string_free(result, false);
}

static bool user_resolved(const history *h, const char *id)
{
    if (!h || !id || !*id)
        return false;
    const char *name = history_internal_str(json_object_get(h->users, id), "username");
    return name && *name;
}

static json_t *user_roles(const json_t *source)
{
    json_t *roles = json_array();
    if (!roles || json_array_size(source) > 10000)
        return roles;
    size_t i;
    json_t *role;
    json_array_foreach(source, i, role)
    {
        const char *id = history_internal_str(role, "id");
        if (!history_uuid(id) || json_string_length(json_object_get(role, "id")) != 36)
            continue;
        json_t *color = json_object_get(role, "color");
        if (!json_is_string(color) || json_string_length(color) != 7)
            color = json_null();
        json_t *copy = json_pack("{s:s,s:O}", "id", id, "color", color);
        if (!copy || json_array_append_new(roles, copy)) {
            json_decref(roles);
            return NULL;
        }
    }
    return roles;
}

static bool change_role(json_t *roles, const char *id, bool add)
{
    size_t index = history_internal_find(roles, id);
    if (!add)
        return index == SIZE_MAX || !json_array_remove(roles, index);
    return index != SIZE_MAX || (json_array_size(roles) < 10000 &&
                                 !json_array_append_new(roles, json_pack("{s:s}", "id", id)));
}

bool history_internal_cache_user(history *h, json_t *u, bool presence)
{
    const char *id = history_internal_str(u, "id"), *name = history_internal_str(u, "username");
    if (!history_uuid(id) || !name || !history_internal_string_field(u, "username", 1024, false) ||
        !history_internal_string_field(u, "nickname", 1024, true) ||
        !history_internal_string_field(u, "typing", 256, true))
        return false;
    json_t *prior = json_object_get(h->users, id);
    bool live = presence || json_is_true(json_object_get(prior, "_presence"));
    json_t *metadata = presence ? u : live ? prior : u;
    const char *nickname = history_internal_str(metadata, "nickname"),
               *phrase = history_internal_str(metadata, "typing");
    size_t old = prior ? (size_t)json_integer_value(json_object_get(prior, "_cost")) : 0;
    char *fold_name = g_utf8_casefold(name, -1),
         *fold_nick = g_utf8_casefold(nickname ? nickname : "", -1);
    if (!fold_name || !fold_nick) {
        g_free(fold_name);
        g_free(fold_nick);
        return false;
    }
    char *normal_name = history_search_normalize(fold_name),
         *normal_nick = history_search_normalize(fold_nick);
    if (!normal_name || !normal_nick) {
        g_free(fold_name);
        g_free(fold_nick);
        g_free(normal_name);
        g_free(normal_nick);
        return false;
    }
    size_t cost = strlen(name) + (nickname ? strlen(nickname) : 0) + (phrase ? strlen(phrase) : 0) +
                  4096 + strlen(fold_name) + strlen(fold_nick) + strlen(normal_name) +
                  strlen(normal_nick);
    /* Presence is partial. REST started before a role event must not undo it. */
    json_t *source = json_object_get(u, "roles");
    bool partial = !json_is_array(source) || presence,
         stale = h->pending != NONE &&
                 (uint64_t)json_integer_value(json_object_get(prior, "_role_event")) >
                     h->request_role_events;
    if (partial)
        source = json_object_get(prior, "roles");
    json_t *roles = user_roles(source);
    json_t *changes = partial || stale ? json_object_get(prior, "_role_changes") : NULL;
    const char *role_id;
    json_t *add;
    if (!partial && stale)
        json_object_foreach(changes, role_id, add)
        {
            if (!change_role(roles, role_id, json_is_true(add))) {
                json_decref(roles);
                roles = NULL;
                break;
            }
        }
    if (!roles) {
        g_free(fold_name);
        g_free(fold_nick);
        g_free(normal_name);
        g_free(normal_nick);
        return false;
    }
    cost += (json_array_size(roles) + json_object_size(changes)) * 512;
    if ((!prior && json_object_size(h->users) >= 10000) ||
        cost > 14U * 1024U * 1024U - (h->user_bytes - old)) {
        h->users_partial = h->users_complete = true;
        g_free(fold_name);
        g_free(fold_nick);
        g_free(normal_name);
        g_free(normal_nick);
        json_decref(roles);
        return true;
    }
    json_t *copy = json_pack(
        "{s:s,s:s,s:O,s:O,s:b}", "id", id, "username", name, "nickname",
        json_object_get(metadata, "nickname") ? json_object_get(metadata, "nickname") : json_null(),
        "typing",
        json_object_get(metadata, "typing") ? json_object_get(metadata, "typing") : json_null(),
        "_presence", live);
    uint64_t now = 0;
    net_now_ms(&now);
    bool prepared = copy && !json_object_set(copy, "roles", roles) &&
                    (!changes || !json_object_set(copy, "_role_changes", changes)) &&
                    !json_object_set_new(
                        copy, "_role_event",
                        json_integer(json_integer_value(json_object_get(prior, "_role_event")))) &&
                    !json_object_set_new(copy, "_cost", json_integer((json_int_t)cost)) &&
                    !json_object_set_new(copy, "_used", json_integer((json_int_t)now)) &&
                    !json_object_set_new(copy, "_fold_name", json_string(fold_name)) &&
                    !json_object_set_new(copy, "_fold_nick", json_string(fold_nick)) &&
                    !json_object_set_new(copy, "_normal_name", json_string(normal_name)) &&
                    !json_object_set_new(copy, "_normal_nick", json_string(normal_nick));
    json_decref(roles);
    g_free(fold_name);
    g_free(fold_nick);
    g_free(normal_name);
    g_free(normal_nick);
    if (!prepared) {
        json_decref(copy);
        return false;
    }
    json_t *banned = json_object_get(u, "banned");
    if (!json_is_boolean(banned))
        banned = json_object_get(prior, "banned");
    if (copy)
        json_object_set_new(copy, "banned", json_is_true(banned) ? json_true() : json_false());
    if (!copy)
        return false;
    bool ok = json_object_set_new(h->users, id, copy) == 0;
    if (ok) {
        h->user_bytes = h->user_bytes - old + cost;
        ++h->users_revision;
    }
    return ok;
}

bool history_internal_role_event(history *h, const json_t *event)
{
    const char *id = history_internal_str(event, "user_id"),
               *role = history_internal_str(event, "role_id");
    json_t *user = json_object_get(h->users, id);
    if (!user) {
        /* A summary may already be in flight for an unresolved author. Keep
         * its live changes until that response can be reconciled. */
        json_t *placeholder = json_pack("{s:s,s:s}", "id", id, "username", "");
        bool ok = placeholder && history_internal_cache_user(h, placeholder, false);
        json_decref(placeholder);
        if (!ok)
            return false;
        user = json_object_get(h->users, id);
        if (!user)
            return true; /* The bounded directory is full. */
    }
    json_t *roles = user_roles(json_object_get(user, "roles"));
    if (!roles)
        return false;
    bool add = !strcmp(history_internal_str(event, "type"), "role_add");
    json_t *prior = json_object_get(user, "_role_changes"),
           *changes = prior ? json_deep_copy(prior) : json_object();
    bool ok = changes && (json_object_size(changes) < 10000 || json_object_get(changes, role)) &&
              !json_object_set_new(changes, role, add ? json_true() : json_false()) &&
              change_role(roles, role, add);
    size_t old = (json_array_size(json_object_get(user, "roles")) + json_object_size(prior)) * 512,
           next = (json_array_size(roles) + json_object_size(changes)) * 512;
    if (next > old && next - old > 14U * 1024U * 1024U - h->user_bytes)
        ok = false;
    size_t cost = (size_t)json_integer_value(json_object_get(user, "_cost"));
    json_t *copy = ok ? json_deep_copy(user) : NULL;
    if (ok)
        ok =
            copy && !json_object_set(copy, "roles", roles) &&
            !json_object_set(copy, "_role_changes", changes) &&
            !json_object_set_new(copy, "_cost", json_integer((json_int_t)(cost - old + next))) &&
            !json_object_set_new(copy, "_role_event", json_integer((json_int_t)++h->role_events)) &&
            !json_object_set(h->users, id, copy);
    if (ok) {
        h->user_bytes = h->user_bytes - old + next;
        ++h->users_revision;
    }
    json_decref(roles);
    json_decref(copy);
    json_decref(changes);
    return ok;
}

void history_internal_typing_remove(history *h, const char *id)
{
    for (size_t i = 0; i < h->typing_count; ++i)
        if (!strcmp(h->typing[i].id, id)) {
            --h->typing_count;
            memmove(&h->typing[i], &h->typing[i + 1], (h->typing_count - i) * sizeof(h->typing[0]));
            break;
        }
}

void history_typing_text(const history *h, char *text, size_t capacity)
{
    if (!text || !capacity)
        return;
    *text = '\0';
    if (!h || !h->typing_count)
        return;
    if (h->typing_count > 4) {
        snprintf(text, capacity, "Várias pessoas estão digitando…");
        return;
    }
    size_t used = 0;
    for (size_t i = 0; i < h->typing_count; ++i) {
        const char *name = history_mention_name((void *)h, h->typing[i].id);
        int wrote = snprintf(text + used, capacity - used, "%s%s", i ? ", " : "",
                             name ? name : "Usuário indisponível");
        if (wrote < 0 || (size_t)wrote >= capacity - used)
            return;
        used += (size_t)wrote;
    }
    const char *phrase =
        h->typing_count == 1
            ? history_internal_str(json_object_get(h->users, h->typing[0].id), "typing")
            : NULL;
    snprintf(text + used, capacity - used, " %s",
             phrase && *phrase      ? phrase
             : h->typing_count == 1 ? "está digitando…"
                                    : "estão digitando…");
}

void history_typing_activity(history *h)
{
    uint64_t now;
    json_t *channel = h ? history_channel(h, h->selected) : NULL;
    if (!h || history_permission(h, PERMISSION_SEND_MESSAGES) != PERMISSION_ALLOWED ||
        !auth_has_session(h->auth) || auth_ws_state(h->auth) != WS_OPEN ||
        h->context != auth_context_generation(h->auth) || !history_internal_str(channel, "type") ||
        (strcmp(history_internal_str(channel, "type"), "text") &&
         strcmp(history_internal_str(channel, "type"), "voice")) ||
        !net_now_ms(&now) || (h->typing_sent && now - h->typing_sent < TYPING_SEND_MS))
        return;
    json_t *event =
        json_pack("{s:s,s:s}", "type", "typing", "channel_id", history_internal_str(channel, "id"));
    if (event && auth_send_event(h->auth, event))
        h->typing_sent = now;
    json_decref(event);
}

bool history_internal_typing_event(history *h, const json_t *event)
{
    const char *channel = history_internal_str((json_t *)event, "channel_id"),
               *id = history_internal_str((json_t *)event, "user_id");
    if (!history_internal_string_field((json_t *)event, "channel_id", 36, false) ||
        !history_uuid(channel) ||
        !history_internal_string_field((json_t *)event, "user_id", 36, false) ||
        !history_uuid(id) || !json_is_boolean(json_object_get(event, "is_typing")))
        return false;
    json_t *active = history_channel(h, h->selected);
    if (auth_ws_state(h->auth) != WS_OPEN || !history_internal_str(active, "id") ||
        strcmp(channel, history_internal_str(active, "id")) ||
        (strcmp(history_internal_str(active, "type"), "text") &&
         strcmp(history_internal_str(active, "type"), "voice")) ||
        !strcmp(id, auth_user_id(h->auth)))
        return true;
    if (json_is_false(json_object_get(event, "is_typing"))) {
        history_internal_typing_remove(h, id);
        return true;
    }
    uint64_t now;
    if (!net_now_ms(&now))
        return true;
    size_t i;
    for (i = 0; i < h->typing_count; ++i)
        if (!strcmp(h->typing[i].id, id))
            break;
    if (i == TYPING_LIMIT)
        return true;
    if (i == h->typing_count) {
        snprintf(h->typing[i].id, sizeof(h->typing[i].id), "%s", id);
        ++h->typing_count;
    }
    h->typing[i].expires = now + TYPING_TIMEOUT_MS;
    json_t *user = json_object_get(h->users, id);
    if ((!user_resolved(h, id) || !json_object_get(user, "typing")) &&
        !json_object_get(h->queried, id))
        h->want_authors = true;
    return true;
}

bool history_internal_presence_event(history *h, const json_t *event)
{
    const char *id = history_internal_str((json_t *)event, "user_id"),
               *state = history_internal_str((json_t *)event, "status");
    if (!history_internal_string_field((json_t *)event, "user_id", 36, false) ||
        !history_uuid(id) || !history_internal_string_field((json_t *)event, "status", 16, false) ||
        (strcmp(state, "online") && strcmp(state, "offline") && strcmp(state, "away") &&
         strcmp(state, "busy")) ||
        !history_internal_string_field((json_t *)event, "nickname", 1024, true) ||
        !history_internal_string_field((json_t *)event, "typing", 256, true))
        return false;
    if (!strcmp(state, "offline")) {
        history_internal_typing_remove(h, id);
        return true;
    }
    json_t *prior = json_object_get(h->users, id);
    json_t *user =
        json_pack("{s:s,s:s,s:O,s:O,s:b}", "id", id, "username",
                  prior ? history_internal_str(prior, "username") : "", "nickname",
                  json_object_get(event, "nickname")            ? json_object_get(event, "nickname")
                  : prior && json_object_get(prior, "nickname") ? json_object_get(prior, "nickname")
                                                                : json_null(),
                  "typing",
                  json_object_get(event, "typing")            ? json_object_get(event, "typing")
                  : prior && json_object_get(prior, "typing") ? json_object_get(prior, "typing")
                                                              : json_null(),
                  "_presence", true);
    bool ok = user && history_internal_cache_user(h, user, true);
    json_decref(user);
    return ok;
}

bool history_internal_users_response(history *h, json_t *body)
{
    json_t *users = json_object_get(body, "users"), *more = json_object_get(body, "has_more");
    size_t count = json_array_size(users);
    if (!json_is_array(users) || count > 100 || !json_is_boolean(more))
        return false;
    for (size_t i = 0; i < count; ++i) {
        json_t *u = json_array_get(users, i);
        uint64_t stamp;
        if (!history_internal_str(u, "created_at") ||
            strlen(history_internal_str(u, "created_at")) >= sizeof(h->users_time) ||
            !session_time_parse(history_internal_str(u, "created_at"), &stamp) ||
            !history_internal_cache_user(h, u, false))
            return false;
        const char *id = history_internal_str(u, "id");
        bool found = false;
        for (size_t j = 0; j < json_array_size(h->directory); ++j)
            if (!strcmp(id, json_string_value(json_array_get(h->directory, j)))) {
                found = true;
                break;
            }
        if (!found && !h->users_partial && json_array_append_new(h->directory, json_string(id)))
            return false;
    }
    if (count) {
        json_t *last = json_array_get(users, count - 1);
        if (!strcmp(h->users_id, history_internal_str(last, "id")))
            return false;
        snprintf(h->users_time, sizeof(h->users_time), "%s",
                 history_internal_str(last, "created_at"));
        snprintf(h->users_id, sizeof(h->users_id), "%s", history_internal_str(last, "id"));
    }
    h->users_complete |= !count || json_is_false(more);
    return true;
}

bool history_internal_request_users(history *h)
{
    char path[512] = "/users?order=asc";
    if (*h->users_id) {
        char *time = curl_easy_escape(NULL, h->users_time, 0);
        if (!time)
            return false;
        snprintf(path, sizeof(path), "/users?order=asc&since=%s&last_id=%s", time, h->users_id);
        curl_free(time);
    }
    return auth_get_json(h->auth, path);
}

bool history_internal_authors_response(history *h, json_t *body)
{
    if (!json_is_array(body) || json_array_size(body) > 1000)
        return false;
    size_t i;
    json_t *u;
    json_array_foreach(body, i, u) if (!history_internal_cache_user(h, u, false)) return false;
    return true;
}

static void collect_mention(void *context, const mention_range *range)
{
    mention_batch *batch = context;
    history *h = batch->model;
    const char *id = range->id;
    if (!batch->ok || user_resolved(h, id) || json_object_get(h->queried, id) ||
        json_object_get(batch->seen, id) || json_array_size(batch->ids) >= 1000 ||
        json_object_size(h->queried) + json_array_size(batch->ids) >= 10000)
        return;
    batch->ok = !json_array_append_new(batch->ids, json_string(id)) &&
                !json_object_set_new(batch->seen, id, json_true());
}

bool history_internal_request_authors(history *h)
{
    json_t *ids = json_array(), *seen = json_object();
    if (!ids || !seen) {
        json_decref(ids);
        json_decref(seen);
        return false;
    }
    size_t i;
    json_t *m;
    bool ok = true;
    for (size_t j = 0; j < h->typing_count; ++j) {
        const char *id = h->typing[j].id;
        json_t *user = json_object_get(h->users, id);
        if ((!user_resolved(h, id) || !json_object_get(user, "typing")) &&
            !json_object_get(h->queried, id) &&
            json_object_size(h->queried) + json_array_size(ids) < 10000) {
            ok &= !json_array_append_new(ids, json_string(id)) &&
                  !json_object_set_new(seen, id, json_true());
        }
    }
    history_internal_actions_users(h, ids, seen);
    json_array_foreach(h->messages, i, m)
    {
        const char *id = history_internal_str(m, "author_id");
        /* Webhook messages carry author_id null (REST) or "" (WS); they are
         * display-only and never resolve via user_summary_batch. */
        if (id && *id && history_uuid(id) && !user_resolved(h, id) &&
            !json_object_get(h->queried, id) && !json_object_get(seen, id) &&
            json_array_size(ids) < 1000 &&
            json_object_size(h->queried) + json_array_size(ids) < 10000) {
            ok &= !json_array_append_new(ids, json_string(id)) &&
                  !json_object_set_new(seen, id, json_true());
        }
        mention_batch batch = {h, ids, seen, ok};
        mention_visit_message(m, collect_mention, &batch);
        ok = batch.ok;
    }
    for (int source = 0; source < 2; ++source)
        json_array_foreach(source ? history_search_results(h) : history_pins(h), i, m)
        {
            const char *id = history_internal_str(m, "author_id");
            if (history_uuid(id) && !user_resolved(h, id) && !json_object_get(h->queried, id) &&
                !json_object_get(seen, id) && json_array_size(ids) < 1000 &&
                json_object_size(h->queried) + json_array_size(ids) < 10000) {
                ok &= !json_array_append_new(ids, json_string(id)) &&
                      !json_object_set_new(seen, id, json_true());
            }
            mention_batch batch = {h, ids, seen, ok};
            mention_visit_message(m, collect_mention, &batch);
            ok = batch.ok;
        }
    if (h->r8) {
        size_t total = inbox_notification_count(h->inbox) + inbox_event_count(h->inbox);
        for (size_t j = 0; j < total && json_array_size(ids) < 1000; ++j) {
            bool event = j >= inbox_notification_count(h->inbox);
            json_t *n =
                event ? inbox_notification_event(h->inbox, j - inbox_notification_count(h->inbox))
                      : inbox_notification(h->inbox, j);
            mention_batch batch = {h, ids, seen, ok};
            mention_range range;
            size_t from = 0;
            while (mention_next(history_internal_str(n, "message_content"), from, &range)) {
                collect_mention(&batch, &range);
                from = range.end;
            }
            ok = batch.ok;
            const char *id = history_internal_str(n, event ? "user_id" : "author_id");
            if (!history_uuid(id) || user_resolved(h, id) || json_object_get(h->queried, id) ||
                json_object_get(seen, id))
                continue;
            ok &= !json_array_append_new(ids, json_string(id)) &&
                  !json_object_set_new(seen, id, json_true());
        }
    }
    if (ok && json_array_size(ids))
        ok = auth_user_summaries(h->auth, ids);
    if (ok) {
        json_decref(h->summary_ids);
        h->summary_ids = json_incref(ids);
    }
    json_decref(ids);
    json_decref(seen);
    return ok;
}

static void protect_mention(void *context, const mention_range *range)
{
    protected_users *protected = context;
    if (json_object_set_new(protected->ids, range->id, json_true()))
        protected->ok = false;
}

bool history_maintain(history *h, uint64_t now)
{
    if (!h || now < h->maintenance || now - h->maintenance < 1000)
        return false;
    h->maintenance = now;
    if (h->users_active)
        return false; /* A popup borrows directory indices. */
    json_t *protected = json_object();
    if (!protected)
        return false;
    protected_users pins = {protected, true};
    for (size_t i = 0; i < history_message_count(h); ++i) {
        json_t *m = history_message(h, i);
        const char *author = history_internal_str(m, "author_id");
        if (author && *author && json_object_set_new(protected, author, json_true()))
            pins.ok = false;
        mention_visit_message(m, protect_mention, &pins);
    }
    for (int source = 0; source < 2; ++source) {
        size_t i;
        json_t *message;
        json_array_foreach(source ? history_search_results(h) : history_pins(h), i, message)
        {
            const char *author = history_internal_str(message, "author_id");
            if (history_uuid(author) && json_object_set_new(protected, author, json_true()))
                pins.ok = false;
            mention_visit_message(message, protect_mention, &pins);
        }
    }
    for (size_t i = 0; i < h->typing_count; ++i)
        if (json_object_set_new(protected, h->typing[i].id, json_true()))
            pins.ok = false;
    if (h->r8) {
        size_t persisted = inbox_notification_count(h->inbox);
        for (size_t i = 0; i < persisted + inbox_event_count(h->inbox); ++i) {
            json_t *n = i < persisted ? inbox_notification(h->inbox, i)
                                      : inbox_notification_event(h->inbox, i - persisted);
            mention_range range;
            size_t from = 0;
            while (mention_next(history_internal_str(n, "message_content"), from, &range)) {
                protect_mention(&pins, &range);
                from = range.end;
            }
        }
    }
    if (!pins.ok) {
        json_decref(protected);
        return false;
    }
    bool changed = false;
    const char *id;
    json_t *u;
    void *iter = json_object_iter(h->users);
    while (iter) {
        id = json_object_iter_key(iter);
        u = json_object_iter_value(iter);
        void *next = json_object_iter_next(h->users, iter);
        uint64_t used = (uint64_t)json_integer_value(json_object_get(u, "_used"));
        if (json_object_get(protected, id))
            json_object_set_new(u, "_used", json_integer((json_int_t)now));
        else if (now >= used && now - used >= 300000) {
            h->user_bytes -= (size_t)json_integer_value(json_object_get(u, "_cost"));
            json_object_del(h->queried, id);
            json_object_del(h->users, id);
            changed = true;
        }
        iter = next;
    }
    if (changed) {
        for (size_t i = json_array_size(h->directory); i > 0; --i)
            if (!json_object_get(h->users, json_string_value(json_array_get(h->directory, i - 1))))
                json_array_remove(h->directory, i - 1);
        h->users_complete = h->users_partial = false;
        h->users_id[0] = h->users_time[0] = 0;
        ++h->users_revision;
    }
    iter = json_object_iter(h->queried);
    while (iter) {
        id = json_object_iter_key(iter);
        uint64_t used = (uint64_t)json_integer_value(json_object_iter_value(iter));
        void *next = json_object_iter_next(h->queried, iter);
        if (!json_object_get(protected, id) && now >= used && now - used >= 300000)
            json_object_del(h->queried, id);
        iter = next;
    }
    json_decref(protected);
    return changed;
}
