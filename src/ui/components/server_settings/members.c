#include "ui/components/server_settings/private.h"
#include "ui/media/message_time.h"
#include <time.h>

/* Membros: assign roles (manage_roles), ban or unban and create a password
 * reset link (manage_server). The list is the server's user directory. */
enum { ROLE, BAN, RESET };
#define MEMBER_ROWS 2000

static json_t *selected_user(server_settings *s)
{
    for (size_t i = 0; *s->selected && i < history_user_count(s->model); ++i) {
        json_t *user = history_user(s->model, i);
        if (!strcmp(admin_str(user, "id"), s->selected))
            return user;
    }
    return NULL;
}
static void enter(server_settings *s)
{
    server_admin_members_active(s->model, true);
}
static void rows(server_settings *s, GArray *out)
{
    char *folded =
        g_utf8_validate(s->query, -1, NULL) ? g_utf8_casefold(s->query, -1) : g_strdup("");
    const char *owner = server_admin_owner(s->model);
    size_t shown = 0;
    for (size_t i = 0; i < history_user_count(s->model); ++i) {
        json_t *user = history_user(s->model, i);
        if (!admin_user_matches(user, folded))
            continue;
        if (shown++ == MEMBER_ROWS) {
            admin_row_add(out, ROW_MORE, "Refine a busca para ver mais", "", 0, true);
            break;
        }
        char detail[160];
        const char *id = admin_str(user, "id");
        snprintf(detail, sizeof(detail), "@%s%s%s", admin_str(user, "username"),
                 !strcmp(id, owner) ? " · dono" : "",
                 json_is_true(json_object_get(user, "banned")) ? " · banido" : "");
        unsigned color = history_member_color(s->model, id);
        admin_row_add(out, id, admin_user_display(user), detail, color == 0x1ffffff ? 0 : color,
                      false);
    }
    g_free(folded);
}
static bool has_role(const json_t *user, const char *role)
{
    size_t i;
    json_t *entry;
    json_array_foreach(json_object_get(user, "roles"), i,
                       entry) if (!strcmp(admin_str(entry, "id"), role)) return true;
    return false;
}
static void fields(server_settings *s, GArray *out)
{
    json_t *user = selected_user(s);
    if (!user) {
        admin_note(out, history_user_count(s->model)       ? "Escolha um membro na lista."
                        : history_users_complete(s->model) ? "Nenhum membro encontrado."
                                                           : "Carregando membros…");
        return;
    }
    const char *id = admin_str(user, "id");
    admin_field_add(out, FIELD_HEADING, 0, admin_user_display(user));
    admin_field *f = admin_field_add(out, FIELD_INFO, 0, "Usuário");
    snprintf(f->value, sizeof(f->value), "@%s", admin_str(user, "username"));
    f = admin_field_add(out, FIELD_INFO, 0, "Situação");
    bool banned = json_is_true(json_object_get(user, "banned"));
    snprintf(f->value, sizeof(f->value), "%s%s", banned ? "Banido" : "Ativo",
             !strcmp(id, server_admin_owner(s->model)) ? " · dono do servidor"
             : !strcmp(id, server_admin_me(s->model))  ? " · você"
                                                       : "");
    if (server_admin_allowed(s->model, PERMISSION_MANAGE_ROLES)) {
        admin_field_add(out, FIELD_HEADING, 0, "Cargos");
        const json_t *roles[4096];
        size_t count = admin_role_list(s, roles, G_N_ELEMENTS(roles));
        if (!count)
            admin_note(out, server_admin_roles(s->model) ? "O servidor não tem cargos."
                                                         : "Carregando cargos…");
        else
            admin_note(out, "Cada caixa é salva no servidor ao ser marcada.");
        for (size_t i = 0; i < count; ++i) {
            f = admin_field_add(out, FIELD_CHECK, ROLE, admin_str(roles[i], "name"));
            snprintf(f->key, sizeof(f->key), "%s", admin_str(roles[i], "id"));
            f->checked = has_role(user, f->key);
        }
    }
    if (!server_admin_allowed(s->model, PERMISSION_MANAGE_SERVER) ||
        !strcmp(id, server_admin_me(s->model)))
        return;
    admin_field_add(out, FIELD_HEADING, 0, "Moderação");
    if (server_admin_member_bannable(s->model, id)) {
        f = admin_field_add(out, FIELD_BUTTON, BAN, banned ? "Desbanir" : "Banir");
        f->destructive = !banned;
        snprintf(f->key, sizeof(f->key), "%s", id);
        if (!banned)
            admin_note(out, "Banir encerra as sessões do membro e impede novos acessos.");
    }
    f = admin_field_add(out, FIELD_BUTTON, RESET, "Gerar link de redefinição de senha");
    f->destructive = true;
    snprintf(f->key, sizeof(f->key), "%s", id);
    const char *url = server_admin_reset_url(s->model, id);
    if (*url) {
        f = admin_field_add(out, FIELD_INFO, 0, "Link válido até");
        if (!message_time_format(server_admin_reset_expires(s->model, id), time(NULL), f->value,
                                 sizeof(f->value)))
            snprintf(f->value, sizeof(f->value), "%s", server_admin_reset_expires(s->model, id));
        admin_note(out, url);
    }
}
static void activate(server_settings *s, const admin_field *f, int direction)
{
    (void)direction;
    if (f->id == ROLE)
        server_admin_member_role(s->model, s->selected, f->key, !f->checked);
    else if (f->id == BAN && admin_confirmed(s, f)) {
        json_t *user = selected_user(s);
        server_admin_member_ban(s->model, f->key, !json_is_true(json_object_get(user, "banned")));
    } else if (f->id == RESET && admin_confirmed(s, f))
        server_admin_member_reset(s->model, f->key);
}
static void choose(server_settings *s, const admin_row *row)
{
    if (strcmp(row->id, ROW_MORE))
        s->focus = 2;
}
const admin_tab_ops tab_members = {
    .list = true,
    .search = true,
    .empty = "Nenhum membro encontrado.",
    .enter = enter,
    .rows = rows,
    .fields = fields,
    .activate = activate,
    .choose = choose,
};
