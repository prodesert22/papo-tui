#include "ui/media/hyperlinks.h"
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Terminals limit the address of a hyperlink to about 2000 bytes. */
#define ADDRESS_LIMIT 2000

typedef struct {
    int y, x, columns; /* Screen cells of one row of a link. */
    char *address;     /* Owned: the address in printable ASCII. */
    unsigned id;       /* The same for every row of one link. */
    cchar_t *cells;    /* Owned: the cells as curses drew them. */
    bool pending;      /* Not written yet, or curses writes over its row. */
} span;
static span *spans;
static size_t count, capacity;

static bool supported(void)
{
#ifdef _WIN32
    /* The console driver of curses does not take terminal sequences. */
    return false;
#else
    static int known = -1;
    if (known < 0) {
        /* The Linux console prints the sequences it does not know. */
        const char *term = g_getenv("TERM");
        known = term && *term && strcmp(term, "dumb") && strcmp(term, "linux");
    }
    return known;
#endif
}

void hyperlinks_frame_begin(void)
{
    for (size_t i = 0; i < count; ++i) {
        g_free(spans[i].address);
        free(spans[i].cells);
    }
    count = 0;
}

void hyperlinks_offer(WINDOW *window, int y, int x, int columns, const char *url)
{
    if (!supported() || !window || !url || y < 0 || x < 0 || y >= getmaxy(window) ||
        x >= getmaxx(window) || columns < 1)
        return;
    if (g_ascii_strncasecmp(url, "https://", 8) && g_ascii_strncasecmp(url, "http://", 7))
        return;
    if (columns > getmaxx(window) - x)
        columns = getmaxx(window) - x;
    if (count == capacity) {
        size_t next = capacity ? capacity * 2 : 16;
        span *grown = realloc(spans, next * sizeof(*spans));
        if (!grown)
            return;
        spans = grown;
        capacity = next;
    }
    /* Only printable ASCII reaches the terminal: GLib keeps the characters
     * of an address and writes every other byte as %XX, so remote text can
     * neither end the sequence nor start another one. */
    char *address = g_uri_escape_string(
        url,
        G_URI_RESERVED_CHARS_GENERIC_DELIMITERS G_URI_RESERVED_CHARS_SUBCOMPONENT_DELIMITERS "%",
        FALSE);
    cchar_t *cells = calloc((size_t)columns + 1, sizeof(*cells));
    int cursor_y, cursor_x;
    getyx(window, cursor_y, cursor_x);
    bool read = address && strlen(address) <= ADDRESS_LIMIT && cells &&
                mvwin_wchnstr(window, y, x, cells, columns) != ERR;
    wmove(window, cursor_y, cursor_x);
    if (!read) {
        g_free(address);
        free(cells);
        return;
    }
    spans[count++] = (span){.y = getbegy(window) + y,
                            .x = getbegx(window) + x,
                            .columns = columns,
                            .address = address,
                            .id = g_str_hash(address),
                            .cells = cells,
                            .pending = true};
}

static bool same_cell(const cchar_t *a, const cchar_t *b)
{
    return a->attr == b->attr &&
#if NCURSES_EXT_COLORS
           a->ext_color == b->ext_color &&
#endif
           !memcmp(a->chars, b->chars, sizeof(a->chars));
}

void hyperlinks_frame_prepare(void)
{
    if (!count)
        return;
    int lines = getmaxy(curscr), columns = getmaxx(curscr), cursor_y, cursor_x, next_y, next_x;
    /* A cleared screen is written again in full, and so is a row that
     * differs anywhere: curses also rewrites short unchanged runs. */
    bool cleared = lines <= 0 || columns <= 0 || is_cleared(curscr) || is_cleared(newscr);
    cchar_t *physical = cleared ? NULL : calloc((size_t)columns + 1, sizeof(*physical));
    cchar_t *wanted = cleared ? NULL : calloc((size_t)columns + 1, sizeof(*wanted));
    getyx(curscr, cursor_y, cursor_x);
    getyx(newscr, next_y, next_x);
    int compared = -1;
    bool written = true;
    for (size_t i = 0; i < count; ++i) {
        span *s = &spans[i];
        if (s->pending)
            continue;
        if (!physical || !wanted || s->y >= lines) {
            s->pending = true;
            continue;
        }
        if (s->y != compared) {
            compared = s->y;
            written = mvwin_wchnstr(curscr, s->y, 0, physical, columns) == ERR ||
                      mvwin_wchnstr(newscr, s->y, 0, wanted, columns) == ERR;
            for (int column = 0; !written && column < columns; ++column)
                written = !same_cell(&physical[column], &wanted[column]);
        }
        s->pending = written;
    }
    /* Reading rows moved the curses cursors; restore their bookkeeping. */
    wmove(curscr, cursor_y, cursor_x);
    wmove(newscr, next_y, next_x);
    free(physical);
    free(wanted);
}

static int put(int byte)
{
    return putc(byte, stdout);
}

/* One cell as curses shows it: its attributes and color come from curses'
 * own description of the terminal, which keeps tracking them. */
static void put_cell(const cchar_t *cell)
{
    wchar_t text[CCHARW_MAX + 1] = {0};
    attr_t attributes = A_NORMAL;
    short pair = 0;
    int extended = 0;
    if (getcchar(cell, text, &attributes, &pair, &extended) == ERR || !text[0]) {
        text[0] = L' ';
        text[1] = 0;
    }
    vid_puts(attributes, pair, &extended, put);
    for (size_t i = 0; i < CCHARW_MAX && text[i]; ++i) {
        char utf8[6];
        int length = g_unichar_to_utf8((gunichar)text[i], utf8);
        fwrite(utf8, 1, (size_t)length, stdout);
    }
}

/* The second column of a wide character, written with the first one. */
static bool continuation(const cchar_t *cell)
{
    unsigned extension = (unsigned)(cell->attr & A_CHARTEXT);
    return extension > 1 && extension < 32;
}

void hyperlinks_frame_flush(void)
{
    bool pending = false;
    for (size_t i = 0; i < count && !pending; ++i)
        pending = spans[i].pending;
    if (!pending)
        return;
    int lines = getmaxy(curscr), columns = getmaxx(curscr), cursor_y, cursor_x;
    cchar_t *shown = columns > 0 ? calloc((size_t)columns + 1, sizeof(*shown)) : NULL;
    getyx(curscr, cursor_y, cursor_x);
    bool started = false;
    for (size_t i = 0; i < count; ++i) {
        span *s = &spans[i];
        if (!s->pending)
            continue;
        s->pending = false;
        if (!shown || s->y >= lines || s->x >= columns)
            continue;
        int width = s->columns < columns - s->x ? s->columns : columns - s->x;
        if (mvwin_wchnstr(curscr, s->y, s->x, shown, width) == ERR)
            continue;
        for (int first = 0; first < width;) {
            /* Only cells still showing the link: a menu, a modal or a
             * selection drawn over it changed them. */
            if (!same_cell(&shown[first], &s->cells[first]) || continuation(&shown[first])) {
                ++first;
                continue;
            }
            int end = first;
            while (end < width && same_cell(&shown[end], &s->cells[end]))
                ++end;
            if (!started) {
                /* The cursor returns to where curses left it. */
                fputs("\0337", stdout);
                started = true;
            }
            fprintf(stdout, "\033[%d;%dH\033]8;id=%x;%s\033\\", s->y + 1, s->x + first + 1, s->id,
                    s->address);
            for (int column = first; column < end; ++column)
                if (!continuation(&shown[column]))
                    put_cell(&shown[column]);
            fputs("\033]8;;\033\\", stdout);
            first = end;
        }
    }
    if (started) {
        int normal = 0;
        vid_puts(A_NORMAL, 0, &normal, put);
        fputs("\0338", stdout);
        fflush(stdout);
    }
    wmove(curscr, cursor_y, cursor_x);
    free(shown);
}
