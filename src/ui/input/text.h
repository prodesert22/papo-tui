#ifndef PAPO_TEXT_H
#define PAPO_TEXT_H

#include <stdint.h>

#include "config/key_binding.h"
#include "model/text/mentions.h"
#include <graphemebreak.h>
#include <ncurses.h>
#include <stdbool.h>

#define TEXT_LIMIT 2048
#define TEXT_UTF8_BYTES (TEXT_LIMIT * 4 + 1)

/* An inline picture projects an original source range into a two-cell object.
 * Resolvers never own input; painters only stage graphics on the main thread. */
typedef size_t (*text_icon_resolver)(void *context, const uint32_t *text, size_t length, size_t at,
                                     uint32_t *icon);
typedef bool (*text_icon_painter)(void *context, WINDOW *window, uint32_t icon, int y, int x,
                                  int pair, bool selected, size_t offset);

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
    text_icon_resolver icon_resolver;
    text_icon_painter icon_painter;
    void *icon_context;
    const uint32_t *icon_map; /* Borrowed only during a projection/measurement. */
} text_buffer;

typedef struct {
    size_t start, end, display_start, display_end;
    bool mention;
    int row, column, width;
    wchar_t glyph[CCHARW_MAX];
    bool newline;
    uint32_t icon;
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

enum {
    TEXT_PASTE_START = KEY_MAX + 1,
    TEXT_PASTE_END,
    TEXT_SHIFT_LEFT,
    TEXT_SHIFT_RIGHT,
    TEXT_SHIFT_UP,
    TEXT_SHIFT_DOWN,
    TEXT_SHIFT_HOME,
    TEXT_SHIFT_END,
    TEXT_CTRL_LEFT,
    TEXT_CTRL_RIGHT,
    TEXT_CTRL_UP,
    TEXT_CTRL_DOWN,
    TEXT_CTRL_HOME,
    TEXT_CTRL_END,
    TEXT_CTRL_SHIFT_LEFT,
    TEXT_CTRL_SHIFT_RIGHT,
    TEXT_CTRL_SHIFT_UP,
    TEXT_CTRL_SHIFT_DOWN,
    TEXT_CTRL_SHIFT_HOME,
    TEXT_CTRL_SHIFT_END,
    TEXT_COPY,
    TEXT_IGNORED,
    TEXT_FOCUS_IN,
    TEXT_FOCUS_OUT,
    TEXT_SHIFT_ENTER,
    TEXT_CTRL_BACKSPACE,
    TEXT_SHIFT_PAGE_UP,
    TEXT_SHIFT_PAGE_DOWN,
    TEXT_KEYBOARD_SUPPORTED,
    /* Alt+arrow: move the focus to the component in that direction. */
    TEXT_ALT_LEFT,
    TEXT_ALT_RIGHT,
    TEXT_ALT_UP,
    TEXT_ALT_DOWN,
    TEXT_CONTROL_BASE,
    /* Synthetic events from the configured push-to-talk binding. Toggle is
     * used by terminals that report a control byte but no key release. */
    TEXT_PTT_PRESS = TEXT_CONTROL_BASE + 32,
    TEXT_PTT_RELEASE,
    TEXT_PTT_TOGGLE,
    TEXT_CAPTURED,
    TEXT_CAPTURE_CANCELLED
}; /* Followed by the encoded ASCII key value. */

void text_input_init(void);
/* Maps the configured push-to-talk combination to synthetic press/release
 * events while a call is active. Passing NULL restores ordinary handling. */
void text_push_to_talk_configure(const key_binding *binding);
/* Captures one raw key combination with curses key decoding disabled. The
 * caller keeps polling; take() reports the combination even when invalid. */
bool text_capture_begin(void);
bool text_capture_active(void);
void text_capture_cancel(void);
bool text_capture_take(key_binding *binding);
int text_read_raw(uint32_t *key);
int text_input_read(uint32_t *key);
/* Focus reporting and negotiated keyboard mode; restore the mode on shutdown. */
void text_input_terminal_active(bool active);
bool text_terminal_focused(void);
bool text_terminal_focus_unknown(void);
bool text_keyboard_events_supported(void);
void text_input_event_reporting(bool enabled);
bool text_selection(const text_buffer *buffer, size_t *start, size_t *end);
void text_unselect(text_buffer *buffer);
/* Common editing controls; returns whether this key belongs to the input. */
bool text_edit(text_buffer *buffer, int type, uint32_t key, int width, bool multiline);
bool text_insert_buffer(text_buffer *buffer, const text_buffer *insertion);
void text_paste_begin(text_paste *paste);
/* Read at most 256 queued codes without blocking; returns end/resize or ERR.
 * The callback keeps the terminal capability reply filter in the input path. */
int text_paste_read(text_paste *paste, uint32_t *key, bool multiline, int (*read_key)(uint32_t *),
                    int timeout_ms);
bool text_paste_commit(text_paste *paste, text_buffer *buffer);
void text_paste_cancel(text_paste *paste);
/* One-row fields share selection, Unicode horizontal scrolling and masking. */
void text_draw_field(WINDOW *window, const text_buffer *buffer, int *first, int y, int x, int width,
                     bool masked);
void text_drag(text_buffer *buffer, int width, int row, int column);

/* Canonical tokens stay in data; only layout projects their labels. */
void text_mentions(text_buffer *buffer, mention_resolver resolver, void *context);
void text_icons(text_buffer *buffer, text_icon_resolver resolver, text_icon_painter painter,
                void *context);
void text_project(const text_buffer *buffer, text_buffer *display);
void text_draw_styled(WINDOW *window, const text_buffer *buffer, const text_layout *layout,
                      const unsigned char *highlight, size_t length, int first, int height, int y,
                      int x, int normal, int accent);
bool text_insert(text_buffer *buffer, uint32_t character, bool multiline);
void text_delete(text_buffer *buffer, bool backward);
void text_delete_word(text_buffer *buffer, bool right);
void text_horizontal(text_buffer *buffer, bool right);
void text_vertical(text_buffer *buffer, int width, int direction);
void text_line_edge(text_buffer *buffer, int width, bool end);
void text_clear(text_buffer *buffer);
void text_measure(const text_buffer *buffer, int width, text_layout *layout);
/* Like text_measure, but wraps at word boundaries instead of mid-word. */
void text_measure_words(const text_buffer *buffer, int width, text_layout *layout);
void text_draw(WINDOW *window, const text_buffer *buffer, const text_layout *layout, int first,
               int height, int y, int x);
void text_click(text_buffer *buffer, int width, int row, int column);
void text_keep_cursor(text_buffer *buffer, const text_layout *layout, int height);
bool text_decode(text_buffer *buffer, const char *utf8);
/* Decode one bounded segment of a long read-only field, sanitizing controls.
 * Advances input without splitting a grapheme at the segment boundary. */
bool text_decode_chunk(text_buffer *buffer, const char **utf8);
bool text_encode(const text_buffer *buffer, char output[TEXT_UTF8_BYTES]);
int text_label(WINDOW *window, int y, int x, int width, const char *utf8);

#endif
