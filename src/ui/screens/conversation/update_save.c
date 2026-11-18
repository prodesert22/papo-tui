#include "app/update.h"
#include "model/text/translation.h"
#include "ui/screens/conversation/private.h"
#include <glib/gstdio.h>
#include <string.h>
/* Saves the package of a newer version where the user chooses. The destination
 * is asked first; the program then downloads the package into its private
 * temporary folder, checks its size and SHA-256 and only then moves it there.
 * Sobre (Settings) starts this and shows its progress; the flow itself belongs
 * to the screen, which owns the file picker. */
typedef struct {
    gint done;
    bool ok;
    char *source;
} move_state;
typedef enum { SAVE_IDLE, SAVE_DIALOG, SAVE_TRANSFER, SAVE_MOVING } save_stage;
struct update_save {
    save_stage stage;
    char *destination;
    move_state *move;
    GCancellable *cancel;
    char status[1200];
};
static void clear_move(gpointer data)
{
    move_state *state = data;
    g_free(state->source);
}
static void report(view *ui, const char *format, ...) G_GNUC_PRINTF(2, 3);
static void report(view *ui, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    g_vsnprintf(ui->update_save->status, sizeof(ui->update_save->status), format, args);
    va_end(args);
    ui->feedback = ui->update_save->status;
}
static void reset(struct update_save *s)
{
    if (s->move) {
        if (s->cancel)
            g_cancellable_cancel(s->cancel);
        g_rc_box_release_full(s->move, clear_move);
        s->move = NULL;
    }
    g_clear_object(&s->cancel);
    g_clear_pointer(&s->destination, g_free);
    s->stage = SAVE_IDLE;
}
bool conversation_update_save_begin(view *ui)
{
    if (!ui->update_save)
        ui->update_save = g_new0(struct update_save, 1);
    struct update_save *s = ui->update_save;
    /* A download that just ended may not have been noticed yet in this pass. */
    conversation_update_save_poll(ui);
    if (s->stage != SAVE_IDLE || file_picker_open(ui->files) || ui->attachment_task ||
        ui->remove_confirm || !*update_asset_name())
        return false;
    conversation_interactions_close(ui);
    mention_popup_close(ui->mentions);
    emoji_picker_close(ui->emojis);
    ui->navigating = ui->dragging = false;
    s->stage = SAVE_DIALOG;
    file_picker_show_save(ui->files, update_asset_name());
    file_picker_save_title(ui->files, _(" Salvar atualização "));
    return true;
}
static void move_done(GObject *source, GAsyncResult *result, gpointer data)
{
    move_state *state = data;
    GError *error = NULL;
    state->ok = g_file_move_finish(G_FILE(source), result, &error);
    if (!state->ok)
        g_unlink(state->source);
    g_clear_error(&error);
    g_atomic_int_set(&state->done, 1);
    g_rc_box_release_full(state, clear_move);
}
static void begin_move(struct update_save *s, char *temporary)
{
    s->move = g_rc_box_new0(move_state);
    s->move->source = temporary;
    s->cancel = g_cancellable_new();
    GFile *from = g_file_new_for_path(temporary), *to = g_file_new_for_path(s->destination);
    g_rc_box_acquire(s->move);
    g_file_move_async(from, to, G_FILE_COPY_OVERWRITE, G_PRIORITY_DEFAULT, s->cancel, NULL, NULL,
                      move_done, s->move);
    g_object_unref(from);
    g_object_unref(to);
    s->stage = SAVE_MOVING;
}
bool conversation_update_save_poll(view *ui)
{
    struct update_save *s = ui->update_save;
    if (!s || s->stage == SAVE_IDLE)
        return false;
    if (s->stage == SAVE_DIALOG) {
        if (file_picker_open(ui->files))
            return false;
        s->destination = file_picker_take_save(ui->files);
        if (!s->destination) {
            reset(s);
            report(ui, "%s", _("Download da atualização cancelado."));
            return true;
        }
        if (!update_download_start()) {
            reset(s);
            report(ui, "%s", _("Não foi possível iniciar o download da atualização."));
            return true;
        }
        s->stage = SAVE_TRANSFER;
        report(ui, _("Baixando %s…"), update_asset_name());
        return true;
    }
    if (s->stage == SAVE_TRANSFER) {
        update_transfer state = update_download_status();
        if (state == UPDATE_TRANSFER_READY) {
            char *temporary = update_download_take();
            if (!temporary) {
                update_download_finish(NULL);
                reset(s);
                return true;
            }
            begin_move(s, temporary);
            return true;
        }
        if (state == UPDATE_TRANSFER_FAILED) {
            /* Sobre says why; the failure stays there until another attempt. */
            reset(s);
            report(ui, "%s", _("Não foi possível baixar a atualização."));
            return true;
        }
        if (state == UPDATE_TRANSFER_NONE) {
            reset(s);
            report(ui, "%s", _("Download da atualização cancelado."));
            return true;
        }
        return false;
    }
    if (s->stage == SAVE_MOVING && g_atomic_int_get(&s->move->done)) {
        bool ok = s->move->ok;
        update_download_finish(ok ? s->destination : NULL);
        if (ok)
            report(ui, _("Arquivo salvo em %s"), s->destination);
        else
            report(ui, _("Não foi possível gravar em %s. Verifique o acesso e o espaço."),
                   s->destination);
        reset(s);
        return true;
    }
    return false;
}
bool conversation_update_save_busy(const view *ui)
{
    return ui->update_save && ui->update_save->stage != SAVE_IDLE;
}
void conversation_update_save_close(view *ui)
{
    struct update_save *s = ui->update_save;
    if (!s)
        return;
    if (s->stage == SAVE_DIALOG && file_picker_open(ui->files))
        file_picker_close(ui->files);
    /* Without this screen nobody is left to place the file. */
    if (s->stage == SAVE_TRANSFER)
        update_download_cancel();
    if (s->stage == SAVE_MOVING && !g_atomic_int_get(&s->move->done))
        update_download_finish(NULL);
    reset(s);
    if (ui->feedback == s->status)
        ui->feedback = NULL;
    g_free(s);
    ui->update_save = NULL;
}
