#include "config/app_mode.h"
#include "model/text/translation.h"
#include "ui/components/button.h"
#include "ui/components/input_border.h"
#include "ui/components/server_settings/private.h"
#include "ui/components/theme.h"
#include "ui/input/mouse.h"

static const char *const tab_names[ADMIN_TABS] = {N_("Visão geral"), N_("Cargos"),   N_("Canais"),
                                                  N_("Emojis"),      N_("Webhooks"), N_("Membros"),
                                                  N_("Auditoria")};
static const admin_tab_ops *ops_of(admin_tab tab)
{
    static const admin_tab_ops *const all[ADMIN_TABS] = {
        &tab_overview, &tab_roles,   &tab_channels, &tab_emojis,
        &tab_webhooks, &tab_members, &tab_audit};
    return all[tab < ADMIN_TABS ? tab : 0];
}

/* Helpers shared by the tabs. */
admin_field *admin_field_add(GArray *fields, field_kind kind, int id, const char *label)
{
    admin_field field;
    memset(&field, 0, sizeof(field));
    field.kind = kind;
    field.id = id;
    snprintf(field.label, sizeof(field.label), "%s", label ? label : "");
    g_array_append_val(fields, field);
    return &g_array_index(fields, admin_field, fields->len - 1);
}
void admin_note(GArray *fields, const char *text)
{
    admin_field *note = admin_field_add(fields, FIELD_NOTE, 0, "");
    snprintf(note->value, sizeof(note->value), "%s", text ? text : "");
}
void admin_row_add(GArray *rows, const char *id, const char *label, const char *detail,
                   unsigned rgb, bool action)
{
    if (rows->len >= SERVER_SETTINGS_ROWS * 16)
        return;
    admin_row row;
    memset(&row, 0, sizeof(row));
    snprintf(row.id, sizeof(row.id), "%s", id ? id : "");
    snprintf(row.label, sizeof(row.label), "%s", label ? label : "");
    snprintf(row.detail, sizeof(row.detail), "%s", detail ? detail : "");
    row.rgb = rgb;
    row.action = action;
    g_array_append_val(rows, row);
}
void admin_error(server_settings *s, const char *message)
{
    snprintf(s->error, sizeof(s->error), "%s", message ? message : "");
}
void admin_invalid(server_settings *s, int field_id, const char *message)
{
    admin_error(s, message);
    s->invalid = s->invalid_pending = true;
    s->invalid_id = field_id;
    s->invalid_tab = s->tab;
}
static bool blank_text(const char *text)
{
    if (!g_utf8_validate(text, -1, NULL))
        return false;
    for (const char *at = text; *at; at = g_utf8_next_char(at))
        if (!g_unichar_isspace(g_utf8_get_char(at)))
            return false;
    return true;
}
bool admin_check_field(server_settings *s, int field_id, const char *label, const char *text,
                       size_t max, bool required)
{
    const char *problem = server_admin_check_text(text, max, required);
    if (!problem)
        return true;
    char message[192];
    if (required && blank_text(text))
        snprintf(message, sizeof(message), _("Preencha o campo %s."), label);
    else
        snprintf(message, sizeof(message), "%s", problem);
    admin_invalid(s, field_id, message);
    return false;
}
bool admin_text(const text_buffer *buffer, char output[TEXT_UTF8_BYTES])
{
    return text_encode(buffer, output);
}
void admin_set_text(text_buffer *buffer, const char *utf8)
{
    if (!utf8 || !text_decode(buffer, utf8))
        text_clear(buffer);
}
const char *admin_str(const json_t *object, const char *key)
{
    const char *value = json_string_value(json_object_get(object, key));
    return value ? value : "";
}
const char *admin_user_name(server_settings *s, const char *id, const char *fallback)
{
    const char *name = id && *id ? history_mention_name(s->model, id) : NULL;
    return name && *name ? name : fallback;
}
const char *admin_user_display(const json_t *user)
{
    const char *nickname = admin_str(user, "nickname");
    return *nickname ? nickname : admin_str(user, "username");
}
bool admin_user_matches(const json_t *user, const char *folded)
{
    if (!user) /* No longer listed. */
        return false;
    if (!folded || !*folded)
        return true;
    bool found = false;
    const char *names[2] = {admin_str(user, "username"), admin_str(user, "nickname")};
    for (int i = 0; i < 2 && !found; ++i) {
        if (!*names[i] || !g_utf8_validate(names[i], -1, NULL))
            continue;
        char *name = g_utf8_casefold(names[i], -1);
        found = name && strstr(name, folded);
        g_free(name);
    }
    return found;
}
unsigned admin_color(const char *hex)
{
    if (!hex || !server_admin_color_valid(hex) || !*hex)
        return 0;
    return 0x1000000 | (unsigned)strtoul(hex + 1, NULL, 16);
}
void admin_select(server_settings *s, const char *id)
{
    snprintf(s->selected, sizeof(s->selected), "%s", id ? id : "");
    s->invalid = false;
    s->draft_for[0] = 1; /* Forces a refill on the next sync. */
    s->dirty = false;
    s->confirm = -1;
}
void admin_sent(server_settings *s)
{
    s->invalid = false;
    s->awaiting = true;
    s->dirty = true;
    s->confirm = -1;
}
bool admin_confirmed(server_settings *s, const admin_field *field)
{
    if (s->confirm == field->id && !strcmp(s->confirm_key, field->key)) {
        s->confirm = -1;
        return true;
    }
    s->confirm = field->id;
    snprintf(s->confirm_key, sizeof(s->confirm_key), "%s", field->key);
    admin_error(s, _("Pressione de novo para confirmar."));
    return false;
}
size_t admin_role_list(server_settings *s, const json_t **roles, size_t capacity)
{
    size_t count = 0;
    const char *id;
    json_t *role;
    json_object_foreach((json_t *)server_admin_roles(s->model), id, role)
    {
        (void)id;
        if (count < capacity)
            roles[count++] = role;
    }
    return count;
}

static bool focusable(const admin_field *f)
{
    return f->kind == FIELD_TEXT || f->kind == FIELD_SECRET || f->kind == FIELD_CHECK ||
           f->kind == FIELD_CHOICE || f->kind == FIELD_BUTTON;
}
static void compute_tabs(server_settings *s)
{
    s->tab_count = 0;
    s->pending = false;
    for (int i = 0; i < ADMIN_TABS; ++i) {
        admin_visibility v = server_admin_tab(s->model, (admin_tab)i);
        if (v == ADMIN_SHOWN)
            s->tabs[s->tab_count++] = (admin_tab)i;
        else if (v == ADMIN_PENDING)
            s->pending = true;
    }
}
static int tab_index(const server_settings *s)
{
    for (int i = 0; i < s->tab_count; ++i)
        if (s->tabs[i] == s->tab)
            return i;
    return -1;
}
static void enter_tab(server_settings *s, admin_tab tab)
{
    /* Tabs that list members turn the directory back on when entered. */
    server_admin_members_active(s->model, false);
    s->tab = tab;
    s->invalid = false;
    s->list_selected = s->list_first = s->field = s->field_first = 0;
    s->selected[0] = 0;
    s->draft_for[0] = 1;
    s->dirty = false;
    s->confirm = -1;
    s->query[0] = 0;
    s->error[0] = 0;
    const admin_tab_ops *ops = ops_of(tab);
    if (ops->enter)
        ops->enter(s);
}

void server_settings_close(server_settings *s)
{
    if (s->open && s->model) {
        server_admin_members_active(s->model, false);
        server_admin_close(s->model);
    }
    s->open = false;
    s->model = NULL;
    text_clear(&s->secret);
    if (s->window) {
        delwin(s->window);
        s->window = NULL;
    }
}
void server_settings_open(server_settings *s, history *model, icon_view *icons, const char *server)
{
    server_settings_close(s);
    memset(s, 0, sizeof(*s));
    s->open = true;
    s->model = model;
    s->icons = icons;
    s->confirm = -1;
    snprintf(s->server, sizeof(s->server), "%s", server ? server : "");
    tab_strip_reset(&s->strip);
    compute_tabs(s);
    s->seen_permissions = server_admin_permissions_revision(model);
    enter_tab(s, s->tab_count ? s->tabs[0] : ADMIN_TAB_EMOJIS);
}
bool server_settings_show(server_settings *s, admin_tab tab, const char *selected)
{
    if (!s->open)
        return false;
    bool shown = false;
    for (int i = 0; i < s->tab_count; ++i)
        shown |= s->tabs[i] == tab;
    if (!shown)
        return false;
    if (s->tab != tab)
        enter_tab(s, tab);
    admin_select(s, selected);
    /* Straight to the item's form, as if it had been chosen in the list. */
    s->focus = 2;
    return true;
}

static GArray *build_rows(server_settings *s)
{
    GArray *rows = g_array_new(FALSE, TRUE, sizeof(admin_row));
    const admin_tab_ops *ops = ops_of(s->tab);
    if (ops->list && ops->rows)
        ops->rows(s, rows);
    return rows;
}
static uint64_t data_signature(server_settings *s)
{
    return server_admin_revision(s->model) * 1000003u +
           server_admin_permissions_revision(s->model) * 7919u + history_users_revision(s->model);
}
/* Keeps the selection on the same entity and refills drafts when it moves or
 * when the data changed under untouched drafts. */
static void settle(server_settings *s, GArray *rows)
{
    const admin_tab_ops *ops = ops_of(s->tab);
    if (ops->list) {
        int found = -1;
        for (guint i = 0; i < rows->len; ++i)
            if (!strcmp(g_array_index(rows, admin_row, i).id, s->selected))
                found = (int)i;
        if (found >= 0)
            s->list_selected = found;
        else if (rows->len) {
            if (s->list_selected >= (int)rows->len)
                s->list_selected = (int)rows->len - 1;
            if (s->list_selected < 0)
                s->list_selected = 0;
            snprintf(s->selected, sizeof(s->selected), "%s",
                     g_array_index(rows, admin_row, s->list_selected).id);
        } else
            s->list_selected = 0;
    }
    if (s->awaiting && !server_admin_busy(s->model)) {
        /* A saved draft gives way to the server's copy; a refused one stays. */
        s->awaiting = false;
        if (server_admin_state(s->model) == ADMIN_SAVED) {
            s->dirty = false;
            s->draft_for[0] = 1;
            text_clear(&s->secret);
        }
    }
    uint64_t signature = data_signature(s);
    bool moved = s->draft_tab != s->tab || strcmp(s->draft_for, s->selected);
    if (moved || (signature != s->filled && !s->dirty && !server_admin_busy(s->model))) {
        if (moved) {
            s->field = s->field_first = 0;
            s->confirm = -1;
        }
        s->dirty = false;
        if (ops->fill)
            ops->fill(s);
        s->draft_tab = s->tab;
        snprintf(s->draft_for, sizeof(s->draft_for), "%s", s->selected);
        s->filled = signature;
    }
}
static GArray *build_fields(server_settings *s)
{
    GArray *fields = g_array_new(FALSE, TRUE, sizeof(admin_field));
    const admin_tab_ops *ops = ops_of(s->tab);
    if (ops->fields)
        ops->fields(s, fields);
    if (fields->len > SERVER_SETTINGS_FIELDS)
        g_array_set_size(fields, SERVER_SETTINGS_FIELDS);
    int count = (int)fields->len;
    /* A refused text field takes the focus, so its cursor shows where to type. */
    if (s->invalid && s->invalid_pending) {
        s->invalid_pending = false;
        for (int i = 0; i < count; ++i) {
            const admin_field *f = &g_array_index(fields, admin_field, i);
            if (s->invalid_tab == s->tab && f->id == s->invalid_id &&
                (f->kind == FIELD_TEXT || f->kind == FIELD_SECRET)) {
                s->field = i;
                s->focus = 2;
                s->confirm = -1;
            }
        }
    }
    /* Keep the focus on a control. */
    if (s->field >= count)
        s->field = count - 1;
    if (s->field < 0)
        s->field = 0;
    for (int step = 0; step < count && !focusable(&g_array_index(fields, admin_field, s->field));
         ++step)
        s->field = (s->field + 1) % count;
    return fields;
}
static bool any_focusable(GArray *fields)
{
    for (guint i = 0; i < fields->len; ++i)
        if (focusable(&g_array_index(fields, admin_field, i)))
            return true;
    return false;
}

bool server_settings_poll(server_settings *s)
{
    if (!s->open || !s->model)
        return false;
    bool changed = false;
    uint64_t permissions = server_admin_permissions_revision(s->model);
    if (permissions != s->seen_permissions) {
        s->seen_permissions = permissions;
        compute_tabs(s);
        if (tab_index(s) < 0) {
            /* The account lost the permission this tab needs. */
            enter_tab(s, s->tab_count ? s->tabs[0] : ADMIN_TAB_EMOJIS);
            s->focus = 0;
            snprintf(s->feedback, sizeof(s->feedback), _("Permissão removida; a aba foi fechada."));
        }
        changed = true;
    }
    uint64_t admin = server_admin_revision(s->model), users = history_users_revision(s->model);
    if (admin != s->seen_admin || users != s->seen_users) {
        s->seen_admin = admin;
        s->seen_users = users;
        changed = true;
    }
    return changed;
}

/* Wraps a note across up to max rows; returns the rows used. */
static int note_rows(WINDOW *w, const char *text, int y, int x, int width, int max, bool draw)
{
    static text_buffer buffer;
    static text_layout layout;
    const char *cursor = text;
    if (width < 4 || !text_decode_chunk(&buffer, &cursor))
        return 1;
    text_measure(&buffer, width, &layout);
    int rows = layout.rows < 1 ? 1 : layout.rows > max ? max : layout.rows;
    if (draw)
        text_draw(w, &buffer, &layout, 0, rows, y, x);
    return rows;
}
/* Emoji previews are 64 pixels tall; Ultralight and text terminals show none. */
#define EMOJI_PREVIEW_PIXELS 64
bool admin_previews(void)
{
    graphics_terminal terminal = graphics_terminal_get();
    return app_mode_get() != APP_MODE_ULTRALIGHT && terminal.supported && terminal.cell_height > 0;
}
/* The text field whose value was refused: it gets a border of three rows. */
static bool field_refused(const server_settings *s, const admin_field *f)
{
    return s->invalid && s->invalid_tab == s->tab && f->id == s->invalid_id &&
           (f->kind == FIELD_TEXT || f->kind == FIELD_SECRET);
}
static int field_height(const server_settings *s, const admin_field *f, int width)
{
    if (f->same_row)
        return 0;
    if (field_refused(s, f))
        return 3;
    if (f->kind == FIELD_EMOJI) {
        int cell = graphics_terminal_get().cell_height;
        return cell > 0 ? (EMOJI_PREVIEW_PIXELS + cell - 1) / cell : 1;
    }
    return f->kind == FIELD_NOTE ? note_rows(NULL, f->value, 0, 0, width, 8, false) : 1;
}
/* The picture comes from the server's emoji catalog, which the picker shares. */
static void draw_emoji(server_settings *s, WINDOW *w, const admin_field *f, int y)
{
    chat_assets *assets = history_assets(s->model);
    uint32_t icon = 0;
    for (size_t i = 0; i < chat_assets_emoji_count(assets) && !icon; ++i)
        if (!strcmp(chat_assets_emoji(assets, i)->id, f->key))
            icon = ICON_CUSTOM | (uint32_t)(i + 1);
    icons_scope(s->icons, "server-settings", 0);
    if (!icon || !icons_paint_size(s->icons, w, icon, y, s->form_x, THEME_PANEL, icon,
                                   EMOJI_PREVIEW_PIXELS)) {
        wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
        text_label(w, y, s->form_x, s->form_w, _("Carregando imagem…"));
    }
}
static void draw_list(server_settings *s, WINDOW *w, GArray *rows)
{
    const admin_tab_ops *ops = ops_of(s->tab);
    int y = s->top, x = s->list_x, width = s->list_w;
    s->row_count = 0;
    if (ops->search) {
        char line[192];
        snprintf(line, sizeof(line), _("Buscar: %s%s"), s->query, s->focus == 1 ? "▏" : "");
        wattrset(w, COLOR_PAIR(*s->query ? THEME_PANEL : THEME_PANEL_MUTED));
        mvwhline(w, y, x, ' ', width);
        text_label(w, y, x, width,
                   *s->query || s->focus == 1 ? line : _("Buscar: digite para filtrar"));
        ++y;
    }
    int visible = s->bottom - y;
    if (visible < 1)
        return;
    if (s->list_selected < s->list_first)
        s->list_first = s->list_selected;
    if (s->list_selected >= s->list_first + visible)
        s->list_first = s->list_selected - visible + 1;
    if (s->list_first < 0)
        s->list_first = 0;
    if (!rows->len) {
        wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
        text_label(w, y, x + 1, width - 1, ops->empty ? _(ops->empty) : _("Nada para mostrar."));
        return;
    }
    for (int i = s->list_first; i < (int)rows->len && y < s->bottom; ++i, ++y) {
        const admin_row *row = &g_array_index(rows, admin_row, i);
        bool selected = i == s->list_selected;
        int pair = selected      ? (s->focus == 1 ? THEME_FOCUSED : THEME_PANEL_ACTIVE)
                   : row->action ? THEME_PANEL_ACCENT
                                 : THEME_PANEL;
        wattrset(w,
                 COLOR_PAIR(pair) | (selected && s->focus == 1 && !has_colors() ? A_REVERSE : 0));
        mvwhline(w, y, x, ' ', width);
        int used = 1;
        if (row->rgb) {
            wattrset(w, COLOR_PAIR(theme_rgb_pair(row->rgb, pair)));
            text_label(w, y, x + used, 1, "●");
            used += 2;
            wattrset(w, COLOR_PAIR(pair));
        }
        int label = text_label(w, y, x + used, width - used - 1, row->label);
        used += label;
        if (*row->detail && used + 2 < width - 1) {
            wattrset(w, COLOR_PAIR(selected ? pair : THEME_PANEL_MUTED));
            text_label(w, y, x + used + 1, width - used - 2, row->detail);
        }
        if (s->row_count < SERVER_SETTINGS_ROWS) {
            s->row_y[s->row_count] = y;
            s->row_index[s->row_count++] = i;
        }
    }
    if (s->list_first > 0) {
        wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
        text_label(w, s->top + (ops->search ? 1 : 0), x + width - 1, 1, "↑");
    }
    if (s->list_first + visible < (int)rows->len) {
        wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
        text_label(w, s->bottom - 1, x + width - 1, 1, "↓");
    }
}
static int label_width(const server_settings *s)
{
    return s->form_w >= 48 ? 18 : s->form_w / 3;
}
/* Cells a button takes, as draw_field paints it. */
static int button_width(const server_settings *s, const admin_field *f, int width)
{
    bool confirming = s->confirm == f->id && !strcmp(s->confirm_key, f->key);
    char line[800];
    snprintf(line, sizeof(line), confirming ? _("Confirmar: %s") : "%s", f->label);
    int bw = text_label(NULL, 0, 0, width, line) + 4;
    return bw > width ? width : bw;
}
static void draw_field(server_settings *s, WINDOW *w, const admin_field *f, int index, int y,
                       int height, int column)
{
    int x = column, width = s->form_w - (column - s->form_x);
    int label_width = width >= 48 ? 18 : width / 3;
    bool focused = s->focus == 2 && s->field == index;
    char line[800];
    switch (f->kind) {
    case FIELD_HEADING:
        wattrset(w, COLOR_PAIR(THEME_PANEL_ACCENT) | A_BOLD);
        text_label(w, y, x, width, f->label);
        break;
    case FIELD_NOTE:
        wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
        note_rows(w, f->value, y, x, width, height, true);
        break;
    case FIELD_INFO:
        wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
        snprintf(line, sizeof(line), "%s:", f->label);
        text_label(w, y, x, label_width - 1, line);
        wattrset(w, COLOR_PAIR(THEME_PANEL));
        if (f->rgb) {
            wattrset(w, COLOR_PAIR(theme_rgb_pair(f->rgb, THEME_PANEL)));
            text_label(w, y, x + label_width, 1, "●");
            wattrset(w, COLOR_PAIR(THEME_PANEL));
            text_label(w, y, x + label_width + 2, width - label_width - 2, f->value);
        } else
            text_label(w, y, x + label_width, width - label_width, f->value);
        break;
    case FIELD_TEXT:
    case FIELD_SECRET: {
        bool refused = field_refused(s, f);
        /* A refused input sits inside a red border: the rows above and below are its own. */
        if (refused)
            ++y;
        wattrset(w, COLOR_PAIR(refused   ? THEME_NOTIFICATION
                               : focused ? THEME_PANEL_ACCENT
                                         : THEME_PANEL) |
                        (focused || refused ? A_BOLD : 0));
        snprintf(line, sizeof(line), "%s:", f->label);
        text_label(w, y, x, label_width - 1, line);
        int counter = f->max ? 9 : 0;
        int fx = x + label_width, fw = width - label_width - counter - (f->rgb ? 2 : 0);
        if (fw < 4)
            fw = 4;
        if (refused) {
            --fw;
            input_border_draw(w, y, fx, fw, THEME_NOTIFICATION, true, true);
        }
        wattrset(w, COLOR_PAIR(focused ? THEME_FOCUSED : THEME_INPUT) |
                        (focused && !has_colors() ? A_REVERSE : 0));
        mvwhline(w, y, fx, ' ', fw);
        if (f->text)
            text_draw_field(w, f->text, f->first, y, fx, fw, f->kind == FIELD_SECRET);
        if (focused) {
            int cy, cx;
            getyx(w, cy, cx);
            s->cursor = true;
            s->cursor_y = getbegy(w) + cy;
            s->cursor_x = getbegx(w) + cx;
        }
        if (f->rgb) {
            wattrset(w, COLOR_PAIR(theme_rgb_pair(f->rgb, THEME_PANEL)));
            text_label(w, y, fx + fw + 1, 1, "●");
        }
        if (f->max && f->text) {
            bool over = f->text->length > f->max;
            wattrset(w, COLOR_PAIR(over ? THEME_ERROR : THEME_PANEL_MUTED));
            snprintf(line, sizeof(line), "%zu/%zu", f->text->length, f->max);
            text_label(w, y, x + width - counter + 1, counter - 1, line);
        }
        break;
    }
    case FIELD_CHECK:
        wattrset(w, COLOR_PAIR(focused ? THEME_FOCUSED : THEME_PANEL) |
                        (focused && !has_colors() ? A_REVERSE : 0));
        snprintf(line, sizeof(line), "[%s] %s", f->checked ? "x" : " ", f->label);
        mvwhline(w, y, x, ' ', width);
        text_label(w, y, x, width, line);
        break;
    case FIELD_CHOICE: {
        wattrset(w,
                 COLOR_PAIR(focused ? THEME_PANEL_ACCENT : THEME_PANEL) | (focused ? A_BOLD : 0));
        snprintf(line, sizeof(line), "%s:", f->label);
        text_label(w, y, x, label_width - 1, line);
        snprintf(line, sizeof(line), "< %s >", f->value);
        int cw = text_label(NULL, 0, 0, width - label_width, line) + 2;
        if (cw > width - label_width)
            cw = width - label_width;
        button_draw(w, y, x + label_width, cw, line, focused ? BUTTON_FOCUSED : BUTTON_NORMAL,
                    BUTTON_PANEL);
        break;
    }
    case FIELD_EMOJI:
        draw_emoji(s, w, f, y);
        break;
    case FIELD_BUTTON: {
        bool confirming = s->confirm == f->id && !strcmp(s->confirm_key, f->key);
        snprintf(line, sizeof(line), confirming ? _("Confirmar: %s") : "%s", f->label);
        int bw = text_label(NULL, 0, 0, width, line) + 4;
        if (bw > width)
            bw = width;
        if ((f->destructive && focused) || confirming) {
            wattrset(w, COLOR_PAIR(THEME_DESTRUCTIVE) | A_BOLD);
            mvwhline(w, y, x, ' ', bw);
            text_label(w, y, x + 2, bw - 4, line);
        } else
            button_draw(w, y, x, bw, line, focused ? BUTTON_FOCUSED : BUTTON_NORMAL, BUTTON_PANEL);
        break;
    }
    }
}
static void draw_form(server_settings *s, WINDOW *w, GArray *fields)
{
    s->field_rows = 0;
    int count = (int)fields->len, available = s->bottom - s->top;
    if (!count || available < 1)
        return;
    /* Forms beyond the height table are cut there rather than overrunning it. */
    if (count > SERVER_SETTINGS_FIELDS)
        count = SERVER_SETTINGS_FIELDS;
    int heights[SERVER_SETTINGS_FIELDS];
    for (int i = 0; i < count; ++i)
        heights[i] = field_height(s, &g_array_index(fields, admin_field, i), s->form_w);
    /* Scroll so the focused control and the title above it stay visible. */
    if (s->focus != 2)
        s->field_first = s->field_first < count ? s->field_first : 0;
    if (s->field < s->field_first)
        s->field_first = s->field;
    for (;;) {
        int used = 0;
        for (int i = s->field_first; i <= s->field && i < count; ++i)
            used += heights[i];
        if (used <= available || s->field_first >= s->field)
            break;
        ++s->field_first;
    }
    if (s->focus != 2 && s->field_first > 0 && s->field == 0)
        s->field_first = 0;
    /* A row starts at the button that owns it. */
    while (s->field_first > 0 && g_array_index(fields, admin_field, s->field_first).same_row)
        --s->field_first;
    int y = s->top, row_y = y, row_end = s->form_x;
    for (int i = s->field_first; i < count && y + heights[i] <= s->bottom; ++i) {
        admin_field *f = &g_array_index(fields, admin_field, i);
        bool beside = f->same_row && i > s->field_first;
        int at = beside ? row_y : y, column = beside ? row_end + 2 : s->form_x;
        if (column >= s->form_x + s->form_w)
            continue;
        draw_field(s, w, f, i, at, beside ? 1 : heights[i], column);
        if (s->field_rows < SERVER_SETTINGS_FIELDS) {
            s->field_y[s->field_rows] = at;
            s->field_x[s->field_rows] = column;
            s->field_index[s->field_rows++] = i;
        }
        row_y = at;
        row_end =
            column + (f->kind == FIELD_BUTTON ? button_width(s, f, s->form_w - (column - s->form_x))
                                              : s->form_w);
        y += heights[i];
    }
    wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
    if (s->field_first > 0)
        text_label(w, s->top, s->form_x + s->form_w - 1, 1, "↑");
    if (y < s->bottom || s->field_rows < count - s->field_first)
        if (s->field_rows && s->field_index[s->field_rows - 1] < count - 1)
            text_label(w, s->bottom - 1, s->form_x + s->form_w - 1, 1, "↓");
}
static void draw_notice(server_settings *s, WINDOW *w, int height, int width)
{
    const char *text = s->error;
    int pair = THEME_ERROR;
    if (!*text && *s->feedback) {
        text = s->feedback;
        pair = THEME_PANEL_MUTED;
    }
    if (!*text) {
        admin_state state = server_admin_state(s->model);
        text = server_admin_notice(s->model);
        if (state == ADMIN_SAVING && !*text)
            text = _("Enviando…");
        pair = state == ADMIN_FAILED || state == ADMIN_UNCERTAIN ? THEME_ERROR
               : state == ADMIN_SAVED                            ? THEME_SUCCESS
                                                                 : THEME_PANEL_MUTED;
    }
    if (!*text && s->pending) {
        text = server_admin_permissions_notice(s->model);
        pair = THEME_PANEL_MUTED;
    }
    if (!*text)
        return;
    wattrset(w, COLOR_PAIR(pair));
    text_label(w, height - 3, 2, width - 18, text);
}
void server_settings_draw(server_settings *s)
{
    if (!s->open || !s->model)
        return;
    int width = COLS > 112 ? 110 : COLS - 2, height = LINES > 34 ? 32 : LINES - 2;
    if (width < 60 || height < 16)
        return;
    if (s->window) {
        delwin(s->window);
        s->window = NULL;
    }
    s->window = newwin(height, width, (LINES - height) / 2, (COLS - width) / 2);
    if (!s->window)
        return;
    WINDOW *w = s->window;
    /* Pictures behind the panel are cut here; the preview drawn inside stays. */
    icons_overlay(s->icons, getbegy(w), getbegx(w), height, width);
    wbkgd(w, COLOR_PAIR(THEME_PANEL));
    werase(w);
    wattrset(w, COLOR_PAIR(THEME_BORDER));
    box(w, 0, 0);
    char title[200];
    snprintf(title, sizeof(title), _("Configurações do servidor · %s"),
             *s->server ? s->server : "servidor");
    wattrset(w, COLOR_PAIR(THEME_PANEL_ACCENT) | A_BOLD);
    text_label(w, 1, 2, width - 4, title);
    const char *labels[ADMIN_TABS];
    for (int i = 0; i < s->tab_count; ++i)
        labels[i] = _(tab_names[s->tabs[i]]);
    tab_strip_draw(&s->strip, w, 3, width, labels, s->tab_count, tab_index(s), s->focus == 0);
    wattrset(w, COLOR_PAIR(THEME_BORDER));
    mvwhline(w, 4, 1, ACS_HLINE, width - 2);
    const admin_tab_ops *ops = ops_of(s->tab);
    s->top = 5;
    s->bottom = height - 3;
    s->cursor = false;
    GArray *rows = build_rows(s);
    settle(s, rows);
    if (ops->list) {
        s->list_x = 2;
        s->list_w = width * 2 / 5 > 38 ? 38 : width * 2 / 5;
        s->form_x = s->list_x + s->list_w + 2;
        s->form_w = width - s->form_x - 2;
        wattrset(w, COLOR_PAIR(THEME_BORDER));
        mvwvline(w, s->top, s->list_x + s->list_w, ACS_VLINE, s->bottom - s->top);
        draw_list(s, w, rows);
    } else {
        s->list_w = s->row_count = 0;
        s->form_x = 2;
        s->form_w = width - 4;
    }
    GArray *fields = build_fields(s);
    draw_form(s, w, fields);
    draw_notice(s, w, height, width);
    button_draw(w, height - 2, width - 14, 12, _("Fechar"),
                s->focus == 3 ? BUTTON_FOCUSED : BUTTON_NORMAL, BUTTON_PANEL);
    g_array_unref(fields);
    g_array_unref(rows);
    wnoutrefresh(w);
}
bool server_settings_cursor(const server_settings *s)
{
    if (!s->open || !s->cursor || !s->window || s->focus != 2)
        return false;
    setsyx(s->cursor_y, s->cursor_x);
    return true;
}
static admin_field *focused_field(GArray *fields, const server_settings *s)
{
    if (s->field < 0 || s->field >= (int)fields->len)
        return NULL;
    admin_field *f = &g_array_index(fields, admin_field, s->field);
    return focusable(f) ? f : NULL;
}
bool server_settings_paste(server_settings *s, text_paste *paste)
{
    if (!s->open || s->focus != 2)
        return false;
    GArray *rows = build_rows(s);
    settle(s, rows);
    GArray *fields = build_fields(s);
    admin_field *f = focused_field(fields, s);
    bool handled = f && (f->kind == FIELD_TEXT || f->kind == FIELD_SECRET) && f->text;
    if (handled) {
        if (!text_paste_commit(paste, f->text))
            admin_error(s, _("Colagem excede o limite do campo."));
        else
            s->error[0] = 0;
        s->dirty = true;
    }
    g_array_unref(fields);
    g_array_unref(rows);
    return handled;
}

static void move_field(server_settings *s, GArray *fields, int direction)
{
    int count = (int)fields->len;
    for (int i = s->field + direction; i >= 0 && i < count; i += direction)
        if (focusable(&g_array_index(fields, admin_field, i))) {
            s->field = i;
            s->confirm = -1;
            return;
        }
    /* Past the edge: leave the form. */
    if (direction > 0)
        s->focus = 3;
    else
        s->focus = ops_of(s->tab)->list ? 1 : 0;
    s->confirm = -1;
}
static void choose_row(server_settings *s, GArray *rows)
{
    if (s->list_selected < 0 || s->list_selected >= (int)rows->len)
        return;
    const admin_row *row = &g_array_index(rows, admin_row, s->list_selected);
    const admin_tab_ops *ops = ops_of(s->tab);
    if (ops->choose)
        ops->choose(s, row);
    else
        s->focus = 2;
}
static void select_row(server_settings *s, GArray *rows, int index)
{
    if (!rows->len)
        return;
    if (index < 0)
        index = 0;
    if (index >= (int)rows->len)
        index = (int)rows->len - 1;
    s->list_selected = index;
    snprintf(s->selected, sizeof(s->selected), "%s", g_array_index(rows, admin_row, index).id);
}
static void switch_tab(server_settings *s, int index)
{
    if (!s->tab_count)
        return;
    index = (index % s->tab_count + s->tab_count) % s->tab_count;
    if (s->tabs[index] != s->tab)
        enter_tab(s, s->tabs[index]);
    s->feedback[0] = 0;
}
static void activate(server_settings *s, const admin_field *f, int direction)
{
    const admin_tab_ops *ops = ops_of(s->tab);
    if (f->kind != FIELD_BUTTON || !f->destructive)
        s->confirm = -1;
    if (ops->activate)
        ops->activate(s, f, direction);
}
static void mouse(server_settings *s, GArray *rows, GArray *fields)
{
    MEVENT e;
    if (ui_mouse_get(&e) != OK || !s->window)
        return;
    s->error[0] = 0;
    int y = e.y, x = e.x;
    if (!wmouse_trafo(s->window, &y, &x, false))
        return;
    bool wheel = (e.bstate & (BUTTON4_PRESSED | BUTTON5_PRESSED)) != 0;
    int direction = e.bstate & BUTTON4_PRESSED ? -1 : 1;
    bool in_list = ops_of(s->tab)->list && x >= s->list_x && x < s->list_x + s->list_w &&
                   y >= s->top && y < s->bottom;
    if (wheel) {
        if (in_list) {
            select_row(s, rows, s->list_selected + direction * 3);
            s->focus = 1;
        } else if (y >= s->top && y < s->bottom) {
            s->focus = 2;
            for (int i = 0; i < 3; ++i) {
                int before = s->field;
                move_field(s, fields, direction);
                s->focus = 2;
                if (before == s->field)
                    break;
            }
        }
        return;
    }
    if (y == 3) {
        int chosen = tab_strip_hit(&s->strip, s->tab_count, tab_index(s), x, getmaxx(s->window));
        if (chosen >= 0)
            switch_tab(s, chosen);
        s->focus = 0;
        return;
    }
    if (y == getmaxy(s->window) - 2 && x >= getmaxx(s->window) - 14) {
        server_settings_close(s);
        return;
    }
    if (in_list) {
        for (int i = 0; i < s->row_count; ++i)
            if (s->row_y[i] == y) {
                bool same = s->list_selected == s->row_index[i];
                select_row(s, rows, s->row_index[i]);
                s->focus = 1;
                const admin_row *row = &g_array_index(rows, admin_row, s->list_selected);
                if (row->action || same)
                    choose_row(s, rows);
                return;
            }
        s->focus = 1;
        return;
    }
    if (x >= s->form_x && y >= s->top && y < s->bottom) {
        int hit = -1, hit_y = -1;
        for (int i = 0; i < s->field_rows; ++i)
            if (s->field_y[i] <= y) {
                hit = s->field_index[i];
                hit_y = s->field_y[i];
            }
        /* Buttons sharing a row: the one that starts left of the pointer. */
        for (int i = 0; i < s->field_rows; ++i)
            if (s->field_y[i] == hit_y && s->field_x[i] <= x)
                hit = s->field_index[i];
        if (hit < 0 || hit >= (int)fields->len)
            return;
        admin_field *f = &g_array_index(fields, admin_field, hit);
        if (!focusable(f))
            return;
        s->focus = 2;
        s->field = hit;
        if (f->kind == FIELD_CHECK || f->kind == FIELD_BUTTON)
            activate(s, f, 0);
        else if (f->kind == FIELD_CHOICE)
            /* The ‹<› arrow goes back; the rest of the choice goes forward. */
            activate(s, f, x <= s->form_x + label_width(s) + 1 ? -1 : 1);
    }
}
static bool list_key(server_settings *s, GArray *rows, int type, uint32_t key)
{
    const admin_tab_ops *ops = ops_of(s->tab);
    bool enter =
        (type == OK && (key == '\r' || key == '\n')) || (type == KEY_CODE_YES && key == KEY_ENTER);
    if (type == KEY_CODE_YES) {
        int visible = s->bottom - s->top - (ops->search ? 1 : 0);
        switch (key) {
        case KEY_UP:
            if (s->list_selected <= 0)
                s->focus = 0;
            else
                select_row(s, rows, s->list_selected - 1);
            return true;
        case KEY_DOWN:
            if (s->list_selected + 1 < (int)rows->len)
                select_row(s, rows, s->list_selected + 1);
            else
                s->focus = 2;
            return true;
        case KEY_PPAGE:
            select_row(s, rows, s->list_selected - (visible > 1 ? visible - 1 : 1));
            return true;
        case KEY_NPAGE:
            select_row(s, rows, s->list_selected + (visible > 1 ? visible - 1 : 1));
            return true;
        case KEY_HOME:
            select_row(s, rows, 0);
            return true;
        case KEY_END:
            select_row(s, rows, (int)rows->len - 1);
            return true;
        case KEY_RIGHT:
            s->focus = 2;
            return true;
        case KEY_BACKSPACE:
            break;
        default:
            if (!enter)
                return false;
        }
    }
    if (enter) {
        choose_row(s, rows);
        return true;
    }
    if (!ops->search)
        return false;
    size_t length = strlen(s->query);
    if ((type == KEY_CODE_YES && key == KEY_BACKSPACE) ||
        (type == OK && (key == 127 || key == 8))) {
        while (length && (s->query[length - 1] & 0xc0) == 0x80)
            --length;
        if (length)
            --length;
        s->query[length] = 0;
        s->list_selected = 0;
        s->selected[0] = 0;
        return true;
    }
    if (type == OK && key >= 32 && key != 127 && g_unichar_isprint(key)) {
        char utf8[8];
        int bytes = g_unichar_to_utf8(key, utf8);
        if (length + (size_t)bytes < sizeof(s->query)) {
            memcpy(s->query + length, utf8, (size_t)bytes);
            s->query[length + (size_t)bytes] = 0;
            s->list_selected = 0;
            s->selected[0] = 0;
        }
        return true;
    }
    return false;
}
static void form_key(server_settings *s, GArray *fields, int type, uint32_t key)
{
    bool enter =
        (type == OK && (key == '\r' || key == '\n')) || (type == KEY_CODE_YES && key == KEY_ENTER);
    bool up = type == KEY_CODE_YES && key == KEY_UP, down = type == KEY_CODE_YES && key == KEY_DOWN;
    admin_field *f = focused_field(fields, s);
    if (!f) {
        if (up)
            s->focus = ops_of(s->tab)->list ? 1 : 0;
        else if (down)
            s->focus = 3;
        return;
    }
    if (up || down) {
        move_field(s, fields, up ? -1 : 1);
        return;
    }
    if (f->kind == FIELD_TEXT || f->kind == FIELD_SECRET) {
        if (enter) {
            const admin_tab_ops *ops = ops_of(s->tab);
            if (ops->submit)
                ops->submit(s);
            return;
        }
        if (f->text && text_edit(f->text, type, key, TEXT_LIMIT * 2 + 1, false)) {
            s->dirty = true;
            s->error[0] = 0;
            if (f->id == s->invalid_id && s->invalid_tab == s->tab)
                s->invalid = false;
        }
        return;
    }
    /* ←/→ move between the buttons of one row. */
    if (type == KEY_CODE_YES && key == KEY_RIGHT && s->field + 1 < (int)fields->len &&
        g_array_index(fields, admin_field, s->field + 1).same_row) {
        ++s->field;
        s->confirm = -1;
        return;
    }
    if (type == KEY_CODE_YES && key == KEY_LEFT && f->same_row && s->field > 0) {
        --s->field;
        s->confirm = -1;
        return;
    }
    if (type == KEY_CODE_YES && key == KEY_LEFT && f->kind != FIELD_CHOICE) {
        if (ops_of(s->tab)->list)
            s->focus = 1;
        return;
    }
    if (f->kind == FIELD_CHOICE && type == KEY_CODE_YES && (key == KEY_LEFT || key == KEY_RIGHT)) {
        activate(s, f, key == KEY_LEFT ? -1 : 1);
        return;
    }
    if (enter || (type == OK && key == ' '))
        activate(s, f, f->kind == FIELD_CHOICE ? 1 : 0);
}
void server_settings_key(server_settings *s, int type, uint32_t key)
{
    if (!s->open || !s->model)
        return;
    GArray *rows = build_rows(s);
    settle(s, rows);
    GArray *fields = build_fields(s);
    const admin_tab_ops *ops = ops_of(s->tab);
    bool enter =
        (type == OK && (key == '\r' || key == '\n')) || (type == KEY_CODE_YES && key == KEY_ENTER);
    char error[sizeof(s->error)];
    snprintf(error, sizeof(error), "%s", s->error);
    /* Releases and pointer motion do nothing, so they leave the last notice
     * alone: the click that raised it is followed by them at once. */
    bool pointer = type == KEY_CODE_YES && key == KEY_MOUSE;
    if (!pointer)
        s->error[0] = 0;
    if (pointer) {
        mouse(s, rows, fields);
        goto done;
    }
    if (type == OK && key == 27) {
        server_settings_close(s);
        goto done;
    }
    bool has_list = ops->list, has_form = any_focusable(fields);
    if ((type == OK && key == '\t') || (type == KEY_CODE_YES && key == KEY_BTAB)) {
        int step = type == OK ? 1 : 3;
        for (int i = 0; i < 4; ++i) {
            s->focus = (s->focus + step) % 4;
            if ((s->focus == 1 && !has_list) || (s->focus == 2 && !has_form))
                continue;
            break;
        }
        s->confirm = -1;
        goto done;
    }
    switch (s->focus) {
    case 0:
        if (type == KEY_CODE_YES && (key == KEY_LEFT || key == KEY_RIGHT))
            switch_tab(s, tab_index(s) + (key == KEY_LEFT ? -1 : 1));
        else if (enter || (type == KEY_CODE_YES && key == KEY_DOWN))
            s->focus = has_list ? 1 : has_form ? 2 : 3;
        break;
    case 1:
        if (!has_list) {
            s->focus = 0;
            break;
        }
        if (!list_key(s, rows, type, key))
            snprintf(s->error, sizeof(s->error), "%s", error);
        if (s->focus == 2 && !has_form)
            s->focus = 3;
        break;
    case 2:
        form_key(s, fields, type, key);
        break;
    default:
        if (type == KEY_CODE_YES && key == KEY_UP)
            s->focus = has_form ? 2 : has_list ? 1 : 0;
        else if (enter)
            server_settings_close(s);
        break;
    }
done:
    g_array_unref(fields);
    g_array_unref(rows);
}
