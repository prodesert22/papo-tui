#ifndef PAPO_TEXT_H
#define PAPO_TEXT_H

#include <graphemebreak.h>
#include "model/mentions.h"
#include <ncurses.h>
#include <stdbool.h>

#define TEXT_LIMIT 2048
#define TEXT_UTF8_BYTES (TEXT_LIMIT * 4 + 1)

/* Owned bounded buffer, Unicode scalar offsets and viewport. Segmentation is
 * provided by libunibreak; cursor offsets always fall between graphemes. */
typedef struct {
    utf32_t data[TEXT_LIMIT + 1];
    size_t length, cursor;
    size_t anchor;
    bool selecting;
    int first_row, goal_column;
    bool mentions;
    mention_resolver resolver; /* Borrowed for the current view. */
    void *resolver_context;
    bool trailing; /* Prefer the preceding visual line at a soft wrap. */
} text_buffer;

typedef struct {
    size_t start, end, display_start, display_end;
    bool mention;
    int row, column, width;
    wchar_t glyph[CCHARW_MAX];
    bool newline;
} text_cell;

typedef struct {
    text_cell cells[TEXT_LIMIT + 1];
    size_t count;
    int rows, cursor_row, cursor_column;
} text_layout;

typedef struct {
    text_buffer text;
    bool active, cr, truncated;
} text_paste;

enum { TEXT_PASTE_START = KEY_MAX + 1, TEXT_PASTE_END,
       TEXT_SHIFT_LEFT, TEXT_SHIFT_RIGHT, TEXT_SHIFT_UP, TEXT_SHIFT_DOWN, TEXT_SHIFT_HOME, TEXT_SHIFT_END,
       TEXT_CTRL_LEFT, TEXT_CTRL_RIGHT, TEXT_CTRL_UP, TEXT_CTRL_DOWN, TEXT_CTRL_HOME, TEXT_CTRL_END,
       TEXT_CTRL_SHIFT_LEFT, TEXT_CTRL_SHIFT_RIGHT, TEXT_CTRL_SHIFT_UP, TEXT_CTRL_SHIFT_DOWN,
       TEXT_CTRL_SHIFT_HOME, TEXT_CTRL_SHIFT_END, TEXT_COPY, TEXT_IGNORED,
       TEXT_CONTROL_BASE }; /* Followed by the encoded ASCII key value. */

void text_input_init(void);
int text_input_read(wint_t *key);
/* Kitty's conditional shortcut mapping; fixed non-secret application marker. */
void text_input_terminal_active(bool active);
bool text_selection(const text_buffer *buffer, size_t *start, size_t *end);
void text_unselect(text_buffer *buffer);
/* Common editing controls; returns whether this key belongs to the input. */
bool text_edit(text_buffer *buffer, int type, wint_t key, int width, bool multiline);
bool text_insert_buffer(text_buffer *buffer, const text_buffer *insertion);
void text_paste_begin(text_paste *paste);
/* Read at most 256 queued codes without blocking; returns end/resize or ERR.
 * The callback keeps the terminal capability reply filter in the input path. */
int text_paste_read(text_paste *paste, wint_t *key, bool multiline,
                    int (*read_key)(wint_t *), int timeout_ms);
bool text_paste_commit(text_paste *paste, text_buffer *buffer);
void text_paste_cancel(text_paste *paste);
/* One-row fields share selection, Unicode horizontal scrolling and masking. */
void text_draw_field(WINDOW *window, const text_buffer *buffer, int *first,
                     int y, int x, int width, bool masked);
void text_drag(text_buffer *buffer, int width, int row, int column);

/* Canonical tokens stay in data; only layout projects their labels. */
void text_mentions(text_buffer *buffer, mention_resolver resolver, void *context);
void text_project(const text_buffer *buffer, text_buffer *display);
void text_draw_styled(WINDOW *window, const text_buffer *buffer, const text_layout *layout,
                      const unsigned char *highlight, size_t length, int first, int height,
                      int y, int x, int normal, int accent);
bool text_insert(text_buffer *buffer, wint_t character, bool multiline);
void text_delete(text_buffer *buffer, bool backward);
void text_horizontal(text_buffer *buffer, bool right);
void text_vertical(text_buffer *buffer, int width, int direction);
void text_line_edge(text_buffer *buffer, int width, bool end);
void text_clear(text_buffer *buffer);
void text_measure(const text_buffer *buffer, int width, text_layout *layout);
void text_draw(WINDOW *window, const text_buffer *buffer, const text_layout *layout,
               int first, int height, int y, int x);
void text_click(text_buffer *buffer, int width, int row, int column);
void text_keep_cursor(text_buffer *buffer, const text_layout *layout, int height);
bool text_decode(text_buffer *buffer, const char *utf8);
/* Decode one bounded segment of a long read-only field, sanitizing controls.
 * Advances input without splitting a grapheme at the segment boundary. */
bool text_decode_chunk(text_buffer *buffer, const char **utf8);
bool text_encode(const text_buffer *buffer, char output[TEXT_UTF8_BYTES]);
int text_label(WINDOW *window, int y, int x, int width, const char *utf8);

#endif
