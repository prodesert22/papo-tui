#define _POSIX_C_SOURCE 200809L
#include "ui/text.h"
#include "ui/theme.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>

/* Build a bounded projection and source-offset map. Labels cannot be longer
 * than their canonical token; this preserves the existing layout capacity. */
static void project(const text_buffer *source, text_buffer *display, size_t *starts, size_t *ends,
                    unsigned char *styles)
{
    if (!source->mentions) {
        *display = *source;
        return;
    }
    text_clear(display);
    display->first_row = source->first_row;
    display->goal_column = source->goal_column;
    display->trailing = source->trailing;
    for (size_t i = 0; i < source->length;) {
        char token[50] = {0};
        mention_range range;
        if (source->mentions && source->data[i] == '@' && i + 49 <= source->length) {
            for (size_t k = 0; k < 49; ++k)
                token[k] = source->data[i + k] < 128 ? (char)source->data[i + k] : '?';
        }
        bool atom = *token && mention_next(token, 0, &range) && range.start == 0 && range.end == 49;
        size_t end = atom ? i + 49 : i + 1;
        if (!atom) {
            size_t at = display->length++;
            display->data[at] = source->data[i];
            if (starts) starts[at] = i;
            if (ends) ends[at] = i + 1;
            if (styles) styles[at] = 0;
            if (source->cursor == i) display->cursor = at;
            ++i;
            continue;
        }
        text_buffer label = {0};
        {
            const char *name = source->resolver ? source->resolver(source->resolver_context, range.id) : NULL;
            if (!text_decode(&label, name && *name ? name : "usuário indisponível"))
                text_decode(&label, "usuário indisponível");
            if (label.length > 47) {
                char breaks[TEXT_LIMIT];
                set_graphemebreaks_utf32(label.data, label.length, NULL, breaks);
                size_t cut = 46;
                while (cut && breaks[cut - 1] != GRAPHEMEBREAK_BREAK)
                    --cut;
                label.length = cut;
                label.data[label.length++] = 0x2026;
            }
            memmove(label.data + 1, label.data, label.length * sizeof(label.data[0]));
            label.data[0] = '@';
            ++label.length;
        }
        size_t begin = display->length;
        for (size_t k = 0; k < label.length; ++k) {
            size_t at = display->length++;
            display->data[at] = label.data[k];
            if (starts) starts[at] = i;
            if (ends) ends[at] = end;
            if (styles) styles[at] = (unsigned char)atom;
        }
        if (source->cursor == i)
            display->cursor = begin;
        else if (source->cursor > i && source->cursor <= end)
            display->cursor = display->length;
        i = end;
    }
    display->data[display->length] = 0;
    if (source->cursor == source->length) display->cursor = display->length;
}
void text_mentions(text_buffer *buffer, mention_resolver resolver, void *context)
{
    buffer->mentions = true;
    buffer->resolver = resolver;
    buffer->resolver_context = context;
}
void text_project(const text_buffer *buffer, text_buffer *display)
{
    project(buffer, display, NULL, NULL, NULL);
}

static void boundaries(const text_buffer *buffer, char breaks[TEXT_LIMIT])
{
    if (buffer->length)
        set_graphemebreaks_utf32(buffer->data, buffer->length, NULL, breaks);
    if (buffer->mentions) for (size_t i = 0; i + 49 <= buffer->length; ++i) {
        if (buffer->data[i] != '@') continue;
        char token[50] = {0}; mention_range r;
        for (size_t k = 0; k < 49; ++k) token[k] = buffer->data[i+k] < 128 ? (char)buffer->data[i+k] : '?';
        if (mention_next(token, 0, &r) && r.start == 0 && r.end == 49) {
            for (size_t k = i; k < i + 48; ++k) breaks[k] = GRAPHEMEBREAK_NOBREAK;
            breaks[i+48] = GRAPHEMEBREAK_BREAK; i += 48;
        }
    }
}

static size_t next(const text_buffer *buffer, size_t from, const char *breaks)
{
    while (from < buffer->length) {
        if (breaks[from++] == GRAPHEMEBREAK_BREAK) break;
    }
    return from;
}

static size_t previous(size_t from, const char *breaks)
{
    if (!from) return 0;
    --from;
    while (from && breaks[from - 1] != GRAPHEMEBREAK_BREAK) --from;
    return from;
}

static void normalize_cursor(text_buffer *buffer)
{
    char breaks[TEXT_LIMIT];
    boundaries(buffer, breaks);
    if (buffer->cursor && buffer->cursor < buffer->length &&
        breaks[buffer->cursor - 1] != GRAPHEMEBREAK_BREAK)
        buffer->cursor = next(buffer, buffer->cursor, breaks);
    buffer->goal_column = -1;
    buffer->trailing = false;
}

static bool allowed(wint_t value)
{
    if (value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff) ||
        value < 32 || (value >= 0x7f && value <= 0x9f)) return false;
    return iswprint(value) || value == 0x200c || value == 0x200d;
}

bool text_insert(text_buffer *buffer, wint_t character, bool multiline)
{
    if (buffer->length >= TEXT_LIMIT) return false;
    if (character == '\r') character = '\n';
    if (character == '\t') character = ' ';
    if (character == '\n' ? !multiline : !allowed(character)) return false;
    memmove(buffer->data + buffer->cursor + 1, buffer->data + buffer->cursor,
            (buffer->length - buffer->cursor + 1) * sizeof(buffer->data[0]));
    buffer->data[buffer->cursor++] = (utf32_t)character;
    ++buffer->length;
    normalize_cursor(buffer);
    return true;
}

void text_delete(text_buffer *buffer, bool backward)
{
    char breaks[TEXT_LIMIT];
    boundaries(buffer, breaks);
    size_t from = backward ? previous(buffer->cursor, breaks) : buffer->cursor;
    size_t to = backward ? buffer->cursor : next(buffer, buffer->cursor, breaks);
    memmove(buffer->data + from, buffer->data + to,
            (buffer->length - to + 1) * sizeof(buffer->data[0]));
    buffer->length -= to - from;
    buffer->cursor = from;
    normalize_cursor(buffer);
}

void text_horizontal(text_buffer *buffer, bool right)
{
    if (right && buffer->trailing) {
        buffer->trailing = false;
        buffer->goal_column = -1;
        return;
    }
    char breaks[TEXT_LIMIT];
    boundaries(buffer, breaks);
    buffer->cursor = right ? next(buffer, buffer->cursor, breaks) : previous(buffer->cursor, breaks);
    buffer->goal_column = -1;
    buffer->trailing = false;
}

static text_cell cell(const text_buffer *buffer, size_t start, size_t end)
{
    text_cell result = {.start = start, .end = end};
    if (end == start + 1 && buffer->data[start] == '\n') {
        result.newline = true;
        return result;
    }
    /* ncurses stores one spacing character and bounded combining marks per
     * cell. Unsupported clusters use one ASCII placeholder, without changing
     * the underlying draft; this keeps cursor positions and wrapping exact. */
    bool supported = end - start < CCHARW_MAX;
    for (size_t i = start; i < end && supported; ++i) {
        wchar_t character = (wchar_t)buffer->data[i];
        int width = wcwidth(character);
        if (!iswprint(character) || character == 0x200c || character == 0x200d ||
            (character >= 0xfe00 && character <= 0xfe0f) ||
            (character >= 0xe0100 && character <= 0xe01ef) ||
            (i == start ? width <= 0 : width != 0)) supported = false;
        else {
            result.glyph[i - start] = character;
            if (i == start) result.width = width;
        }
    }
    if (!supported) {
        memset(result.glyph, 0, sizeof(result.glyph));
        result.glyph[0] = '?';
        result.width = 1;
    }
    return result;
}

static void measure_plain(const text_buffer *buffer, int width, text_layout *layout)
{
    memset(layout, 0, sizeof(*layout));
    /* Reserve a cell for an end-of-line cursor, without a phantom blank line
     * when an explicit newline follows text filling the available width. */
    if (width < 3) width = 3;
    --width;
    char breaks[TEXT_LIMIT];
    boundaries(buffer, breaks);
    int row = 0, column = 0;
    for (size_t start = 0; start < buffer->length;) {
        size_t end = next(buffer, start, breaks);
        text_cell value = cell(buffer, start, end);
        if (column + value.width > width) { ++row; column = 0; }
        value.row = row;
        value.column = column;
        layout->cells[layout->count++] = value;
        if (buffer->cursor == start) { layout->cursor_row = row; layout->cursor_column = column; }
        column += value.width;
        if (value.newline) { ++row; column = 0; }
        start = end;
    }
    layout->cells[layout->count++] = (text_cell){.start = buffer->length, .end = buffer->length,
                                               .row = row, .column = column};
    if (buffer->cursor == buffer->length) { layout->cursor_row = row; layout->cursor_column = column; }
    layout->rows = row + 1;
    if (buffer->trailing) {
        for (size_t i = 0; i + 1 < layout->count; ++i) {
            const text_cell *point = &layout->cells[i];
            if (!point->newline && point->end == buffer->cursor &&
                point->row < layout->cursor_row) {
                layout->cursor_row = point->row;
                layout->cursor_column = point->column + point->width;
                break;
            }
        }
    }
}

void text_measure(const text_buffer *buffer, int width, text_layout *layout)
{
    if (!buffer->mentions) {
        measure_plain(buffer, width, layout);
        for (size_t i = 0; i < layout->count; ++i) {
            layout->cells[i].display_start = layout->cells[i].start;
            layout->cells[i].display_end = layout->cells[i].end;
        }
        return;
    }
    text_buffer display; size_t starts[TEXT_LIMIT+1], ends[TEXT_LIMIT+1];
    unsigned char styles[TEXT_LIMIT+1] = {0};
    project(buffer, &display, starts, ends, styles);
    starts[display.length] = ends[display.length] = buffer->length;
    measure_plain(&display, width, layout);
    for (size_t i = 0; i < layout->count; ++i) {
        text_cell *c = &layout->cells[i]; size_t a = c->start, b = c->end;
        c->display_start = a; c->display_end = b; c->mention = styles[a];
        c->start = starts[a]; c->end = b > a ? ends[b-1] : starts[a];
    }
}

static size_t offset_at(const text_layout *layout, int row, int column)
{
    size_t result = 0;
    for (size_t i = 0; i < layout->count; ++i) {
        const text_cell *point = &layout->cells[i];
        if (point->row > row || (point->row == row && point->column > column)) break;
        result = !point->newline && column >= point->column + point->width ? point->end : point->start;
    }
    return result;
}

static void position_at(text_buffer *buffer, const text_layout *layout, int row, int column)
{
    buffer->cursor = offset_at(layout, row, column);
    buffer->trailing = false;
    for (size_t i = 0; i + 1 < layout->count; ++i) {
        const text_cell *point = &layout->cells[i];
        if (!point->newline && point->end == buffer->cursor && point->row == row &&
            layout->cells[i + 1].row > row) buffer->trailing = true;
    }
}

void text_vertical(text_buffer *buffer, int width, int direction)
{
    text_layout layout;
    text_measure(buffer, width, &layout);
    if (buffer->goal_column < 0) buffer->goal_column = layout.cursor_column;
    int row = layout.cursor_row + direction;
    if (row < 0 || row >= layout.rows) return;
    position_at(buffer, &layout, row, buffer->goal_column);
}

void text_line_edge(text_buffer *buffer, int width, bool end)
{
    text_layout layout;
    text_measure(buffer, width, &layout);
    position_at(buffer, &layout, layout.cursor_row, end ? INT_MAX : 0);
    buffer->goal_column = -1;
}

void text_click(text_buffer *buffer, int width, int row, int column)
{
    text_layout layout;
    text_measure(buffer, width, &layout);
    position_at(buffer, &layout, row, column);
    buffer->goal_column = -1;
}

void text_keep_cursor(text_buffer *buffer, const text_layout *layout, int height)
{
    if (layout->cursor_row < buffer->first_row) buffer->first_row = layout->cursor_row;
    if (layout->cursor_row >= buffer->first_row + height) buffer->first_row = layout->cursor_row - height + 1;
    int last = layout->rows - height;
    if (last < 0) last = 0;
    if (buffer->first_row > last) buffer->first_row = last;
}

void text_draw(WINDOW *window, const text_buffer *buffer, const text_layout *layout,
               int first, int height, int y, int x)
{
    (void)buffer;
    attr_t attributes = getattrs(window);
    for (size_t i = 0; i + 1 < layout->count; ++i) {
        const text_cell *point = &layout->cells[i];
        if (point->newline || point->row < first || point->row >= first + height) continue;
        cchar_t glyph;
        if (setcchar(&glyph, point->glyph, (attributes & ~A_COLOR) | (point->mention ? A_BOLD : 0), point->mention ? PANEL_ACCENT : PAIR_NUMBER(attributes), NULL) == OK)
            mvwadd_wch(window, y + point->row - first, x + point->column, &glyph);
    }
}

void text_draw_styled(WINDOW *w, const text_buffer *b, const text_layout *l, const unsigned char *mask,
                      size_t length, int first, int height, int y, int x, int normal, int accent)
{
    (void)b;
    for (size_t i = 0; i + 1 < l->count; ++i) {
        const text_cell *c = &l->cells[i];
        if (c->newline || c->row < first || c->row >= first + height) continue;
        bool marked = mask && c->start < length && mask[c->start];
        cchar_t glyph;
        if (setcchar(&glyph, c->glyph, marked ? A_BOLD : A_NORMAL, marked ? accent : normal, NULL) == OK)
            mvwadd_wch(w, y + c->row - first, x + c->column, &glyph);
    }
}

void text_clear(text_buffer *buffer)
{
    memset(buffer, 0, sizeof(*buffer));
    buffer->goal_column = -1;
}

bool text_decode(text_buffer *buffer, const char *utf8)
{
    text_clear(buffer);
    mbstate_t state = {0};
    while (*utf8) {
        wchar_t character;
        size_t length = mbrtowc(&character, utf8, MB_CUR_MAX, &state);
        if (!length || length == (size_t)-1 || length == (size_t)-2) return false;
        utf8 += length;
        if (character == '\t') character = ' ';
        if (character == '\r') continue;
        if (character != '\n' && !allowed((wint_t)character)) continue;
        if (buffer->length == TEXT_LIMIT) return false;
        buffer->data[buffer->length++] = (utf32_t)character;
    }
    buffer->cursor = buffer->length;
    return true;
}

bool text_encode(const text_buffer *buffer, char output[TEXT_UTF8_BYTES])
{
    mbstate_t state = {0};
    size_t length = 0;
    for (size_t i = 0; i < buffer->length; ++i) {
        char bytes[MB_LEN_MAX];
        size_t count = wcrtomb(bytes, (wchar_t)buffer->data[i], &state);
        if (count == (size_t)-1 || length + count >= TEXT_UTF8_BYTES) return false;
        memcpy(output + length, bytes, count);
        length += count;
    }
    output[length] = '\0';
    return true;
}

bool text_decode_chunk(text_buffer *buffer, const char **utf8)
{
    text_clear(buffer);
    const char *positions[TEXT_LIMIT + 1];
    const char *p = *utf8;
    mbstate_t state = {0};
    while (*p && buffer->length < TEXT_LIMIT) {
        const char *start = p;
        wchar_t character;
        size_t length = mbrtowc(&character, p, MB_CUR_MAX, &state);
        if (!length || length == (size_t)-1 || length == (size_t)-2) return false;
        p += length;
        if (character == '\t') character = ' ';
        if (character == '\r') continue;
        if (character != '\n' && !allowed((wint_t)character)) continue;
        positions[buffer->length] = start;
        buffer->data[buffer->length++] = (utf32_t)character;
    }
    if (*p && buffer->length == TEXT_LIMIT) {
        char breaks[TEXT_LIMIT];
        boundaries(buffer, breaks);
        size_t cut = previous(buffer->length, breaks);
        if (cut) { p = positions[cut]; buffer->length = cut; }
        /* A single cluster exceeding the segment capacity uses the existing
         * placeholder rendering; consume it in bounded pieces. */
    }
    buffer->data[buffer->length] = 0;
    buffer->cursor = buffer->length;
    *utf8 = p;
    return true;
}

int text_label(WINDOW *window, int y, int x, int width, const char *utf8)
{
    if (width <= 0) return 0;
    text_buffer buffer;
    text_layout layout;
    if (!text_decode_chunk(&buffer, &utf8)) return 0;
    text_measure(&buffer, width + 1, &layout);
    int used = 0;
    for (size_t i = 0; i + 1 < layout.count; ++i) {
        text_cell *point = &layout.cells[i];
        if (point->row || point->newline || used + point->width > width) break;
        if (window) {
            cchar_t glyph;
            attr_t attributes = getattrs(window);
            if (setcchar(&glyph, point->glyph, attributes & ~A_COLOR, PAIR_NUMBER(attributes), NULL) == OK)
                mvwadd_wch(window, y, x + used, &glyph);
        }
        used += point->width;
    }
    return used;
}
