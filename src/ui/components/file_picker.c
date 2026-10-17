#include "ui/components/file_picker.h"
#include "platform/file_transfer.h"
#include "ui/components/button.h"
#include "ui/components/theme.h"
#include <string.h>

typedef struct {
    char *name, *path;
    bool directory;
} entry;
typedef struct {
    GPtrArray *entries;
    char *single;
} directory_result;
struct file_picker {
    bool open, native;
    WINDOW *window;
    GTask *task;
    GCancellable *cancel;
    GPtrArray *entries, *selected, *result;
    char *directory;
    char notice[256];
    text_buffer path;
    int focus, cursor, first, path_first;
};
static void free_entry(gpointer data)
{
    entry *item = data;
    g_free(item->name);
    g_free(item->path);
    g_free(item);
}
static void free_directory(gpointer data)
{
    directory_result *result = data;
    g_ptr_array_unref(result->entries);
    g_free(result->single);
    g_free(result);
}
static gint compare_entry(gconstpointer a, gconstpointer b)
{
    const entry *left = *(entry *const *)a, *right = *(entry *const *)b;
    if (left->directory != right->directory)
        return left->directory ? -1 : 1;
    return g_utf8_collate(left->name, right->name);
}
static void native_worker(GTask *task, gpointer source, gpointer data, GCancellable *cancel)
{
    (void)source;
    (void)data;
    GError *error = NULL;
    GPtrArray *paths = platform_pick_files(cancel, &error);
    if (paths)
        g_task_return_pointer(task, paths, (GDestroyNotify)g_ptr_array_unref);
    else
        g_task_return_error(task, error);
}
static void directory_worker(GTask *task, gpointer source, gpointer data, GCancellable *cancel)
{
    (void)source;
    GError *error = NULL;
    GFile *directory = g_file_new_for_path(data);
    directory_result *result = g_new0(directory_result, 1);
    result->entries = g_ptr_array_new_with_free_func(free_entry);
    GFileInfo *info = g_file_query_info(directory, G_FILE_ATTRIBUTE_STANDARD_TYPE,
                                        G_FILE_QUERY_INFO_NONE, cancel, &error);
    if (info && g_file_info_get_file_type(info) == G_FILE_TYPE_REGULAR)
        result->single = g_strdup(data);
    else if (info) {
        GFileEnumerator *enumerator = g_file_enumerate_children(
            directory,
            G_FILE_ATTRIBUTE_STANDARD_NAME "," G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME
                                           "," G_FILE_ATTRIBUTE_STANDARD_TYPE,
            G_FILE_QUERY_INFO_NONE, cancel, &error);
        GFile *parent = g_file_get_parent(directory);
        if (parent) {
            entry *up = g_new0(entry, 1);
            up->name = g_strdup("..");
            up->path = g_file_get_path(parent);
            up->directory = true;
            g_ptr_array_add(result->entries, up);
            g_object_unref(parent);
        }
        while (enumerator && !error) {
            GFileInfo *child = g_file_enumerator_next_file(enumerator, cancel, &error);
            if (!child)
                break;
            GFileType type = g_file_info_get_file_type(child);
            if (type == G_FILE_TYPE_DIRECTORY || type == G_FILE_TYPE_REGULAR) {
                entry *item = g_new0(entry, 1);
                item->name = g_strdup(g_file_info_get_display_name(child));
                GFile *file = g_file_get_child(directory, g_file_info_get_name(child));
                item->path = g_file_get_path(file);
                item->directory = type == G_FILE_TYPE_DIRECTORY;
                g_object_unref(file);
                g_ptr_array_add(result->entries, item);
            }
            g_object_unref(child);
            if (result->entries->len > 10000) {
                g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_NO_SPACE,
                                    "Mais de 10.000 entradas; informe um subdiretório no caminho.");
                break;
            }
        }
        g_clear_object(&enumerator);
    }
    g_clear_object(&info);
    g_object_unref(directory);
    if (error) {
        free_directory(result);
        g_task_return_error(task, error);
    } else {
        g_ptr_array_sort(result->entries, compare_entry);
        g_task_return_pointer(task, result, free_directory);
    }
}
static void stop_task(file_picker *p)
{
    if (p->cancel)
        g_cancellable_cancel(p->cancel);
    g_clear_object(&p->cancel);
    g_clear_object(&p->task);
}
static void browse(file_picker *p, const char *path)
{
    stop_task(p);
    g_clear_pointer(&p->entries, g_ptr_array_unref);
    p->cursor = p->first = 0;
    char *absolute = g_canonicalize_filename(path, p->directory);
    g_free(p->directory);
    p->directory = absolute;
    char *display = g_filename_to_utf8(absolute, -1, NULL, NULL, NULL);
    text_decode(&p->path, display ? display : "");
    g_free(display);
    p->cancel = g_cancellable_new();
    p->task = file_transfer_task_new(p->cancel);
    g_task_set_task_data(p->task, g_strdup(absolute), g_free);
    g_task_run_in_thread(p->task, directory_worker);
    g_strlcpy(p->notice, "Carregando arquivos…", sizeof(p->notice));
}
file_picker *file_picker_create(void)
{
    file_picker *p = g_new0(file_picker, 1);
    p->directory = g_strdup(g_get_home_dir());
    p->selected = g_ptr_array_new_with_free_func(g_free);
    return p;
}
void file_picker_close(file_picker *p)
{
    if (!p)
        return;
    stop_task(p);
    p->open = p->native = false;
    if (p->window)
        delwin(p->window);
    p->window = NULL;
    g_ptr_array_set_size(p->selected, 0);
}
void file_picker_destroy(file_picker *p)
{
    if (!p)
        return;
    file_picker_close(p);
    g_free(p->directory);
    g_ptr_array_unref(p->selected);
    g_clear_pointer(&p->entries, g_ptr_array_unref);
    g_clear_pointer(&p->result, g_ptr_array_unref);
    g_free(p);
}
void file_picker_show(file_picker *p)
{
    file_picker_close(p);
    p->open = true;
    p->focus = 0;
    g_clear_pointer(&p->result, g_ptr_array_unref);
    if (!PAPO_NATIVE_FILE_DIALOGS) {
        browse(p, p->directory);
        return;
    }
    p->native = true;
    p->cancel = g_cancellable_new();
    p->task = file_transfer_task_new(p->cancel);
    g_task_run_in_thread(p->task, native_worker);
}
bool file_picker_open(const file_picker *p)
{
    return p && p->open;
}
bool file_picker_paste(file_picker *p, text_paste *paste)
{
    if (!file_picker_open(p) || p->native || p->focus != 0)
        return false;
    if (!text_paste_commit(paste, &p->path))
        g_strlcpy(p->notice, "Caminho excede o limite; campo preservado.", sizeof(p->notice));
    return true;
}
bool file_picker_poll(file_picker *p)
{
    if (!p || !p->task || !g_task_get_completed(p->task))
        return false;
    GError *error = NULL;
    gpointer result = g_task_propagate_pointer(p->task, &error);
    g_clear_object(&p->task);
    g_clear_object(&p->cancel);
    if (p->native) {
        p->native = false;
        if (result) {
            p->result = result;
            file_picker_close(p);
        } else if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
            file_picker_close(p);
        else {
            char *path = g_strdup(p->directory);
            browse(p, path);
            g_free(path);
        }
    } else if (result) {
        directory_result *directory = result;
        if (directory->single) {
            p->result = g_ptr_array_new_with_free_func(g_free);
            g_ptr_array_add(p->result, g_strdup(directory->single));
            file_picker_close(p);
            g_free(p->directory);
            p->directory = g_path_get_dirname(directory->single);
        } else {
            p->entries = g_ptr_array_ref(directory->entries);
            p->notice[0] = 0;
        }
        free_directory(directory);
    } else
        g_strlcpy(p->notice, "Não foi possível listar o caminho. Verifique o acesso.",
                  sizeof(p->notice));
    if (error && g_error_matches(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE))
        g_strlcpy(p->notice, error->message, sizeof(p->notice));
    g_clear_error(&error);
    return true;
}
static int selected(file_picker *p, const char *path)
{
    for (guint i = 0; i < p->selected->len; ++i)
        if (!strcmp(path, g_ptr_array_index(p->selected, i)))
            return (int)i;
    return -1;
}
static void toggle(file_picker *p, entry *item)
{
    int at = selected(p, item->path);
    if (at >= 0)
        g_ptr_array_remove_index(p->selected, (guint)at);
    else if (p->selected->len < ATTACHMENT_LIMIT)
        g_ptr_array_add(p->selected, g_strdup(item->path));
    else
        g_strlcpy(p->notice, "Limite local: 25 anexos por mensagem.", sizeof(p->notice));
}
static void accept(file_picker *p)
{
    if (!p->selected->len) {
        g_strlcpy(p->notice, "Marque arquivos com Espaço ou Enter.", sizeof(p->notice));
        return;
    }
    p->result = g_ptr_array_new_with_free_func(g_free);
    for (guint i = 0; i < p->selected->len; ++i)
        g_ptr_array_add(p->result, g_strdup(g_ptr_array_index(p->selected, i)));
    file_picker_close(p);
}
bool file_picker_key(file_picker *p, int type, uint32_t key)
{
    if (!file_picker_open(p))
        return false;
    if (type == OK && key == 27) {
        file_picker_close(p);
        return true;
    }
    if (p->native)
        return true;
    if ((type == OK && key == '\t') || (type == KEY_CODE_YES && key == KEY_BTAB)) {
        p->focus = (p->focus + (type == OK ? 1 : 3)) % 4;
        return true;
    }
    bool enter =
        (type == OK && (key == '\n' || key == '\r')) || (type == KEY_CODE_YES && key == KEY_ENTER);
    if (p->focus == 0) {
        if (enter) {
            char utf8[TEXT_UTF8_BYTES];
            if (text_encode(&p->path, utf8)) {
                char *path = g_filename_from_utf8(utf8, -1, NULL, NULL, NULL);
                if (path) {
                    browse(p, path);
                    g_free(path);
                    p->focus = 1;
                }
            }
        } else
            text_edit(&p->path, type, key, COLS - 10, false);
    } else if (p->focus == 1) {
        int count = p->entries ? (int)p->entries->len : 0;
        if (type == KEY_CODE_YES && (key == KEY_UP || key == KEY_DOWN))
            p->cursor = CLAMP(p->cursor + (key == KEY_DOWN ? 1 : -1), 0, MAX(0, count - 1));
        else if (type == KEY_CODE_YES && (key == KEY_NPAGE || key == KEY_PPAGE))
            p->cursor = CLAMP(p->cursor + (key == KEY_NPAGE ? 8 : -8), 0, MAX(0, count - 1));
        else if (count && (enter || (type == OK && key == ' '))) {
            entry *item = g_ptr_array_index(p->entries, p->cursor);
            if (item->directory && enter) {
                char *path = g_strdup(item->path);
                browse(p, path);
                g_free(path);
            } else if (!item->directory)
                toggle(p, item);
        }
    } else if (enter || (type == OK && key == ' ')) {
        if (p->focus == 2)
            file_picker_close(p);
        else
            accept(p);
    }
    return true;
}
bool file_picker_mouse(file_picker *p, const MEVENT *event)
{
    if (!file_picker_open(p))
        return false;
    if (!p->window)
        return true;
    int y = event->y - getbegy(p->window), x = event->x - getbegx(p->window);
    int height = getmaxy(p->window), width = getmaxx(p->window);
    if (y < 0 || x < 0 || y >= height || x >= width)
        return true;
    if (event->bstate & (BUTTON4_PRESSED | BUTTON5_PRESSED)) {
        p->focus = 1;
        file_picker_key(p, KEY_CODE_YES, event->bstate & BUTTON4_PRESSED ? KEY_UP : KEY_DOWN);
    } else if (event->bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED)) {
        if (y == 2)
            p->focus = 0;
        else if (y >= 4 && y < height - 4 && p->entries &&
                 y - 4 + p->first < (int)p->entries->len) {
            p->focus = 1;
            p->cursor = y - 4 + p->first;
            file_picker_key(p, OK, '\n');
        } else if (y == height - 2) {
            if (x >= 2 && x < 14)
                file_picker_close(p);
            else if (x >= width - 14 && x < width - 2)
                accept(p);
        }
    }
    return true;
}
WINDOW *file_picker_draw(file_picker *p)
{
    if (!file_picker_open(p) || p->native || COLS < 44 || LINES < 14)
        return NULL;
    int height = MIN(LINES - 2, 24), width = MIN(COLS - 4, 90);
    int y = (LINES - height) / 2, x = (COLS - width) / 2;
    if (p->window && (getmaxy(p->window) != height || getmaxx(p->window) != width ||
                      getbegy(p->window) != y || getbegx(p->window) != x)) {
        delwin(p->window);
        p->window = NULL;
    }
    if (!p->window)
        p->window = newwin(height, width, y, x);
    if (!p->window)
        return NULL;
    WINDOW *w = p->window;
    wbkgd(w, COLOR_PAIR(THEME_PANEL));
    werase(w);
    wattrset(w, COLOR_PAIR(THEME_BORDER));
    box(w, 0, 0);
    wattrset(w, COLOR_PAIR(THEME_PANEL_ACCENT) | A_BOLD);
    text_label(w, 0, 2, width - 4, " Anexar arquivos ");
    wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
    text_label(w, 1, 2, width - 4, "Caminho:");
    wattrset(w, COLOR_PAIR(p->focus == 0 ? THEME_FOCUSED : THEME_PANEL));
    text_draw_field(w, &p->path, &p->path_first, 2, 2, width - 4, false);
    int rows = height - 8;
    if (p->cursor < p->first)
        p->first = p->cursor;
    if (p->cursor >= p->first + rows)
        p->first = p->cursor - rows + 1;
    for (int row = 0; p->entries && row < rows && row + p->first < (int)p->entries->len; ++row) {
        entry *item = g_ptr_array_index(p->entries, row + p->first);
        wattrset(w, COLOR_PAIR(p->focus == 1 && row + p->first == p->cursor ? THEME_FOCUSED
                                                                            : THEME_PANEL));
        char *label = g_strdup_printf("%s %s%s",
                                      item->directory                ? " / "
                                      : selected(p, item->path) >= 0 ? "[x]"
                                                                     : "[ ]",
                                      item->name, item->directory ? "/" : "");
        text_label(w, 4 + row, 2, width - 4, label);
        g_free(label);
    }
    wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
    char hint[256];
    g_snprintf(hint, sizeof(hint), "%u marcado(s) · Espaço marcar · Tab ações", p->selected->len);
    text_label(w, height - 4, 2, width - 4, hint);
    text_label(w, height - 3, 2, width - 4, p->notice);
    button_draw(w, height - 2, 2, 12, "Cancelar", p->focus == 2 ? BUTTON_FOCUSED : BUTTON_NORMAL,
                BUTTON_PANEL);
    button_draw(w, height - 2, width - 14, 12, "Anexar",
                p->focus == 3 ? BUTTON_FOCUSED : BUTTON_NORMAL, BUTTON_PANEL);
    wnoutrefresh(w);
    return w;
}
GPtrArray *file_picker_take_files(file_picker *p)
{
    GPtrArray *result = p->result;
    p->result = NULL;
    return result;
}
