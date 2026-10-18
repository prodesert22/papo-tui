#include "model/conversation/members.h"
#include "model/conversation/private.h"

enum member_request { MEMBER_IDLE, MEMBER_SUMMARY, MEMBER_BLOCKS, MEMBER_PROFILE, MEMBER_MUTATE };
struct history_members {
    char target[37], notice[256];
    char query[8193], selected[37];
    char reset_url[4097], expires[65];
    json_t *profile, *blocks;
    enum member_request request, wanted;
    member_action action;
    uint64_t avatar_version;
    bool ready, blocked, banned, visible, summary_refresh;
};
static const char *s(const json_t *o, const char *key)
{
    const char *v = json_string_value(json_object_get(o, key));
    return v ? v : "";
}
static struct history_members *members(history *h)
{
    if (!h->members)
        h->members = calloc(1, sizeof(*h->members));
    return h->members;
}
bool history_internal_members_mutating(history *h)
{
    return h->members &&
           (h->members->request == MEMBER_MUTATE || h->members->wanted == MEMBER_MUTATE);
}
void history_internal_members_destroy(history *h)
{
    if (!h->members)
        return;
    json_decref(h->members->profile);
    json_decref(h->members->blocks);
    memset(h->members, 0, sizeof(*h->members));
    free(h->members);
    h->members = NULL;
}
void history_member_close(history *h)
{
    if (!h || !h->members)
        return;
    struct history_members *m = h->members;
    m->visible = false;
    memset(m->reset_url, 0, sizeof(m->reset_url));
    m->expires[0] = 0;
    if (history_internal_members_mutating(h))
        return; /* Keep the receipt owner even after closing the panel. */
    if (h->pending == MEMBERS) {
        auth_cancel_read(h->auth);
        h->pending = NONE;
    }
    m->wanted = m->request = MEMBER_IDLE;
    m->ready = false;
    json_decref(m->profile);
    m->profile = NULL;
}
void history_internal_members_recover(history *h)
{
    if (!h->members || history_internal_members_mutating(h))
        return;
    h->members->wanted = h->members->request = MEMBER_IDLE;
    h->members->ready = false;
    history_member_refresh(h);
    snprintf(h->members->notice, sizeof(h->members->notice),
             "Conexão alterada. Ctrl+R: atualizar membro.");
}
static bool select_member(history *h, const char *id, enum member_request request)
{
    if (!h || !history_uuid(id) || history_internal_members_mutating(h))
        return false;
    json_t *user = json_object_get(h->users, id);
    bool known = request == MEMBER_SUMMARY && (!h->members || !h->members->summary_refresh) &&
                 json_is_boolean(json_object_get(user, "banned"));
    bool preparing = known && h->members &&
                     (h->members->request == MEMBER_BLOCKS || h->members->wanted == MEMBER_BLOCKS);
    if (!preparing)
        history_member_close(h);
    struct history_members *m = members(h);
    if (!m)
        return false;
    snprintf(m->target, sizeof(m->target), "%s", id);
    if (!preparing)
        m->wanted = request;
    m->visible = true;
    snprintf(m->notice, sizeof(m->notice), request == MEMBER_PROFILE ? "Carregando perfil…" : "");
    if (known) {
        m->banned = json_is_true(json_object_get(user, "banned"));
        m->blocked = json_object_get(m->blocks, id) != NULL;
        m->ready = m->blocks != NULL;
        if (!preparing)
            m->wanted = m->ready ? MEMBER_IDLE : MEMBER_BLOCKS;
    }
    return true;
}
void history_member_refresh(history *h)
{
    if (!h || !h->members || history_internal_members_mutating(h))
        return;
    json_decref(h->members->blocks);
    h->members->blocks = NULL;
    h->members->summary_refresh = true;
    h->members->ready = false;
}
void history_member_prepare(history *h)
{
    if (!h || history_internal_members_mutating(h))
        return;
    history_member_close(h);
    struct history_members *m = members(h);
    if (!m)
        return;
    json_decref(m->blocks);
    m->blocks = NULL;
    m->target[0] = 0;
    m->wanted = MEMBER_BLOCKS;
}
bool history_member_menu(history *h, const char *id)
{
    return select_member(h, id, MEMBER_SUMMARY);
}
bool history_member_profile_open(history *h, const char *id)
{
    return select_member(h, id, MEMBER_PROFILE);
}
json_t *history_member_profile(history *h)
{
    return h->members ? h->members->profile : NULL;
}
const char *history_member_target(history *h)
{
    return h->members ? h->members->target : "";
}
const char *history_member_notice(history *h)
{
    return h->members ? h->members->notice : "";
}
bool history_member_ready(history *h)
{
    return h->members && h->members->ready;
}
bool history_member_busy(history *h)
{
    return h->members && (h->members->wanted != MEMBER_IDLE || h->members->request != MEMBER_IDLE);
}
bool history_member_blocked(history *h)
{
    return h->members && h->members->blocked;
}
bool history_member_banned(history *h)
{
    return h->members && h->members->banned;
}
bool history_member_admin(history *h)
{
    const char *id = history_member_target(h);
    return *id && strcmp(id, auth_user_id(h->auth)) &&
           permissions_role(auth_permissions(h->auth), PERMISSION_MANAGE_SERVER) ==
               PERMISSION_ALLOWED;
}
bool history_member_can_ban(history *h)
{
    return history_member_admin(h) &&
           strcmp(history_member_target(h), permissions_owner(auth_permissions(h->auth)));
}
bool history_member_change(history *h, member_action action)
{
    struct history_members *m = h->members;
    if (!m || !m->ready || history_member_busy(h) || action < MEMBER_BLOCK ||
        action > MEMBER_RESET || !strcmp(m->target, auth_user_id(h->auth)) ||
        (action >= MEMBER_BAN && !history_member_admin(h)) ||
        ((action == MEMBER_BAN || action == MEMBER_UNBAN) && !history_member_can_ban(h)))
        return false;
    m->action = action;
    m->wanted = MEMBER_MUTATE;
    snprintf(m->notice, sizeof(m->notice), "Enviando ação…");
    return true;
}
const char *history_member_reset_url(history *h)
{
    return h->members ? h->members->reset_url : "";
}
const char *history_member_reset_expires(history *h)
{
    return h->members ? h->members->expires : "";
}
bool history_internal_members_dispatch(history *h)
{
    struct history_members *m = h->members;
    if (!m || m->wanted == MEMBER_IDLE || m->request != MEMBER_IDLE)
        return false;
    /* Let the first directory page paint before preparing background actions. */
    if (!m->visible && m->wanted == MEMBER_BLOCKS && !json_array_size(h->directory) &&
        h->members_active && !h->users_complete && !h->users_failed)
        return false;
    enum member_request wanted = m->wanted;
    char path[96];
    bool started = false;
    if (wanted == MEMBER_SUMMARY) {
        json_t *ids = json_pack("[s]", m->target);
        started = ids && auth_user_summaries(h->auth, ids);
        json_decref(ids);
    } else if (wanted == MEMBER_BLOCKS)
        started = auth_get_json(h->auth, "/users/blocks");
    else if (wanted == MEMBER_PROFILE) {
        const author_avatar *avatar = chat_assets_avatar(h->assets, m->target);
        m->avatar_version = avatar ? avatar->version : 0;
        snprintf(path, sizeof(path), "/users/%s/profile", m->target);
        started = auth_get_json(h->auth, path);
    } else if (wanted == MEMBER_MUTATE) {
        if ((m->action >= MEMBER_BAN && !history_member_admin(h)) ||
            ((m->action == MEMBER_BAN || m->action == MEMBER_UNBAN) &&
             !history_member_can_ban(h))) {
            m->wanted = MEMBER_IDLE;
            snprintf(m->notice, sizeof(m->notice), "Permissão removida; ação cancelada.");
            return true;
        }
        started = auth_member_action(h->auth, m->target, (int)m->action);
    }
    if (!started)
        return false;
    m->request = wanted;
    m->wanted = MEMBER_IDLE;
    h->pending = MEMBERS;
    h->request_context = h->context;
    h->request_role_events = h->role_events;
    return true;
}
void history_internal_members_response(history *h, json_t *body, long code, bool uncertain)
{
    struct history_members *m = h->members;
    if (!m)
        return;
    enum member_request request = m->request;
    m->request = MEMBER_IDLE;
    bool ok = code >= 200 && code < 300;
    if (request == MEMBER_SUMMARY && ok) {
        json_t *u = json_array_get(body, 0);
        ok = json_array_size(body) == 1 && !strcmp(s(u, "id"), m->target) &&
             json_is_boolean(json_object_get(u, "banned")) &&
             history_internal_cache_user(h, u, false);
        if (ok) {
            m->banned = json_is_true(json_object_get(u, "banned"));
            m->summary_refresh = false;
            m->blocked = json_object_get(m->blocks, m->target) != NULL;
            m->ready = m->blocks != NULL;
            m->wanted = m->ready ? MEMBER_IDLE : MEMBER_BLOCKS;
        }
    } else if (request == MEMBER_BLOCKS && ok) {
        json_t *users = json_object_get(body, "users");
        ok = json_is_array(users) && json_array_size(users) <= 10000;
        json_t *blocks = json_object();
        ok = ok && blocks;
        m->blocked = false;
        size_t i;
        json_t *u;
        json_array_foreach(users, i, u)
        {
            if (!history_uuid(s(u, "id")))
                ok = false;
            else if (json_object_set(blocks, s(u, "id"), json_true()))
                ok = false;
            if (!strcmp(s(u, "id"), m->target))
                m->blocked = true;
        }
        json_decref(m->blocks);
        m->blocks = ok ? blocks : NULL;
        if (!ok)
            json_decref(blocks);
        m->ready = ok && *m->target;
    } else if (request == MEMBER_PROFILE && ok) {
        ok = json_is_object(body) && !strcmp(s(body, "id"), m->target) &&
             history_internal_string_field(body, "username", 1024, false) &&
             history_internal_string_field(body, "nickname", 1024, true) &&
             history_internal_string_field(body, "description", 4096, true) &&
             history_internal_string_field(body, "status_message", 2048, true) &&
             history_internal_string_field(body, "banner_media", 64, true) &&
             (!json_object_get(body, "roles") || json_is_array(json_object_get(body, "roles"))) &&
             json_array_size(json_object_get(body, "roles")) <= 10000;
        if (ok) {
            history_internal_cache_user(h, body, false);
            chat_assets_profile_observe(h->assets, body, m->avatar_version);
            m->profile = json_deep_copy(body);
            ok = m->profile != NULL;
            /* Avatar bytes live in the shared asset cache, not in the modal. */
            if (m->profile)
                json_object_del(m->profile, "avatar_blob");
        }
    } else if (request == MEMBER_MUTATE && ok) {
        if (m->action == MEMBER_RESET) {
            const char *url = s(body, "reset_url"), *expires = s(body, "expires_at");
            uint64_t stamp;
            ok = strlen(url) > 0 && strlen(url) <= 2048 && strlen(expires) < sizeof(m->expires) &&
                 session_time_parse(expires, &stamp) &&
                 (g_str_has_prefix(url, "https://") || g_str_has_prefix(url, "http://"));
            for (const unsigned char *p = (const unsigned char *)url; ok && *p; ++p)
                if (*p <= 32 || *p == 127)
                    ok = false;
            if (ok && m->visible) {
                snprintf(m->reset_url, sizeof(m->reset_url), "%s", url);
                snprintf(m->expires, sizeof(m->expires), "%s", expires);
            }
        } else if (m->action == MEMBER_BLOCK || m->action == MEMBER_UNBLOCK) {
            m->blocked = m->action == MEMBER_BLOCK;
            if (m->blocked) {
                if (json_object_set(m->blocks, m->target, json_true())) {
                    json_decref(m->blocks);
                    m->blocks = NULL;
                }
            } else
                json_object_del(m->blocks, m->target);
            inbox_refresh(h->inbox);
        } else {
            m->banned = m->action == MEMBER_BAN;
            json_t *user = json_object_get(h->users, m->target);
            if (user) {
                json_object_set_new(user, "banned", json_boolean(m->banned));
                ++h->users_revision;
            }
        }
    }
    if (!ok) {
        m->wanted = MEMBER_IDLE;
        if (request == MEMBER_MUTATE) {
            m->ready = false;
            history_member_refresh(h);
            snprintf(m->notice, sizeof(m->notice),
                     (uncertain || (code >= 200 && code < 300))
                         ? "Resultado incerto. Ctrl+R: consultar estado antes de repetir."
                         : "Ação recusada ou indisponível. Ctrl+R: atualizar estado.");
        } else
            snprintf(m->notice, sizeof(m->notice),
                     code == 404 ? "Membro não encontrado. Esc: voltar."
                                 : "Falha ao consultar membro. Ctrl+R: tentar novamente.");
    } else if (m->wanted == MEMBER_IDLE)
        snprintf(m->notice, sizeof(m->notice), request == MEMBER_MUTATE ? "Ação concluída." : "");
}

void history_member_view_save(history *h, const char *query, const char *selected)
{
    struct history_members *m = members(h);
    if (!m || !query || !selected || strlen(query) >= sizeof(m->query))
        return;
    snprintf(m->query, sizeof(m->query), "%s", query);
    snprintf(m->selected, sizeof(m->selected), "%s", history_uuid(selected) ? selected : "");
}
const char *history_member_view_query(history *h)
{
    return h->members ? h->members->query : "";
}
const char *history_member_view_selected(history *h)
{
    return h->members ? h->members->selected : "";
}
