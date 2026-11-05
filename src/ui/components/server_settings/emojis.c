#include "ui/components/server_settings/private.h"
#include "ui/media/message_time.h"
#include <time.h>

/* Emojis: add custom emojis and delete them (author or manage_server). */
enum { NAME, PICK, DELETE, RETRY };

static const json_t *selected_emoji(server_settings *s)
{
    size_t i;
    json_t *emoji;
    json_array_foreach(server_admin_items(s->model, ADMIN_LIST_EMOJIS), i, emoji)
    {
        if (*s->selected && !strcmp(admin_str(emoji, "id"), s->selected))
            return emoji;
    }
    return NULL;
}
static void enter(server_settings *s)
{
    server_admin_load(s->model, ADMIN_LIST_EMOJIS, false);
    /* Previews use the emoji picker's catalog, which holds the pictures. */
    if (admin_previews())
        chat_assets_emojis_request(history_assets(s->model), false);
}
static void rows(server_settings *s, GArray *out)
{
    admin_row_add(out, ROW_NEW, "+ Adicionar emoji", "", 0, true);
    size_t i;
    json_t *emoji;
    json_array_foreach(server_admin_items(s->model, ADMIN_LIST_EMOJIS), i, emoji)
    {
        char label[160];
        snprintf(label, sizeof(label), ":%s:", admin_str(emoji, "name"));
        admin_row_add(out, admin_str(emoji, "id"), label,
                      admin_user_name(s, admin_str(emoji, "created_by"), ""), 0, false);
    }
    if (server_admin_more(s->model, ADMIN_LIST_EMOJIS))
        admin_row_add(out, ROW_MORE,
                      server_admin_loading(s->model, ADMIN_LIST_EMOJIS) ? "Carregando…"
                                                                        : "Carregar mais…",
                      "", 0, true);
}
static void choose(server_settings *s, const admin_row *row)
{
    if (!strcmp(row->id, ROW_MORE))
        server_admin_load(s->model, ADMIN_LIST_EMOJIS, true);
    else
        s->focus = 2;
}
static void fill(server_settings *s)
{
    if (!selected_emoji(s)) {
        admin_set_text(&s->name, "");
        s->name_first = 0;
    }
}
static void fields(server_settings *s, GArray *out)
{
    if (server_admin_failed(s->model, ADMIN_LIST_EMOJIS)) {
        admin_note(out, "Não foi possível carregar os emojis.");
        admin_field_add(out, FIELD_BUTTON, RETRY, "Tentar de novo");
    } else if (!server_admin_items(s->model, ADMIN_LIST_EMOJIS))
        admin_note(out, "Carregando emojis…");
    const json_t *emoji = selected_emoji(s);
    if (!emoji) {
        if (!strcmp(s->selected, ROW_MORE))
            return;
        admin_field_add(out, FIELD_HEADING, 0, "Adicionar emoji");
        admin_field *f = admin_field_add(out, FIELD_TEXT, NAME, "Nome");
        f->text = &s->name;
        f->first = &s->name_first;
        admin_note(out, "Escolha uma imagem GIF, JPEG, PNG ou WEBP. Imagens grandes são reduzidas; "
                        "animações grandes são recusadas.");
        admin_field_add(out, FIELD_BUTTON, PICK, "Escolher imagem e enviar…");
        return;
    }
    char title[160];
    snprintf(title, sizeof(title), ":%s:", admin_str(emoji, "name"));
    admin_field_add(out, FIELD_HEADING, 0, title);
    admin_field *f;
    if (admin_previews()) {
        f = admin_field_add(out, FIELD_EMOJI, 0, title);
        snprintf(f->key, sizeof(f->key), "%s", s->selected);
    }
    f = admin_field_add(out, FIELD_INFO, 0, "Formato");
    snprintf(f->value, sizeof(f->value), "%s", admin_str(emoji, "format"));
    f = admin_field_add(out, FIELD_INFO, 0, "Adicionado em");
    if (!message_time_format(admin_str(emoji, "created_at"), time(NULL), f->value,
                             sizeof(f->value)))
        snprintf(f->value, sizeof(f->value), "—");
    f = admin_field_add(out, FIELD_INFO, 0, "Autor");
    snprintf(f->value, sizeof(f->value), "%s",
             admin_user_name(s, admin_str(emoji, "created_by"), "—"));
    if (server_admin_emoji_deletable(s->model, emoji)) {
        f = admin_field_add(out, FIELD_BUTTON, DELETE, "Excluir emoji");
        f->destructive = true;
        snprintf(f->key, sizeof(f->key), "%s", s->selected);
    } else
        admin_note(out, "Só quem adicionou o emoji ou quem gerencia o servidor pode excluí-lo.");
}
static void pick(server_settings *s)
{
    static char name[TEXT_UTF8_BYTES];
    if (!admin_text(&s->name, name)) {
        admin_error(s, "Texto inválido.");
        return;
    }
    g_strstrip(name);
    const char *problem = server_admin_check_text(name, 0, true);
    if (problem || strlen(name) >= sizeof(s->emoji_name)) {
        admin_error(s, problem ? problem : "Nome longo demais.");
        return;
    }
    if (server_admin_busy(s->model)) {
        admin_error(s, "Aguarde a alteração anterior terminar.");
        return;
    }
    memcpy(s->emoji_name, name, strlen(name) + 1);
    s->pick = SERVER_PICK_EMOJI;
}
static void activate(server_settings *s, const admin_field *f, int direction)
{
    (void)direction;
    if (f->id == PICK)
        pick(s);
    else if (f->id == RETRY)
        server_admin_load(s->model, ADMIN_LIST_EMOJIS, false);
    else if (f->id == DELETE && admin_confirmed(s, f)) {
        if (server_admin_emoji_delete(s->model, f->key))
            admin_select(s, ROW_NEW);
    }
}
const admin_tab_ops tab_emojis = {
    .list = true,
    .empty = "Carregando emojis…",
    .enter = enter,
    .rows = rows,
    .fill = fill,
    .fields = fields,
    .activate = activate,
    .submit = pick,
    .choose = choose,
};
