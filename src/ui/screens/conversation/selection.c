#include "ui/screens/conversation/private.h"
#include <limits.h>

/* A visible cell of a history row: its body column, width and text. Image
 * cells are enlarged emojis drawn over blank cells. */
typedef void (*cell_visitor)(void *context, int column, int width, const char *text, bool image);

typedef struct {
    int column, width;
    char text[128];
} piece;

typedef struct {
    piece *items;
    size_t count, capacity;
    bool failed;
} pieces;

typedef struct {
    view *ui;
    int y, from, to;
} painter;

static bool find(view *ui, const char *id, size_t *index)
{
    if (!*id)
        return false;
    for (size_t i = conversation_entry_count(ui); i > 0; --i)
        if (!strcmp(conversation_str(conversation_entry(ui, i - 1), "id"), id)) {
            *index = i - 1;
            return true;
        }
    return false;
}

static int compare(size_t ai, const history_point *a, size_t bi, const history_point *b)
{
    if (ai != bi)
        return ai < bi ? -1 : 1;
    if (a->line != b->line)
        return a->line < b->line ? -1 : 1;
    return a->column < b->column ? -1 : a->column > b->column;
}

static bool same(const history_point *a, const history_point *b)
{
    return !strcmp(a->id, b->id) && a->line == b->line && a->column == b->column;
}

/* Orders anchor and head; false when either message left the history. */
static bool ordered(view *ui, history_point *from, size_t *from_index, history_point *to,
                    size_t *to_index)
{
    size_t anchor, head;
    if (!find(ui, ui->select_anchor.id, &anchor) || !find(ui, ui->select_head.id, &head))
        return false;
    bool forward = compare(anchor, &ui->select_anchor, head, &ui->select_head) <= 0;
    *from = forward ? ui->select_anchor : ui->select_head;
    *to = forward ? ui->select_head : ui->select_anchor;
    *from_index = forward ? anchor : head;
    *to_index = forward ? head : anchor;
    return true;
}

/* The selected columns of line in message index, or false outside it. */
static bool columns(size_t index, int line, const history_point *from, size_t from_index,
                    const history_point *to, size_t to_index, int *first, int *last)
{
    if (index < from_index || index > to_index || (index == from_index && line < from->line) ||
        (index == to_index && line > to->line))
        return false;
    *first = index == from_index && line == from->line ? from->column : INT_MIN;
    *last = index == to_index && line == to->line ? to->column : INT_MAX;
    return true;
}

static bool covers(int column, int width, int first, int last)
{
    return column <= last && column + (width > 0 ? width : 1) - 1 >= first;
}

static void encode(const text_buffer *buffer, size_t start, size_t end, char *out, size_t size)
{
    size_t used = 0;
    for (size_t i = start; i < end; ++i) {
        char bytes[6];
        int length = g_unichar_to_utf8((gunichar)buffer->data[i], bytes);
        if (used + (size_t)length >= size)
            break;
        memcpy(out + used, bytes, (size_t)length);
        used += (size_t)length;
    }
    out[used] = 0;
}

/* Visits the cells as message_row_draw lays them out. The initials drawn in
 * the avatar gutter are decoration and stay out of the selection. */
static void row_cells(view *ui, const message_row *r, cell_visitor visit, void *context)
{
    int base = 2 + r->x + r->indent;
    text_buffer *buffer = NULL;
    text_layout *layout = NULL;
    for (int k = 0; k < r->count; ++k) {
        const message_segment *s = &r->segments[k];
        if (!s->text || !*s->text || !strcmp(s->key, "header:avatar"))
            continue;
        if (!buffer && !(buffer = malloc(sizeof(*buffer))))
            break;
        if (!layout && !(layout = malloc(sizeof(*layout))))
            break;
        if (!text_decode(buffer, s->text))
            continue;
        text_measure(buffer, s->width + 1, layout);
        int used = 0;
        for (size_t c = 0; c + 1 < layout->count; ++c) {
            const text_cell *cell = &layout->cells[c];
            if (cell->row || cell->newline || used + cell->width > s->width)
                break;
            char text[128] = {0};
            bool icon = false;
            /* Inline emoji images stand in for their text on a wide blank. */
            if (cell->end == cell->start + 1 && buffer->data[cell->start] == 0x3000)
                for (size_t n = 0; n < r->icon_count && !icon; ++n)
                    icon = !r->icons[n].pixels && r->icons[n].x == s->x + used &&
                           icons_text(ui->icons, r->icons[n].id, text, sizeof(text));
            if (!icon)
                encode(buffer, cell->start, cell->end, text, sizeof(text));
            visit(context, base + s->x + used, cell->width, text, false);
            used += cell->width;
        }
    }
    free(buffer);
    free(layout);
    int cell_width = graphics_terminal_get().cell_width;
    for (size_t n = 0; n < r->icon_count; ++n) {
        const message_icon_region *icon = &r->icons[n];
        char text[128];
        if (!icon->pixels || !icons_text(ui->icons, icon->id, text, sizeof(text)))
            continue;
        int width = cell_width > 0 ? MAX(1, icon->pixels / cell_width) : 2;
        /* An enlarged emoji spans several rows; its text belongs to the first. */
        visit(context, base + icon->x, width, icon->row ? "" : text, true);
    }
}

static void paint_cell(void *context, int column, int width, const char *text, bool image)
{
    (void)text;
    painter *p = context;
    if (image || !covers(column, width, p->from, p->to) || column < 0 ||
        column >= getmaxx(p->ui->body))
        return;
    /* The same colors as a selection in the editor. */
    mvwchgat(p->ui->body, p->y, column, MAX(width, 1), A_BOLD | (has_colors() ? 0 : A_REVERSE),
             has_colors() ? THEME_TEXT_SELECTION : 0, NULL);
}

static void collect(void *context, int column, int width, const char *text, bool image)
{
    (void)image;
    pieces *p = context;
    if (!*text || p->failed)
        return;
    if (p->count == p->capacity) {
        size_t capacity = p->capacity ? p->capacity * 2 : 64;
        piece *items = realloc(p->items, capacity * sizeof(*items));
        if (!items) {
            p->failed = true;
            return;
        }
        p->items = items;
        p->capacity = capacity;
    }
    piece *item = &p->items[p->count++];
    item->column = column;
    item->width = width;
    snprintf(item->text, sizeof(item->text), "%s", text);
}

static int by_column(const void *a, const void *b)
{
    const piece *x = a, *y = b;
    return x->column < y->column ? -1 : x->column > y->column;
}

static void wipe_text(GString *text)
{
    volatile char *bytes = text->str;
    for (size_t i = 0; i < text->allocated_len; ++i)
        bytes[i] = 0;
}

/* The selected text, one history row per line. Blank rows between messages
 * collapse into one empty line, as they read on screen. Owned by the caller. */
static GString *selected_text(view *ui)
{
    history_point from, to;
    size_t from_index, to_index;
    if (!ui->body || !ordered(ui, &from, &from_index, &to, &to_index))
        return NULL;
    int width = getmaxx(ui->body) - 4;
    GString *out = g_string_new(NULL);
    GString *line_text = g_string_new(NULL);
    bool any = false, failed = false;
    size_t blank = 0;
    for (size_t i = from_index; i <= to_index && !failed; ++i) {
        cached_layout *cached = conversation_find_layout(ui, i, width);
        message_blocks *b = conversation_entry_blocks(ui, i, cached, width);
        if (!b || !b->count)
            continue;
        int top = INT_MAX, bottom = INT_MIN;
        for (size_t row = 0; row < b->count; ++row) {
            top = MIN(top, b->rows[row].y);
            bottom = MAX(bottom, b->rows[row].y);
        }
        for (int line = top; line <= bottom && !failed; ++line) {
            int first, last;
            if (!columns(i, line, &from, from_index, &to, to_index, &first, &last))
                continue;
            pieces found = {0};
            bool rows = false;
            for (size_t row = 0; row < b->count; ++row)
                if (b->rows[row].y == line) {
                    rows = true;
                    row_cells(ui, &b->rows[row], collect, &found);
                }
            failed = found.failed;
            if (found.count)
                qsort(found.items, found.count, sizeof(*found.items), by_column);
            g_string_truncate(line_text, 0);
            int end = INT_MIN;
            for (size_t k = 0; k < found.count; ++k) {
                const piece *item = &found.items[k];
                if (!covers(item->column, item->width, first, last))
                    continue;
                if (end != INT_MIN && item->column > end)
                    g_string_append_c(line_text, ' ');
                g_string_append(line_text, item->text);
                end = item->column + item->width;
            }
            free(found.items);
            while (line_text->len && line_text->str[line_text->len - 1] == ' ')
                g_string_truncate(line_text, line_text->len - 1);
            if (!rows)
                continue;
            if (!line_text->len) {
                ++blank;
                continue;
            }
            if (any)
                g_string_append(out, blank ? "\n\n" : "\n");
            g_string_append_len(out, line_text->str, (gssize)line_text->len);
            any = true;
            blank = 0;
            if (out->len > CLIPBOARD_TEXT_LIMIT)
                break;
        }
    }
    wipe_text(line_text);
    g_string_free(line_text, true);
    if (failed) {
        wipe_text(out);
        g_string_free(out, true);
        return NULL;
    }
    return out;
}

void conversation_selection_rows(view *ui)
{
    int count = ui->visible > 0 ? ui->visible : 0;
    if (count != ui->select_row_count) {
        history_row *rows = count ? realloc(ui->select_rows, (size_t)count * sizeof(*rows)) : NULL;
        if (count && !rows)
            count = 0;
        if (!count) {
            free(ui->select_rows);
            rows = NULL;
        }
        ui->select_rows = rows;
        ui->select_row_count = count;
    }
    if (ui->select_rows)
        memset(ui->select_rows, 0, (size_t)ui->select_row_count * sizeof(*ui->select_rows));
    if ((ui->select_shown || ui->select_dragging) &&
        !ordered(ui, &ui->select_from, &ui->select_from_index, &ui->select_to,
                 &ui->select_to_index))
        conversation_selection_clear(ui);
}

void conversation_selection_row(view *ui, int row, const char *id, int line)
{
    if (row < 0 || row >= ui->select_row_count || !id || !*id)
        return;
    snprintf(ui->select_rows[row].id, sizeof(ui->select_rows[row].id), "%s", id);
    ui->select_rows[row].line = line;
}

void conversation_selection_paint(view *ui, size_t index, const message_row *r, int y)
{
    painter p = {.ui = ui, .y = y};
    if (!ui->select_shown || !ui->body ||
        !columns(index, r->y, &ui->select_from, ui->select_from_index, &ui->select_to,
                 ui->select_to_index, &p.from, &p.to))
        return;
    row_cells(ui, r, paint_cell, &p);
}

/* The history cell under the pointer. Beyond the history rows the point
 * clamps to the nearest message: its start below the top, its end above. */
static bool point(view *ui, int y, int x, history_point *out)
{
    if (!ui->body || !ui->select_row_count)
        return false;
    int row = y - getbegy(ui->body) - ui->history_top;
    int column = CLAMP(x - getbegx(ui->body), 0, getmaxx(ui->body));
    if (row < 0) {
        row = 0;
        column = 0;
    } else if (row >= ui->select_row_count) {
        row = ui->select_row_count - 1;
        column = getmaxx(ui->body);
    }
    for (int k = row; k >= 0; --k)
        if (*ui->select_rows[k].id) {
            snprintf(out->id, sizeof(out->id), "%s", ui->select_rows[k].id);
            out->line = ui->select_rows[k].line;
            out->column = k == row ? column : getmaxx(ui->body);
            return true;
        }
    for (int k = row + 1; k < ui->select_row_count; ++k)
        if (*ui->select_rows[k].id) {
            snprintf(out->id, sizeof(out->id), "%s", ui->select_rows[k].id);
            out->line = ui->select_rows[k].line;
            out->column = 0;
            return true;
        }
    return false;
}

bool conversation_selection_begin(view *ui, int y, int x)
{
    conversation_selection_clear(ui);
    history_point start;
    if (!point(ui, y, x, &start))
        return false;
    ui->select_anchor = ui->select_head = start;
    ui->select_dragging = true;
    return true;
}

void conversation_selection_drag(view *ui, int y, int x, bool released)
{
    if (!ui->select_dragging || !ui->body)
        return;
    int top = getbegy(ui->body) + ui->history_top;
    /* Moving the pointer past an edge scrolls one row per report. The rows
     * are mapped again at once: reports can arrive before the next paint. */
    if (!released && (y < top || y >= top + ui->visible)) {
        conversation_scroll_history(ui, y < top ? -1 : 1, true);
        int ignored;
        ui->select_mapping = true;
        conversation_selection_rows(ui);
        conversation_walk(ui, conversation_position(ui), false, false, &ignored);
        ui->select_mapping = false;
    }
    history_point head;
    if (point(ui, y, x, &head))
        ui->select_head = head;
    ui->select_shown = !same(&ui->select_anchor, &ui->select_head);
    if (!released)
        return;
    ui->select_dragging = false;
    if (!ui->select_shown)
        return;
    GString *text = selected_text(ui);
    if (text && text->len)
        clipboard_copy_text(text->str);
    else
        ui->select_shown = false;
    if (text) {
        wipe_text(text);
        g_string_free(text, true);
    }
}

void conversation_selection_clear(view *ui)
{
    ui->select_dragging = ui->select_shown = false;
    memset(&ui->select_anchor, 0, sizeof(ui->select_anchor));
    memset(&ui->select_head, 0, sizeof(ui->select_head));
}

void conversation_selection_destroy(view *ui)
{
    conversation_selection_clear(ui);
    free(ui->select_rows);
    ui->select_rows = NULL;
    ui->select_row_count = 0;
}

bool conversation_message_has_text(const json_t *message)
{
    const char *content = conversation_str(message, "content");
    for (const char *at = content; at && *at; at = g_utf8_next_char(at))
        if (!g_unichar_isspace(g_utf8_get_char(at)))
            return true;
    return false;
}

bool conversation_copy_message(view *ui, const json_t *message)
{
    const char *content = conversation_str(message, "content");
    if (!conversation_message_has_text(message) || !g_utf8_validate(content, -1, NULL))
        return false;
    GString *text = g_string_new(NULL);
    mention_range range;
    size_t from = 0;
    while (mention_next(content, from, &range)) {
        g_string_append_len(text, content + from, (gssize)(range.start - from));
        const char *name = history_mention_name(ui->model, range.id);
        g_string_append_printf(text, "@%s", name && *name ? name : "usuário indisponível");
        from = range.end;
    }
    g_string_append(text, content + from);
    bool copied = text->len <= CLIPBOARD_TEXT_LIMIT && clipboard_copy_text(text->str);
    wipe_text(text);
    g_string_free(text, true);
    return copied;
}
