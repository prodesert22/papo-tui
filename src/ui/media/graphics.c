#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "ui/media/graphics.h"
#include "config/app_mode.h"
#include "net/clock.h"
#include "platform/runtime.h"
#include "ui/components/theme.h"
#include "ui/input/text.h"
#include "ui/media/media_service.h"
#include "ui/media/sixel.h"
#include <glib.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MEDIA_PAIR_START (THEME_PAIR_END + 32)
#define MEDIA_PAIR_LIMIT 8192
typedef struct {
    int foreground, background;
} media_pair;
static media_pair media_pairs[MEDIA_PAIR_LIMIT];
static size_t media_pair_count;
static GHashTable *media_frame_pairs;
static uint64_t media_pair_keys[MEDIA_PAIR_LIMIT];

static int color_distance(int a, int b)
{
    int r = ((a >> 16) & 255) - ((b >> 16) & 255);
    int g = ((a >> 8) & 255) - ((b >> 8) & 255);
    int blue = (a & 255) - (b & 255);
    return r * r + g * g + blue * blue;
}

/* Resolve RGB against the terminal palette, excluding theme overrides. Direct
 * color terminfo uses packed RGB as the ncurses color number. */
static int media_color(int rgb, const int *palette, int count)
{
    if (COLORS >= 0x1000000)
        return rgb;
    if (COLORS >= 256) {
        /* Squared RGB distance is separable on the xterm color cube. This
         * finds exactly the same minimum as scanning all 216 cube entries. */
        static const int level[] = {0, 95, 135, 175, 215, 255};
        int rgb_components[] = {(rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255}, indices[3];
        for (int channel = 0; channel < 3; ++channel) {
            int best = 0, distance = INT_MAX;
            for (int i = 0; i < 6; ++i) {
                int delta = rgb_components[channel] - level[i], next = delta * delta;
                if (next < distance) {
                    best = i;
                    distance = next;
                }
            }
            indices[channel] = best;
        }
        int best = 16 + indices[0] * 36 + indices[1] * 6 + indices[2];
        int cube = (level[indices[0]] << 16) | (level[indices[1]] << 8) | level[indices[2]];
        int distance = color_distance(rgb, cube);
        for (int i = 232; i < 256; ++i) {
            if (i >= 240 && i <= 253)
                continue; /* Theme-owned slots. */
            int gray = (8 + (i - 232) * 10) * 0x10101, next = color_distance(rgb, gray);
            if (next < distance) {
                best = i;
                distance = next;
            }
        }
        return best;
    }
    if (count > 16)
        count = 16;
    int best = 0, distance = INT_MAX;
    /* The first 16 slots may be recolored by the user. Prefer the stable
     * xterm cube/grays when the terminal offers 256 colors. */
    for (int i = COLORS >= 256 ? 16 : 0; i < count; ++i) {
        if (palette[i] < 0)
            continue;
        int next = color_distance(rgb, palette[i]);
        if (next < distance) {
            best = i;
            distance = next;
        }
    }
    return best;
}

static int media_color_pair(GHashTable *frame, int fg, int bg, int limit)
{
    uint64_t key = ((uint64_t)(uint32_t)fg << 32) | (uint32_t)bg;
    gpointer found = g_hash_table_lookup(frame, &key);
    if (found)
        return GPOINTER_TO_INT(found);
    /* A frame owns its pair assignments: never recycle a pair between
     * two visible cells. Later frames may redefine unused assignments. */
    int used = (int)g_hash_table_size(frame);
    if (used >= limit) {
        /* Very small pair tables: retain the closest allocated pair rather
         * than redefining one already used by another cell in this frame. */
        int best = THEME_PANEL, distance = INT_MAX;
        for (int i = 0; i < used; ++i) {
            int next = abs(fg - media_pairs[i].foreground) + abs(bg - media_pairs[i].background);
            if (next < distance) {
                distance = next;
                best = MEDIA_PAIR_START + i;
            }
        }
        return best;
    }
    int pair = MEDIA_PAIR_START + used;
    if ((size_t)used >= media_pair_count || media_pairs[used].foreground != fg ||
        media_pairs[used].background != bg) {
        if (init_extended_pair(pair, fg, bg) == ERR)
            return THEME_PANEL;
        media_pairs[used] = (media_pair){fg, bg};
        if ((size_t)used >= media_pair_count)
            media_pair_count = (size_t)used + 1;
    }
    media_pair_keys[used] = key;
    g_hash_table_insert(frame, &media_pair_keys[used], GINT_TO_POINTER(pair));
    return pair;
}

static graphics_terminal terminal;
static bool probed;
typedef struct {
    graphics_protocol protocol;
    uint32_t id, placement_id;
    uint64_t revision;
    int y, x, rows, columns, top, bottom;
    bool scaled;
    const graphics_image *image; /* Borrowed only until this frame is flushed. */
} displayed_image;
static displayed_image *displayed, *desired;
static size_t displayed_capacity, desired_capacity;
static size_t displayed_count, desired_count;
static WINDOW *physical_snapshot;
static bool synchronized;

void graphics_update_begin(void)
{
    graphics_terminal t = graphics_terminal_get();
    if (!synchronized && t.supported && (t.synchronized || t.protocol == GRAPHICS_KITTY)) {
        fputs("\033[?2026h", stdout);
        fflush(stdout);
        synchronized = true;
    }
}
void graphics_update_end(void)
{
    if (synchronized) {
        fputs("\033[?2026l", stdout);
        fflush(stdout);
        synchronized = false;
    }
}

bool graphics_terminal_reply(graphics_terminal *t, const char *reply)
{
    if (!t || !reply || strlen(reply) > 255)
        return false;
    if (!strcmp(reply, "\033[?2026;1$y") || !strcmp(reply, "\033[?2026;2$y")) {
        t->synchronized = true;
        return true;
    }
    if (!strcmp(reply, "\033[?2026;0$y") || !strcmp(reply, "\033[?2026;4$y")) {
        t->synchronized = false;
        return true;
    }
    char expected[64];
    snprintf(expected, sizeof(expected), "\033_Gi=%u;OK\033\\", GRAPHICS_PROBE_ID);
    if (!strcmp(reply, expected)) {
        t->protocol = GRAPHICS_KITTY;
        return true;
    }
    sixel_terminal legacy = {t->protocol == GRAPHICS_SIXEL, t->cell_width, t->cell_height};
    if (!sixel_terminal_reply(&legacy, reply))
        return false;
    t->cell_width = legacy.cell_width;
    t->cell_height = legacy.cell_height;
    if (!strncmp(reply, "\033[?", 3) && t->protocol != GRAPHICS_KITTY) {
        if (legacy.supported)
            t->protocol = GRAPHICS_SIXEL;
        else if (t->protocol == GRAPHICS_SIXEL)
            t->protocol = GRAPHICS_TEXT;
    }
    return true;
}

void graphics_terminal_probe(void)
{
    if (app_mode_get() != APP_MODE_NORMAL)
        return;
    if (probed || !isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO))
        return;
    probed = true;
    const char *term = getenv("TERM");
    if (!term || !strcmp(term, "dumb"))
        return;
    if (media_service_iterm2())
        terminal.protocol = GRAPHICS_ITERM2;
    fprintf(stdout, "\033_Gi=%u,s=1,v=1,a=q,t=d,f=24;AAAA\033\\", GRAPHICS_PROBE_ID);
    fputs("\033[c\033[16t\033[14t\033[?2026$p", stdout);
    fflush(stdout);
}

int graphics_terminal_input(uint32_t *key)
{
    static char pending[256];
    static size_t used;
    static uint64_t started;
    for (;;) {
        uint64_t now = 0;
        net_now_ms(&now);
        if (used && now - started > 250)
            used = 0;
        int type = text_input_read(key);
        if (type == ERR)
            return ERR;
        if (!used) {
            if (type != OK || *key != 27)
                return type;
            pending[used++] = 27;
            started = now;
            /* A standalone Escape keeps its ordinary behavior. */
            uint32_t next;
            int next_type = text_read_raw(&next);
            if (next_type != OK || (next != '[' && next != '_')) {
                used = 0;
                if (next_type == OK)
                    unget_wch(next);
                else if (next_type == KEY_CODE_YES)
                    ungetch((int)next);
                *key = 27;
                return OK;
            }
            pending[used++] = (char)next;
            continue;
        }
        if (type != OK || *key > 127 || used == sizeof(pending) - 1) {
            used = 0;
            if (type == KEY_CODE_YES)
                return type;
            continue;
        }
        pending[used++] = (char)*key;
        pending[used] = 0;
        bool apc = pending[1] == '_';
        if (used == 3 && (apc ? pending[2] != 'G'
                              : pending[2] != '?' && !(pending[2] >= '0' && pending[2] <= '9'))) {
            while (used > 1)
                unget_wch((unsigned char)pending[--used]);
            used = 0;
            *key = 27;
            return OK;
        }
        bool complete = apc ? used > 3 && pending[used - 2] == 27 && *key == '\\'
                            : *key >= 0x40 && *key <= 0x7e;
        if (complete) {
            /* Consume graphics acknowledgements even on rejection; they are
             * terminal responses and must never become editor text. */
            if ((probed && graphics_terminal_reply(&terminal, pending)) || apc) {
                used = 0;
                continue;
            }
            /* Unmapped CSI-u reports must not insert modifier numbers into
             * the draft or act as Escape. Known keys are decoded by curses. */
            if (*key == 'u') {
                used = 0;
                *key = TEXT_IGNORED;
                return KEY_CODE_YES;
            }
            while (used > 1)
                unget_wch((unsigned char)pending[--used]);
            used = 0;
            *key = 27;
            return OK;
        } else if (!apc && !((*key >= '0' && *key <= '9') || *key == '?' || *key == ';' ||
                             *key == '$' || *key == ':'))
            used = 0;
    }
}

graphics_terminal graphics_terminal_get(void)
{
    graphics_terminal t = terminal;
    if (app_mode_get() == APP_MODE_NORMAL && media_service_iterm2())
        t.protocol = GRAPHICS_ITERM2;
    unsigned rows, columns, pixels_x, pixels_y;
    if (papo_terminal_size(&rows, &columns, &pixels_x, &pixels_y) && pixels_x && pixels_y) {
        t.cell_width = pixels_x / columns;
        t.cell_height = pixels_y / rows;
    }
    t.supported = t.protocol != GRAPHICS_TEXT && t.cell_width > 0 && t.cell_width <= 128 &&
                  t.cell_height > 0 && t.cell_height <= 128;
    if (app_mode_get() == APP_MODE_ULTRALIGHT) {
        t.protocol = GRAPHICS_TEXT;
        t.supported = false;
    } else if (app_mode_get() == APP_MODE_LIGHT || !t.supported) {
        t.protocol = GRAPHICS_CHAFA;
        t.supported = true;
        t.synchronized = false;
        if (t.cell_width < 1 || t.cell_width > 128)
            t.cell_width = 8;
        if (t.cell_height < 1 || t.cell_height > 128)
            t.cell_height = 16;
    }
    return t;
}

static bool same_placement(const displayed_image *a, const displayed_image *b)
{
    return a->id == b->id && a->placement_id == b->placement_id && a->protocol == b->protocol &&
           a->y == b->y && a->x == b->x && a->rows == b->rows && a->columns == b->columns &&
           a->top == b->top && a->bottom == b->bottom && a->scaled == b->scaled;
}
static displayed_image *find_image(displayed_image *images, size_t count, uint32_t id)
{
    for (size_t i = 0; i < count; ++i)
        if (images[i].placement_id == id)
            return &images[i];
    return NULL;
}
static void erase_image(const displayed_image *d)
{
    if (d->protocol == GRAPHICS_CHAFA)
        return;
    if (d->protocol == GRAPHICS_KITTY)
        fprintf(stdout, "\033_Ga=d,d=i,i=%u,p=%u,q=2\033\\", d->id, d->placement_id);
    else {
        for (int row = 0; row < d->rows && d->y + row < LINES; ++row) {
            fprintf(stdout, "\033[%d;%dH", d->y + row + 1, d->x + 1);
            for (int col = 0; col < d->columns && d->x + col < COLS; ++col)
                fputc(' ', stdout);
        }
        /* Raw pixel erasure also changes text cells. Let curses repaint them. */
        if (curscr && d->y < LINES)
            wredrawln(curscr, d->y, d->rows < LINES - d->y ? d->rows : LINES - d->y);
    }
}
static bool has_pixel_images(const displayed_image *images, size_t count)
{
    for (size_t i = 0; i < count; ++i)
        if (images[i].protocol != GRAPHICS_CHAFA)
            return true;
    return false;
}
void graphics_frame_begin(void)
{
    desired_count = 0;
    if (physical_snapshot)
        delwin(physical_snapshot);
    bool pixels = false;
    for (size_t i = 0; i < displayed_count; ++i)
        pixels |=
            displayed[i].protocol == GRAPHICS_SIXEL || displayed[i].protocol == GRAPHICS_ITERM2;
    physical_snapshot = pixels && curscr ? dupwin(curscr) : NULL;
}
void graphics_frame_invalidate(void)
{
    for (size_t i = 0; i < displayed_count; ++i)
        displayed[i].revision = 0;
}
void graphics_frame_occlude(int y, int x, int rows, int columns)
{
    size_t count = 0;
    for (size_t i = 0; i < desired_count; ++i) {
        displayed_image image = desired[i];
        if (image.y < y + rows && image.y + image.rows > y && image.x < x + columns &&
            image.x + image.columns > x)
            continue;
        desired[count++] = image;
    }
    desired_count = count;
}
void graphics_image_place(const graphics_image *image, int y, int x, int top, int bottom)
{
    graphics_image_place_id(image, image ? image->id : 0, y, x, top, bottom);
}
static void place_image(const graphics_image *image, uint32_t placement_id, int y, int x, int top,
                        int bottom, bool scaled)
{
    if (!image || !image->data || image->protocol != graphics_terminal_get().protocol || top < 0 ||
        bottom < 0 || top + bottom >= image->height || y < 0 || x < 0 || !placement_id ||
        desired_count >= 65536)
        return;
    if (desired_count == desired_capacity) {
        size_t capacity = desired_capacity ? desired_capacity * 2 : 64;
        displayed_image *next = realloc(desired, capacity * sizeof(*next));
        if (!next)
            return;
        desired = next;
        desired_capacity = capacity;
    }
    int rows = scaled
                   ? image->rows
                   : (image->height - top - bottom + image->cell_height - 1) / image->cell_height;
    if (y + rows > LINES || x + image->columns > COLS)
        return;
    desired[desired_count++] =
        (displayed_image){image->protocol, image->id, placement_id, image->revision, y,    x, rows,
                          image->columns,  top,       bottom,       scaled,          image};
}
void graphics_image_place_id(const graphics_image *image, uint32_t placement_id, int y, int x,
                             int top, int bottom)
{
    place_image(image, placement_id, y, x, top, bottom, false);
}
void graphics_image_place_scaled(const graphics_image *image, int y, int x)
{
    if (image && image->protocol == GRAPHICS_KITTY)
        place_image(image, image->id, y, x, 0, 0, true);
}

void graphics_frame_prepare(void)
{
    if (!displayed_count && !desired_count)
        return;
    /* Character rendering uses ncurses' cursor bookkeeping. External cursor
     * saves/restores are needed only when writing pixel protocol controls. */
    bool pixels = has_pixel_images(displayed, displayed_count);
    if (pixels)
        fputs("\0337", stdout);
    for (size_t i = 0; i < displayed_count; ++i) {
        displayed_image *old = &displayed[i],
                        *next = find_image(desired, desired_count, old->placement_id);
        /* Kitty placement IDs belong to an image, so changing the encoded
         * variant needs an explicit deletion even when the occurrence ID stays
         * the same. The old image may still be used elsewhere in the history. */
        if (!next || old->id != next->id ||
            (old->protocol != GRAPHICS_KITTY && !same_placement(old, next)))
            erase_image(old);
        if (old->protocol == GRAPHICS_KITTY) {
            bool retained = false;
            for (size_t j = 0; j < desired_count; ++j)
                retained |= desired[j].id == old->id;
            if (!retained)
                fprintf(stdout, "\033_Ga=d,d=I,i=%u,q=2\033\\", old->id);
        }
    }
    if (pixels)
        fputs("\0338", stdout);
    fflush(stdout);
    /* Pixels in Sixel/iTerm2 are tied to text cells. Preserve the physical text
     * beneath stationary pictures across forced history redraws and focus
     * background changes. Kitty placements are independent of text cells. */
    if (physical_snapshot)
        for (size_t i = 0; i < desired_count; ++i) {
            displayed_image *next = &desired[i],
                            *old = find_image(displayed, displayed_count, next->placement_id);
            if (next->protocol == GRAPHICS_CHAFA || next->protocol == GRAPHICS_KITTY || !old ||
                !same_placement(old, next) || old->revision != next->revision)
                continue;
            int bottom = next->y + next->rows - 1, right = next->x + next->columns - 1;
            if (bottom >= getmaxy(physical_snapshot) || right >= getmaxx(physical_snapshot))
                continue;
            copywin(physical_snapshot, curscr, next->y, next->x, next->y, next->x, bottom, right,
                    false);
            copywin(physical_snapshot, newscr, next->y, next->x, next->y, next->x, bottom, right,
                    false);
        }
    if (physical_snapshot) {
        delwin(physical_snapshot);
        physical_snapshot = NULL;
    }
    bool chafa = false;
    for (size_t i = 0; i < desired_count; ++i)
        chafa |= desired[i].protocol == GRAPHICS_CHAFA;
    if (!chafa)
        return;
    int saved_y = -1, saved_x = -1;
    getsyx(saved_y, saved_x);
    int palette[16], palette_size = COLORS < 16 ? COLORS : 16;
    for (int i = 0; i < 16; ++i)
        palette[i] = -1;
    if (COLORS < 256)
        for (int i = 0; i < palette_size; ++i) {
            short r, g, b;
            palette[i] = color_content((short)i, &r, &g, &b) == OK
                             ? (((r * 255 + 500) / 1000) << 16) | (((g * 255 + 500) / 1000) << 8) |
                                   ((b * 255 + 500) / 1000)
                             : -1;
        }
    if (!media_frame_pairs)
        media_frame_pairs = g_hash_table_new(g_int64_hash, g_int64_equal);
    else
        g_hash_table_remove_all(media_frame_pairs);
    GHashTable *frame_pairs = media_frame_pairs;
    int pair_limit = COLOR_PAIRS - MEDIA_PAIR_START;
    if (pair_limit > MEDIA_PAIR_LIMIT)
        pair_limit = MEDIA_PAIR_LIMIT;
    for (size_t n = 0; n < desired_count; ++n) {
        const displayed_image *d = &desired[n];
        if (d->protocol != GRAPHICS_CHAFA)
            continue;
        const graphics_image *image = d->image;
        const graphics_cell *cells = (const graphics_cell *)image->data;
        int first = d->top / image->cell_height;
        for (int row = 0; row < d->rows && first + row < image->rows; ++row)
            for (int col = 0; col < image->columns; ++col) {
                const graphics_cell *cell =
                    &cells[(size_t)(first + row) * (size_t)image->columns + (size_t)col];
                int pair = THEME_PANEL;
                if (has_colors() && palette_size > 0 && pair_limit > 0) {
                    int fg = media_color(cell->foreground < 0 ? 0xffffff : cell->foreground,
                                         palette, palette_size);
                    int bg = media_color(cell->background < 0 ? 0 : cell->background, palette,
                                         palette_size);
                    pair = media_color_pair(frame_pairs, fg, bg, pair_limit);
                }
                wchar_t value[2] = {(wchar_t)cell->character, 0};
                cchar_t c;
                if (cell->character < 32 || (cell->character >= 127 && cell->character < 160))
                    value[0] = ' ';
                if (setcchar(&c, value, 0, (short)pair, NULL) == OK)
                    mvwadd_wch(newscr, d->y + row, d->x + col, &c);
            }
    }
    setsyx(saved_y, saved_x);
}
void graphics_frame_flush(void)
{
    if (!displayed_count && !desired_count)
        return;
    bool pixels = has_pixel_images(desired, desired_count);
    if (pixels)
        fputs("\0337", stdout);
    for (size_t i = 0; i < desired_count; ++i) {
        displayed_image *next = &desired[i],
                        *old = find_image(displayed, displayed_count, next->placement_id);
        bool pixels_changed = !old || old->revision != next->revision;
        bool moved = !old || !same_placement(old, next);
        if (!pixels_changed && !moved)
            continue;
        if (next->protocol == GRAPHICS_CHAFA)
            continue;
        fprintf(stdout, "\033[%d;%dH", next->y + 1, next->x + 1);
        if (next->protocol == GRAPHICS_KITTY) {
            bool uploaded = false;
            for (size_t j = 0; j < displayed_count; ++j)
                uploaded |= displayed[j].id == next->id && displayed[j].revision == next->revision;
            for (size_t j = 0; j < i; ++j)
                uploaded |= desired[j].id == next->id && desired[j].revision == next->revision;
            if (!uploaded)
                fwrite(next->image->data, 1, next->image->length, stdout);
            /* Reusing the placement ID moves/crops the existing image atomically. */
            fprintf(stdout, "\033_Ga=p,i=%u,p=%u,q=2,C=1,x=0,y=%d,w=%d,h=%d,c=%d,r=%d\033\\",
                    next->id, next->placement_id, next->top, next->image->width,
                    next->image->height - next->top - next->bottom,
                    next->scaled ? next->columns : 0, next->scaled ? next->rows : 0);
        } else
            fwrite(next->image->data, 1, next->image->length, stdout);
    }
    if (displayed_capacity < desired_count) {
        displayed_image *next = realloc(displayed, desired_capacity * sizeof(*next));
        if (!next) {
            desired_count = 0;
            if (pixels)
                fputs("\0338", stdout);
            fflush(stdout);
            return;
        }
        displayed = next;
        displayed_capacity = desired_capacity;
    }
    if (desired_count)
        memcpy(displayed, desired, desired_count * sizeof(*desired));
    displayed_count = desired_count;
    for (size_t i = 0; i < displayed_count; ++i)
        displayed[i].image = NULL;
    desired_count = 0;
    if (pixels)
        fputs("\0338", stdout);
    fflush(stdout);
}
void graphics_images_clear(void)
{
    if (media_frame_pairs) {
        g_hash_table_destroy(media_frame_pairs);
        media_frame_pairs = NULL;
    }
    media_pair_count = 0;
    if (physical_snapshot) {
        delwin(physical_snapshot);
        physical_snapshot = NULL;
    }
    desired_count = 0;
    if (!displayed_count)
        return;
    bool pixels = has_pixel_images(displayed, displayed_count);
    if (pixels)
        fputs("\0337", stdout);
    for (size_t i = 0; i < displayed_count; ++i) {
        erase_image(&displayed[i]);
        if (displayed[i].protocol == GRAPHICS_KITTY)
            fprintf(stdout, "\033_Ga=d,d=I,i=%u,q=2\033\\", displayed[i].id);
    }
    displayed_count = 0;
    if (pixels)
        fputs("\0338", stdout);
    fflush(stdout);
}
