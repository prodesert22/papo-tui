#include "model/conversation/profile_edit.h"
#include "model/conversation/private.h"

enum op_kind { OP_FIELDS, OP_STATUS, OP_IMAGE, OP_BANNER_REMOVE };
#define FIELD_COUNT 4
#define QUEUE_LIMIT 8
static const char *const field_names[FIELD_COUNT] = {"nickname", "status", "description", "typing"};
/* The status phrase is sent as "status" and read back as "status_message". */
static const char *const profile_names[FIELD_COUNT] = {"nickname", "status_message", "description",
                                                       "typing"};
static const size_t field_limits[FIELD_COUNT] = {PROFILE_NICKNAME_MAX, PROFILE_STATUS_MAX,
                                                 PROFILE_DESCRIPTION_MAX, PROFILE_TYPING_MAX};
struct profile_op {
    enum op_kind kind;
    char *fields[FIELD_COUNT]; /* Owned; NULL keeps the current value. */
    char status[8];            /* "" means online (null). */
    profile_image image;
    char *data; /* Owned Base64 image. */
    char format[8];
};
struct history_profile_edit {
    json_t *profile;
    struct profile_op queue[QUEUE_LIMIT], sent;
    size_t count;
    bool want_load, loading, sending;
    profile_state state;
    char notice[256];
    uint64_t revision, loads;
    char *password; /* Owned secret until dispatched; erased on release. */
    bool password_not_started;
};

static void op_clear(struct profile_op *op)
{
    for (size_t i = 0; i < FIELD_COUNT; ++i)
        g_free(op->fields[i]);
    g_free(op->data);
    memset(op, 0, sizeof(*op));
}
static struct history_profile_edit *edit(history *h)
{
    if (!h->profile_edit)
        h->profile_edit = calloc(1, sizeof(*h->profile_edit));
    return h->profile_edit;
}
static void changed(struct history_profile_edit *p)
{
    ++p->revision;
}
/* Remote text never brings terminal controls into the notice. */
static void set_notice(struct history_profile_edit *p, const char *prefix, const char *remote)
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
    snprintf(p->notice, sizeof(p->notice), "%s", text->str);
    /* Never end inside a UTF-8 sequence after truncation. */
    size_t length = strlen(p->notice);
    while (length && !g_utf8_validate(p->notice, (gssize)length, NULL))
        p->notice[--length] = 0;
    g_string_free(text, TRUE);
    changed(p);
}
static void erase_password(struct history_profile_edit *p)
{
    if (!p->password)
        return;
    volatile char *secret = p->password;
    for (size_t i = 0, n = strlen(p->password); i < n; ++i)
        secret[i] = 0;
    g_free(p->password);
    p->password = NULL;
}
void history_internal_profile_destroy(history *h)
{
    struct history_profile_edit *p = h->profile_edit;
    if (!p)
        return;
    for (size_t i = 0; i < p->count; ++i)
        op_clear(&p->queue[i]);
    op_clear(&p->sent);
    erase_password(p);
    json_decref(p->profile);
    free(p);
    h->profile_edit = NULL;
}
bool history_password_change(history *h, const char *password)
{
    struct history_profile_edit *p = edit(h);
    if (!p || !password || p->password || !auth_has_session(h->auth) ||
        auth_password_state(h->auth) == AUTH_PASSWORD_PENDING)
        return false;
    p->password = g_strdup(password);
    p->password_not_started = false;
    changed(p);
    return p->password != NULL;
}
bool history_password_waiting(history *h)
{
    return h->profile_edit && h->profile_edit->password;
}
void history_password_cancel(history *h)
{
    if (h->profile_edit && h->profile_edit->password) {
        erase_password(h->profile_edit);
        changed(h->profile_edit);
    }
}
bool history_password_not_started(history *h)
{
    return h->profile_edit && h->profile_edit->password_not_started;
}
/* Runs before the other requests: the change replaces this session anyway. */
bool history_internal_password_dispatch(history *h)
{
    struct history_profile_edit *p = h->profile_edit;
    if (!p || !p->password || h->pending != NONE || auth_busy(h->auth))
        return false;
    bool started = auth_change_password(h->auth, p->password);
    erase_password(p);
    p->password_not_started = !started && auth_password_state(h->auth) != AUTH_PASSWORD_REFUSED;
    changed(p);
    return started;
}
bool history_internal_profile_mutating(history *h)
{
    return h->profile_edit && h->profile_edit->sending;
}

const char *history_profile_check(const char *field, const char *text)
{
    size_t index = FIELD_COUNT;
    for (size_t i = 0; i < FIELD_COUNT; ++i)
        if (field && !strcmp(field, field_names[i]))
            index = i;
    if (index == FIELD_COUNT || !text || !g_utf8_validate(text, -1, NULL))
        return "Texto inválido.";
    if ((size_t)g_utf8_strlen(text, -1) > field_limits[index])
        return index == 0   ? "Apelido com mais de 32 caracteres."
               : index == 2 ? "Descrição com mais de 512 caracteres."
               : index == 1 ? "Frase de status com mais de 64 caracteres."
                            : "Frase de digitação com mais de 64 caracteres.";
    /* Only the description may span lines; no field carries other controls. */
    for (const char *at = text; *at; at = g_utf8_next_char(at)) {
        gunichar c = g_utf8_get_char(at);
        if ((c == '\n' && index != 2) || (c != '\n' && g_unichar_iscntrl(c)))
            return index == 2 ? "A descrição contém caracteres de controle."
                              : "Use uma única linha, sem caracteres de controle.";
    }
    return NULL;
}
static struct profile_op *enqueue(history *h, enum op_kind kind)
{
    struct history_profile_edit *p = edit(h);
    if (!p || !auth_has_session(h->auth) || !history_uuid(auth_user_id(h->auth)))
        return NULL;
    if (p->count == QUEUE_LIMIT) {
        set_notice(p, "Aguarde as alterações anteriores do perfil", NULL);
        return NULL;
    }
    struct profile_op *op = &p->queue[p->count++];
    memset(op, 0, sizeof(*op));
    op->kind = kind;
    p->state = PROFILE_SAVING;
    p->notice[0] = 0;
    changed(p);
    return op;
}

void history_profile_load(history *h, bool force)
{
    struct history_profile_edit *p = edit(h);
    if (!p || (!force && (p->profile || p->loading)))
        return;
    p->want_load = true;
    if (p->state == PROFILE_IDLE)
        p->state = PROFILE_LOADING;
    changed(p);
}
const json_t *history_profile(history *h)
{
    return h && h->profile_edit ? h->profile_edit->profile : NULL;
}
bool history_profile_save(history *h, const char *nickname, const char *status_phrase,
                          const char *description, const char *typing)
{
    const char *values[FIELD_COUNT] = {nickname, status_phrase, description, typing};
    for (size_t i = 0; i < FIELD_COUNT; ++i)
        if (values[i] && history_profile_check(field_names[i], values[i]))
            return false;
    struct profile_op *op = enqueue(h, OP_FIELDS);
    if (!op)
        return false;
    for (size_t i = 0; i < FIELD_COUNT; ++i)
        op->fields[i] = values[i] ? g_strdup(values[i]) : NULL;
    /* The four fields are required together: complete them from a load. */
    if (!h->profile_edit->profile)
        history_profile_load(h, false);
    return true;
}
bool history_profile_set_status(history *h, const char *status)
{
    if (status && strcmp(status, "away") && strcmp(status, "busy"))
        return false;
    struct profile_op *op = enqueue(h, OP_STATUS);
    if (!op)
        return false;
    snprintf(op->status, sizeof(op->status), "%s", status ? status : "");
    return true;
}
bool history_profile_set_image(history *h, profile_image kind, char *base64, const char *format)
{
    if (!base64 || !*base64 || !format ||
        (strcmp(format, "GIF") && strcmp(format, "JPEG") && strcmp(format, "PNG") &&
         strcmp(format, "WEBP"))) {
        g_free(base64);
        return false;
    }
    struct profile_op *op = enqueue(h, OP_IMAGE);
    if (!op) {
        g_free(base64);
        return false;
    }
    op->image = kind;
    op->data = base64;
    snprintf(op->format, sizeof(op->format), "%s", format);
    return true;
}
bool history_profile_remove_banner(history *h)
{
    return enqueue(h, OP_BANNER_REMOVE) != NULL;
}
profile_state history_profile_state(history *h)
{
    return h && h->profile_edit ? h->profile_edit->state : PROFILE_IDLE;
}
const char *history_profile_notice(history *h)
{
    return h && h->profile_edit ? h->profile_edit->notice : "";
}
uint64_t history_profile_revision(history *h)
{
    return h && h->profile_edit ? h->profile_edit->revision : 0;
}
uint64_t history_profile_loads(history *h)
{
    return h && h->profile_edit ? h->profile_edit->loads : 0;
}

static const char *current(const json_t *profile, size_t index)
{
    const char *value = json_string_value(json_object_get(profile, profile_names[index]));
    return value ? value : "";
}
static json_t *request_body(const struct history_profile_edit *p, const struct profile_op *op)
{
    if (op->kind == OP_FIELDS) {
        json_t *body = json_object();
        for (size_t i = 0; body && i < FIELD_COUNT; ++i) {
            /* An absent typing phrase keeps the server's value. */
            if (i == 3 && !op->fields[i])
                continue;
            if (json_object_set_new(
                    body, field_names[i],
                    json_string(op->fields[i] ? op->fields[i] : current(p->profile, i)))) {
                json_decref(body);
                return NULL;
            }
        }
        return body;
    }
    if (op->kind == OP_STATUS)
        return *op->status ? json_pack("{s:s}", "status", op->status)
                           : json_pack("{s:n}", "status");
    if (op->kind == OP_BANNER_REMOVE)
        return json_pack("{s:s,s:s}", "banner", "", "banner_format", "");
    return op->image == PROFILE_AVATAR
               ? json_pack("{s:s,s:s}", "avatar", op->data, "avatar_format", op->format)
               : json_pack("{s:s,s:s}", "banner", op->data, "banner_format", op->format);
}
static const char *op_path(const struct profile_op *op)
{
    return op->kind == OP_FIELDS   ? ""
           : op->kind == OP_STATUS ? "/status"
           : op->kind == OP_IMAGE  ? (op->image == PROFILE_AVATAR ? "/avatar" : "/banner")
                                   : "/banner";
}
bool history_internal_profile_dispatch(history *h)
{
    struct history_profile_edit *p = h->profile_edit;
    const char *me = auth_user_id(h->auth);
    if (!p || h->pending != NONE || !auth_has_session(h->auth) || !history_uuid(me))
        return false;
    char path[128];
    if (p->want_load || (p->count && p->queue[0].kind == OP_FIELDS && !p->profile)) {
        snprintf(path, sizeof(path), "/users/%s/profile", me);
        if (!auth_get_json(h->auth, path))
            return false;
        p->want_load = false;
        p->loading = true;
    } else if (p->count) {
        json_t *body = request_body(p, &p->queue[0]);
        snprintf(path, sizeof(path), "/users/%s%s", me, op_path(&p->queue[0]));
        bool started = body && auth_mutate_json(h->auth, "PUT", path, body);
        json_decref(body);
        if (!started)
            return false;
        p->sent = p->queue[0];
        memmove(p->queue, p->queue + 1, --p->count * sizeof(p->queue[0]));
        memset(&p->queue[p->count], 0, sizeof(p->queue[0]));
        p->sending = true;
    } else
        return false;
    h->pending = PROFILE_EDIT;
    h->request_context = h->context;
    h->request_role_events = h->role_events;
    return true;
}

static json_t *value_or_null(const json_t *profile, const char *key)
{
    json_t *value = json_object_get(profile, key);
    return value ? value : json_null();
}
/* Mirrors a confirmed change locally, as a presence update from the server
 * would, so names and status change at once; later events still win. */
static void observe(history *h, const json_t *profile, const char *status)
{
    const char *me = auth_user_id(h->auth);
    const char *presence = inbox_presence(h->inbox, me);
    if (!status)
        status = presence && *presence && strcmp(presence, "offline") ? presence : "online";
    json_t *event = json_pack("{s:s,s:s,s:s,s:O,s:O,s:O}", "type", "presence_update", "user_id", me,
                              "status", status, "nickname", value_or_null(profile, "nickname"),
                              "status_message", value_or_null(profile, "status_message"), "typing",
                              value_or_null(profile, "typing"));
    if (event) {
        inbox_event(h->inbox, event);
        history_internal_presence_event(h, event);
        ++h->users_revision;
    }
    json_decref(event);
}
static bool valid_profile(json_t *body, const char *me)
{
    return json_is_object(body) && !strcmp(history_internal_str(body, "id"), me) &&
           history_internal_string_field(body, "username", 1024, false) &&
           history_internal_string_field(body, "nickname", 1024, true) &&
           history_internal_string_field(body, "description", 4096, true) &&
           history_internal_string_field(body, "status_message", 2048, true) &&
           history_internal_string_field(body, "typing", 256, true) &&
           history_internal_string_field(body, "banner_media", 64, true) &&
           history_internal_string_field(body, "status", 16, true);
}
static void confirmed(history *h, struct history_profile_edit *p)
{
    struct profile_op *op = &p->sent;
    if (op->kind == OP_FIELDS && p->profile) {
        for (size_t i = 0; i < FIELD_COUNT; ++i)
            if (op->fields[i])
                json_object_set_new(p->profile, profile_names[i], json_string(op->fields[i]));
        observe(h, p->profile, NULL);
        set_notice(p, "Perfil salvo", NULL);
    } else if (op->kind == OP_STATUS) {
        if (p->profile)
            json_object_set_new(p->profile, "status",
                                *op->status ? json_string(op->status) : json_null());
        observe(h, p->profile, *op->status ? op->status : "online");
        set_notice(p,
                   !strcmp(op->status, "away")   ? "Status: ausente"
                   : !strcmp(op->status, "busy") ? "Status: ocupado"
                                                 : "Status: online",
                   NULL);
    } else if (op->kind == OP_IMAGE && op->image == PROFILE_AVATAR) {
        chat_assets_avatar_invalidate(h->assets, auth_user_id(h->auth));
        set_notice(p, "Avatar atualizado", NULL);
    } else {
        /* The new banner reference only comes with the profile. */
        p->want_load = true;
        set_notice(p, op->kind == OP_BANNER_REMOVE ? "Banner removido" : "Banner atualizado", NULL);
    }
}
void history_internal_profile_response(history *h, json_t *body, long code, bool uncertain)
{
    struct history_profile_edit *p = h->profile_edit;
    if (!p)
        return;
    bool ok = code >= 200 && code < 300;
    const char *detail = json_string_value(json_object_get(body, "detail"));
    if (!detail)
        detail = json_string_value(json_object_get(body, "title"));
    if (p->loading) {
        p->loading = false;
        if (ok && valid_profile(body, auth_user_id(h->auth))) {
            json_t *copy = json_deep_copy(body);
            if (copy) {
                /* Avatar bytes live in the shared asset cache. */
                json_object_del(copy, "avatar_blob");
                json_decref(p->profile);
                p->profile = copy;
                ++p->loads;
                history_internal_cache_user(h, body, false);
            }
            if (p->state == PROFILE_LOADING)
                p->state = PROFILE_IDLE;
            changed(p);
        } else {
            /* Queued text changes cannot be completed without the profile. */
            for (size_t i = 0; i < p->count;)
                if (p->queue[i].kind == OP_FIELDS) {
                    op_clear(&p->queue[i]);
                    memmove(p->queue + i, p->queue + i + 1, (--p->count - i) * sizeof(*p->queue));
                    memset(&p->queue[p->count], 0, sizeof(*p->queue));
                } else
                    ++i;
            p->state = PROFILE_FAILED;
            set_notice(p, "Não foi possível carregar o perfil", NULL);
        }
        return;
    }
    if (!p->sending)
        return;
    p->sending = false;
    if (ok)
        confirmed(h, p);
    else {
        /* Never replayed. Later queued changes are dropped, and an unknown
         * outcome reloads the profile to show what the server kept. */
        for (size_t i = 0; i < p->count; ++i)
            op_clear(&p->queue[i]);
        p->count = 0;
        if (uncertain || !code) {
            p->state = PROFILE_UNCERTAIN;
            p->want_load = true;
            set_notice(p, "Alteração não confirmada; confira o perfil antes de repetir", NULL);
        } else {
            p->state = PROFILE_FAILED;
            set_notice(p,
                       code == 429 ? "Muitas alterações; aguarde e tente de novo"
                                   : "O servidor recusou a alteração",
                       code == 429 ? NULL : detail);
        }
    }
    op_clear(&p->sent);
    if (ok && !p->count)
        p->state = PROFILE_SAVED;
    changed(p);
}
