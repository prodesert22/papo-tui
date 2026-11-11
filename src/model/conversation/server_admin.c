#include "model/conversation/server_admin.h"
#include "model/conversation/private.h"
#include "model/text/translation.h"

/* What a confirmed mutation changed, for its notice and the data to reload. */
enum admin_kind {
    OP_SERVER,
    OP_ICON,
    OP_ICON_REMOVE,
    OP_ROLE_CREATE,
    OP_ROLE_SAVE,
    OP_ROLE_DELETE,
    OP_CHANNEL_CREATE,
    OP_CHANNEL_SAVE,
    OP_CHANNEL_DELETE,
    OP_CHANNEL_MOVE,
    OP_CHANNEL_ROLE,
    OP_CHANNEL_ROLE_REMOVE,
    OP_EMOJI_CREATE,
    OP_EMOJI_DELETE,
    OP_WEBHOOK_CREATE,
    OP_WEBHOOK_NICKNAME,
    OP_WEBHOOK_AVATAR,
    OP_WEBHOOK_AVATAR_REMOVE,
    OP_WEBHOOK_REVOKE,
    OP_MEMBER_ROLE_ADD,
    OP_MEMBER_ROLE_REMOVE,
    OP_MEMBER_BAN,
    OP_MEMBER_UNBAN,
    OP_MEMBER_RESET
};
struct admin_op {
    enum admin_kind kind;
    char method[8];
    char *path;   /* Owned. */
    json_t *body; /* Owned; NULL for DELETE and bodyless member actions. */
    char target[37], extra[37];
};
/* A page-by-page list read with the contract's since + last_id cursor. */
struct admin_page {
    json_t *items;
    bool want, loading, more, failed;
    char since[65], last[37];
};
enum read_kind { READ_NONE, READ_SERVER, READ_LIST };
struct history_server_admin {
    json_t *server;
    bool want_server;
    struct admin_page pages[ADMIN_LISTS];
    admin_audit_query audit;
    json_t *audit_actions; /* Set of action names seen since the panel opened. */
    json_t *audit_sorted;
    struct admin_op queued, sent;
    bool has_queued, sending;
    enum read_kind reading;
    admin_list reading_list;
    admin_state state;
    char notice[256];
    char reset_url[2049], reset_expires[65], reset_user[37];
    char webhook_token[129], webhook_for[37], webhook_created[37];
    bool token_lost;
    uint64_t revision;
};
#define LIST_LIMIT 2000U
static const char *const entity_types[] = {"user", "server",    "channel", "message", "attachment",
                                           "role", "user_role", "emoji",   "webhook"};

static void changed(struct history_server_admin *a)
{
    ++a->revision;
}
/* Zeroes a secret in a way the compiler may not drop before the memory goes. */
static void wipe(void *secret, size_t size)
{
    volatile unsigned char *bytes = secret;
    while (size--)
        *bytes++ = 0;
}
static struct history_server_admin *admin(history *h)
{
    if (!h->server_admin)
        h->server_admin = calloc(1, sizeof(*h->server_admin));
    return h->server_admin;
}
/* Remote text never brings terminal controls into the notice. */
static void set_notice(struct history_server_admin *a, const char *prefix, const char *remote)
{
    GString *text = g_string_new(prefix);
    if (remote && *remote && g_utf8_validate(remote, -1, NULL)) {
        g_string_append(text, ": ");
        for (const char *at = remote; *at && text->len < 200; at = g_utf8_next_char(at)) {
            gunichar c = g_utf8_get_char(at);
            if (g_unichar_iscntrl(c) || g_unichar_type(c) == G_UNICODE_FORMAT)
                g_string_append_c(text, ' ');
            else
                g_string_append_unichar(text, c);
        }
    }
    snprintf(a->notice, sizeof(a->notice), "%s", text->str);
    size_t length = strlen(a->notice);
    while (length && !g_utf8_validate(a->notice, (gssize)length, NULL))
        a->notice[--length] = 0;
    g_string_free(text, TRUE);
    changed(a);
}
static void op_clear(struct admin_op *op)
{
    g_free(op->path);
    json_decref(op->body);
    memset(op, 0, sizeof(*op));
}
static void page_clear(struct admin_page *page)
{
    json_decref(page->items);
    memset(page, 0, sizeof(*page));
}
void history_internal_server_admin_destroy(history *h)
{
    struct history_server_admin *a = h->server_admin;
    if (!a)
        return;
    op_clear(&a->queued);
    op_clear(&a->sent);
    for (int i = 0; i < ADMIN_LISTS; ++i)
        page_clear(&a->pages[i]);
    json_decref(a->server);
    json_decref(a->audit_actions);
    json_decref(a->audit_sorted);
    memset(a->reset_url, 0, sizeof(a->reset_url));
    wipe(a->webhook_token, sizeof(a->webhook_token));
    free(a);
    h->server_admin = NULL;
    h->admin_users = false;
}

static admin_visibility of(permission_decision decision)
{
    return decision == PERMISSION_ALLOWED  ? ADMIN_SHOWN
           : decision == PERMISSION_DENIED ? ADMIN_HIDDEN
                                           : ADMIN_PENDING;
}
admin_visibility server_admin_visibility(const permissions *p, admin_tab tab)
{
    switch (tab) {
    case ADMIN_TAB_OVERVIEW:
    case ADMIN_TAB_AUDIT:
        return of(permissions_role(p, PERMISSION_MANAGE_SERVER));
    case ADMIN_TAB_ROLES:
        return of(permissions_role(p, PERMISSION_MANAGE_ROLES));
    case ADMIN_TAB_CHANNELS:
        return of(permissions_role(p, PERMISSION_MANAGE_CHANNELS));
    case ADMIN_TAB_EMOJIS:
        /* The contract sets no permission for adding emojis. */
        return ADMIN_SHOWN;
    case ADMIN_TAB_WEBHOOKS:
        return of(permissions_role(p, PERMISSION_MANAGE_WEBHOOKS));
    case ADMIN_TAB_MEMBERS: {
        admin_visibility roles = of(permissions_role(p, PERMISSION_MANAGE_ROLES)),
                         server = of(permissions_role(p, PERMISSION_MANAGE_SERVER));
        return roles == ADMIN_SHOWN || server == ADMIN_SHOWN       ? ADMIN_SHOWN
               : roles == ADMIN_PENDING || server == ADMIN_PENDING ? ADMIN_PENDING
                                                                   : ADMIN_HIDDEN;
    }
    default:
        return ADMIN_HIDDEN;
    }
}
admin_visibility server_admin_tab(history *h, admin_tab tab)
{
    return server_admin_visibility(auth_permissions(h->auth), tab);
}
bool server_admin_allowed(history *h, role_permission permission)
{
    return permissions_role(auth_permissions(h->auth), permission) == PERMISSION_ALLOWED;
}

const json_t *server_admin_roles(history *h)
{
    return permissions_roles(auth_permissions(h->auth));
}
const json_t *server_admin_channels(history *h)
{
    return permissions_channels(auth_permissions(h->auth));
}
const char *server_admin_owner(history *h)
{
    return permissions_owner(auth_permissions(h->auth));
}
const char *server_admin_me(history *h)
{
    return auth_user_id(h->auth);
}
uint64_t server_admin_permissions_revision(history *h)
{
    return permissions_revision(auth_permissions(h->auth));
}
const char *server_admin_permissions_notice(history *h)
{
    return auth_permissions_notice(h->auth);
}

const char *server_admin_check_text(const char *text, size_t max, bool required)
{
    if (!text || !g_utf8_validate(text, -1, NULL))
        return _("Texto inválido.");
    bool blank = true;
    for (const char *at = text; *at && blank; at = g_utf8_next_char(at))
        blank = g_unichar_isspace(g_utf8_get_char(at));
    if (required && blank)
        return _("Preencha este campo.");
    if (max && (size_t)g_utf8_strlen(text, -1) > max)
        return max == ADMIN_SERVER_NAME_MAX        ? _("Nome com mais de 32 caracteres.")
               : max == ADMIN_TOPIC_MAX            ? _("Tópico com mais de 512 caracteres.")
               : max == ADMIN_WEBHOOK_NAME_MAX     ? _("Nome com mais de 80 caracteres.")
               : max == ADMIN_WEBHOOK_NICKNAME_MAX ? _("Apelido com mais de 100 caracteres.")
                                                   : _("Texto longo demais.");
    for (const char *at = text; *at; at = g_utf8_next_char(at))
        if (g_unichar_iscntrl(g_utf8_get_char(at)))
            return _("Use uma única linha, sem caracteres de controle.");
    return NULL;
}
bool server_admin_color_valid(const char *color)
{
    if (!color)
        return false;
    if (!*color)
        return true;
    if (strlen(color) != 7 || color[0] != '#')
        return false;
    for (int i = 1; i < 7; ++i)
        if (!g_ascii_isxdigit(color[i]))
            return false;
    return true;
}
json_t *server_admin_patch_body(const json_t *server, const char *name, int visibility,
                                const char *password, const char **problem)
{
    static const char *reason;
    reason = NULL;
    if (problem)
        *problem = NULL;
    if (!json_is_object(server)) {
        reason = _("O servidor ainda não foi carregado.");
        goto fail;
    }
    json_t *body = json_object();
    if (!body) {
        reason = _("Memória insuficiente.");
        goto fail;
    }
    const char *current = history_internal_str(server, "name");
    if (name && strcmp(name, current ? current : "")) {
        if ((reason = server_admin_check_text(name, ADMIN_SERVER_NAME_MAX, true)) ||
            json_object_set_new(body, "name", json_string(name))) {
            json_decref(body);
            reason = reason ? reason : _("Memória insuficiente.");
            goto fail;
        }
    }
    bool public_now = json_is_true(json_object_get(server, "public"));
    bool public_next = visibility < 0 ? public_now : visibility == 1;
    if (public_next != public_now &&
        json_object_set_new(body, "public", json_boolean(public_next))) {
        json_decref(body);
        reason = _("Memória insuficiente.");
        goto fail;
    }
    bool has_password = password && *password;
    if (public_now && !public_next && !has_password) {
        json_decref(body);
        reason = _("Informe a senha do servidor para torná-lo privado.");
        goto fail;
    }
    if (has_password && !public_next) {
        if (server_admin_check_text(password, 0, true) ||
            json_object_set_new(body, "password", json_string(password))) {
            json_decref(body);
            reason = _("Senha inválida.");
            goto fail;
        }
    }
    if (!json_object_size(body)) {
        json_decref(body);
        reason = _("Nenhuma alteração para salvar.");
        goto fail;
    }
    return body;
fail:
    if (problem)
        *problem = reason;
    return NULL;
}

/* Queues the single mutation; the path is copied and the body is taken. */
static bool enqueue(history *h, enum admin_kind kind, const char *method, const char *path,
                    json_t *body, const char *target, const char *extra)
{
    struct history_server_admin *a = admin(h);
    if (!a || !auth_has_session(h->auth) || !path) {
        json_decref(body);
        return false;
    }
    if (a->has_queued || a->sending) {
        json_decref(body);
        set_notice(a, _("Aguarde a alteração anterior terminar"), NULL);
        return false;
    }
    char *copy = g_strdup(path);
    if (!copy) {
        json_decref(body);
        return false;
    }
    op_clear(&a->queued);
    a->queued.kind = kind;
    snprintf(a->queued.method, sizeof(a->queued.method), "%s", method);
    a->queued.path = copy;
    a->queued.body = body;
    snprintf(a->queued.target, sizeof(a->queued.target), "%s", target ? target : "");
    snprintf(a->queued.extra, sizeof(a->queued.extra), "%s", extra ? extra : "");
    a->has_queued = true;
    a->state = ADMIN_SAVING;
    a->notice[0] = 0;
    changed(a);
    return true;
}

void server_admin_load_server(history *h)
{
    struct history_server_admin *a = admin(h);
    if (!a)
        return;
    a->want_server = true;
    changed(a);
}
const json_t *server_admin_server(history *h)
{
    return h && h->server_admin ? h->server_admin->server : NULL;
}
void server_admin_load(history *h, admin_list list, bool more)
{
    struct history_server_admin *a = admin(h);
    if (!a || list >= ADMIN_LISTS)
        return;
    struct admin_page *page = &a->pages[list];
    if (more && (!page->more || page->loading || page->want))
        return;
    if (!more)
        /* A page still in flight is dropped when it arrives. */
        page_clear(page);
    page->want = true;
    page->failed = false;
    changed(a);
}
const json_t *server_admin_items(history *h, admin_list list)
{
    return h && h->server_admin && list < ADMIN_LISTS ? h->server_admin->pages[list].items : NULL;
}
bool server_admin_more(history *h, admin_list list)
{
    return h && h->server_admin && list < ADMIN_LISTS && h->server_admin->pages[list].more;
}
bool server_admin_loading(history *h, admin_list list)
{
    return h && h->server_admin && list < ADMIN_LISTS &&
           (h->server_admin->pages[list].want || h->server_admin->pages[list].loading);
}
bool server_admin_failed(history *h, admin_list list)
{
    return h && h->server_admin && list < ADMIN_LISTS && h->server_admin->pages[list].failed;
}
static bool entity_known(const char *entity)
{
    for (size_t i = 0; i < G_N_ELEMENTS(entity_types); ++i)
        if (!strcmp(entity, entity_types[i]))
            return true;
    return false;
}
bool server_admin_audit_valid(const admin_audit_query *q)
{
    uint64_t stamp;
    if (!q || (*q->entity && !entity_known(q->entity)) || (*q->actor && !history_uuid(q->actor)) ||
        (*q->until && !session_time_parse(q->until, &stamp)) ||
        !g_utf8_validate(q->action, -1, NULL) || strlen(q->action) >= sizeof(q->action))
        return false;
    for (const char *at = q->action; *at; at = g_utf8_next_char(at)) {
        gunichar c = g_utf8_get_char(at);
        if (g_unichar_iscntrl(c) || g_unichar_isspace(c))
            return false;
    }
    return !*q->until || !q->from || stamp >= q->from;
}
bool server_admin_audit_query(history *h, const admin_audit_query *query)
{
    struct history_server_admin *a = admin(h);
    if (!a || !server_admin_audit_valid(query))
        return false;
    a->audit = *query;
    /* An answer for the previous filters is discarded when it arrives. */
    page_clear(&a->pages[ADMIN_LIST_AUDIT]);
    a->pages[ADMIN_LIST_AUDIT].want = true;
    changed(a);
    return true;
}
const admin_audit_query *server_admin_audit_current(history *h)
{
    struct history_server_admin *a = admin(h);
    static const admin_audit_query none;
    return a ? &a->audit : &none;
}
static int by_name(const void *x, const void *y)
{
    return strcmp(json_string_value(*(json_t *const *)x), json_string_value(*(json_t *const *)y));
}
const json_t *server_admin_audit_actions(history *h)
{
    struct history_server_admin *a = h ? h->server_admin : NULL;
    if (!a || !a->audit_actions)
        return NULL;
    if (!a->audit_sorted) {
        size_t count = json_object_size(a->audit_actions), n = 0;
        json_t **names = calloc(count ? count : 1, sizeof(*names));
        const char *name;
        json_t *value;
        json_object_foreach(a->audit_actions, name, value)
        {
            (void)value;
            if (names)
                names[n++] = json_string(name);
        }
        if (names)
            qsort(names, n, sizeof(*names), by_name);
        a->audit_sorted = json_array();
        for (size_t i = 0; names && i < n; ++i)
            if (!a->audit_sorted || !names[i] || json_array_append_new(a->audit_sorted, names[i]))
                json_decref(names[i]);
        free(names);
    }
    return a->audit_sorted;
}
void server_admin_members_active(history *h, bool active)
{
    if (h->admin_users != active && active) {
        h->users_failed = false;
        ++h->users_revision;
    }
    h->admin_users = active;
}
void server_admin_close(history *h)
{
    struct history_server_admin *a = h->server_admin;
    h->admin_users = false;
    if (!a)
        return;
    /* An answer in flight is dropped on arrival. */
    for (int i = 0; i < ADMIN_LISTS; ++i)
        page_clear(&a->pages[i]);
    a->want_server = false;
    memset(&a->audit, 0, sizeof(a->audit));
    json_decref(a->audit_actions);
    json_decref(a->audit_sorted);
    a->audit_actions = a->audit_sorted = NULL;
    memset(a->reset_url, 0, sizeof(a->reset_url));
    a->reset_expires[0] = a->reset_user[0] = 0;
    wipe(a->webhook_token, sizeof(a->webhook_token));
    a->webhook_for[0] = a->webhook_created[0] = 0;
    a->token_lost = false;
    if (!a->sending && !a->has_queued) {
        a->notice[0] = 0;
        a->state = ADMIN_IDLE;
    }
    changed(a);
}

bool server_admin_patch(history *h, json_t *body)
{
    if (!json_is_object(body) || !json_object_size(body)) {
        json_decref(body);
        return false;
    }
    return enqueue(h, OP_SERVER, "PATCH", "/server", body, NULL, NULL);
}
static bool image_format(const char *format)
{
    return format && (!strcmp(format, "GIF") || !strcmp(format, "JPEG") || !strcmp(format, "JPG") ||
                      !strcmp(format, "PNG") || !strcmp(format, "WEBP"));
}
bool server_admin_set_icon(history *h, char *base64, const char *format)
{
    if (!base64 || !*base64 || !image_format(format)) {
        g_free(base64);
        return false;
    }
    json_t *body = json_pack("{s:s,s:s}", "icon_blob", base64, "icon_format", format);
    g_free(base64);
    return body && enqueue(h, OP_ICON, "PATCH", "/server", body, NULL, NULL);
}
bool server_admin_remove_icon(history *h)
{
    json_t *body = json_pack("{s:s,s:s}", "icon_blob", "", "icon_format", "");
    return body && enqueue(h, OP_ICON_REMOVE, "PATCH", "/server", body, NULL, NULL);
}
static const char *const role_keys[PERMISSION_ROLE_COUNT] = {
    "manage_server", "manage_channels",  "manage_roles",   "ban_members",
    "pin_message",   "everyone_message", "send_attachment", "manage_webhooks"};
static const char *const channel_keys[PERMISSION_CHANNEL_COUNT] = {
    "read_channel", "send_messages", "delete_messages", "connect_voice"};
static json_t *flags_object(const char *const *keys, const bool *flags, size_t count)
{
    json_t *object = json_object();
    for (size_t i = 0; object && i < count; ++i)
        if (json_object_set_new(object, keys[i], json_boolean(flags[i]))) {
            json_decref(object);
            return NULL;
        }
    return object;
}
bool server_admin_role_save(history *h, const char *id, const char *name, const char *color,
                            const bool flags[PERMISSION_ROLE_COUNT])
{
    if (server_admin_check_text(name, 0, true) || !server_admin_color_valid(color) ||
        (id && !history_uuid(id)))
        return false;
    json_t *permissions = flags_object(role_keys, flags, PERMISSION_ROLE_COUNT);
    json_t *body =
        permissions ? json_pack("{s:s,s:o}", "name", name, "permissions", permissions) : NULL;
    /* The contract has no way to clear a color: an empty field sends none. */
    if (body && *color && json_object_set_new(body, "color", json_string(color))) {
        json_decref(body);
        body = NULL;
    }
    if (!body)
        return false;
    char path[64];
    snprintf(path, sizeof(path), id ? "/roles/%s" : "/roles", id ? id : "");
    return enqueue(h, id ? OP_ROLE_SAVE : OP_ROLE_CREATE, id ? "PUT" : "POST", path, body, id,
                   NULL);
}
bool server_admin_role_delete(history *h, const char *id)
{
    if (!history_uuid(id))
        return false;
    char path[64];
    snprintf(path, sizeof(path), "/roles/%s", id);
    return enqueue(h, OP_ROLE_DELETE, "DELETE", path, NULL, id, NULL);
}
bool server_admin_channel_create(history *h, const char *name, const char *type, const char *topic)
{
    if (server_admin_check_text(name, 0, true) || !type ||
        (strcmp(type, "text") && strcmp(type, "voice") && strcmp(type, "category")) ||
        (topic && server_admin_check_text(topic, ADMIN_TOPIC_MAX, false)))
        return false;
    json_t *body = json_pack("{s:s,s:s}", "name", name, "type", type);
    /* A category has no topic. */
    if (body && topic && *topic && strcmp(type, "category") &&
        json_object_set_new(body, "topic", json_string(topic))) {
        json_decref(body);
        body = NULL;
    }
    return body && enqueue(h, OP_CHANNEL_CREATE, "POST", "/channels", body, NULL, NULL);
}
bool server_admin_channel_save(history *h, const char *id, const char *name, const char *topic)
{
    if (!history_uuid(id) || server_admin_check_text(name, 0, true) ||
        (topic && server_admin_check_text(topic, ADMIN_TOPIC_MAX, false)))
        return false;
    /* NULL keeps the topic; "" clears it. */
    json_t *body = topic ? json_pack("{s:s,s:s}", "name", name, "topic", topic)
                         : json_pack("{s:s}", "name", name);
    char path[64];
    snprintf(path, sizeof(path), "/channels/%s", id);
    return body && enqueue(h, OP_CHANNEL_SAVE, "PUT", path, body, id, NULL);
}
bool server_admin_channel_delete(history *h, const char *id)
{
    if (!history_uuid(id))
        return false;
    char path[64];
    snprintf(path, sizeof(path), "/channels/%s", id);
    return enqueue(h, OP_CHANNEL_DELETE, "DELETE", path, NULL, id, NULL);
}
bool server_admin_channel_move(history *h, const char *id, int from, int to)
{
    if (!history_uuid(id) || from < 1 || to < 1 || from == to)
        return false;
    json_t *body = json_pack("{s:i,s:i}", "old_position", from, "new_position", to);
    char path[96];
    snprintf(path, sizeof(path), "/channels/%s/change_position", id);
    return body && enqueue(h, OP_CHANNEL_MOVE, "PUT", path, body, id, NULL);
}
bool server_admin_channel_role(history *h, const char *channel, const char *role,
                               const bool flags[PERMISSION_CHANNEL_COUNT])
{
    if (!history_uuid(channel) || !history_uuid(role))
        return false;
    json_t *permissions = flags_object(channel_keys, flags, PERMISSION_CHANNEL_COUNT);
    json_t *body = permissions ? json_pack("{s:o}", "permissions", permissions) : NULL;
    char path[128];
    snprintf(path, sizeof(path), "/channels/%s/permissions/%s", channel, role);
    return body && enqueue(h, OP_CHANNEL_ROLE, "PUT", path, body, channel, role);
}
bool server_admin_channel_role_remove(history *h, const char *channel, const char *role)
{
    if (!history_uuid(channel) || !history_uuid(role))
        return false;
    char path[128];
    snprintf(path, sizeof(path), "/channels/%s/role/%s", channel, role);
    return enqueue(h, OP_CHANNEL_ROLE_REMOVE, "DELETE", path, NULL, channel, role);
}
bool server_admin_emoji_create(history *h, const char *name, char *base64, const char *format)
{
    if (server_admin_check_text(name, 0, true) || !base64 || !*base64 || !image_format(format)) {
        g_free(base64);
        return false;
    }
    json_t *body = json_pack("{s:s,s:s,s:s}", "name", name, "image_blob", base64, "format", format);
    g_free(base64);
    return body && enqueue(h, OP_EMOJI_CREATE, "POST", "/emojis", body, NULL, NULL);
}
bool server_admin_emoji_delete(history *h, const char *id)
{
    if (!history_uuid(id))
        return false;
    char path[64];
    snprintf(path, sizeof(path), "/emojis/%s", id);
    return enqueue(h, OP_EMOJI_DELETE, "DELETE", path, NULL, id, NULL);
}
/* The channel must take messages: the contract allows webhooks only there. */
static bool webhook_channel(history *h, const char *channel)
{
    const char *type =
        history_internal_str(json_object_get(server_admin_channels(h), channel), "type");
    return history_uuid(channel) && (!type || !strcmp(type, "text") || !strcmp(type, "voice"));
}
bool server_admin_webhook_create(history *h, const char *channel, const char *name)
{
    if (!webhook_channel(h, channel) || server_admin_check_text(name, ADMIN_WEBHOOK_NAME_MAX, true))
        return false;
    json_t *body = json_pack("{s:s,s:s}", "channel_id", channel, "name", name);
    return body && enqueue(h, OP_WEBHOOK_CREATE, "POST", "/webhooks", body, NULL, NULL);
}
bool server_admin_webhook_nickname(history *h, const char *id, const char *nickname)
{
    if (!history_uuid(id) || server_admin_check_text(nickname, ADMIN_WEBHOOK_NICKNAME_MAX, false))
        return false;
    json_t *body = json_pack("{s:s}", "display_name", nickname);
    char path[64];
    snprintf(path, sizeof(path), "/webhooks/%s", id);
    return body && enqueue(h, OP_WEBHOOK_NICKNAME, "PATCH", path, body, id, NULL);
}
bool server_admin_webhook_avatar(history *h, const char *id, char *base64, const char *format)
{
    if (!history_uuid(id) || !base64 || !*base64 || !image_format(format)) {
        g_free(base64);
        return false;
    }
    json_t *body = json_pack("{s:s,s:s}", "avatar", base64, "avatar_format", format);
    g_free(base64);
    char path[64];
    snprintf(path, sizeof(path), "/webhooks/%s", id);
    return body && enqueue(h, OP_WEBHOOK_AVATAR, "PATCH", path, body, id, NULL);
}
bool server_admin_webhook_avatar_remove(history *h, const char *id)
{
    if (!history_uuid(id))
        return false;
    json_t *body = json_pack("{s:s}", "avatar", "");
    char path[64];
    snprintf(path, sizeof(path), "/webhooks/%s", id);
    return body && enqueue(h, OP_WEBHOOK_AVATAR_REMOVE, "PATCH", path, body, id, NULL);
}
bool server_admin_webhook_revoke(history *h, const char *id)
{
    if (!history_uuid(id))
        return false;
    char path[64];
    snprintf(path, sizeof(path), "/webhooks/%s", id);
    return enqueue(h, OP_WEBHOOK_REVOKE, "DELETE", path, NULL, id, NULL);
}
const char *server_admin_webhook_token(history *h, const char *id)
{
    struct history_server_admin *a = h ? h->server_admin : NULL;
    return a && id && *id && !strcmp(a->webhook_for, id) ? a->webhook_token : "";
}
bool server_admin_webhook_token_lost(history *h, const char *id)
{
    return server_admin_webhook_held(h, id) && h->server_admin->token_lost;
}
bool server_admin_webhook_held(history *h, const char *id)
{
    return h && h->server_admin && id && *id && !strcmp(h->server_admin->webhook_for, id);
}
const char *server_admin_webhook_created(history *h)
{
    return h && h->server_admin ? h->server_admin->webhook_created : "";
}
void server_admin_webhook_created_clear(history *h)
{
    if (h && h->server_admin)
        h->server_admin->webhook_created[0] = 0;
}
const char *server_admin_instance_url(history *h)
{
    return auth_instance_url(h->auth);
}
void server_admin_webhook_forget_token(history *h)
{
    struct history_server_admin *a = h ? h->server_admin : NULL;
    if (!a)
        return;
    wipe(a->webhook_token, sizeof(a->webhook_token));
    a->webhook_for[0] = 0;
    a->token_lost = false;
    changed(a);
}
bool server_admin_emoji_deletable(history *h, const json_t *emoji)
{
    const char *author = history_internal_str(emoji, "created_by");
    return (author && *author && !strcmp(author, auth_user_id(h->auth))) ||
           server_admin_allowed(h, PERMISSION_MANAGE_SERVER);
}
bool server_admin_member_role(history *h, const char *user, const char *role, bool assign)
{
    if (!history_uuid(user) || !history_uuid(role))
        return false;
    char path[128];
    if (assign) {
        json_t *body = json_pack("{s:s}", "role_id", role);
        snprintf(path, sizeof(path), "/users/%s/roles", user);
        return body && enqueue(h, OP_MEMBER_ROLE_ADD, "POST", path, body, user, role);
    }
    snprintf(path, sizeof(path), "/users/%s/roles/%s", user, role);
    return enqueue(h, OP_MEMBER_ROLE_REMOVE, "DELETE", path, NULL, user, role);
}
bool server_admin_member_bannable(history *h, const char *user)
{
    const char *owner = permissions_owner(auth_permissions(h->auth));
    return history_uuid(user) && strcmp(user, auth_user_id(h->auth)) && *owner &&
           strcmp(user, owner) && server_admin_allowed(h, PERMISSION_MANAGE_SERVER);
}
/* Ban and reset use auth's member actions; the path is only a label here. */
bool server_admin_member_ban(history *h, const char *user, bool ban)
{
    if (!server_admin_member_bannable(h, user))
        return false;
    return enqueue(h, ban ? OP_MEMBER_BAN : OP_MEMBER_UNBAN, "PUT", "ban", NULL, user, NULL);
}
bool server_admin_member_reset(history *h, const char *user)
{
    if (!history_uuid(user) || !strcmp(user, auth_user_id(h->auth)) ||
        !server_admin_allowed(h, PERMISSION_MANAGE_SERVER))
        return false;
    return enqueue(h, OP_MEMBER_RESET, "POST", "reset", NULL, user, NULL);
}
const char *server_admin_reset_url(history *h, const char *user)
{
    struct history_server_admin *a = h ? h->server_admin : NULL;
    return a && user && !strcmp(a->reset_user, user) ? a->reset_url : "";
}
const char *server_admin_reset_expires(history *h, const char *user)
{
    struct history_server_admin *a = h ? h->server_admin : NULL;
    return a && user && !strcmp(a->reset_user, user) ? a->reset_expires : "";
}
admin_state server_admin_state(history *h)
{
    return h && h->server_admin ? h->server_admin->state : ADMIN_IDLE;
}
bool server_admin_busy(history *h)
{
    return h && h->server_admin && (h->server_admin->has_queued || h->server_admin->sending);
}
const char *server_admin_notice(history *h)
{
    return h && h->server_admin ? h->server_admin->notice : "";
}
uint64_t server_admin_revision(history *h)
{
    return h && h->server_admin ? h->server_admin->revision : 0;
}

bool history_internal_server_admin_mutating(history *h)
{
    return h->server_admin && h->server_admin->sending;
}
/* Appends &key=escaped(value) when value is set. */
static bool query_add(GString *path, const char *key, const char *value)
{
    if (!value || !*value)
        return true;
    char *escaped = curl_easy_escape(NULL, value, 0);
    if (!escaped)
        return false;
    g_string_append_printf(path, "&%s=%s", key, escaped);
    curl_free(escaped);
    return true;
}
static bool list_path(struct history_server_admin *a, admin_list list, char *path, size_t size)
{
    struct admin_page *page = &a->pages[list];
    /* The contract returns every active webhook at once, without a cursor. */
    if (list == ADMIN_LIST_WEBHOOKS) {
        snprintf(path, size, "/webhooks");
        return true;
    }
    GString *text = g_string_new(list == ADMIN_LIST_EMOJIS ? "/emojis?order=asc"
                                                           : "/admin/audit-logs?order=desc");
    bool ok = query_add(text, "since", page->since) &&
              (!*page->since || query_add(text, "last_id", page->last));
    if (list == ADMIN_LIST_AUDIT)
        ok = ok && query_add(text, "entity_type", a->audit.entity) &&
             query_add(text, "action", a->audit.action) &&
             query_add(text, "actor_id", a->audit.actor) &&
             query_add(text, "until", a->audit.until);
    ok = ok && text->len < size;
    if (ok)
        memcpy(path, text->str, text->len + 1);
    g_string_free(text, TRUE);
    return ok;
}
bool history_internal_server_admin_dispatch(history *h)
{
    struct history_server_admin *a = h->server_admin;
    if (!a || h->pending != NONE || !auth_has_session(h->auth))
        return false;
    if (a->has_queued) {
        struct admin_op *op = &a->queued;
        bool started;
        if (op->kind == OP_MEMBER_BAN || op->kind == OP_MEMBER_UNBAN || op->kind == OP_MEMBER_RESET)
            /* auth_member_action: 2 ban, 3 unban, 4 reset. */
            started = auth_member_action(h->auth, op->target,
                                         op->kind == OP_MEMBER_BAN     ? 2
                                         : op->kind == OP_MEMBER_UNBAN ? 3
                                                                       : 4);
        else
            started = auth_mutate_json(h->auth, op->method, op->path, op->body);
        if (!started)
            return false;
        op_clear(&a->sent);
        a->sent = *op;
        memset(op, 0, sizeof(*op));
        a->has_queued = false;
        a->sending = true;
    } else if (a->want_server) {
        if (!auth_get_json(h->auth, "/server"))
            return false;
        a->want_server = false;
        a->reading = READ_SERVER;
    } else {
        admin_list list = ADMIN_LISTS;
        for (int i = 0; i < ADMIN_LISTS && list == ADMIN_LISTS; ++i)
            if (a->pages[i].want && !a->pages[i].loading)
                list = (admin_list)i;
        if (list == ADMIN_LISTS)
            return false;
        char path[512];
        if (!list_path(a, list, path, sizeof(path))) {
            a->pages[list].want = false;
            a->pages[list].failed = true;
            changed(a);
            return false;
        }
        if (!auth_get_json(h->auth, path))
            return false;
        a->pages[list].want = false;
        a->pages[list].loading = true;
        a->reading = READ_LIST;
        a->reading_list = list;
    }
    h->pending = SERVER_ADMIN;
    h->request_context = h->context;
    h->request_role_events = h->role_events;
    return true;
}

static bool valid_server(json_t *body)
{
    return json_is_object(body) && history_internal_string_field(body, "name", 1024, false) &&
           history_internal_string_field(body, "owner_username", 1024, true) &&
           history_internal_string_field(body, "created_at", 64, true) &&
           history_internal_string_field(body, "icon_format", 8, true) &&
           (!json_object_get(body, "public") || json_is_boolean(json_object_get(body, "public")));
}
/* The icon itself stays out of the copy; only whether one is set. */
static void keep_server(struct history_server_admin *a, json_t *body)
{
    json_t *copy = json_deep_copy(body);
    if (!copy)
        return;
    const char *icon = json_string_value(json_object_get(copy, "icon_blob"));
    json_object_set_new(copy, "_icon", json_boolean(icon && *icon));
    json_object_del(copy, "icon_blob");
    json_decref(a->server);
    a->server = copy;
}
static bool valid_item(json_t *item, admin_list list)
{
    if (!json_is_object(item) || !history_uuid(history_internal_str(item, "id")) ||
        !history_internal_string_field(item, "created_at", 64, false))
        return false;
    if (list == ADMIN_LIST_EMOJIS)
        return history_internal_string_field(item, "name", 512, false) &&
               history_internal_string_field(item, "format", 8, true) &&
               history_internal_string_field(item, "created_by", 36, true);
    if (list == ADMIN_LIST_WEBHOOKS)
        return history_uuid(history_internal_str(item, "channel_id")) &&
               history_internal_string_field(item, "name", 512, false) &&
               history_internal_string_field(item, "display_name", 512, true) &&
               history_internal_string_field(item, "created_by", 36, true);
    json_t *metadata = json_object_get(item, "metadata");
    return history_internal_string_field(item, "actor_username", 1024, true) &&
           history_internal_string_field(item, "action", 256, false) &&
           history_internal_string_field(item, "entity_type", 64, true) &&
           (!metadata || json_is_object(metadata) || json_is_null(metadata));
}
static void page_response(struct history_server_admin *a, admin_list list, json_t *body, long code)
{
    struct admin_page *page = &a->pages[list];
    if (!page->loading)
        return; /* Closed or refiltered meanwhile. */
    page->loading = false;
    json_t *items = json_object_get(body, list == ADMIN_LIST_EMOJIS     ? "emojis"
                                          : list == ADMIN_LIST_WEBHOOKS ? "webhooks"
                                                                        : "logs");
    /* Webhooks come whole, so their one answer is bounded by the list limit. */
    bool ok = code == 200 && json_is_array(items) &&
              json_array_size(items) <= (list == ADMIN_LIST_WEBHOOKS ? LIST_LIMIT : 100);
    size_t i;
    json_t *item;
    json_array_foreach(items, i, item) if (ok && !valid_item(item, list)) ok = false;
    if (ok && !page->items)
        page->items = json_array();
    /* Webhooks always come whole: a new answer replaces the earlier one. */
    if (ok && list == ADMIN_LIST_WEBHOOKS)
        json_array_clear(page->items);
    if (ok && json_array_size(page->items) + json_array_size(items) > LIST_LIMIT)
        ok = false;
    if (!ok || !page->items) {
        page->failed = true;
        changed(a);
        return;
    }
    bool older = false;
    json_array_foreach(items, i, item)
    {
        if (list == ADMIN_LIST_AUDIT) {
            const char *action = history_internal_str(item, "action");
            if (!a->audit_actions)
                a->audit_actions = json_object();
            if (a->audit_actions && json_object_size(a->audit_actions) < 500 &&
                !json_object_get(a->audit_actions, action) &&
                !json_object_set_new(a->audit_actions, action, json_true())) {
                json_decref(a->audit_sorted);
                a->audit_sorted = NULL;
            }
            /* Newest first: the first event before "from" ends the list. */
            uint64_t stamp;
            if (a->audit.from &&
                session_time_parse(history_internal_str(item, "created_at"), &stamp) &&
                stamp < a->audit.from) {
                older = true;
                break;
            }
        }
        json_t *copy = json_deep_copy(item);
        if (copy)
            json_object_del(copy, "image_blob");
        if (!copy || json_array_append_new(page->items, copy)) {
            page->failed = true;
            break;
        }
    }
    json_t *last = json_array_get(items, json_array_size(items) - 1);
    page->more = !older && last && json_is_true(json_object_get(body, "has_more")) &&
                 (strcmp(page->last, history_internal_str(last, "id")) ||
                  strcmp(page->since, history_internal_str(last, "created_at")));
    if (last) {
        snprintf(page->since, sizeof(page->since), "%s", history_internal_str(last, "created_at"));
        snprintf(page->last, sizeof(page->last), "%s", history_internal_str(last, "id"));
    }
    changed(a);
}
/* A confirmed membership change shows at once, as its event would. */
static void observe_role(history *h, const struct admin_op *op, bool add)
{
    json_t *event = json_pack("{s:s,s:s,s:s}", "type", add ? "role_add" : "role_remove", "user_id",
                              op->target, "role_id", op->extra);
    if (event)
        history_internal_role_event(h, event);
    json_decref(event);
}
static void observe_ban(history *h, const char *user, bool banned)
{
    json_t *cached = json_object_get(h->users, user);
    if (cached) {
        json_object_set_new(cached, "banned", json_boolean(banned));
        ++h->users_revision;
    }
}
static const char *success_notice(enum admin_kind kind)
{
    switch (kind) {
    case OP_SERVER:
        return _("Servidor atualizado");
    case OP_ICON:
        return _("Ícone atualizado");
    case OP_ICON_REMOVE:
        return _("Ícone removido");
    case OP_ROLE_CREATE:
        return _("Cargo criado");
    case OP_ROLE_SAVE:
        return _("Cargo salvo");
    case OP_ROLE_DELETE:
        return _("Cargo excluído");
    case OP_CHANNEL_CREATE:
        return _("Canal criado");
    case OP_CHANNEL_SAVE:
        return _("Canal salvo");
    case OP_CHANNEL_DELETE:
        return _("Canal excluído");
    case OP_CHANNEL_MOVE:
        return _("Canal movido");
    case OP_CHANNEL_ROLE:
        return _("Permissões do canal salvas");
    case OP_CHANNEL_ROLE_REMOVE:
        return _("Vínculo do cargo removido");
    case OP_EMOJI_CREATE:
        return _("Emoji adicionado");
    case OP_EMOJI_DELETE:
        return _("Emoji excluído");
    case OP_WEBHOOK_CREATE:
        return _("Webhook criado");
    case OP_WEBHOOK_NICKNAME:
        return _("Apelido do webhook salvo");
    case OP_WEBHOOK_AVATAR:
        return _("Avatar do webhook atualizado");
    case OP_WEBHOOK_AVATAR_REMOVE:
        return _("Avatar do webhook removido");
    case OP_WEBHOOK_REVOKE:
        return _("Webhook revogado");
    case OP_MEMBER_ROLE_ADD:
        return _("Cargo atribuído");
    case OP_MEMBER_ROLE_REMOVE:
        return _("Cargo removido do membro");
    case OP_MEMBER_BAN:
        return _("Membro banido");
    case OP_MEMBER_UNBAN:
        return _("Membro desbanido");
    default:
        return _("Link de redefinição gerado");
    }
}
static const char *conflict_notice(enum admin_kind kind)
{
    switch (kind) {
    case OP_ROLE_CREATE:
    case OP_ROLE_SAVE:
        return _("Já existe um cargo com esse nome");
    case OP_CHANNEL_CREATE:
        return _("Nome de canal já existe ou o limite de canais foi atingido");
    case OP_CHANNEL_SAVE:
        return _("Já existe um canal com esse nome");
    case OP_CHANNEL_MOVE:
        return _("A ordem dos canais mudou; confira e tente de novo");
    case OP_EMOJI_CREATE:
        return _("Nome de emoji já existe ou o limite de emojis foi atingido");
    default:
        return _("O servidor recusou a alteração por conflito");
    }
}
/* Reloads what a mutation may have changed, confirmed or not. */
static void reconcile(history *h, struct history_server_admin *a, enum admin_kind kind)
{
    if (kind == OP_SERVER || kind == OP_ICON || kind == OP_ICON_REMOVE)
        a->want_server = true;
    else if (kind == OP_EMOJI_CREATE || kind == OP_EMOJI_DELETE) {
        server_admin_load(h, ADMIN_LIST_EMOJIS, false);
        chat_assets_emojis_request(h->assets, true);
    } else if (kind >= OP_WEBHOOK_CREATE && kind <= OP_WEBHOOK_REVOKE) {
        /* Read again without emptying the list: the open form belongs to one
         * of its items and must not lose its place meanwhile. */
        struct admin_page *page = &a->pages[ADMIN_LIST_WEBHOOKS];
        page->want = true;
        page->failed = false;
        changed(a);
    } else if (kind >= OP_MEMBER_ROLE_ADD && kind <= OP_MEMBER_UNBAN)
        history_users_refresh(h);
    /* Roles, channels and the owner come back with the permission snapshot. */
    auth_permissions_refresh(h->auth);
}
static void reset_response(struct history_server_admin *a, const struct admin_op *op, json_t *body,
                           bool *ok)
{
    const char *url = history_internal_str(body, "reset_url"),
               *expires = history_internal_str(body, "expires_at");
    uint64_t stamp;
    *ok = url && expires && strlen(url) > 0 && strlen(url) < sizeof(a->reset_url) &&
          strlen(expires) < sizeof(a->reset_expires) && session_time_parse(expires, &stamp) &&
          (g_str_has_prefix(url, "https://") || g_str_has_prefix(url, "http://"));
    for (const unsigned char *p = (const unsigned char *)url; *ok && *p; ++p)
        if (*p <= 32 || *p == 127)
            *ok = false;
    if (*ok) {
        snprintf(a->reset_url, sizeof(a->reset_url), "%s", url);
        snprintf(a->reset_expires, sizeof(a->reset_expires), "%s", expires);
        snprintf(a->reset_user, sizeof(a->reset_user), "%s", op->target);
    }
}
/* The token comes only in this answer. It is held for that webhook alone and
 * never copied into a notice. An answer without a usable one still created the
 * webhook: the form says so and offers revoking it. */
static void webhook_created(struct history_server_admin *a, json_t *body)
{
    const char *id = history_internal_str(body, "id"),
               *token = history_internal_str(body, "token");
    wipe(a->webhook_token, sizeof(a->webhook_token));
    a->webhook_for[0] = a->webhook_created[0] = 0;
    a->token_lost = false;
    if (!history_uuid(id))
        return;
    bool usable = token && *token && strlen(token) < sizeof(a->webhook_token);
    for (const unsigned char *p = (const unsigned char *)token; usable && *p; ++p)
        usable = *p > 32 && *p < 127;
    snprintf(a->webhook_created, sizeof(a->webhook_created), "%s", id);
    snprintf(a->webhook_for, sizeof(a->webhook_for), "%s", id);
    if (usable)
        snprintf(a->webhook_token, sizeof(a->webhook_token), "%s", token);
    else
        a->token_lost = true;
}
static void mutation_response(history *h, struct history_server_admin *a, json_t *body, long code,
                              bool uncertain)
{
    struct admin_op *op = &a->sent;
    a->sending = false;
    bool ok = code >= 200 && code < 300 && !uncertain;
    const char *detail = json_string_value(json_object_get(body, "detail"));
    if (!detail)
        detail = json_string_value(json_object_get(body, "title"));
    if (ok && op->kind == OP_MEMBER_RESET)
        reset_response(a, op, body, &ok);
    if (ok && op->kind == OP_WEBHOOK_CREATE)
        webhook_created(a, body);
    if (ok && op->kind == OP_WEBHOOK_REVOKE && !strcmp(a->webhook_for, op->target))
        server_admin_webhook_forget_token(h);
    if (ok) {
        if ((op->kind == OP_SERVER || op->kind == OP_ICON || op->kind == OP_ICON_REMOVE) &&
            valid_server(body))
            keep_server(a, body);
        if (op->kind == OP_MEMBER_ROLE_ADD || op->kind == OP_MEMBER_ROLE_REMOVE)
            observe_role(h, op, op->kind == OP_MEMBER_ROLE_ADD);
        if (op->kind == OP_MEMBER_BAN || op->kind == OP_MEMBER_UNBAN)
            observe_ban(h, op->target, op->kind == OP_MEMBER_BAN);
        a->state = ADMIN_SAVED;
        set_notice(a, success_notice(op->kind), NULL);
        if (op->kind == OP_WEBHOOK_CREATE && a->token_lost)
            set_notice(a, _("Webhook criado, mas sem token na resposta; revogue-o e crie outro"),
                       NULL);
        if (op->kind != OP_MEMBER_RESET && op->kind != OP_MEMBER_ROLE_ADD &&
            op->kind != OP_MEMBER_ROLE_REMOVE && op->kind != OP_MEMBER_BAN &&
            op->kind != OP_MEMBER_UNBAN)
            reconcile(h, a, op->kind);
        else
            auth_permissions_refresh(h->auth);
    } else if (uncertain || !code || (code >= 200 && code < 300)) {
        /* Never replayed: show what the server kept before a manual retry. */
        a->state = ADMIN_UNCERTAIN;
        set_notice(a, _("Alteração não confirmada; confira antes de repetir"), NULL);
        reconcile(h, a, op->kind);
    } else {
        a->state = ADMIN_FAILED;
        if (code == 429)
            set_notice(a, _("Muitas alterações; aguarde e tente de novo"), NULL);
        else if (code == 409)
            set_notice(a, conflict_notice(op->kind), detail);
        else if (code == 403)
            set_notice(a, _("Sem permissão para esta ação"), detail);
        else if (code == 404)
            set_notice(a, _("Item não encontrado; a lista foi atualizada"), NULL);
        else
            set_notice(a, _("O servidor recusou a alteração"), detail);
        if (code == 403 || code == 404 || code == 409)
            reconcile(h, a, op->kind);
    }
    op_clear(op);
}
void history_internal_server_admin_response(history *h, json_t *body, long code, bool uncertain)
{
    struct history_server_admin *a = h->server_admin;
    if (!a)
        return;
    enum read_kind reading = a->reading;
    a->reading = READ_NONE;
    if (a->sending) {
        mutation_response(h, a, body, code, uncertain);
        return;
    }
    if (reading == READ_SERVER) {
        if (code == 200 && valid_server(body)) {
            keep_server(a, body);
            changed(a);
        } else if (!a->server)
            set_notice(a, _("Não foi possível carregar o servidor"), NULL);
    } else if (reading == READ_LIST)
        page_response(a, a->reading_list, body, code);
}
