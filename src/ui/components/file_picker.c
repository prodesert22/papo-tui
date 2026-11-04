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
/* Outcome of checking the typed save destination off the UI thread. */
typedef struct {
    char *path;
    bool directory, exists;
} save_target;
struct file_picker {
    bool open, native, save, resolving, choice;
    char *name, *save_result, *confirm;
    WINDOW *window;
    GTask *task;
    GCancellable *cancel;
    GPtrArray *entries, *selected, *result, *choice_result;
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
static void free_target(gpointer data)
{
    save_target *target = data;
    g_free(target->path);
    g_free(target);
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
    GPtrArray *paths = NULL;
    const char *name = g_task_get_task_data(task);
    if (name) {
        char *path = platform_pick_save(name, cancel, &error);
        if (path) {
            paths = g_ptr_array_new_with_free_func(g_free);
            g_ptr_array_add(paths, path);
        }
    } else
        paths = platform_pick_files(cancel, &error);
    if (paths)
        g_task_return_pointer(task, paths, (GDestroyNotify)g_ptr_array_unref);
    else
        g_task_return_error(task, error);
}
static bool existing_directory(GFile *file, GCancellable *cancel)
{
    GFileInfo *info = g_file_query_info(file, G_FILE_ATTRIBUTE_STANDARD_TYPE,
                                        G_FILE_QUERY_INFO_NONE, cancel, NULL);
    bool result = info && g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY;
    g_clear_object(&info);
    return result;
}
static void target_worker(GTask *task, gpointer source, gpointer data, GCancellable *cancel)
{
    (void)source;
    GError *error = NULL;
    GFile *file = g_file_new_for_path(data);
    GFileInfo *info = g_file_query_info(file, G_FILE_ATTRIBUTE_STANDARD_TYPE,
                                        G_FILE_QUERY_INFO_NONE, cancel, &error);
    save_target *target = g_new0(save_target, 1);
    target->path = g_strdup(data);
    if (info) {
        GFileType type = g_file_info_get_file_type(info);
        target->directory = type == G_FILE_TYPE_DIRECTORY;
        target->exists = true;
        if (type != G_FILE_TYPE_DIRECTORY && type != G_FILE_TYPE_REGULAR)
            g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_NOT_REGULAR_FILE,
                                "O destino não é um arquivo comum.");
    } else if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
        GFile *parent = g_file_get_parent(file);
        g_clear_error(&error);
        if (!parent || !existing_directory(parent, cancel))
            g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                                "A pasta do destino não existe.");
        g_clear_object(&parent);
    }
    g_clear_object(&info);
    g_object_unref(file);
    if (error) {
        free_target(target);
        g_task_return_error(task, error);
    } else
        g_task_return_pointer(task, target, free_target);
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
    p->resolving = false;
}
static void browse(file_picker *p, const char *path)
{
    stop_task(p);
    g_clear_pointer(&p->entries, g_ptr_array_unref);
    p->cursor = p->first = 0;
    char *absolute = g_canonicalize_filename(path, p->directory);
    g_free(p->directory);
    p->directory = absolute;
    char *shown = p->save && p->name ? g_build_filename(absolute, p->name, NULL) : NULL;
    char *display = g_filename_to_utf8(shown ? shown : absolute, -1, NULL, NULL, NULL);
    g_free(shown);
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
    p->resolving = false;
}
void file_picker_destroy(file_picker *p)
{
    if (!p)
        return;
    file_picker_close(p);
    g_free(p->directory);
    g_free(p->name);
    g_free(p->save_result);
    g_free(p->confirm);
    g_ptr_array_unref(p->selected);
    g_clear_pointer(&p->entries, g_ptr_array_unref);
    g_clear_pointer(&p->result, g_ptr_array_unref);
    g_clear_pointer(&p->choice_result, g_ptr_array_unref);
    g_free(p);
}
void file_picker_show(file_picker *p)
{
    file_picker_close(p);
    p->open = true;
    p->focus = 0;
    p->save = p->choice = false;
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
void file_picker_show_save(file_picker *p, const char *name)
{
    file_picker_close(p);
    p->open = p->save = true;
    p->choice = false;
    p->focus = 0;
    g_free(p->name);
    p->name = g_strdup(name && *name ? name : "arquivo");
    g_clear_pointer(&p->confirm, g_free);
    g_clear_pointer(&p->save_result, g_free);
    if (!PAPO_NATIVE_FILE_DIALOGS) {
        const char *downloads = g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
        if (downloads && g_file_test(downloads, G_FILE_TEST_IS_DIR)) {
            g_free(p->directory);
            p->directory = g_strdup(downloads);
        }
        browse(p, p->directory);
        return;
    }
    p->native = true;
    p->cancel = g_cancellable_new();
    p->task = file_transfer_task_new(p->cancel);
    g_task_set_task_data(p->task, g_strdup(p->name), g_free);
    g_task_run_in_thread(p->task, native_worker);
}
void file_picker_show_choice(file_picker *p, const char *const *labels, size_t count)
{
    file_picker_close(p);
    p->open = p->choice = true;
    p->save = false;
    p->focus = 1;
    p->cursor = p->first = 0;
    g_clear_pointer(&p->choice_result, g_ptr_array_unref);
    g_clear_pointer(&p->entries, g_ptr_array_unref);
    p->entries = g_ptr_array_new_with_free_func(free_entry);
    for (size_t i = 0; i < count; ++i) {
        entry *item = g_new0(entry, 1);
        item->name = g_strdup(labels[i]);
        item->path = g_strdup_printf("%zu", i);
        g_ptr_array_add(p->entries, item);
    }
    p->notice[0] = 0;
}
GPtrArray *file_picker_take_choice(file_picker *p)
{
    GPtrArray *result = p->choice_result;
    p->choice_result = NULL;
    return result;
}
bool file_picker_saving(const file_picker *p)
{
    return p && p->open && p->save;
}
char *file_picker_take_save(file_picker *p)
{
    char *path = p->save_result;
    p->save_result = NULL;
    return path;
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
        if (result && p->save) {
            GPtrArray *paths = result;
            g_free(p->save_result);
            p->save_result = paths->len ? g_strdup(g_ptr_array_index(paths, 0)) : NULL;
            g_ptr_array_unref(paths);
            file_picker_close(p);
        } else if (result) {
            p->result = result;
            file_picker_close(p);
        } else if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
            file_picker_close(p);
        else {
            char *path = g_strdup(p->directory);
            browse(p, path);
            g_free(path);
        }
    } else if (result && p->resolving) {
        save_target *target = result;
        p->resolving = false;
        if (target->directory) {
            char *path = g_strdup(target->path);
            browse(p, path);
            g_free(path);
        } else if (target->exists && (!p->confirm || strcmp(p->confirm, target->path))) {
            g_free(p->confirm);
            p->confirm = g_strdup(target->path);
            g_strlcpy(p->notice, "O arquivo já existe. Salve de novo para substituí-lo.",
                      sizeof(p->notice));
        } else {
            g_free(p->save_result);
            p->save_result = g_strdup(target->path);
            file_picker_close(p);
            g_free(p->directory);
            p->directory = g_path_get_dirname(target->path);
        }
        free_target(target);
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
    } else if (p->resolving) {
        p->resolving = false;
        g_strlcpy(p->notice,
                  error && g_error_matches(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)
                      ? error->message
                      : "Não foi possível usar esse destino. Verifique o acesso.",
                  sizeof(p->notice));
    } else
        g_strlcpy(p->notice, "Não foi possível listar o caminho. Verifique o acesso.",
                  sizeof(p->notice));
    if (error && !p->resolving && g_error_matches(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE))
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
/* Save mode: checks the typed destination before the dialog closes. */
static void resolve(file_picker *p)
{
    char utf8[TEXT_UTF8_BYTES];
    char *path =
        text_encode(&p->path, utf8) ? g_filename_from_utf8(utf8, -1, NULL, NULL, NULL) : NULL;
    if (!path || !*path) {
        g_free(path);
        g_strlcpy(p->notice, "Informe o caminho do arquivo.", sizeof(p->notice));
        return;
    }
    stop_task(p);
    char *absolute = g_canonicalize_filename(path, p->directory);
    g_free(path);
    p->resolving = true;
    p->cancel = g_cancellable_new();
    p->task = file_transfer_task_new(p->cancel);
    g_task_set_task_data(p->task, absolute, g_free);
    g_task_run_in_thread(p->task, target_worker);
    g_strlcpy(p->notice, "Verificando destino…", sizeof(p->notice));
}
static void accept(file_picker *p)
{
    if (p->choice) {
        if (!p->selected->len) {
            g_strlcpy(p->notice, "Marque ao menos um anexo com Espaço.", sizeof(p->notice));
            return;
        }
        /* Item positions, in list order, whatever order they were marked. */
        p->choice_result = g_ptr_array_new_with_free_func(g_free);
        for (guint i = 0; p->entries && i < p->entries->len; ++i) {
            entry *item = g_ptr_array_index(p->entries, i);
            if (selected(p, item->path) >= 0)
                g_ptr_array_add(p->choice_result, g_strdup(item->path));
        }
        file_picker_close(p);
        return;
    }
    if (p->save) {
        resolve(p);
        return;
    }
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
        if (p->choice && !p->focus)
            p->focus = type == OK ? 1 : 3;
        return true;
    }
    bool enter =
        (type == OK && (key == '\n' || key == '\r')) || (type == KEY_CODE_YES && key == KEY_ENTER);
    if (p->choice && p->focus == 1 && type == OK && (key == 'a' || key == 'A') && p->entries) {
        /* Everything marked becomes unmarked; otherwise mark everything. */
        bool all = p->selected->len == p->entries->len;
        g_ptr_array_set_size(p->selected, 0);
        for (guint i = 0; !all && i < p->entries->len && i < ATTACHMENT_LIMIT; ++i)
            g_ptr_array_add(p->selected,
                            g_strdup(((entry *)g_ptr_array_index(p->entries, i))->path));
        return true;
    }
    if (p->focus == 0 && p->save) {
        if (enter)
            resolve(p);
        else
            text_edit(&p->path, type, key, COLS - 10, false);
    } else if (p->focus == 0) {
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
            } else if (!item->directory && p->save && enter) {
                g_free(p->name);
                p->name = g_path_get_basename(item->path);
                char *display = g_filename_to_utf8(item->path, -1, NULL, NULL, NULL);
                text_decode(&p->path, display ? display : "");
                g_free(display);
                p->focus = 3;
            } else if (!item->directory && !p->save)
                toggle(p, item);
        }
    } else if (enter || (type == OK && key == ' ')) {
        if (p->focus == 2 && p->save)
            file_picker_close(p);
        else if (p->focus == 2)
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
            p->focus = p->choice ? p->focus : 0;
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
    text_label(w, 0, 2, width - 4,
               p->choice ? " Baixar anexos "
               : p->save ? " Salvar anexo "
                         : " Anexar arquivos ");
    wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
    text_label(w, 1, 2, width - 4,
               p->choice ? "Escolha os anexos que deseja baixar:"
               : p->save ? "Salvar em (pasta e nome do arquivo):"
                         : "Caminho:");
    if (!p->choice) {
        wattrset(w, COLOR_PAIR(p->focus == 0 ? THEME_FOCUSED : THEME_PANEL));
        text_draw_field(w, &p->path, &p->path_first, 2, 2, width - 4, false);
    }
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
                                      : p->save                      ? "   "
                                      : selected(p, item->path) >= 0 ? "[x]"
                                                                     : "[ ]",
                                      item->name, item->directory ? "/" : "");
        text_label(w, 4 + row, 2, width - 4, label);
        g_free(label);
    }
    wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
    char hint[256];
    if (p->choice)
        g_snprintf(hint, sizeof(hint), "%u marcado(s) · Espaço marcar · A todos · Tab ações",
                   p->selected->len);
    else if (p->save)
        g_strlcpy(hint, "Enter abre pasta ou usa o nome do arquivo · Tab ações", sizeof(hint));
    else
        g_snprintf(hint, sizeof(hint), "%u marcado(s) · Espaço marcar · Tab ações",
                   p->selected->len);
    text_label(w, height - 4, 2, width - 4, hint);
    text_label(w, height - 3, 2, width - 4, p->notice);
    button_draw(w, height - 2, 2, 12, "Cancelar", p->focus == 2 ? BUTTON_FOCUSED : BUTTON_NORMAL,
                BUTTON_PANEL);
    button_draw(w, height - 2, width - 14, 12,
                p->choice ? "Baixar"
                : p->save ? "Salvar"
                          : "Anexar",
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
