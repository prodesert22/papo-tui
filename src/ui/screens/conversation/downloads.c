#include "net/http/download.h"
#include "ui/screens/conversation/private.h"
#include <glib/gstdio.h>
#include <string.h>

/* Saves message attachments to disk. Each file asks for its destination first
 * (native dialog, or the terminal modal when none exists); the bytes then
 * download into a private temporary file and are moved to the chosen path. */
typedef struct {
    char id[37];
    char *name, *dest;
    int64_t size;
} pending;
typedef struct {
    gint done;
    bool ok;
    char *source;
} move_state;
typedef enum { DOWNLOAD_IDLE, DOWNLOAD_DIALOG, DOWNLOAD_TRANSFER, DOWNLOAD_MOVING } download_stage;
struct downloads {
    GPtrArray *queue; /* pending; the head is the file in progress. */
    download_stage stage;
    media_download *transfer;
    size_t tab;
    uint64_t generation;
    GCancellable *cancel;
    move_state *move;
    int percent;
    char status[320];
};

static void free_pending(gpointer data)
{
    pending *item = data;
    g_free(item->name);
    g_free(item->dest);
    g_free(item);
}
static void clear_move(gpointer data)
{
    move_state *state = data;
    g_free(state->source);
}
/* The suggested name comes from remote data: keep one path component, drop
 * control characters and the characters file systems refuse. */
static char *safe_name(const char *remote)
{
    char *base = g_path_get_basename(remote && *remote ? remote : "arquivo");
    GString *clean = g_string_new(NULL);
    for (const char *p = base; *p; p = g_utf8_next_char(p)) {
        gunichar c = g_utf8_get_char(p);
        if (c < 0x20 || c == 0x7f || (c >= 0x80 && c < 0xa0) || strchr("/\\:*?\"<>|", (int)c))
            g_string_append_c(clean, '_');
        else
            g_string_append_unichar(clean, c);
    }
    g_free(base);
    char *name = g_strstrip(g_string_free(clean, false));
    if (!*name || !strcmp(name, ".") || !strcmp(name, "..")) {
        g_free(name);
        return g_strdup("arquivo");
    }
    if (strlen(name) > 200) {
        char *end = g_utf8_find_prev_char(name, name + 200);
        char *cut = end ? g_utf8_next_char(end) : name + 200;
        *cut = 0;
    }
    return name;
}
static void set_status(view *ui, const char *format, ...) G_GNUC_PRINTF(2, 3);
static void set_status(view *ui, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    g_vsnprintf(ui->downloads->status, sizeof(ui->downloads->status), format, args);
    va_end(args);
    ui->feedback = ui->downloads->status;
}
static bool ensure(view *ui)
{
    if (!ui->downloads)
        ui->downloads = g_new0(struct downloads, 1);
    if (!ui->downloads->queue)
        ui->downloads->queue = g_ptr_array_new_with_free_func(free_pending);
    return ui->downloads->queue != NULL;
}
static auth_client *tab_auth(view *ui)
{
    return ui->active_tab < ui->connections->count ? ui->connections->items[ui->active_tab].auth
                                                   : NULL;
}
static void drop_transfer(struct downloads *d)
{
    if (d->move) {
        if (d->cancel)
            g_cancellable_cancel(d->cancel);
        g_rc_box_release_full(d->move, clear_move);
        d->move = NULL;
    }
    g_clear_object(&d->cancel);
    if (d->transfer)
        media_download_cancel(d->transfer);
    d->stage = DOWNLOAD_IDLE;
}
static void abort_all(view *ui, const char *reason)
{
    struct downloads *d = ui->downloads;
    if (d->stage == DOWNLOAD_DIALOG && file_picker_saving(ui->files))
        file_picker_close(ui->files);
    g_ptr_array_set_size(d->queue, 0);
    drop_transfer(d);
    if (reason)
        set_status(ui, "%s", reason);
}
/* Asks where to save the file at the head of the queue. */
static void show_head(view *ui)
{
    struct downloads *d = ui->downloads;
    pending *item = g_ptr_array_index(d->queue, 0);
    conversation_interactions_close(ui);
    mention_popup_close(ui->mentions);
    emoji_picker_close(ui->emojis);
    ui->navigating = ui->dragging = false;
    d->stage = DOWNLOAD_DIALOG;
    file_picker_show_save(ui->files, item->name);
}
/* The head is finished: move to the next file, if any. */
static void next_file(view *ui)
{
    struct downloads *d = ui->downloads;
    drop_transfer(d);
    if (d->queue->len > 0)
        g_ptr_array_remove_index(d->queue, 0);
    if (d->queue->len)
        show_head(ui);
}
bool conversation_download_available(view *ui, const json_t *message)
{
    (void)ui;
    const json_t *list = json_object_get(message, "attachments");
    if (!json_is_array(list) || !json_array_size(list))
        return false;
    size_t index;
    json_t *item;
    json_array_foreach(list, index, item)
    {
        const char *id = json_string_value(json_object_get(item, "id"));
        if (!id || strlen(id) != 36)
            return false;
    }
    return true;
}
bool conversation_download_start(view *ui, const char *message_id, int only)
{
    json_t *message = history_find_message(ui->model, message_id);
    if (!conversation_download_available(ui, message) || !ensure(ui))
        return false;
    if (file_picker_open(ui->files) || ui->attachment_task || ui->remove_confirm) {
        ui->feedback = "Feche o seletor de arquivos aberto antes de baixar.";
        return false;
    }
    struct downloads *d = ui->downloads;
    if (d->stage == DOWNLOAD_IDLE) {
        d->tab = ui->active_tab;
        d->generation = auth_context_generation(tab_auth(ui));
    }
    bool was_idle = !d->queue->len;
    size_t index;
    json_t *item;
    json_array_foreach(json_object_get(message, "attachments"), index, item)
    {
        if (only >= 0 && index != (size_t)only)
            continue;
        pending *entry = g_new0(pending, 1);
        g_strlcpy(entry->id, json_string_value(json_object_get(item, "id")), sizeof(entry->id));
        entry->name = safe_name(json_string_value(json_object_get(item, "original_file_name")));
        json_t *size = json_object_get(item, "size_bytes");
        entry->size = json_is_integer(size) ? (int64_t)json_integer_value(size) : -1;
        g_ptr_array_add(d->queue, entry);
    }
    if (was_idle && d->queue->len)
        show_head(ui);
    else
        set_status(ui, "Download na fila: %u arquivo(s).", d->queue->len);
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
static void begin_move(view *ui, pending *item, char *temporary)
{
    struct downloads *d = ui->downloads;
    d->move = g_rc_box_new0(move_state);
    d->move->source = temporary;
    d->cancel = g_cancellable_new();
    GFile *from = g_file_new_for_path(temporary), *to = g_file_new_for_path(item->dest);
    g_rc_box_acquire(d->move);
    g_file_move_async(from, to, G_FILE_COPY_OVERWRITE, G_PRIORITY_DEFAULT, d->cancel, NULL, NULL,
                      move_done, d->move);
    g_object_unref(from);
    g_object_unref(to);
    d->stage = DOWNLOAD_MOVING;
}
bool conversation_downloads_poll(view *ui)
{
    struct downloads *d = ui->downloads;
    if (!d || !d->queue || !d->queue->len || d->stage == DOWNLOAD_IDLE)
        return false;
    auth_client *auth = tab_auth(ui);
    if (!auth || ui->active_tab != d->tab || !auth_has_session(auth) ||
        auth_context_generation(auth) != d->generation) {
        abort_all(ui, "Download cancelado: a sessão ou a aba mudou.");
        return true;
    }
    pending *item = g_ptr_array_index(d->queue, 0);
    bool changed = false;
    if (d->stage == DOWNLOAD_DIALOG) {
        if (file_picker_open(ui->files))
            return false;
        g_free(item->dest);
        item->dest = file_picker_take_save(ui->files);
        if (!item->dest) {
            abort_all(ui, "Download cancelado.");
            return true;
        }
        if (!d->transfer)
            d->transfer = media_download_create(auth);
        if (!d->transfer || !media_download_file_request(d->transfer, item->id)) {
            set_status(ui, "Não foi possível iniciar o download de %s.", item->name);
            next_file(ui);
            return true;
        }
        d->stage = DOWNLOAD_TRANSFER;
        d->percent = -1;
        set_status(ui, "Baixando %s…", item->name);
        return true;
    }
    if (d->stage == DOWNLOAD_TRANSFER) {
        media_download_poll(d->transfer);
        media_state state = media_download_state(d->transfer);
        if (state == MEDIA_READY) {
            char *temporary = media_download_take_file(d->transfer);
            if (!temporary) {
                set_status(ui, "Não foi possível salvar %s.", item->name);
                next_file(ui);
            } else {
                begin_move(ui, item, temporary);
                set_status(ui, "Salvando %s…", item->name);
            }
            return true;
        }
        if (state == MEDIA_FAILED || state == MEDIA_IDLE) {
            set_status(ui, "Falha ao baixar %s: %s", item->name, media_download_error(d->transfer));
            next_file(ui);
            return true;
        }
        size_t total = media_download_total(d->transfer);
        if (!total && item->size > 0)
            total = (size_t)item->size;
        int percent = total ? (int)MIN(100, media_download_bytes(d->transfer) * 100 / total) : -1;
        if (percent != d->percent) {
            d->percent = percent;
            if (percent >= 0)
                set_status(ui, "Baixando %s… %d%%", item->name, percent);
            changed = true;
        }
        return changed;
    }
    if (d->stage == DOWNLOAD_MOVING && g_atomic_int_get(&d->move->done)) {
        bool ok = d->move->ok;
        if (ok)
            set_status(ui, "Arquivo salvo em %s", item->dest);
        else
            set_status(ui, "Não foi possível gravar em %s. Verifique o acesso e o espaço.",
                       item->dest);
        next_file(ui);
        return true;
    }
    return changed;
}
void conversation_downloads_close(view *ui)
{
    struct downloads *d = ui->downloads;
    if (!d)
        return;
    if (d->queue)
        abort_all(ui, NULL);
    g_clear_pointer(&d->queue, g_ptr_array_unref);
    media_download_destroy(d->transfer);
    g_free(d);
    ui->downloads = NULL;
    ui->feedback = NULL;
}
