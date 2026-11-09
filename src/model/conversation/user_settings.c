#include "model/conversation/user_settings.h"
#include "model/conversation/private.h"

struct history_user_settings {
    user_config config, wanted;
    json_t *raw; /* Last server copy; its unknown keys are sent back unchanged. */
    uint64_t seen, revision;
    bool synced, queued, sending;
    user_settings_state state;
    char notice[256];
};

/* Shared by every account, so a recreated module never repeats a revision. */
static uint64_t serial;

static struct history_user_settings *settings(history *h)
{
    if (!h->user_settings) {
        h->user_settings = calloc(1, sizeof(*h->user_settings));
        if (h->user_settings) {
            user_config_defaults(&h->user_settings->config);
            h->user_settings->revision = ++serial;
        }
    }
    return h->user_settings;
}
static void changed(struct history_user_settings *s)
{
    s->revision = ++serial;
}
static void set_notice(struct history_user_settings *s, const char *prefix, const char *remote)
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
    g_string_append_c(text, '.');
    snprintf(s->notice, sizeof(s->notice), "%s", text->str);
    g_string_free(text, TRUE);
}
/* Adopts a newer whoami copy unless a save would be overtaken by it. */
static void sync(history *h, struct history_user_settings *s)
{
    uint64_t revision = auth_user_settings_revision(h->auth);
    if (s->queued || s->sending || (s->synced && s->seen == revision))
        return;
    const json_t *remote = auth_user_settings(h->auth);
    if (!remote)
        return;
    s->seen = revision;
    s->synced = true;
    user_config next;
    user_config_read(&next, remote);
    json_t *copy = json_deep_copy(remote);
    if (copy) {
        json_decref(s->raw);
        s->raw = copy;
    }
    if (!user_config_equal(&next, &s->config)) {
        s->config = next;
        changed(s);
    }
}

const user_config *history_user_config(history *h)
{
    static user_config defaults;
    static bool ready;
    struct history_user_settings *s = h ? settings(h) : NULL;
    if (s) {
        sync(h, s);
        return &s->config;
    }
    if (!ready) {
        user_config_defaults(&defaults);
        ready = true;
    }
    return &defaults;
}
bool history_user_settings_busy(history *h)
{
    return h && h->user_settings && (h->user_settings->queued || h->user_settings->sending);
}
bool history_user_settings_save(history *h, const user_config *config)
{
    struct history_user_settings *s = h ? settings(h) : NULL;
    if (!s || !config || !auth_has_session(h->auth) || s->queued || s->sending)
        return false;
    sync(h, s);
    if (user_config_equal(config, &s->config))
        return false;
    s->wanted = *config;
    s->queued = true;
    s->state = USER_SETTINGS_SAVING;
    snprintf(s->notice, sizeof(s->notice), "Salvando configurações da conta…");
    changed(s);
    return true;
}
user_settings_state history_user_settings_state(history *h)
{
    return h && h->user_settings ? h->user_settings->state : USER_SETTINGS_IDLE;
}
const char *history_user_settings_notice(history *h)
{
    return h && h->user_settings ? h->user_settings->notice : "";
}
uint64_t history_user_settings_revision(history *h)
{
    struct history_user_settings *s = h ? settings(h) : NULL;
    if (!s)
        return 0;
    sync(h, s);
    return s->revision;
}

void history_internal_user_settings_destroy(history *h)
{
    if (!h->user_settings)
        return;
    json_decref(h->user_settings->raw);
    free(h->user_settings);
    h->user_settings = NULL;
}
bool history_internal_user_settings_dispatch(history *h)
{
    struct history_user_settings *s = h->user_settings;
    if (!s || !s->queued)
        return false;
    json_t *config = user_config_encode(&s->wanted, s->raw);
    json_t *body = config ? json_pack("{s:o}", "config", config) : NULL;
    bool started = body && auth_mutate_json(h->auth, "PUT", "/users/settings", body);
    json_decref(body);
    s->queued = false;
    if (!started) {
        /* Nothing left the client: the choice stays open to repeat by hand. */
        s->state = USER_SETTINGS_FAILED;
        set_notice(s, "Não foi possível enviar as configurações", NULL);
        changed(s);
        return false;
    }
    s->sending = true;
    h->pending = USER_SETTINGS;
    h->request_context = h->context;
    return true;
}
/* UserSettings of this account: user_id echoed, config valid. */
static bool valid_reply(history *h, json_t *body)
{
    const char *user = history_internal_str(body, "user_id");
    json_t *version = json_object_get(body, "version");
    return json_is_object(body) && history_internal_string_field(body, "user_id", 36, false) &&
           user && !strcmp(user, auth_user_id(h->auth)) && (!version || json_is_integer(version)) &&
           user_config_valid(json_object_get(body, "config"));
}
void history_internal_user_settings_response(history *h, json_t *body, long code, bool uncertain)
{
    struct history_user_settings *s = h->user_settings;
    if (!s || !s->sending)
        return;
    s->sending = false;
    const char *detail = json_string_value(json_object_get(body, "detail"));
    if (!detail)
        detail = json_string_value(json_object_get(body, "title"));
    if (code == 200 && valid_reply(h, body)) {
        json_t *config = json_object_get(body, "config");
        user_config_read(&s->config, config);
        json_t *copy = json_deep_copy(config);
        if (copy) {
            json_decref(s->raw);
            s->raw = copy;
        }
        /* The whoami copy is now older than this reply: wait for the next one
         * instead of showing the previous values again. */
        s->seen = auth_user_settings_revision(h->auth);
        s->synced = true;
        s->state = USER_SETTINGS_SAVED;
        snprintf(s->notice, sizeof(s->notice), "Configurações da conta salvas.");
    } else if (uncertain || !code || (code >= 200 && code < 300)) {
        /* Shown values return to the last server copy until whoami confirms. */
        s->synced = false;
        s->state = USER_SETTINGS_UNCERTAIN;
        snprintf(s->notice, sizeof(s->notice),
                 "Resultado não confirmado. Confira as configurações antes de repetir.");
    } else {
        s->state = USER_SETTINGS_FAILED;
        if (code == 429)
            set_notice(s, "Muitas alterações; aguarde e tente de novo", NULL);
        else if (code == 401)
            set_notice(s, "Sessão expirada; entre novamente e repita a alteração", NULL);
        else
            set_notice(s, "O servidor recusou as configurações", detail);
    }
    changed(s);
}
