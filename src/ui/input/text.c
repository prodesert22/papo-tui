#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "ui/input/text.h"
#include "platform/unicode.h"
#include "ui/components/theme.h"
#include <stdint.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>
#include <wordbreak.h>

void text_input_init(void)
{
    define_key("\033[I", TEXT_FOCUS_IN);
    define_key("\033[O", TEXT_FOCUS_OUT);
    for (int flags = 0; flags < 32; ++flags) {
        char sequence[24];
        snprintf(sequence, sizeof(sequence), "\033[?%du", flags);
        define_key(sequence, TEXT_KEYBOARD_SUPPORTED);
    }
    init_graphemebreak();
    init_wordbreak();
    define_key("\033[200~", TEXT_PASTE_START);
    define_key("\033[201~", TEXT_PASTE_END);
    /* ncurses owns sequence decoding. CSI-u disambiguates Ctrl+Shift+C from
     * Ctrl+C; accept xterm's modifyOtherKeys encoding as well. CapsLock and
     * NumLock add 64/128 to the modifier; neither changes the intended key. */
    for (int locks = 0; locks <= 192; locks += 64)
        for (int ascii = 32; ascii <= 126; ++ascii)
            for (int modifier = 5; modifier <= 6; ++modifier) {
                int key = TEXT_IGNORED;
                if ((ascii >= 'a' && ascii <= 'z') || (ascii >= 'A' && ascii <= 'Z') ||
                    (ascii >= '[' && ascii <= '_') || ascii == '@' || ascii == ' ')
                    key = TEXT_CONTROL_BASE + (ascii & 31);
                if (modifier == 6 && (ascii == 'c' || ascii == 'C'))
                    key = TEXT_COPY;
                char sequence[32];
                snprintf(sequence, sizeof(sequence), "\033[%d;%du", ascii, modifier + locks);
                define_key(sequence, key);
                snprintf(sequence, sizeof(sequence), "\033[27;%d;%d~", modifier + locks, ascii);
                define_key(sequence, key);
            }
    const int codes[] = {27, 13, 9, 127};
    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); ++i) {
        char sequence[32];
        int key = codes[i] == 127 ? KEY_BACKSPACE : TEXT_CONTROL_BASE + codes[i];
        snprintf(sequence, sizeof(sequence), "\033[%du", codes[i]);
        define_key(sequence, key);
        for (int locks = 0; locks <= 192; locks += 64) {
            snprintf(sequence, sizeof(sequence), "\033[%d;%du", codes[i], 1 + locks);
            define_key(sequence, key);
            if (codes[i] == 9) {
                snprintf(sequence, sizeof(sequence), "\033[9;%du", 2 + locks);
                define_key(sequence, KEY_BTAB);
            }
        }
    }
    for (int locks = 0; locks <= 192; locks += 64) {
        char sequence[32];
        snprintf(sequence, sizeof(sequence), "\033[13;%du", 2 + locks);
        define_key(sequence, TEXT_SHIFT_ENTER);
        snprintf(sequence, sizeof(sequence), "\033[27;%d;13~", 2 + locks);
        define_key(sequence, TEXT_SHIFT_ENTER);
        snprintf(sequence, sizeof(sequence), "\033[57414;%du", 2 + locks);
        define_key(sequence, TEXT_SHIFT_ENTER);
    }
    define_key("\033[27;2;9~", KEY_BTAB);
    const char suffix[] = "DCABHF";
    const int modifiers[] = {1, 2, 5, 6};
    const int plain_keys[] = {KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN, KEY_HOME, KEY_END};
    const int keypad_directions[] = {57417, 57418, 57419, 57420, 57423, 57424};
    for (int locks = 0; locks <= 192; locks += 64)
        for (int group = 0; group < 4; ++group)
            for (int direction = 0; direction < 6; ++direction) {
                int key =
                    group ? TEXT_SHIFT_LEFT + (group - 1) * 6 + direction : plain_keys[direction];
                char sequence[24];
                snprintf(sequence, sizeof(sequence), "\033[1;%d%c", modifiers[group] + locks,
                         suffix[direction]);
                define_key(sequence, key);
                snprintf(sequence, sizeof(sequence), "\033[%d;%du", keypad_directions[direction],
                         modifiers[group] + locks);
                define_key(sequence, key);
                if (!group && !locks) {
                    snprintf(sequence, sizeof(sequence), "\033[%du", keypad_directions[direction]);
                    define_key(sequence, key);
                }
                if (direction >= 4) {
                    snprintf(sequence, sizeof(sequence), "\033[%d;%d~", direction == 4 ? 1 : 4,
                             modifiers[group] + locks);
                    define_key(sequence, key);
                    snprintf(sequence, sizeof(sequence), "\033[%d;%d~", direction == 4 ? 7 : 8,
                             modifiers[group] + locks);
                    define_key(sequence, key);
                }
            }
    /* Disambiguation also gives non-text keypad keys their own CSI-u codes. */
    const int keypad_codes[] = {57414, 57421, 57422, 57425, 57426};
    const int keypad_keys[] = {KEY_ENTER, KEY_PPAGE, KEY_NPAGE, KEY_IC, KEY_DC};
    for (size_t i = 0; i < sizeof(keypad_codes) / sizeof(keypad_codes[0]); ++i) {
        char sequence[32];
        snprintf(sequence, sizeof(sequence), "\033[%du", keypad_codes[i]);
        define_key(sequence, keypad_keys[i]);
        for (int locks = 0; locks <= 192; locks += 64) {
            snprintf(sequence, sizeof(sequence), "\033[%d;%du", keypad_codes[i], 1 + locks);
            define_key(sequence, keypad_keys[i]);
        }
    }
    const int numbers[] = {2, 3, 5, 6, 11, 12, 13, 14, 15, 17, 18, 19, 20, 21, 23, 24};
    const int keys[] = {KEY_IC,   KEY_DC,    KEY_PPAGE, KEY_NPAGE, KEY_F(1), KEY_F(2),
                        KEY_F(3), KEY_F(4),  KEY_F(5),  KEY_F(6),  KEY_F(7), KEY_F(8),
                        KEY_F(9), KEY_F(10), KEY_F(11), KEY_F(12)};
    for (size_t i = 0; i < sizeof(numbers) / sizeof(numbers[0]); ++i)
        for (int locks = 0; locks <= 192; locks += 64) {
            char sequence[32];
            snprintf(sequence, sizeof(sequence), "\033[%d;%d~", numbers[i], 1 + locks);
            define_key(sequence, keys[i]);
        }
    for (int i = 0; i < 4; ++i)
        for (int locks = 0; locks <= 192; locks += 64) {
            char sequence[32];
            snprintf(sequence, sizeof(sequence), "\033[1;%d%c", 1 + locks, "PQRS"[i]);
            define_key(sequence, KEY_F(i + 1));
        }
}
static bool focus_known, terminal_focused, input_active, keyboard_active;
bool text_terminal_focused(void)
{
    return focus_known && terminal_focused;
}
bool text_terminal_focus_unknown(void)
{
    return !focus_known;
}
int text_read_raw(uint32_t *key)
{
    wint_t native = 0;
    int type = get_wch(&native);
    *key = (uint32_t)native;
#ifdef _WIN32
    static uint32_t high;
    if (type == OK && native >= 0xd800 && native <= 0xdbff) {
        high = native;
        return ERR;
    }
    if (type == OK && high) {
        uint32_t pending = high;
        high = 0;
        if (native >= 0xdc00 && native <= 0xdfff)
            *key = 0x10000 + ((pending - 0xd800) << 10) + native - 0xdc00;
    } else if (type != ERR)
        high = 0;
#endif
    return type;
}

int text_input_read(uint32_t *key)
{
    int type = text_read_raw(key);
    if (type == KEY_CODE_YES && *key == TEXT_KEYBOARD_SUPPORTED) {
        if (input_active && !keyboard_active) {
            /* Only disambiguate keys. Leave text, repeats and release events
             * in their ordinary form; curses decodes the registered keys. */
            fputs("\033[>1u", stdout);
            fflush(stdout);
            keyboard_active = true;
        }
        return ERR;
    }
    if (type == KEY_CODE_YES && (*key == TEXT_FOCUS_IN || *key == TEXT_FOCUS_OUT)) {
        focus_known = true;
        terminal_focused = *key == TEXT_FOCUS_IN;
    }
    if (type == KEY_CODE_YES && *key >= TEXT_CONTROL_BASE && *key < TEXT_CONTROL_BASE + 32) {
        *key -= TEXT_CONTROL_BASE;
        return OK;
    }
    return type;
}
void text_input_terminal_active(bool active)
{
    if (active && !input_active)
        fputs("\033[?u", stdout);
    if (!active && keyboard_active) {
        fputs("\033[<u", stdout);
        keyboard_active = false;
    }
    input_active = active;
    fputs(active ? "\033[?1004h" : "\033[?1004l", stdout);
    fflush(stdout);
    const char *id = getenv("KITTY_WINDOW_ID"), *term = getenv("TERM");
    if ((id && *id) || (term && !strcmp(term, "xterm-kitty"))) {
        fputs(active ? "\033]1337;SetUserVar=papo_tui=MQ==\a" : "\033]1337;SetUserVar=papo_tui\a",
              stdout);
        fflush(stdout);
    }
}
bool text_selection(const text_buffer *buffer, size_t *start, size_t *end)
{
    size_t a = buffer->anchor < buffer->length ? buffer->anchor : buffer->length;
    size_t b = buffer->cursor < buffer->length ? buffer->cursor : buffer->length;
    if (!buffer->selecting || a == b)
        return false;
    if (start)
        *start = a < b ? a : b;
    if (end)
        *end = a > b ? a : b;
    return true;
}
void text_unselect(text_buffer *buffer)
{
    buffer->selecting = false;
    buffer->anchor = buffer->cursor;
}

/* Build a bounded projection and source-offset map. Labels cannot be longer
 * than their canonical token; this preserves the existing layout capacity. */
static void project(const text_buffer *source, text_buffer *display, size_t *starts, size_t *ends,
                    unsigned char *styles, uint32_t *icons)
{
    if (!source->mentions && !source->icon_resolver) {
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
        uint32_t icon = 0;
        size_t icon_length = !atom && source->icon_resolver
                                 ? source->icon_resolver(source->icon_context, source->data,
                                                         source->length, i, &icon)
                                 : 0;
        if (icon_length && icon_length <= source->length - i) {
            size_t at = display->length++;
            display->data[at] = 0x3000;
            if (starts)
                starts[at] = i;
            if (ends)
                ends[at] = i + icon_length;
            if (icons)
                icons[at] = icon;
            if (styles)
                styles[at] = 0;
            if (source->cursor == i)
                display->cursor = at;
            else if (source->cursor > i && source->cursor <= i + icon_length)
                display->cursor = at + 1;
            i += icon_length;
            continue;
        }
        if (!atom) {
            size_t at = display->length++;
            display->data[at] = source->data[i];
            if (starts)
                starts[at] = i;
            if (ends)
                ends[at] = i + 1;
            if (styles)
                styles[at] = 0;
            if (source->cursor == i)
                display->cursor = at;
            ++i;
            continue;
        }
        text_buffer label = {0};
        {
            const char *name =
                source->resolver ? source->resolver(source->resolver_context, range.id) : NULL;
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
            if (starts)
                starts[at] = i;
            if (ends)
                ends[at] = end;
            if (styles)
                styles[at] = (unsigned char)atom;
        }
        if (source->cursor == i)
            display->cursor = begin;
        else if (source->cursor > i && source->cursor <= end)
            display->cursor = display->length;
        i = end;
    }
    display->data[display->length] = 0;
    if (source->cursor == source->length)
        display->cursor = display->length;
}
void text_mentions(text_buffer *buffer, mention_resolver resolver, void *context)
{
    buffer->mentions = true;
    buffer->resolver = resolver;
    buffer->resolver_context = context;
}
void text_project(const text_buffer *buffer, text_buffer *display)
{
    project(buffer, display, NULL, NULL, NULL, NULL);
}

void text_icons(text_buffer *buffer, text_icon_resolver resolver, text_icon_painter painter,
                void *context)
{
    buffer->icon_resolver = resolver;
    buffer->icon_painter = painter;
    buffer->icon_context = context;
    buffer->icon_map = NULL;
}

static void boundaries(const text_buffer *buffer, char breaks[TEXT_LIMIT])
{
    if (buffer->length)
        set_graphemebreaks_utf32(buffer->data, buffer->length, NULL, breaks);
    if (buffer->icon_resolver)
        for (size_t i = 0; i < buffer->length; ++i) {
            uint32_t icon;
            size_t length =
                buffer->icon_resolver(buffer->icon_context, buffer->data, buffer->length, i, &icon);
            if (!length || length > buffer->length - i)
                continue;
            for (size_t k = i; k + 1 < i + length; ++k)
                breaks[k] = GRAPHEMEBREAK_NOBREAK;
            breaks[i + length - 1] = GRAPHEMEBREAK_BREAK;
            i += length - 1;
        }
    if (buffer->mentions)
        for (size_t i = 0; i + 49 <= buffer->length; ++i) {
            if (buffer->data[i] != '@')
                continue;
            char token[50] = {0};
            mention_range r;
            for (size_t k = 0; k < 49; ++k)
                token[k] = buffer->data[i + k] < 128 ? (char)buffer->data[i + k] : '?';
            if (mention_next(token, 0, &r) && r.start == 0 && r.end == 49) {
                for (size_t k = i; k < i + 48; ++k)
                    breaks[k] = GRAPHEMEBREAK_NOBREAK;
                breaks[i + 48] = GRAPHEMEBREAK_BREAK;
                i += 48;
            }
        }
}

static size_t next(const text_buffer *buffer, size_t from, const char *breaks)
{
    while (from < buffer->length) {
        if (breaks[from++] == GRAPHEMEBREAK_BREAK)
            break;
    }
    return from;
}

static size_t previous(size_t from, const char *breaks)
{
    if (!from)
        return 0;
    --from;
    while (from && breaks[from - 1] != GRAPHEMEBREAK_BREAK)
        --from;
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

static bool allowed(uint32_t value)
{
    if (value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff) || value < 32 ||
        (value >= 0x7f && value <= 0x9f))
        return false;
    return g_unichar_isprint(value) || value == 0x200c || value == 0x200d;
}

bool text_insert(text_buffer *buffer, uint32_t character, bool multiline)
{
    if (character == '\r')
        character = '\n';
    if (character == '\t')
        character = ' ';
    if (character == '\n' ? !multiline : !allowed(character))
        return false;
    size_t from, to;
    if (text_selection(buffer, &from, &to)) {
        memmove(buffer->data + from, buffer->data + to,
                (buffer->length - to + 1) * sizeof(buffer->data[0]));
        buffer->length -= to - from;
        buffer->cursor = from;
    }
    if (buffer->length >= TEXT_LIMIT)
        return false;
    text_unselect(buffer);
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
    text_selection(buffer, &from, &to);
    memmove(buffer->data + from, buffer->data + to,
            (buffer->length - to + 1) * sizeof(buffer->data[0]));
    buffer->length -= to - from;
    buffer->cursor = from;
    normalize_cursor(buffer);
    text_unselect(buffer);
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
    buffer->cursor =
        right ? next(buffer, buffer->cursor, breaks) : previous(buffer->cursor, breaks);
    buffer->goal_column = -1;
    buffer->trailing = false;
}

static text_cell cell(const text_buffer *buffer, size_t start, size_t end)
{
    text_cell result = {.start = start, .end = end};
    if (buffer->icon_map && buffer->icon_map[start]) {
        result.icon = buffer->icon_map[start];
        result.width = 2;
        result.glyph[0] = ' ';
        return result;
    }
    if (end == start + 1 && buffer->data[start] == '\n') {
        result.newline = true;
        return result;
    }
    /* ncurses stores one spacing character and bounded combining marks per
     * cell. Unsupported clusters use one ASCII placeholder, without changing
     * the underlying draft; this keeps cursor positions and wrapping exact. */
    bool supported = end - start < CCHARW_MAX;
    for (size_t i = start; i < end && supported; ++i) {
        if (buffer->data[i] > WCHAR_MAX) {
            supported = false;
            break;
        }
        uint32_t character = buffer->data[i];
        int width = papo_unicode_width(character);
        if (!g_unichar_isprint(character) || character == 0x200c || character == 0x200d ||
            (character >= 0xfe00 && character <= 0xfe0f) ||
            (character >= 0xe0100 && character <= 0xe01ef) ||
            (i == start ? width <= 0 : width != 0))
            supported = false;
        else {
            result.glyph[i - start] = (wchar_t)character;
            if (i == start)
                result.width = width;
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
    if (width < 3)
        width = 3;
    --width;
    char breaks[TEXT_LIMIT];
    boundaries(buffer, breaks);
    int row = 0, column = 0;
    for (size_t start = 0; start < buffer->length;) {
        size_t end = next(buffer, start, breaks);
        text_cell value = cell(buffer, start, end);
        if (column + value.width > width) {
            ++row;
            column = 0;
        }
        value.row = row;
        value.column = column;
        layout->cells[layout->count++] = value;
        if (buffer->cursor == start) {
            layout->cursor_row = row;
            layout->cursor_column = column;
        }
        column += value.width;
        if (value.newline) {
            ++row;
            column = 0;
        }
        start = end;
    }
    layout->cells[layout->count++] =
        (text_cell){.start = buffer->length, .end = buffer->length, .row = row, .column = column};
    if (buffer->cursor == buffer->length) {
        layout->cursor_row = row;
        layout->cursor_column = column;
    }
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
    if (!buffer->mentions && !buffer->icon_resolver) {
        measure_plain(buffer, width, layout);
        for (size_t i = 0; i < layout->count; ++i) {
            layout->cells[i].display_start = layout->cells[i].start;
            layout->cells[i].display_end = layout->cells[i].end;
        }
        return;
    }
    text_buffer display;
    size_t starts[TEXT_LIMIT + 1], ends[TEXT_LIMIT + 1];
    unsigned char styles[TEXT_LIMIT + 1] = {0};
    uint32_t icons[TEXT_LIMIT + 1] = {0};
    project(buffer, &display, starts, ends, styles, icons);
    display.icon_map = icons;
    display.icon_resolver = NULL;
    display.mentions = false;
    starts[display.length] = ends[display.length] = buffer->length;
    measure_plain(&display, width, layout);
    for (size_t i = 0; i < layout->count; ++i) {
        text_cell *c = &layout->cells[i];
        size_t a = c->start, b = c->end;
        c->display_start = a;
        c->display_end = b;
        c->mention = styles[a];
        c->start = starts[a];
        c->end = b > a ? ends[b - 1] : starts[a];
    }
}

static size_t offset_at(const text_layout *layout, int row, int column)
{
    size_t result = 0;
    for (size_t i = 0; i < layout->count; ++i) {
        const text_cell *point = &layout->cells[i];
        if (point->row > row || (point->row == row && point->column > column))
            break;
        result =
            !point->newline && column >= point->column + point->width ? point->end : point->start;
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
            layout->cells[i + 1].row > row)
            buffer->trailing = true;
    }
}

void text_vertical(text_buffer *buffer, int width, int direction)
{
    text_layout layout;
    text_measure(buffer, width, &layout);
    if (buffer->goal_column < 0)
        buffer->goal_column = layout.cursor_column;
    int row = layout.cursor_row + direction;
    if (row < 0 || row >= layout.rows)
        return;
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
    text_unselect(buffer);
}
void text_drag(text_buffer *buffer, int width, int row, int column)
{
    size_t anchor = buffer->selecting ? buffer->anchor : buffer->cursor;
    text_click(buffer, width, row, column);
    buffer->anchor = anchor;
    buffer->selecting = true;
}

static void text_word(text_buffer *buffer, bool right)
{
    char words[TEXT_LIMIT], graphemes[TEXT_LIMIT];
    if (!buffer->length)
        return;
    set_wordbreaks_utf32(buffer->data, buffer->length, NULL, words);
    boundaries(buffer, graphemes);
    for (size_t i = 0; i < buffer->length; ++i)
        if (graphemes[i] != GRAPHEMEBREAK_BREAK)
            words[i] = WORDBREAK_NOBREAK;
    size_t at = buffer->cursor;
    if (right) {
        while (at < buffer->length && g_unichar_isspace(buffer->data[at]))
            at = next(buffer, at, graphemes);
        while (at < buffer->length)
            if (words[at++] == WORDBREAK_BREAK)
                break;
    } else {
        while (at && g_unichar_isspace(buffer->data[previous(at, graphemes)]))
            at = previous(at, graphemes);
        if (at) {
            --at;
            while (at && words[at - 1] != WORDBREAK_BREAK)
                --at;
        }
    }
    buffer->cursor = at;
    buffer->goal_column = -1;
    buffer->trailing = false;
}
bool text_edit(text_buffer *buffer, int type, uint32_t key, int width, bool multiline)
{
    if (type == KEY_CODE_YES && key == TEXT_SHIFT_ENTER) {
        if (multiline)
            text_insert(buffer, '\n', true);
        return multiline;
    }
    if (type == OK) {
        if (key == 3 || key == 21) {
            text_clear(buffer);
            return true;
        }
        if (key == 8 || key == 127) {
            text_delete(buffer, true);
            return true;
        }
        if (multiline && key == 14) {
            text_insert(buffer, '\n', true);
            return true;
        }
        if (allowed(key)) {
            text_insert(buffer, key, multiline);
            return true;
        }
        return false;
    }
    if (type != KEY_CODE_YES)
        return false;
    if (key == KEY_BACKSPACE || key == KEY_DC) {
        text_delete(buffer, key == KEY_BACKSPACE);
        return true;
    }
    int direction = -1;
    bool shift = false, control = false;
    if (key >= TEXT_SHIFT_LEFT && key <= TEXT_CTRL_SHIFT_END) {
        int group = (int)(key - TEXT_SHIFT_LEFT) / 6;
        direction = (int)(key - TEXT_SHIFT_LEFT) % 6;
        shift = group != 1;
        control = group != 0;
    } else {
        if (key == KEY_LEFT || key == KEY_SLEFT)
            direction = 0;
        if (key == KEY_RIGHT || key == KEY_SRIGHT)
            direction = 1;
        if (key == KEY_UP || key == KEY_SR)
            direction = 2;
        if (key == KEY_DOWN || key == KEY_SF)
            direction = 3;
        if (key == KEY_HOME || key == KEY_SHOME)
            direction = 4;
        if (key == KEY_END || key == KEY_SEND)
            direction = 5;
        shift = key == KEY_SLEFT || key == KEY_SRIGHT || key == KEY_SR || key == KEY_SF ||
                key == KEY_SHOME || key == KEY_SEND;
    }
    if (direction < 0)
        return false;
    size_t anchor = buffer->selecting ? buffer->anchor : buffer->cursor, lo, hi;
    bool collapse = !shift && !control && direction < 2 && text_selection(buffer, &lo, &hi);
    if (collapse) {
        buffer->cursor = direction ? hi : lo;
        buffer->goal_column = -1;
        buffer->trailing = false;
    } else if (control && direction < 2)
        text_word(buffer, direction == 1);
    else if (control) {
        buffer->cursor = direction == 3 || direction == 5 ? buffer->length : 0;
        buffer->trailing = false;
        buffer->goal_column = -1;
    } else if (direction < 2)
        text_horizontal(buffer, direction == 1);
    else if (direction < 4)
        text_vertical(buffer, width, direction == 2 ? -1 : 1);
    else
        text_line_edge(buffer, width, direction == 5);
    if (shift) {
        buffer->anchor = anchor;
        buffer->selecting = true;
    } else
        text_unselect(buffer);
    return true;
}

void text_keep_cursor(text_buffer *buffer, const text_layout *layout, int height)
{
    if (layout->cursor_row < buffer->first_row)
        buffer->first_row = layout->cursor_row;
    if (layout->cursor_row >= buffer->first_row + height)
        buffer->first_row = layout->cursor_row - height + 1;
    int last = layout->rows - height;
    if (last < 0)
        last = 0;
    if (buffer->first_row > last)
        buffer->first_row = last;
}

bool text_insert_buffer(text_buffer *buffer, const text_buffer *insertion)
{
    if (!insertion->length)
        return true;
    size_t from = buffer->cursor, to = buffer->cursor;
    text_selection(buffer, &from, &to);
    if (insertion->length > TEXT_LIMIT - (buffer->length - (to - from)))
        return false;
    memmove(buffer->data + from + insertion->length, buffer->data + to,
            (buffer->length - to + 1) * sizeof(buffer->data[0]));
    memcpy(buffer->data + from, insertion->data, insertion->length * sizeof(buffer->data[0]));
    buffer->length = buffer->length - (to - from) + insertion->length;
    buffer->cursor = from + insertion->length;
    normalize_cursor(buffer);
    text_unselect(buffer);
    return true;
}
void text_paste_cancel(text_paste *paste)
{
    volatile unsigned char *p = (volatile unsigned char *)paste;
    for (size_t i = 0; i < sizeof(*paste); ++i)
        p[i] = 0;
}
void text_paste_begin(text_paste *paste)
{
    text_paste_cancel(paste);
    paste->active = true;
}
int text_paste_read(text_paste *paste, uint32_t *key, bool multiline, int (*read_key)(uint32_t *),
                    int timeout_ms)
{
    timeout(0);
    int type = ERR;
    for (int count = 0; count < 256; ++count) {
        if (!count)
            timeout(timeout_ms);
        type = read_key(key);
        timeout(0);
        if (type == ERR)
            break;
        if (type == KEY_CODE_YES) {
            if (*key == TEXT_PASTE_END || *key == KEY_RESIZE)
                break;
            if (*key == KEY_MOUSE) {
                MEVENT ignored;
                while (getmouse(&ignored) == OK) {
                }
            }
            type = ERR;
            continue;
        }
        uint32_t value = *key;
        if (value == '\n' && paste->cr) {
            paste->cr = false;
            type = ERR;
            continue;
        }
        paste->cr = value == '\r';
        if (value == '\r')
            value = '\n';
        if (value == '\t')
            value = ' ';
        if ((value == '\n' && multiline) || allowed(value)) {
            if (paste->text.length < TEXT_LIMIT)
                paste->text.data[paste->text.length++] = value;
            else
                paste->truncated = true;
        }
        type = ERR;
    }
    timeout(timeout_ms);
    paste->text.data[paste->text.length] = 0;
    return type;
}
bool text_paste_commit(text_paste *paste, text_buffer *buffer)
{
    bool ok = !paste->truncated && buffer && text_insert_buffer(buffer, &paste->text);
    text_paste_cancel(paste);
    return ok;
}

void text_draw_field(WINDOW *window, const text_buffer *buffer, int *first, int y, int x, int width,
                     bool masked)
{
    text_layout layout;
    text_measure(buffer, TEXT_LIMIT * 2 + 1, &layout);
    if (layout.cursor_column < *first)
        *first = layout.cursor_column;
    if (layout.cursor_column >= *first + width)
        *first = layout.cursor_column - width + 1;
    for (size_t i = 0; i + 1 < layout.count; ++i) {
        const text_cell *c = &layout.cells[i];
        if (c->column < *first && c->column + c->width > *first)
            *first = c->column + c->width;
    }
    attr_t attributes = getattrs(window);
    size_t lo = 0, hi = 0;
    bool selection = text_selection(buffer, &lo, &hi);
    for (size_t i = 0; i + 1 < layout.count; ++i) {
        const text_cell *c = &layout.cells[i];
        if (c->newline || c->column < *first || c->column + c->width > *first + width)
            continue;
        bool selected = selection && c->start < hi && c->end > lo;
        attr_t style = selected ? A_BOLD | (has_colors() ? 0 : A_REVERSE) : attributes & ~A_COLOR;
        short pair = selected ? (has_colors() ? THEME_TEXT_SELECTION : 0) : PAIR_NUMBER(attributes);
        cchar_t glyph;
        if (setcchar(&glyph, masked ? L"*" : c->glyph, style, pair, NULL) == OK)
            for (int n = 0; n < (masked ? c->width : 1); ++n)
                mvwadd_wch(window, y, x + c->column - *first + n, &glyph);
    }
    wmove(window, y, x + layout.cursor_column - *first);
}

void text_draw(WINDOW *window, const text_buffer *buffer, const text_layout *layout, int first,
               int height, int y, int x)
{
    attr_t attributes = getattrs(window);
    size_t lo = 0, hi = 0;
    bool selection = text_selection(buffer, &lo, &hi);
    for (size_t i = 0; i + 1 < layout->count; ++i) {
        const text_cell *point = &layout->cells[i];
        bool selected = selection && point->start < hi && point->end > lo;
        if ((point->newline && !selected) || point->row < first || point->row >= first + height)
            continue;
        cchar_t glyph;
        short pair = selected         ? (has_colors() ? THEME_TEXT_SELECTION : 0)
                     : point->mention ? THEME_PANEL_ACCENT
                                      : PAIR_NUMBER(attributes);
        attr_t style = selected ? A_BOLD | (has_colors() ? 0 : A_REVERSE)
                                : (attributes & ~A_COLOR) | (point->mention ? A_BOLD : 0);
        if (point->icon && buffer->icon_painter) {
            wattrset(window, style | COLOR_PAIR(pair));
            mvwhline(window, y + point->row - first, x + point->column, ' ', 2);
            if (!buffer->icon_painter(buffer->icon_context, window, point->icon,
                                      y + point->row - first, x + point->column, pair, selected,
                                      point->start))
                mvwaddstr(window, y + point->row - first, x + point->column, "· ");
            continue;
        }
        if (setcchar(&glyph, point->newline ? L" " : point->glyph, style, pair, NULL) == OK)
            mvwadd_wch(window, y + point->row - first, x + point->column, &glyph);
    }
    wattrset(window, attributes);
}

void text_draw_styled(WINDOW *w, const text_buffer *b, const text_layout *l,
                      const unsigned char *mask, size_t length, int first, int height, int y, int x,
                      int normal, int accent)
{
    (void)b;
    for (size_t i = 0; i + 1 < l->count; ++i) {
        const text_cell *c = &l->cells[i];
        if (c->newline || c->row < first || c->row >= first + height)
            continue;
        bool marked = mask && c->start < length && mask[c->start];
        cchar_t glyph;
        if (setcchar(&glyph, c->glyph, marked ? A_BOLD : A_NORMAL, marked ? accent : normal,
                     NULL) == OK)
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
    while (*utf8) {
        uint32_t character;
        size_t length = papo_utf8_next(utf8, &character);
        if (!length || length == (size_t)-1 || length == (size_t)-2)
            return false;
        utf8 += length;
        if (character == '\t')
            character = ' ';
        if (character == '\r')
            continue;
        if (character != '\n' && !allowed((uint32_t)character))
            continue;
        if (buffer->length == TEXT_LIMIT)
            return false;
        buffer->data[buffer->length++] = (utf32_t)character;
    }
    buffer->cursor = buffer->length;
    return true;
}

bool text_encode(const text_buffer *buffer, char output[TEXT_UTF8_BYTES])
{
    if (!buffer || !output || buffer->length > TEXT_LIMIT)
        return false;
    size_t length = 0;
    for (size_t i = 0; i < buffer->length; ++i) {
        gunichar scalar = buffer->data[i];
        if (!scalar || !g_unichar_validate(scalar))
            return false;
        char bytes[6];
        size_t count = (size_t)g_unichar_to_utf8(scalar, bytes);
        if (count >= TEXT_UTF8_BYTES - length)
            return false;
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
    while (*p && buffer->length < TEXT_LIMIT) {
        const char *start = p;
        uint32_t character;
        size_t length = papo_utf8_next(p, &character);
        if (!length || length == (size_t)-1 || length == (size_t)-2)
            return false;
        p += length;
        if (character == '\t')
            character = ' ';
        if (character == '\r')
            continue;
        if (character != '\n' && !allowed((uint32_t)character))
            continue;
        positions[buffer->length] = start;
        buffer->data[buffer->length++] = (utf32_t)character;
    }
    if (*p && buffer->length == TEXT_LIMIT) {
        char breaks[TEXT_LIMIT];
        boundaries(buffer, breaks);
        size_t cut = previous(buffer->length, breaks);
        if (cut) {
            p = positions[cut];
            buffer->length = cut;
        }
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
    if (width <= 0)
        return 0;
    text_buffer buffer;
    text_layout layout;
    if (!text_decode_chunk(&buffer, &utf8))
        return 0;
    text_measure(&buffer, width + 1, &layout);
    int used = 0;
    for (size_t i = 0; i + 1 < layout.count; ++i) {
        text_cell *point = &layout.cells[i];
        if (point->row || point->newline || used + point->width > width)
            break;
        if (window) {
            cchar_t glyph;
            attr_t attributes = getattrs(window);
            if (setcchar(&glyph, point->glyph, attributes & ~A_COLOR, PAIR_NUMBER(attributes),
                         NULL) == OK)
                mvwadd_wch(window, y, x + used, &glyph);
        }
        used += point->width;
    }
    return used;
}
