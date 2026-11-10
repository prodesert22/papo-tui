#include "ui/components/server_settings/private.h"
#include "ui/media/message_time.h"
#include <time.h>

/* Visão geral: the server's name, icon and visibility (PATCH /server). */
enum { NAME, ICON, ICON_REMOVE, VISIBILITY, PASSWORD, SAVE, RETRY };

static void enter(server_settings *s)
{
    server_admin_load_server(s->model);
}
static void fill(server_settings *s)
{
    const json_t *server = server_admin_server(s->model);
    admin_set_text(&s->name, admin_str(server, "name"));
    s->name_first = 0;
    s->choice = json_is_true(json_object_get(server, "public")) ? 1 : 0;
    text_clear(&s->secret);
    s->secret_first = 0;
}
static void fields(server_settings *s, GArray *out)
{
    const json_t *server = server_admin_server(s->model);
    if (!server) {
        bool failed = *server_admin_notice(s->model) && server_admin_state(s->model) == ADMIN_IDLE;
        admin_note(out, failed ? "Não foi possível carregar o servidor." : "Carregando servidor…");
        if (failed)
            admin_field_add(out, FIELD_BUTTON, RETRY, "Tentar de novo");
        return;
    }
    admin_field *f = admin_field_add(out, FIELD_INFO, 0, "Dono");
    snprintf(f->value, sizeof(f->value), "%s",
             *admin_str(server, "owner_username") ? admin_str(server, "owner_username") : "—");
    f = admin_field_add(out, FIELD_INFO, 0, "Criado em");
    if (!message_time_format(admin_str(server, "created_at"), time(NULL), f->value,
                             sizeof(f->value)))
        snprintf(f->value, sizeof(f->value), "—");
    f = admin_field_add(out, FIELD_INFO, 0, "Tamanho");
    snprintf(f->value, sizeof(f->value), "%lld canais · %lld membros · %lld cargos",
             (long long)json_integer_value(json_object_get(server, "channel_count")),
             (long long)json_integer_value(json_object_get(server, "member_count")),
             (long long)json_integer_value(json_object_get(server, "role_count")));
    admin_field_add(out, FIELD_HEADING, 0, "Identidade");
    f = admin_field_add(out, FIELD_TEXT, NAME, "Nome");
    f->text = &s->name;
    f->first = &s->name_first;
    f->max = ADMIN_SERVER_NAME_MAX;
    bool icon = json_is_true(json_object_get(server, "_icon"));
    f = admin_field_add(out, FIELD_INFO, 0, "Ícone");
    snprintf(f->value, sizeof(f->value), icon ? "%s" : "Sem ícone",
             *admin_str(server, "icon_format") ? admin_str(server, "icon_format") : "Definido");
    admin_field_add(out, FIELD_BUTTON, ICON, "Alterar ícone…");
    if (icon)
        admin_field_add(out, FIELD_BUTTON, ICON_REMOVE, "Remover ícone")->destructive = true;
    admin_field_add(out, FIELD_HEADING, 0, "Acesso");
    f = admin_field_add(out, FIELD_CHOICE, VISIBILITY, "Visibilidade");
    snprintf(f->value, sizeof(f->value), "%s", s->choice ? "Público" : "Privado");
    bool was_public = json_is_true(json_object_get(server, "public"));
    if (!s->choice) {
        f = admin_field_add(out, FIELD_SECRET, PASSWORD, was_public ? "Senha" : "Nova senha");
        f->text = &s->secret;
        f->first = &s->secret_first;
        admin_note(out, was_public
                            ? "Quem entrar precisará desta senha. Ela não fica salva no aplicativo."
                            : "Deixe vazia para manter a senha atual.");
    }
    admin_field_add(out, FIELD_BUTTON, SAVE, "Salvar alterações");
}
static void save(server_settings *s)
{
    static char name[TEXT_UTF8_BYTES], password[TEXT_UTF8_BYTES];
    if (!admin_text(&s->name, name) || !admin_text(&s->secret, password)) {
        admin_error(s, "Texto inválido.");
        return;
    }
    if (!admin_check_field(s, NAME, "Nome", name, ADMIN_SERVER_NAME_MAX, true)) {
        memset(password, 0, sizeof(password));
        return;
    }
    /* Whether a refusal is about the password, to point at its field. */
    bool password_problem =
        !s->choice && ((json_is_true(json_object_get(server_admin_server(s->model), "public")) &&
                        !*password) ||
                       (*password && server_admin_check_text(password, 0, true)));
    const char *problem = NULL;
    json_t *body = server_admin_patch_body(server_admin_server(s->model), name, s->choice,
                                           s->choice ? NULL : password, &problem);
    memset(password, 0, sizeof(password));
    if (!body) {
        if (password_problem)
            admin_invalid(s, PASSWORD, problem);
        else
            admin_error(s, problem);
        return;
    }
    bool queued = server_admin_patch(s->model, body);
    /* The password leaves the form as soon as it is handed to the request. */
    text_clear(&s->secret);
    if (queued)
        admin_sent(s);
}
static void activate(server_settings *s, const admin_field *f, int direction)
{
    if (f->id == VISIBILITY) {
        s->choice = !s->choice;
        s->dirty = true;
        (void)direction;
    } else if (f->id == ICON)
        s->pick = SERVER_PICK_ICON;
    else if (f->id == ICON_REMOVE) {
        if (admin_confirmed(s, f) && !server_admin_remove_icon(s->model))
            admin_error(s, "Não foi possível remover o ícone agora.");
    } else if (f->id == SAVE)
        save(s);
    else if (f->id == RETRY)
        server_admin_load_server(s->model);
}
const admin_tab_ops tab_overview = {
    .list = false,
    .enter = enter,
    .fill = fill,
    .fields = fields,
    .activate = activate,
    .submit = save,
};
