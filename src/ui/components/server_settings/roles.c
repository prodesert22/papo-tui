#include "model/text/translation.h"
#include "ui/components/server_settings/private.h"

/* Cargos: create, edit and delete roles (manage_roles). */
enum { NAME, COLOR, FLAG, SAVE, DELETE_ITEM };
static const char *const flag_keys[PERMISSION_ROLE_COUNT] = {
    "manage_server", "manage_channels",  "manage_roles",   "ban_members",
    "pin_message",   "everyone_message", "send_attachment", "manage_webhooks"};
static const char *const flag_labels[PERMISSION_ROLE_COUNT] = {
    N_("Gerenciar servidor"), N_("Gerenciar canais"),  N_("Gerenciar cargos"),
    N_("Banir membros"),      N_("Fixar mensagens"),   N_("Mencionar @everyone"),
    N_("Enviar anexos"),      N_("Gerenciar webhooks")};

static const json_t *selected_role(server_settings *s)
{
    return *s->selected ? json_object_get(server_admin_roles(s->model), s->selected) : NULL;
}
static void rows(server_settings *s, GArray *out)
{
    admin_row_add(out, ROW_NEW, _("+ Novo cargo"), "", 0, true);
    const json_t *roles[4096];
    size_t count = admin_role_list(s, roles, G_N_ELEMENTS(roles));
    for (size_t i = 0; i < count; ++i)
        admin_row_add(out, admin_str(roles[i], "id"), admin_str(roles[i], "name"), "",
                      admin_color(admin_str(roles[i], "color")), false);
}
static void fill(server_settings *s)
{
    const json_t *role = selected_role(s);
    admin_set_text(&s->name, role ? admin_str(role, "name") : "");
    admin_set_text(&s->aux, role ? admin_str(role, "color") : "");
    s->name_first = s->aux_first = 0;
    const json_t *flags = json_object_get(role, "permissions");
    for (int i = 0; i < PERMISSION_ROLE_COUNT; ++i)
        s->flags[i] = json_is_true(json_object_get(flags, flag_keys[i]));
}
static void fields(server_settings *s, GArray *out)
{
    if (!server_admin_roles(s->model)) {
        admin_note(out, _("Carregando cargos…"));
        return;
    }
    bool creating = !selected_role(s);
    admin_field_add(out, FIELD_HEADING, 0, creating ? _("Novo cargo") : _("Editar cargo"));
    admin_field *f = admin_field_add(out, FIELD_TEXT, NAME, _("Nome"));
    f->text = &s->name;
    f->first = &s->name_first;
    f = admin_field_add(out, FIELD_TEXT, COLOR, _("Cor"));
    f->text = &s->aux;
    f->first = &s->aux_first;
    static char color[TEXT_UTF8_BYTES];
    if (admin_text(&s->aux, color))
        f->rgb = admin_color(color);
    admin_note(out, _("Cor no formato #RRGGBB. Vazio não envia cor."));
    admin_field_add(out, FIELD_HEADING, 0, _("Permissões"));
    for (int i = 0; i < PERMISSION_ROLE_COUNT; ++i) {
        f = admin_field_add(out, FIELD_CHECK, FLAG, _(flag_labels[i]));
        f->arg = i;
        f->checked = s->flags[i];
    }
    admin_field_add(out, FIELD_BUTTON, SAVE, creating ? _("Criar cargo") : _("Salvar cargo"));
    if (!creating) {
        f = admin_field_add(out, FIELD_BUTTON, DELETE_ITEM, _("Excluir cargo"));
        f->destructive = true;
        snprintf(f->key, sizeof(f->key), "%s", s->selected);
    }
}
static void save(server_settings *s)
{
    static char name[TEXT_UTF8_BYTES], color[TEXT_UTF8_BYTES];
    if (!admin_text(&s->name, name) || !admin_text(&s->aux, color)) {
        admin_error(s, _("Texto inválido."));
        return;
    }
    g_strstrip(color);
    if (!admin_check_field(s, NAME, _("Nome"), name, 0, true))
        return;
    if (!server_admin_color_valid(color)) {
        admin_invalid(s, COLOR, _("Use uma cor no formato #RRGGBB, como #3BA55D."));
        return;
    }
    const json_t *role = selected_role(s);
    if (server_admin_role_save(s->model, role ? s->selected : NULL, name, color, s->flags))
        admin_sent(s);
}
static void activate(server_settings *s, const admin_field *f, int direction)
{
    (void)direction;
    if (f->id == FLAG && f->arg >= 0 && f->arg < PERMISSION_ROLE_COUNT) {
        s->flags[f->arg] = !s->flags[f->arg];
        s->dirty = true;
    } else if (f->id == SAVE)
        save(s);
    else if (f->id == DELETE_ITEM && admin_confirmed(s, f)) {
        if (server_admin_role_delete(s->model, f->key))
            admin_select(s, ROW_NEW);
    }
}
const admin_tab_ops tab_roles = {
    .list = true,
    .empty = N_("Nenhum cargo."),
    .rows = rows,
    .fill = fill,
    .fields = fields,
    .activate = activate,
    .submit = save,
};
