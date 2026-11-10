#include "ui/components/server_settings/private.h"

/* Canais: create, edit, reorder and delete channels, and each role's access
 * to a channel (manage_channels). */
enum { NAME, TYPE, TOPIC, SAVE, UP, DOWN, DELETE, PERMISSION, UNLINK, ADD_ROLE, LINK };
static const char *const types[3] = {"text", "voice", "category"};
static const char *const type_labels[3] = {"Texto", "Voz", "Categoria"};
static const char *const flag_keys[PERMISSION_CHANNEL_COUNT] = {"read_channel", "send_messages",
                                                                "delete_messages", "connect_voice"};
static const char *const flag_labels[PERMISSION_CHANNEL_COUNT] = {
    "Ler o canal", "Enviar mensagens", "Excluir mensagens", "Entrar em chamadas"};
#define CHANNEL_MAX 500

static int by_position(const void *a, const void *b)
{
    const json_t *x = *(const json_t *const *)a, *y = *(const json_t *const *)b;
    json_int_t p = json_integer_value(json_object_get(x, "position")),
               q = json_integer_value(json_object_get(y, "position"));
    return p < q ? -1 : p > q ? 1 : strcmp(admin_str(x, "id"), admin_str(y, "id"));
}
static size_t sorted(server_settings *s, const json_t **out)
{
    size_t count = 0;
    const char *id;
    json_t *channel;
    json_object_foreach((json_t *)server_admin_channels(s->model), id, channel)
    {
        (void)id;
        if (count < CHANNEL_MAX)
            out[count++] = channel;
    }
    qsort(out, count, sizeof(*out), by_position);
    return count;
}
static const json_t *selected_channel(server_settings *s)
{
    return *s->selected ? json_object_get(server_admin_channels(s->model), s->selected) : NULL;
}
static int type_index(const char *type)
{
    return !strcmp(type, "voice") ? 1 : !strcmp(type, "category") ? 2 : 0;
}
static void rows(server_settings *s, GArray *out)
{
    admin_row_add(out, ROW_NEW, "+ Novo canal", "", 0, true);
    const json_t *channels[CHANNEL_MAX];
    size_t count = sorted(s, channels);
    for (size_t i = 0; i < count; ++i) {
        char label[256];
        int type = type_index(admin_str(channels[i], "type"));
        snprintf(label, sizeof(label), "%s%s",
                 type == 2   ? "── "
                 : type == 1 ? "♪ "
                             : "# ",
                 admin_str(channels[i], "name"));
        admin_row_add(out, admin_str(channels[i], "id"), label, "", 0, false);
    }
}
static void fill(server_settings *s)
{
    const json_t *channel = selected_channel(s);
    admin_set_text(&s->name, channel ? admin_str(channel, "name") : "");
    admin_set_text(&s->aux, channel ? admin_str(channel, "topic") : "");
    s->name_first = s->aux_first = 0;
    s->choice = channel ? type_index(admin_str(channel, "type")) : 0;
    s->choice2 = 0;
}
/* Roles that have no entry in this channel yet, in catalog order. */
static size_t unlinked(server_settings *s, const json_t *channel, const json_t **out,
                       size_t capacity)
{
    const json_t *roles[4096];
    size_t count = admin_role_list(s, roles, G_N_ELEMENTS(roles)), found = 0;
    const json_t *entries = json_object_get(channel, "permissions");
    for (size_t i = 0; i < count && found < capacity; ++i) {
        bool linked = false;
        size_t j;
        json_t *entry;
        json_array_foreach(entries, j, entry) if (!strcmp(admin_str(entry, "role_id"),
                                                          admin_str(roles[i], "id"))) linked = true;
        if (!linked)
            out[found++] = roles[i];
    }
    return found;
}
static void neighbours(server_settings *s, const json_t *channel, const json_t **above,
                       const json_t **below)
{
    const json_t *channels[CHANNEL_MAX];
    size_t count = sorted(s, channels);
    *above = *below = NULL;
    for (size_t i = 0; i < count; ++i)
        if (channels[i] == channel) {
            *above = i ? channels[i - 1] : NULL;
            *below = i + 1 < count ? channels[i + 1] : NULL;
        }
}
static void fields(server_settings *s, GArray *out)
{
    if (!server_admin_channels(s->model)) {
        admin_note(out, "Carregando canais…");
        return;
    }
    const json_t *channel = selected_channel(s);
    admin_field_add(out, FIELD_HEADING, 0, channel ? "Editar canal" : "Novo canal");
    admin_field *f = admin_field_add(out, FIELD_TEXT, NAME, "Nome");
    f->text = &s->name;
    f->first = &s->name_first;
    if (channel) {
        f = admin_field_add(out, FIELD_INFO, 0, "Tipo");
        snprintf(f->value, sizeof(f->value), "%s", type_labels[s->choice]);
    } else {
        f = admin_field_add(out, FIELD_CHOICE, TYPE, "Tipo");
        snprintf(f->value, sizeof(f->value), "%s", type_labels[s->choice]);
    }
    if (s->choice != 2) {
        f = admin_field_add(out, FIELD_TEXT, TOPIC, "Tópico");
        f->text = &s->aux;
        f->first = &s->aux_first;
        f->max = ADMIN_TOPIC_MAX;
    }
    admin_field_add(out, FIELD_BUTTON, SAVE, channel ? "Salvar canal" : "Criar canal");
    if (!channel)
        return;
    const json_t *above, *below;
    neighbours(s, channel, &above, &below);
    if (above)
        admin_field_add(out, FIELD_BUTTON, UP, "Mover para cima");
    if (below)
        admin_field_add(out, FIELD_BUTTON, DOWN, "Mover para baixo");
    f = admin_field_add(out, FIELD_BUTTON, DELETE, "Excluir canal");
    f->destructive = true;
    snprintf(f->key, sizeof(f->key), "%s", s->selected);
    admin_field_add(out, FIELD_HEADING, 0, "Permissões por cargo");
    admin_note(out, "Cada caixa é salva no servidor ao ser marcada.");
    size_t i;
    json_t *entry;
    json_array_foreach(json_object_get(channel, "permissions"), i, entry)
    {
        const char *role = admin_str(entry, "role_id");
        const json_t *info = json_object_get(server_admin_roles(s->model), role);
        f = admin_field_add(out, FIELD_HEADING, 0,
                            *admin_str(info, "name")         ? admin_str(info, "name")
                            : *admin_str(entry, "role_name") ? admin_str(entry, "role_name")
                                                             : "Cargo");
        const json_t *flags = json_object_get(entry, "permissions");
        for (int k = 0; k < PERMISSION_CHANNEL_COUNT; ++k) {
            f = admin_field_add(out, FIELD_CHECK, PERMISSION, flag_labels[k]);
            f->arg = k;
            f->checked = json_is_true(json_object_get(flags, flag_keys[k]));
            snprintf(f->key, sizeof(f->key), "%s", role);
        }
        f = admin_field_add(out, FIELD_BUTTON, UNLINK, "Remover vínculo");
        f->destructive = true;
        snprintf(f->key, sizeof(f->key), "%s", role);
    }
    const json_t *candidates[4096];
    size_t count = unlinked(s, channel, candidates, G_N_ELEMENTS(candidates));
    if (!count)
        return;
    s->choice2 = (s->choice2 % (int)count + (int)count) % (int)count;
    f = admin_field_add(out, FIELD_CHOICE, ADD_ROLE, "Novo vínculo");
    snprintf(f->value, sizeof(f->value), "%s", admin_str(candidates[s->choice2], "name"));
    f = admin_field_add(out, FIELD_BUTTON, LINK, "Vincular cargo");
    snprintf(f->key, sizeof(f->key), "%s", admin_str(candidates[s->choice2], "id"));
}
static void save(server_settings *s)
{
    static char name[TEXT_UTF8_BYTES], topic[TEXT_UTF8_BYTES];
    if (!admin_text(&s->name, name) || !admin_text(&s->aux, topic)) {
        admin_error(s, "Texto inválido.");
        return;
    }
    if (!admin_check_field(s, NAME, "Nome", name, 0, true) ||
        (s->choice != 2 && !admin_check_field(s, TOPIC, "Tópico", topic, ADMIN_TOPIC_MAX, false)))
        return;
    const json_t *channel = selected_channel(s);
    bool queued;
    if (channel) {
        /* A category has no topic; an unchanged topic is not sent. */
        bool same = !strcmp(topic, admin_str(channel, "topic"));
        queued = server_admin_channel_save(s->model, s->selected, name,
                                           s->choice == 2 || same ? NULL : topic);
    } else
        queued = server_admin_channel_create(s->model, name, types[s->choice],
                                             s->choice == 2 ? NULL : topic);
    if (queued)
        admin_sent(s);
}
static void reorder(server_settings *s, bool up)
{
    const json_t *channel = selected_channel(s), *above, *below;
    if (!channel)
        return;
    neighbours(s, channel, &above, &below);
    const json_t *target = up ? above : below;
    if (!target)
        return;
    int from = (int)json_integer_value(json_object_get(channel, "position")),
        to = (int)json_integer_value(json_object_get(target, "position"));
    if (!server_admin_channel_move(s->model, s->selected, from, to))
        admin_error(s, "Não foi possível mover o canal agora.");
}
static void toggle(server_settings *s, const admin_field *f)
{
    const json_t *channel = selected_channel(s);
    size_t i;
    json_t *entry;
    json_array_foreach(json_object_get(channel, "permissions"), i, entry)
    {
        if (strcmp(admin_str(entry, "role_id"), f->key))
            continue;
        bool flags[PERMISSION_CHANNEL_COUNT];
        const json_t *current = json_object_get(entry, "permissions");
        for (int k = 0; k < PERMISSION_CHANNEL_COUNT; ++k)
            flags[k] = json_is_true(json_object_get(current, flag_keys[k]));
        flags[f->arg] = !flags[f->arg];
        server_admin_channel_role(s->model, s->selected, f->key, flags);
        return;
    }
}
static void activate(server_settings *s, const admin_field *f, int direction)
{
    if (f->id == TYPE) {
        s->choice = (s->choice + (direction < 0 ? 2 : 1)) % 3;
        s->dirty = true;
    } else if (f->id == ADD_ROLE) {
        s->choice2 += direction < 0 ? -1 : 1;
    } else if (f->id == SAVE)
        save(s);
    else if (f->id == UP || f->id == DOWN)
        reorder(s, f->id == UP);
    else if (f->id == DELETE && admin_confirmed(s, f)) {
        if (server_admin_channel_delete(s->model, f->key))
            admin_select(s, ROW_NEW);
    } else if (f->id == PERMISSION && f->arg >= 0 && f->arg < PERMISSION_CHANNEL_COUNT)
        toggle(s, f);
    else if (f->id == UNLINK && admin_confirmed(s, f))
        server_admin_channel_role_remove(s->model, s->selected, f->key);
    else if (f->id == LINK) {
        /* A new entry starts with reading and sending; the boxes adjust it. */
        bool flags[PERMISSION_CHANNEL_COUNT] = {true, true, false, false};
        server_admin_channel_role(s->model, s->selected, f->key, flags);
    }
}
const admin_tab_ops tab_channels = {
    .list = true,
    .empty = "Nenhum canal.",
    .rows = rows,
    .fill = fill,
    .fields = fields,
    .activate = activate,
    .submit = save,
};
