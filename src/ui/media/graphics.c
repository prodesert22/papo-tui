#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "ui/media/graphics.h"
#include "net/clock.h"
#include "platform/runtime.h"
#include "ui/input/text.h"
#include "ui/media/sixel.h"
#include <chafa.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static graphics_terminal terminal;
static bool probed;
static atomic_uint_fast32_t next_id = 0x50510000U;
static atomic_uint_fast64_t next_revision = 1;
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

uint32_t graphics_image_next_id(void)
{
    uint32_t id = (uint32_t)atomic_fetch_add(&next_id, 1);
    return id ? id : (uint32_t)atomic_fetch_add(&next_id, 1);
}
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
    if (probed || !isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO))
        return;
    probed = true;
    const char *term = getenv("TERM");
    if (!term || !strcmp(term, "dumb"))
        return;
    chafa_set_n_threads(1);
    gchar **environment = g_get_environ();
    ChafaTermInfo *info = chafa_term_db_detect(chafa_term_db_get_default(), environment);
    /* Kitty and Sixel require replies. iTerm2 has no image support query;
     * use the library's known terminal signatures, excluding multiplexers. */
    const char *tmux = getenv("TMUX"), *screen = getenv("STY");
    if (info && chafa_term_info_get_best_pixel_mode(info) == CHAFA_PIXEL_MODE_ITERM2 &&
        chafa_term_info_get_passthrough_type(info) == CHAFA_PASSTHROUGH_NONE && (!tmux || !*tmux) &&
        (!screen || !*screen))
        terminal.protocol = GRAPHICS_ITERM2;
    if (info)
        chafa_term_info_unref(info);
    g_strfreev(environment);
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
    unsigned rows, columns, pixels_x, pixels_y;
    if (papo_terminal_size(&rows, &columns, &pixels_x, &pixels_y) && pixels_x && pixels_y) {
        t.cell_width = pixels_x / columns;
        t.cell_height = pixels_y / rows;
    }
    t.supported = t.protocol != GRAPHICS_TEXT && t.cell_width > 0 && t.cell_width <= 128 &&
                  t.cell_height > 0 && t.cell_height <= 128;
    return t;
}

bool graphics_image_encode(const unsigned char *pixels, int width, int height, graphics_terminal t,
                           graphics_image *out)
{
    return graphics_image_encode_id(pixels, width, height, t, graphics_image_next_id(), out);
}

bool graphics_image_encode_id(const unsigned char *pixels, int width, int height,
                              graphics_terminal t, uint32_t id, graphics_image *out)
{
    memset(out, 0, sizeof(*out));
    if (!pixels || !graphics_image_capacity(width, height) || t.protocol == GRAPHICS_TEXT ||
        t.cell_width < 1 || t.cell_width > 128 || t.cell_height < 1 || t.cell_height > 128)
        return false;
    out->protocol = t.protocol;
    out->id = id;
    out->revision = atomic_fetch_add(&next_revision, 1);
    out->width = width;
    out->height = height;
    out->cell_width = t.cell_width;
    out->cell_height = t.cell_height;
    out->columns = (width + t.cell_width - 1) / t.cell_width;
    out->rows = (height + t.cell_height - 1) / t.cell_height;
    if (t.protocol == GRAPHICS_SIXEL) {
        sixel_image image;
        if (!sixel_image_encode(pixels, width, height, &image))
            return false;
        out->data = image.data;
        out->length = image.length;
        return true;
    }
    ChafaCanvasConfig *config = chafa_canvas_config_new();
    ChafaTermInfo *info = chafa_term_db_get_fallback_info(chafa_term_db_get_default());
    ChafaCanvas *canvas = NULL;
    GString *printed = NULL;
    ChafaFrame *frame = NULL;
    ChafaImage *source = NULL;
    ChafaPlacement *placement = NULL;
    bool ok = false;
    if (!config || !info)
        goto done;
    chafa_canvas_config_set_pixel_mode(
        config, t.protocol == GRAPHICS_KITTY ? CHAFA_PIXEL_MODE_KITTY : CHAFA_PIXEL_MODE_ITERM2);
    chafa_canvas_config_set_geometry(config, out->columns, out->rows);
    chafa_canvas_config_set_cell_geometry(config, t.cell_width, t.cell_height);
    if (t.protocol == GRAPHICS_KITTY) {
        char header[160];
        snprintf(header, sizeof(header),
                 "\033_Ga=t,f=%%1,s=%%2,v=%%3,c=%%4,r=%%5,i=%u,m=1,q=2,C=1\033\\", out->id);
        if (!chafa_term_info_set_seq(info, CHAFA_TERM_SEQ_BEGIN_KITTY_IMMEDIATE_IMAGE_V1, header,
                                     NULL))
            goto done;
    }
    canvas = chafa_canvas_new(config);
    if (!canvas)
        goto done;
    frame = chafa_frame_new_borrow(pixels, CHAFA_PIXEL_RGB8, width, height, width * 3);
    source = chafa_image_new();
    if (!frame || !source)
        goto done;
    chafa_image_set_frame(source, frame);
    placement = chafa_placement_new(source, 1);
    if (!placement)
        goto done;
    chafa_placement_set_tuck(placement, CHAFA_TUCK_SHRINK_TO_FIT);
    chafa_placement_set_halign(placement, CHAFA_ALIGN_START);
    chafa_placement_set_valign(placement, CHAFA_ALIGN_START);
    chafa_canvas_set_placement(canvas, placement);
    printed = chafa_canvas_print(canvas, info);
    if (!printed || !printed->len || printed->len > graphics_image_capacity(width, height))
        goto done;
    out->data = malloc(printed->len);
    if (!out->data)
        goto done;
    memcpy(out->data, printed->str, printed->len);
    out->length = printed->len;
    ok = true;
done:
    if (printed)
        g_string_free(printed, true);
    if (canvas)
        chafa_canvas_unref(canvas);
    if (placement)
        chafa_placement_unref(placement);
    if (source)
        chafa_image_unref(source);
    if (frame)
        chafa_frame_unref(frame);
    if (config)
        chafa_canvas_config_unref(config);
    if (info)
        chafa_term_info_unref(info);
    if (!ok)
        graphics_image_free(out);
    return ok;
}

bool graphics_image_encode_preview(const unsigned char *pixels, int width, int height,
                                   graphics_terminal t, uint32_t id, graphics_image *out)
{
    if (!graphics_image_encode_id(pixels, width, height, t, id, out))
        return false;
    if (t.protocol == GRAPHICS_KITTY)
        return true;
    int rows = (height + t.cell_height - 1) / t.cell_height;
    if (rows > 6)
        return true;
    size_t count = (size_t)rows * (size_t)(rows + 1) / 2 - 1;
    if (!count)
        return true;
    out->crops = calloc(count, sizeof(*out->crops));
    if (!out->crops) {
        graphics_image_free(out);
        return false;
    }
    out->crop_count = count;
    size_t at = 0;
    for (int top = 0; top < rows; ++top)
        for (int bottom = 0; bottom < rows - top; ++bottom) {
            if (!top && !bottom)
                continue;
            int crop_bottom = bottom ? height - (rows - bottom) * t.cell_height : 0;
            int h = height - top * t.cell_height - crop_bottom;
            if (h < 1)
                continue;
            graphics_image *crop = &out->crops[at++];
            if (!graphics_image_encode_id(pixels + (size_t)top * (size_t)t.cell_height *
                                                       (size_t)width * 3,
                                          width, h, t, id, crop)) {
                graphics_image_free(out);
                return false;
            }
            crop->crop_top = top * t.cell_height;
            crop->crop_bottom = crop_bottom;
        }
    return true;
}
const graphics_image *graphics_image_crop(const graphics_image *image, int top, int bottom)
{
    if (top == image->crop_top && bottom == image->crop_bottom)
        return image;
    for (size_t i = 0; i < image->crop_count; ++i)
        if (image->crops[i].crop_top == top && image->crops[i].crop_bottom == bottom)
            return &image->crops[i];
    return NULL;
}
size_t graphics_image_bytes(const graphics_image *image)
{
    size_t bytes = image->length + image->crop_count * sizeof(*image->crops);
    for (size_t i = 0; i < image->crop_count; ++i)
        bytes += image->crops[i].length;
    return bytes;
}
void graphics_image_free(graphics_image *image)
{
    if (!image)
        return;
    for (size_t i = 0; i < image->crop_count; ++i)
        graphics_image_free(&image->crops[i]);
    free(image->crops);
    free(image->data);
    memset(image, 0, sizeof(*image));
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
void graphics_frame_begin(void)
{
    desired_count = 0;
    if (physical_snapshot)
        delwin(physical_snapshot);
    physical_snapshot = displayed_count && curscr ? dupwin(curscr) : NULL;
}
void graphics_frame_invalidate(void)
{
    for (size_t i = 0; i < displayed_count; ++i)
        displayed[i].revision = 0;
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
    fputs("\0337", stdout);
    for (size_t i = 0; i < displayed_count; ++i) {
        displayed_image *old = &displayed[i],
                        *next = find_image(desired, desired_count, old->placement_id);
        if (!next || (old->protocol != GRAPHICS_KITTY && !same_placement(old, next)))
            erase_image(old);
        if (old->protocol == GRAPHICS_KITTY) {
            bool retained = false;
            for (size_t j = 0; j < desired_count; ++j)
                retained |= desired[j].id == old->id;
            if (!retained)
                fprintf(stdout, "\033_Ga=d,d=I,i=%u,q=2\033\\", old->id);
        }
    }
    fputs("\0338", stdout);
    fflush(stdout);
    /* Pixels in Sixel/iTerm2 are tied to text cells. Preserve the physical text
     * beneath stationary pictures across forced history redraws and focus
     * background changes. Kitty placements are independent of text cells. */
    if (physical_snapshot)
        for (size_t i = 0; i < desired_count; ++i) {
            displayed_image *next = &desired[i],
                            *old = find_image(displayed, displayed_count, next->placement_id);
            if (next->protocol == GRAPHICS_KITTY || !old || !same_placement(old, next) ||
                old->revision != next->revision)
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
}
void graphics_frame_flush(void)
{
    fputs("\0337", stdout);
    for (size_t i = 0; i < desired_count; ++i) {
        displayed_image *next = &desired[i],
                        *old = find_image(displayed, displayed_count, next->placement_id);
        bool pixels_changed = !old || old->revision != next->revision;
        bool moved = !old || !same_placement(old, next);
        if (!pixels_changed && !moved)
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
    fputs("\0338", stdout);
    fflush(stdout);
}
void graphics_images_clear(void)
{
    if (physical_snapshot) {
        delwin(physical_snapshot);
        physical_snapshot = NULL;
    }
    desired_count = 0;
    if (!displayed_count)
        return;
    fputs("\0337", stdout);
    for (size_t i = 0; i < displayed_count; ++i) {
        erase_image(&displayed[i]);
        if (displayed[i].protocol == GRAPHICS_KITTY)
            fprintf(stdout, "\033_Ga=d,d=I,i=%u,q=2\033\\", displayed[i].id);
    }
    displayed_count = 0;
    fputs("\0338", stdout);
    fflush(stdout);
}
