#include "model/text/translation.h"
#include "platform/file_transfer.h"
#include "ui/input/profile_commands.h"
#include "ui/media/profile_image.h"
#include "ui/screens/conversation/private.h"

/* Changing the signed-in user's own profile from the conversation screen:
 * avatar and banner files picked here are prepared in a worker, and the
 * editor's profile commands go to the active server's model. */
typedef struct {
    char *path;
    int side;
    profile_image_result result;
} image_job;
static void free_job(gpointer data)
{
    image_job *job = data;
    g_free(job->path);
    g_free(job->result.base64);
    g_free(job);
}
static void image_worker(GTask *task, gpointer source, gpointer data, GCancellable *cancel)
{
    (void)source;
    image_job *job = data;
    g_task_return_boolean(task, profile_image_prepare(job->path, job->side, &job->result, cancel));
}
/* Follows the model's outcome in the footer for the command just given. */
static void watch(view *ui, const char *message)
{
    ui->profile_watch = ui->model;
    ui->profile_revision = history_profile_revision(ui->model);
    ui->profile_seen[0] = 0;
    g_strlcpy(ui->profile_notice, message, sizeof(ui->profile_notice));
    ui->feedback = ui->profile_notice;
}
void conversation_profile_close(view *ui)
{
    if (ui->profile_cancel)
        g_cancellable_cancel(ui->profile_cancel);
    g_clear_object(&ui->profile_cancel);
    g_clear_object(&ui->profile_task);
    ui->profile_pick = 0;
    ui->profile_watch = NULL;
}
void conversation_profile_pick(view *ui, profile_image kind)
{
    if (ui->profile_task || file_picker_open(ui->files) || ui->attachment_task) {
        ui->feedback = _("Aguarde a imagem ou os anexos em preparação.");
        return;
    }
    conversation_interactions_close(ui);
    mention_popup_close(ui->mentions);
    emoji_picker_close(ui->emojis);
    ui->profile_pick = kind == PROFILE_AVATAR ? 1 : 2;
    file_picker_show_image(ui->files);
}
void conversation_server_image_pick(view *ui, server_settings_pick kind, const char *name)
{
    if (ui->profile_task || file_picker_open(ui->files) || ui->attachment_task) {
        snprintf(ui->server_settings.error, sizeof(ui->server_settings.error), "%s",
                 _("Aguarde a imagem ou os anexos em preparação."));
        return;
    }
    /* The emoji's name or the webhook's ID the image is for. */
    g_strlcpy(ui->pick_emoji, kind != SERVER_PICK_ICON && name ? name : "",
              sizeof(ui->pick_emoji));
    ui->profile_pick = kind == SERVER_PICK_WEBHOOK ? 5 : kind == SERVER_PICK_EMOJI ? 4 : 3;
    file_picker_show_image(ui->files);
}
bool conversation_profile_picked(view *ui, GPtrArray *paths)
{
    if (!ui->profile_pick)
        return false;
    int pick = ui->profile_pick;
    profile_image kind = pick == 1 ? PROFILE_AVATAR : PROFILE_BANNER;
    ui->profile_pick = 0;
    if (!paths->len) {
        g_ptr_array_unref(paths);
        return true;
    }
    image_job *job = g_new0(image_job, 1);
    job->path = g_strdup(g_ptr_array_index(paths, 0));
    /* The server icon and emojis share the avatar's 512 px limit. */
    job->side = pick == 2 ? PROFILE_BANNER_SIDE : PROFILE_AVATAR_SIDE;
    g_ptr_array_unref(paths);
    ui->profile_server = pick == 3 ? 1 : pick == 4 ? 2 : pick == 5 ? 3 : 0;
    ui->profile_kind = kind;
    ui->profile_target = ui->model;
    ui->profile_generation = auth_context_generation(ui->connections->items[ui->active_tab].auth);
    ui->profile_cancel = g_cancellable_new();
    ui->profile_task = file_transfer_task_new(ui->profile_cancel);
    g_task_set_task_data(ui->profile_task, job, free_job);
    g_task_run_in_thread(ui->profile_task, image_worker);
    watch(ui, ui->profile_server == 1   ? _("Preparando o ícone…")
              : ui->profile_server == 2 ? _("Preparando o emoji…")
              : ui->profile_server == 3 ? _("Preparando o avatar do webhook…")
              : kind == PROFILE_AVATAR  ? _("Preparando o avatar…")
                                        : _("Preparando o banner…"));
    return true;
}
/* The server icon, an emoji or a webhook's avatar is handed to the
 * administration model; the modal shows its outcome. */
static void server_image_ready(view *ui, char *data, const char *format, bool reduced)
{
    bool emoji = ui->profile_server == 2, webhook = ui->profile_server == 3;
    bool queued = webhook ? server_admin_webhook_avatar(ui->model, ui->pick_emoji, data, format)
                  : emoji ? server_admin_emoji_create(ui->model, ui->pick_emoji, data, format)
                          : server_admin_set_icon(ui->model, data, format);
    if (queued && ui->server_settings.open && emoji) {
        /* The name stays until the server confirms the emoji. */
        ui->server_settings.awaiting = ui->server_settings.dirty = true;
    }
    watch(ui, !queued   ? _("Não foi possível enviar a imagem agora.")
              : webhook ? (reduced ? _("Enviando avatar reduzido…") : _("Enviando avatar…"))
              : emoji   ? (reduced ? _("Enviando emoji reduzido…") : _("Enviando emoji…"))
                        : (reduced ? _("Enviando ícone reduzido…") : _("Enviando ícone…")));
}
bool conversation_profile_poll(view *ui)
{
    bool changed = false;
    if (ui->profile_task && g_task_get_completed(ui->profile_task)) {
        image_job *job = g_task_get_task_data(ui->profile_task);
        bool ok = g_task_propagate_boolean(ui->profile_task, NULL);
        auth_client *auth = ui->connections->items[ui->active_tab].auth;
        /* The image belongs to the server it was picked for. */
        if (ui->profile_target != ui->model ||
            ui->profile_generation != auth_context_generation(auth))
            watch(ui, _("Imagem descartada: o servidor ou a sessão mudou."));
        else if (!ok)
            watch(ui,
                  *job->result.error ? job->result.error : _("Não foi possível usar a imagem."));
        else if (ui->profile_server) {
            char *data = job->result.base64;
            job->result.base64 = NULL;
            server_image_ready(ui, data, job->result.format, job->result.reduced);
        } else {
            char *data = job->result.base64;
            job->result.base64 = NULL;
            bool queued =
                history_profile_set_image(ui->model, ui->profile_kind, data, job->result.format);
            watch(ui, !queued ? _("Não foi possível enviar a imagem agora.")
                      : ui->profile_kind == PROFILE_AVATAR
                          ? (job->result.reduced ? _("Enviando avatar reduzido…")
                                                 : _("Enviando avatar…"))
                          : (job->result.reduced ? _("Enviando banner reduzido…")
                                                 : _("Enviando banner…")));
        }
        g_clear_object(&ui->profile_task);
        g_clear_object(&ui->profile_cancel);
        ui->profile_server = 0;
        changed = true;
    }
    if (ui->profile_watch && ui->profile_watch == ui->model &&
        history_profile_revision(ui->model) != ui->profile_revision) {
        ui->profile_revision = history_profile_revision(ui->model);
        const char *notice = history_profile_notice(ui->model);
        /* A later reload repeats the same notice; it must not hide newer
         * footer text such as a command's usage. */
        if (*notice && strcmp(notice, ui->profile_seen)) {
            g_strlcpy(ui->profile_seen, notice, sizeof(ui->profile_seen));
            g_strlcpy(ui->profile_notice, notice, sizeof(ui->profile_notice));
            ui->feedback = ui->profile_notice;
            changed = true;
        }
    } else if (ui->profile_watch && ui->profile_watch != ui->model)
        ui->profile_watch = NULL;
    return changed;
}
static void clear_draft(view *ui)
{
    text_clear(conversation_input(ui));
    send_editor_changed(ui->editor);
}
bool conversation_profile_command(view *ui)
{
    text_buffer *input = conversation_input(ui);
    static char text[TEXT_UTF8_BYTES];
    if (!input || !input->length || input->data[0] != '/' || !text_encode(input, text))
        return false;
    static profile_command command;
    profile_command_parse(text, &command);
    if (command.kind == PROFILE_COMMAND_NONE)
        return false;
    if (command.kind == PROFILE_COMMAND_LITERAL) {
        /* "//text" is sent as "/text". */
        memmove(input->data, input->data + 1, input->length * sizeof(input->data[0]));
        --input->length;
        input->cursor = input->cursor ? input->cursor - 1 : 0;
        text_unselect(input);
        send_editor_changed(ui->editor);
        return false;
    }
    if (command.kind == PROFILE_COMMAND_USAGE) {
        send_editor_refuse(ui->editor, true);
        ui->feedback = command.usage;
        return true;
    }
    if (command.kind == PROFILE_COMMAND_AVATAR || command.kind == PROFILE_COMMAND_BANNER) {
        clear_draft(ui);
        conversation_profile_pick(ui, command.kind == PROFILE_COMMAND_AVATAR ? PROFILE_AVATAR
                                                                             : PROFILE_BANNER);
        return true;
    }
    const char *problem = NULL;
    bool queued = false;
    if (command.kind == PROFILE_COMMAND_NICK || command.kind == PROFILE_COMMAND_BIO) {
        bool nick = command.kind == PROFILE_COMMAND_NICK;
        problem = history_profile_check(nick ? "nickname" : "description", command.argument);
        if (!problem)
            queued = nick ? history_profile_save(ui->model, command.argument, NULL, NULL, NULL)
                          : history_profile_save(ui->model, NULL, NULL, command.argument, NULL);
    } else if (command.kind == PROFILE_COMMAND_STATUS)
        queued = history_profile_set_status(ui->model, *command.status ? command.status : NULL);
    else
        queued = history_profile_remove_banner(ui->model);
    if (problem) {
        g_strlcpy(ui->profile_notice, problem, sizeof(ui->profile_notice));
        ui->feedback = ui->profile_notice;
        return true;
    }
    if (!queued) {
        const char *notice = history_profile_notice(ui->model);
        watch(ui, *notice ? notice : _("Não foi possível alterar o perfil agora."));
        return true;
    }
    clear_draft(ui);
    watch(ui, command.kind == PROFILE_COMMAND_NICK
                  ? (*command.argument ? _("Alterando o apelido…") : _("Removendo o apelido…"))
              : command.kind == PROFILE_COMMAND_BIO    ? _("Alterando a descrição…")
              : command.kind == PROFILE_COMMAND_STATUS ? _("Alterando o status…")
                                                       : _("Removendo o banner…"));
    return true;
}
