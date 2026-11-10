#include "platform/clipboard.h"
#include "ui/components/server_settings/private.h"
#include "ui/media/message_time.h"
#include <time.h>

/* Webhooks: create, rename (nickname), change the avatar of and revoke the
 * webhooks of the server (manage_webhooks). The access token of a new webhook
 * is shown here once and only while its form stays open. */
enum { CHANNEL, NAME, CREATE, NICKNAME, SAVE, AVATAR, AVATAR_REMOVE, REVOKE, COPY_URL, COPY_TOKEN, RETRY };
#define CHANNEL_CHOICES 4096

static const json_t *selected_webhook(server_settings *s)
{
    size_t i;
    json_t *webhook;
    json_array_foreach(server_admin_items(s->model, ADMIN_LIST_WEBHOOKS), i, webhook)
    {
        if (*s->selected && !strcmp(admin_str(webhook, "id"), s->selected))
            return webhook;
    }
    return NULL;
}
static const char *channel_name(server_settings *s, const char *id)
{
    const char *name = admin_str(json_object_get(server_admin_channels(s->model), id), "name");
    return *name ? name : "canal indisponível";
}
static int by_position(const void *x, const void *y)
{
    json_int_t a = json_integer_value(json_object_get(*(json_t *const *)x, "position")),
               b = json_integer_value(json_object_get(*(json_t *const *)y, "position"));
    return a < b ? -1 : a > b;
}
/* Channels that take messages, in the server's order: the contract allows
 * webhooks only in text and voice channels. */
static size_t channel_choices(server_settings *s, json_t **out, size_t capacity)
{
    size_t count = 0;
    const char *id;
    json_t *channel;
    json_object_foreach((json_t *)server_admin_channels(s->model), id, channel)
    {
        const char *type = admin_str(channel, "type");
        if (count < capacity && (!strcmp(type, "text") || !strcmp(type, "voice")))
            out[count++] = channel;
    }
    qsort(out, count, sizeof(*out), by_position);
    return count;
}
static void enter(server_settings *s)
{
    server_admin_load(s->model, ADMIN_LIST_WEBHOOKS, false);
}
static void rows(server_settings *s, GArray *out)
{
    admin_row_add(out, ROW_NEW, "+ Novo webhook", "", 0, true);
    size_t i;
    json_t *webhook;
    json_array_foreach(server_admin_items(s->model, ADMIN_LIST_WEBHOOKS), i, webhook)
    {
        const char *nickname = admin_str(webhook, "display_name");
        char detail[160];
        snprintf(detail, sizeof(detail), "#%s",
                 channel_name(s, admin_str(webhook, "channel_id")));
        admin_row_add(out, admin_str(webhook, "id"),
                      *nickname ? nickname : admin_str(webhook, "name"), detail, 0, false);
    }
}
static void choose(server_settings *s, const admin_row *row)
{
    (void)row;
    s->focus = 2;
}
static void fill(server_settings *s)
{
    /* A webhook created a moment ago is opened as soon as the list has it, so
     * its token is in front of the account. */
    const char *created = server_admin_webhook_created(s->model);
    if (*created && !*s->selected) {
        size_t i;
        json_t *webhook;
        json_array_foreach(server_admin_items(s->model, ADMIN_LIST_WEBHOOKS), i, webhook) if (
            !strcmp(admin_str(webhook, "id"), created))
        {
            snprintf(s->selected, sizeof(s->selected), "%s", created);
            server_admin_webhook_created_clear(s->model);
            break;
        }
    }
    /* The token leaves with the form that showed it. */
    if (!server_admin_webhook_held(s->model, s->selected) &&
        !*server_admin_webhook_created(s->model))
        server_admin_webhook_forget_token(s->model);
    const json_t *webhook = selected_webhook(s);
    admin_set_text(&s->name, "");
    admin_set_text(&s->aux, webhook ? admin_str(webhook, "display_name") : "");
    s->name_first = s->aux_first = 0;
    s->choice = 0;
}
static void new_fields(server_settings *s, GArray *out)
{
    admin_field_add(out, FIELD_HEADING, 0, "Novo webhook");
    json_t *channels[CHANNEL_CHOICES];
    size_t count = channel_choices(s, channels, CHANNEL_CHOICES);
    if (!count) {
        admin_note(out, server_admin_channels(s->model)
                            ? "O servidor não tem canal de texto ou de voz."
                            : "Carregando canais…");
        return;
    }
    if (s->choice < 0 || (size_t)s->choice >= count)
        s->choice = 0;
    admin_field *f = admin_field_add(out, FIELD_CHOICE, CHANNEL, "Canal");
    snprintf(f->value, sizeof(f->value), "#%s", admin_str(channels[s->choice], "name"));
    f = admin_field_add(out, FIELD_TEXT, NAME, "Nome");
    f->text = &s->name;
    f->first = &s->name_first;
    admin_note(out, "Até 80 caracteres. É o nome que aparece como autor das mensagens enviadas "
                    "pelo webhook, até você definir um apelido.");
    admin_field_add(out, FIELD_BUTTON, CREATE, "Criar webhook");
}
/* Address the webhook receives messages on, from this instance's base address. */
static void send_url(server_settings *s, const char *id, char url[640])
{
    const char *base = server_admin_instance_url(s->model);
    int length = (int)strlen(base) - (g_str_has_suffix(base, "/") ? 1 : 0);
    snprintf(url, 640, "%.*s/webhooks/%s/messages", length, base, id);
}
static void token_fields(server_settings *s, GArray *out, const char *id, const char *token)
{
    admin_field_add(out, FIELD_HEADING, 0, "Token de acesso");
    admin_note(out, "O token aparece só agora. Copie-o antes de sair desta tela: depois não há "
                    "como ver de novo, apenas revogar o webhook e criar outro. Quem tem o token "
                    "envia mensagens neste canal.");
    /* Notes wrap, so the whole address and token stay readable. */
    char url[640];
    send_url(s, id, url);
    admin_note(out, "URL de envio:");
    admin_note(out, url);
    admin_note(out, "Token:");
    admin_note(out, token);
    admin_field_add(out, FIELD_BUTTON, COPY_URL, "Copiar URL");
    admin_field_add(out, FIELD_BUTTON, COPY_TOKEN, "Copiar token");
}
static void fields(server_settings *s, GArray *out)
{
    if (server_admin_failed(s->model, ADMIN_LIST_WEBHOOKS)) {
        admin_note(out, "Não foi possível carregar os webhooks.");
        admin_field_add(out, FIELD_BUTTON, RETRY, "Tentar de novo");
    } else if (!server_admin_items(s->model, ADMIN_LIST_WEBHOOKS))
        admin_note(out, "Carregando webhooks…");
    const json_t *webhook = selected_webhook(s);
    if (!webhook) {
        new_fields(s, out);
        return;
    }
    const char *id = admin_str(webhook, "id"), *nickname = admin_str(webhook, "display_name");
    admin_field_add(out, FIELD_HEADING, 0, *nickname ? nickname : admin_str(webhook, "name"));
    admin_field *f = admin_field_add(out, FIELD_INFO, 0, "Canal");
    snprintf(f->value, sizeof(f->value), "#%s", channel_name(s, admin_str(webhook, "channel_id")));
    f = admin_field_add(out, FIELD_INFO, 0, "Nome");
    snprintf(f->value, sizeof(f->value), "%s", admin_str(webhook, "name"));
    f = admin_field_add(out, FIELD_INFO, 0, "Criado por");
    snprintf(f->value, sizeof(f->value), "%s",
             admin_user_name(s, admin_str(webhook, "created_by"), "—"));
    f = admin_field_add(out, FIELD_INFO, 0, "Criado em");
    if (!message_time_format(admin_str(webhook, "created_at"), time(NULL), f->value,
                             sizeof(f->value)))
        snprintf(f->value, sizeof(f->value), "—");
    const char *token = server_admin_webhook_token(s->model, id);
    if (*token)
        token_fields(s, out, id, token);
    else if (server_admin_webhook_token_lost(s->model, id))
        admin_note(out, "O servidor não enviou o token deste webhook. Revogue-o e crie outro.");
    admin_field_add(out, FIELD_HEADING, 0, "Identidade");
    f = admin_field_add(out, FIELD_TEXT, NICKNAME, "Apelido");
    f->text = &s->aux;
    f->first = &s->aux_first;
    admin_note(out, "Até 100 caracteres. Vazio usa o nome do webhook.");
    admin_field_add(out, FIELD_BUTTON, SAVE, "Salvar apelido");
    admin_note(out, "Imagem GIF, JPEG, PNG ou WEBP. Imagens grandes são reduzidas; o servidor "
                    "recusa avatares acima de 2 MB ou 512 pixels.");
    admin_field_add(out, FIELD_BUTTON, AVATAR, "Escolher avatar e enviar…");
    f = admin_field_add(out, FIELD_BUTTON, AVATAR_REMOVE, "Remover avatar");
    f->destructive = true;
    snprintf(f->key, sizeof(f->key), "%s", id);
    f = admin_field_add(out, FIELD_BUTTON, REVOKE, "Revogar webhook");
    f->destructive = true;
    snprintf(f->key, sizeof(f->key), "%s", id);
    admin_note(out, "Revogar impede novos envios com o token. As mensagens já enviadas ficam.");
}
static void create(server_settings *s)
{
    static char name[TEXT_UTF8_BYTES];
    if (!admin_text(&s->name, name)) {
        admin_error(s, "Texto inválido.");
        return;
    }
    g_strstrip(name);
    json_t *channels[CHANNEL_CHOICES];
    size_t count = channel_choices(s, channels, CHANNEL_CHOICES);
    if (!count || s->choice < 0 || (size_t)s->choice >= count) {
        admin_error(s, "Escolha um canal de texto ou de voz.");
        return;
    }
    if (!admin_check_field(s, NAME, "Nome", name, ADMIN_WEBHOOK_NAME_MAX, true))
        return;
    if (server_admin_webhook_create(s->model, admin_str(channels[s->choice], "id"), name))
        admin_sent(s);
}
static void save(server_settings *s)
{
    static char nickname[TEXT_UTF8_BYTES];
    if (!selected_webhook(s)) {
        create(s);
        return;
    }
    if (!admin_text(&s->aux, nickname)) {
        admin_error(s, "Texto inválido.");
        return;
    }
    g_strstrip(nickname);
    if (!admin_check_field(s, NICKNAME, "Apelido", nickname, ADMIN_WEBHOOK_NICKNAME_MAX, false))
        return;
    if (server_admin_webhook_nickname(s->model, s->selected, nickname))
        admin_sent(s);
}
static void copy(server_settings *s, const char *text, const char *done)
{
    if (*text && clipboard_copy_text(text))
        snprintf(s->feedback, sizeof(s->feedback), "%s", done);
    else
        admin_error(s, "Não foi possível copiar agora.");
}
static void activate(server_settings *s, const admin_field *f, int direction)
{
    if (f->id == CHANNEL) {
        json_t *channels[CHANNEL_CHOICES];
        int count = (int)channel_choices(s, channels, CHANNEL_CHOICES);
        if (count) {
            s->choice = ((s->choice + (direction < 0 ? -1 : 1)) % count + count) % count;
            s->dirty = true;
        }
    } else if (f->id == CREATE)
        create(s);
    else if (f->id == SAVE)
        save(s);
    else if (f->id == AVATAR) {
        if (server_admin_busy(s->model)) {
            admin_error(s, "Aguarde a alteração anterior terminar.");
            return;
        }
        snprintf(s->webhook_pick, sizeof(s->webhook_pick), "%.36s", s->selected);
        s->pick = SERVER_PICK_WEBHOOK;
    } else if (f->id == AVATAR_REMOVE) {
        if (admin_confirmed(s, f) && !server_admin_webhook_avatar_remove(s->model, f->key))
            admin_error(s, "Não foi possível remover o avatar agora.");
    } else if (f->id == REVOKE && admin_confirmed(s, f)) {
        if (server_admin_webhook_revoke(s->model, f->key))
            admin_select(s, ROW_NEW);
    } else if (f->id == COPY_URL) {
        char url[640];
        send_url(s, s->selected, url);
        copy(s, url, "URL copiada.");
    } else if (f->id == COPY_TOKEN)
        copy(s, server_admin_webhook_token(s->model, s->selected), "Token copiado.");
    else if (f->id == RETRY)
        server_admin_load(s->model, ADMIN_LIST_WEBHOOKS, false);
}
const admin_tab_ops tab_webhooks = {
    .list = true,
    .empty = "Carregando webhooks…",
    .enter = enter,
    .rows = rows,
    .fill = fill,
    .fields = fields,
    .activate = activate,
    .submit = save,
    .choose = choose,
};
