#ifndef PAPO_TEXT_H
#define PAPO_TEXT_H

#include <graphemebreak.h>
#include <ncurses.h>
#include <stdbool.h>

#define TEXT_LIMIT 2048
#define TEXT_UTF8_BYTES (TEXT_LIMIT * 4 + 1)

/* Owned bounded buffer, Unicode scalar offsets and viewport. Segmentation is
 * provided by libunibreak; cursor offsets always fall between graphemes. */
typedef struct {
    utf32_t data[TEXT_LIMIT + 1];
    size_t length, cursor;
    int first_row, goal_column;
    bool trailing; /* Prefer the preceding visual line at a soft wrap. */
} text_buffer;

typedef struct {
    size_t start, end;
    int row, column, width;
    wchar_t glyph[CCHARW_MAX];
    bool newline;
} text_cell;

typedef struct {
    text_cell cells[TEXT_LIMIT + 1];
    size_t count;
    int rows, cursor_row, cursor_column;
} text_layout;

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
bool text_encode(const text_buffer *buffer, char output[TEXT_UTF8_BYTES]);
int text_label(WINDOW *window, int y, int x, int width, const char *utf8);

#endif
