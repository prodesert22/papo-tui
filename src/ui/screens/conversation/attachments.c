#include "ui/screens/conversation/private.h"

typedef struct {
    GPtrArray *paths, *files;
    char *text;
    char notice[256];
    bool clipboard;
    gint importing;
} import_job;
static void free_job(gpointer data)
{
    import_job *job = data;
    g_clear_pointer(&job->paths, g_ptr_array_unref);
    g_clear_pointer(&job->files, g_ptr_array_unref);
    g_free(job->text);
    g_free(job);
}
static void import_worker(GTask *task, gpointer source, gpointer data, GCancellable *cancel)
{
    (void)source;
    import_job *job = data;
    GError *error = NULL;
    clipboard_content *content = job->clipboard ? platform_read_clipboard(cancel, &error) : NULL;
    g_atomic_int_set(&job->importing, 1);
    if (content && content->paths)
        job->paths = g_ptr_array_ref(content->paths);
    if (content && content->text)
        job->text = g_strdup(content->text);
    job->files = g_ptr_array_new_with_free_func((GDestroyNotify)attachment_unref);
    if (content && content->image) {
        attachment_file *file =
            attachment_import_bytes(content->image, content->image_name, cancel, &error);
        if (file)
            g_ptr_array_add(job->files, file);
    }
    for (guint i = 0; job->paths && i < job->paths->len && !g_cancellable_is_cancelled(cancel);
         ++i) {
        if (job->files->len >= ATTACHMENT_LIMIT) {
            g_strlcpy(job->notice, "Limite local: 25 anexos por mensagem.", sizeof(job->notice));
            break;
        }
        GError *failure = NULL;
        attachment_file *file =
            attachment_import(g_ptr_array_index(job->paths, i), cancel, &failure);
        if (file)
            g_ptr_array_add(job->files, file);
        else {
            g_strlcpy(job->notice,
                      g_error_matches(failure, G_IO_ERROR, G_IO_ERROR_NO_SPACE)
                          ? "Sem espaço para anexos (limite local: 512 MiB)."
                          : "Um arquivo não pôde ser preparado. Verifique o acesso e selecione "
                            "novamente.",
                      sizeof(job->notice));
        }
        g_clear_error(&failure);
    }
    clipboard_content_free(content);
    if (error) {
        g_task_return_error(task, error);
        return;
    }
    g_task_return_boolean(task, true);
}
const GPtrArray *conversation_attachments(view *ui)
{
    return *ui->edit_message || !conversation_input(ui)
               ? NULL
               : outbox_attachments(send_editor_outbox(ui->editor), ui->editor_channel);
}
static void bind_context(view *ui)
{
    g_strlcpy(ui->attachment_channel, ui->editor_channel, sizeof(ui->attachment_channel));
    ui->attachment_generation =
        auth_context_generation(ui->connections->items[ui->active_tab].auth);
}
static void start_import(view *ui, GPtrArray *paths, bool clipboard)
{
    import_job *job = g_new0(import_job, 1);
    job->paths = paths;
    job->clipboard = clipboard;
    bind_context(ui);
    net_now_ms(&ui->attachment_started);
    ui->attachment_clipboard = clipboard;
    ui->attachment_cancel = g_cancellable_new();
    ui->attachment_task = file_transfer_task_new(ui->attachment_cancel);
    g_task_set_task_data(ui->attachment_task, job, free_job);
    g_task_run_in_thread(ui->attachment_task, import_worker);
    ui->feedback = clipboard ? "Lendo clipboard…" : "Preparando anexos…";
}
void conversation_attachments_close(view *ui)
{
    if (ui->attachment_cancel)
        g_cancellable_cancel(ui->attachment_cancel);
    g_clear_object(&ui->attachment_cancel);
    g_clear_object(&ui->attachment_task);
    file_picker_close(ui->files);
    ui->remove_confirm = false;
    ui->attachment_channel[0] = 0;
    if (ui->attachment_dialog)
        delwin(ui->attachment_dialog);
    ui->attachment_dialog = NULL;
}
void conversation_attachments_open(view *ui, bool clipboard)
{
    if (!conversation_input(ui) || *ui->edit_message || ui->attachment_task ||
        file_picker_open(ui->files) || ui->remove_confirm)
        return;
    conversation_interactions_close(ui);
    mention_popup_close(ui->mentions);
    emoji_picker_close(ui->emojis);
    ui->navigating = ui->dragging = false;
    bind_context(ui);
    if (clipboard)
        start_import(ui, NULL, true);
    else
        file_picker_show(ui->files);
}
bool conversation_attachments_poll(view *ui, auth_client *auth)
{
    /* GTask completions use the default context; all UI application stays here. */
    file_transfer_tasks_poll();
    if (*ui->attachment_channel &&
        (strcmp(ui->attachment_channel, ui->editor_channel) || *ui->edit_message ||
         ui->attachment_generation != auth_context_generation(auth) || !auth_has_session(auth))) {
        conversation_attachments_close(ui);
        return true;
    }
    bool changed = file_picker_poll(ui->files);
    GPtrArray *paths = file_picker_take_files(ui->files);
    if (paths) {
        if (paths->len)
            start_import(ui, paths, false);
        else
            g_ptr_array_unref(paths);
        conversation_set_focus(ui, MESSAGE_INPUT);
        changed = true;
    }
    if (ui->attachment_task) {
        import_job *job = g_task_get_task_data(ui->attachment_task);
        uint64_t now = 0;
        net_now_ms(&now);
        if (ui->attachment_clipboard && !g_atomic_int_get(&job->importing) &&
            now - ui->attachment_started >= 3000)
            g_cancellable_cancel(ui->attachment_cancel);
        if (g_task_get_completed(ui->attachment_task)) {
            GError *error = NULL;
            bool ok = g_task_propagate_boolean(ui->attachment_task, &error);
            ui->feedback = NULL;
            if (ok) {
                for (guint i = 0; i < job->files->len; ++i)
                    if (!outbox_add_attachment(send_editor_outbox(ui->editor), ui->editor_channel,
                                               g_ptr_array_index(job->files, i)))
                        g_strlcpy(job->notice, "Limite local: 25 anexos por mensagem.",
                                  sizeof(job->notice));
                if (job->text) {
                    text_buffer *text = g_new0(text_buffer, 1);
                    if (!text_decode(text, job->text) ||
                        !text_insert_buffer(conversation_input(ui), text))
                        g_strlcpy(job->notice, "Colagem excede o limite; rascunho preservado.",
                                  sizeof(job->notice));
                    else {
                        send_editor_changed(ui->editor);
                        history_typing_activity(ui->model);
                    }
                    g_free(text);
                }
                if (job->notice[0]) {
                    g_strlcpy(ui->attachment_notice, job->notice, sizeof(ui->attachment_notice));
                    ui->feedback = ui->attachment_notice;
                }
            } else {
                g_strlcpy(ui->attachment_notice,
                          g_error_matches(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED)
                              ? error->message
                              : "Não foi possível colar. Use Anexar/F3 ou a colagem do terminal.",
                          sizeof(ui->attachment_notice));
                ui->feedback = ui->attachment_notice;
            }
            g_clear_error(&error);
            g_clear_object(&ui->attachment_task);
            g_clear_object(&ui->attachment_cancel);
            conversation_set_focus(ui, MESSAGE_INPUT);
            changed = true;
        }
    }
    const GPtrArray *files = conversation_attachments(ui);
    int count = files ? (int)files->len : 0;
    if (count != ui->attachment_count) {
        ui->attachment_count = count;
        ui->attachment_cursor = CLAMP(ui->attachment_cursor, 0, MAX(0, count - 1));
        if (!count && ui->focus == ATTACHMENT_ROW)
            conversation_set_focus(ui, MESSAGE_INPUT);
        changed = true;
    }
    return changed;
}
static void confirm_remove(view *ui)
{
    const GPtrArray *files = conversation_attachments(ui);
    if (!files || !files->len)
        return;
    ui->attachment_cursor = CLAMP(ui->attachment_cursor, 0, (int)files->len - 1);
    ui->remove_id = attachment_id(g_ptr_array_index(files, ui->attachment_cursor));
    ui->remove_confirm = true;
    ui->remove_accept = false;
    bind_context(ui);
}
bool conversation_attachments_key(view *ui, int type, uint32_t key)
{
    if (file_picker_key(ui->files, type, key))
        return true;
    if (ui->attachment_task && type == OK && key == 27) {
        conversation_attachments_close(ui);
        ui->feedback = "Preparação cancelada; rascunho preservado.";
        return true;
    }
    bool enter =
        (type == OK && (key == '\n' || key == '\r')) || (type == KEY_CODE_YES && key == KEY_ENTER);
    if (ui->remove_confirm) {
        if (type == OK && key == 27)
            ui->remove_confirm = false;
        else if ((type == OK && key == '\t') ||
                 (type == KEY_CODE_YES && (key == KEY_BTAB || key == KEY_LEFT || key == KEY_RIGHT)))
            ui->remove_accept = !ui->remove_accept;
        else if (enter) {
            if (ui->remove_accept)
                outbox_remove_attachment(send_editor_outbox(ui->editor), ui->editor_channel,
                                         ui->remove_id);
            ui->remove_confirm = false;
        }
        return true;
    }
    if (ui->focus == ATTACH_BUTTON) {
        if (enter || (type == OK && key == ' ')) {
            conversation_attachments_open(ui, false);
            return true;
        }
    }
    if (ui->focus == ATTACHMENT_ROW) {
        if (enter) {
            confirm_remove(ui);
            return true;
        }
        if (type == KEY_CODE_YES && (key == KEY_LEFT || key == KEY_RIGHT)) {
            ui->attachment_cursor = CLAMP(ui->attachment_cursor + (key == KEY_RIGHT ? 1 : -1), 0,
                                          MAX(0, ui->attachment_count - 1));
            return true;
        }
    }
    return false;
}
bool conversation_attachments_mouse(view *ui, const MEVENT *event)
{
    if (file_picker_mouse(ui->files, event))
        return true;
    if (ui->remove_confirm) {
        if (ui->attachment_dialog && event->bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED)) {
            int y = event->y - getbegy(ui->attachment_dialog),
                x = event->x - getbegx(ui->attachment_dialog);
            if (y == 4 && x >= 2 && x < 14)
                ui->remove_confirm = false;
            else if (y == 4 && x >= getmaxx(ui->attachment_dialog) - 14 &&
                     x < getmaxx(ui->attachment_dialog) - 2) {
                ui->remove_accept = true;
                conversation_attachments_key(ui, OK, '\n');
            }
        }
        return true;
    }
    if (!ui->body || !(event->bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED)))
        return false;
    int y = event->y - getbegy(ui->body), x = event->x - getbegx(ui->body);
    if (y == conversation_input_top(ui) && x >= 1 && x < 11 && !*ui->edit_message) {
        conversation_set_focus(ui, ATTACH_BUTTON);
        conversation_attachments_open(ui, false);
        return true;
    }
    const GPtrArray *files = conversation_attachments(ui);
    if (files && files->len && ui->attachment_bar &&
        y == ui->attachment_top + (ui->attachment_rows == 3 ? 1 : 0)) {
        for (guint i = 0; i < files->len; ++i)
            if (conversation_hit(ui->attachment_hits[i], x)) {
                ui->attachment_cursor = (int)i;
                conversation_set_focus(ui, ATTACHMENT_ROW);
                if (conversation_hit(ui->attachment_remove_hits[i], x))
                    confirm_remove(ui);
                return true;
            }
    }
    return false;
}
void conversation_attachments_draw_row(view *ui)
{
    const GPtrArray *files = conversation_attachments(ui);
    memset(ui->attachment_hits, 0, sizeof(ui->attachment_hits));
    memset(ui->attachment_remove_hits, 0, sizeof(ui->attachment_remove_hits));
    if (!ui->attachment_bar || !files || !files->len)
        return;
    wbkgd(ui->attachment_bar, COLOR_PAIR(THEME_PANEL));
    werase(ui->attachment_bar);
    if (ui->attachment_rows == 3) {
        wattrset(ui->attachment_bar, COLOR_PAIR(THEME_BORDER));
        box(ui->attachment_bar, 0, 0);
        text_label(ui->attachment_bar, 0, 2, getmaxx(ui->attachment_bar) - 4, " Anexos ");
    }
    int available = getmaxx(ui->attachment_bar) - 2;
    int close_width = text_label(NULL, 0, 0, 8, " 🗙]");
    int glyph_width = text_label(NULL, 0, 0, 4, "🗙");
    int widths[ATTACHMENT_LIMIT];
    for (guint i = 0; i < files->len; ++i)
        widths[i] =
            MIN(available, MIN(40, text_label(NULL, 0, 0, available,
                                              attachment_name(g_ptr_array_index(files, i))) +
                                       1 + close_width));
    int chosen = CLAMP(ui->attachment_cursor, 0, (int)files->len - 1);
    ui->attachment_first = CLAMP(ui->attachment_first, 0, chosen);
    for (;;) {
        int used = 0;
        for (int i = ui->attachment_first; i <= chosen; ++i)
            used += widths[i] + 1;
        if (used <= available || ui->attachment_first == chosen)
            break;
        ++ui->attachment_first;
    }
    int x = 1, row = ui->attachment_rows == 3 ? 1 : 0;
    for (guint i = (guint)ui->attachment_first; i < files->len && x + widths[i] <= available + 1;
         ++i) {
        int width = widths[i];
        wattrset(ui->attachment_bar,
                 COLOR_PAIR(ui->focus == ATTACHMENT_ROW && (int)i == chosen ? THEME_FOCUSED
                                                                            : THEME_PANEL_MUTED));
        mvwaddch(ui->attachment_bar, row, x, '[');
        text_label(ui->attachment_bar, row, x + 1, width - 1 - close_width,
                   attachment_name(g_ptr_array_index(files, i)));
        text_label(ui->attachment_bar, row, x + width - close_width, close_width, " 🗙]");
        ui->attachment_hits[i] = (click_range){x, width};
        ui->attachment_remove_hits[i] = (click_range){x + width - close_width + 1, glyph_width};
        x += width + 1;
    }
    wnoutrefresh(ui->attachment_bar);
}
WINDOW *conversation_attachments_draw_dialog(view *ui)
{
    if (!ui->remove_confirm) {
        if (ui->attachment_dialog)
            delwin(ui->attachment_dialog);
        ui->attachment_dialog = NULL;
        return file_picker_draw(ui->files);
    }
    if (COLS < 44 || LINES < 14)
        return NULL;
    int width = MIN(COLS - 4, 64), y = (LINES - 7) / 2, x = (COLS - width) / 2;
    if (ui->attachment_dialog &&
        (getmaxx(ui->attachment_dialog) != width || getbegy(ui->attachment_dialog) != y ||
         getbegx(ui->attachment_dialog) != x)) {
        delwin(ui->attachment_dialog);
        ui->attachment_dialog = NULL;
    }
    if (!ui->attachment_dialog)
        ui->attachment_dialog = newwin(7, width, y, x);
    WINDOW *w = ui->attachment_dialog;
    if (!w)
        return NULL;
    wbkgd(w, COLOR_PAIR(THEME_PANEL));
    werase(w);
    wattrset(w, COLOR_PAIR(THEME_BORDER));
    box(w, 0, 0);
    wattrset(w, COLOR_PAIR(THEME_PANEL_ACCENT) | A_BOLD);
    text_label(w, 1, 2, width - 4, "Remover anexo?");
    wattrset(w, COLOR_PAIR(THEME_PROFILE_TEXT));
    const GPtrArray *files = conversation_attachments(ui);
    for (guint i = 0; files && i < files->len; ++i) {
        attachment_file *file = g_ptr_array_index(files, i);
        if (attachment_id(file) == ui->remove_id)
            text_label(w, 2, 2, width - 4, attachment_name(file));
    }
    button_draw(w, 4, 2, 12, "Cancelar", !ui->remove_accept ? BUTTON_FOCUSED : BUTTON_NORMAL,
                BUTTON_PANEL);
    button_draw(w, 4, width - 14, 12, "Remover", ui->remove_accept ? BUTTON_FOCUSED : BUTTON_NORMAL,
                BUTTON_PANEL);
    wnoutrefresh(w);
    return w;
}
