#define _POSIX_C_SOURCE 200809L
#include "ui/message_blocks.h"
#include "ui/text.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const char *str(json_t *obj, const char *key)
{
    const char *s = json_string_value(json_object_get(obj, key));
    return s ? s : "";
}
uint64_t message_media_version(const json_t *entry)
{
    char *json = json_dumps(entry, JSON_COMPACT | JSON_SORT_KEYS);
    if (!json) return 0;
    uint64_t hash = UINT64_C(14695981039346656037);
    for (const unsigned char *p = (const unsigned char *)json; *p; ++p) { hash ^= *p; hash *= UINT64_C(1099511628211); }
    free(json); return hash;
}
static bool reserve(message_blocks *b, size_t count)
{
    if (count <= b->capacity) return true;
    if (count > 262144) return false;
    size_t capacity = count + 32;
    message_row *rows = realloc(b->rows, capacity * sizeof(*rows));
    if (!rows) return false;
    memset(rows + b->capacity, 0, (capacity - b->capacity) * sizeof(*rows));
    for (size_t i = b->capacity; i < capacity; ++i) rows[i].media_index = -1;
    b->rows = rows; b->capacity = capacity;
    return true;
}
static bool blank(message_blocks *b, bool card, bool top, bool bottom, unsigned color)
{
    if (!reserve(b, b->count + 1)) return false;
    message_row *r = &b->rows[b->count++];
    r->card = card; r->top = top; r->bottom = bottom; r->color = color;
    return true;
}
/* Appends one wrapped column at a specified row. Other columns can occupy the
 * same rows. All terminal controls pass through the shared Unicode decoder. */
static bool column(message_blocks *b, size_t start, const char *key, const char *value,
                   int x, int width, bool bold, bool card, unsigned color, size_t *end)
{
    if (width < 1) width = 1;
    size_t base = 0, row = start;
    const char *p = value;
    do {
        text_buffer buffer;
        text_layout layout;
        if (!text_decode_chunk(&buffer, &p)) return false;
        text_measure(&buffer, width, &layout);
        for (int line = 0; line < layout.rows; ++line) {
            if (!reserve(b, row + 1)) return false;
            message_row *r = &b->rows[row];
            if (r->count == MESSAGE_COLUMNS) return false;
            message_segment *s = &r->segments[r->count++];
            size_t first = buffer.length, last = first;
            for (size_t k = 0; k < layout.count; ++k) if (layout.cells[k].row == line) {
                if (first == buffer.length) first = layout.cells[k].start;
                last = layout.cells[k].end;
            }
            text_buffer part = {0};
            part.length = last - first;
            memcpy(part.data, buffer.data + first, part.length * sizeof(part.data[0]));
            /* Row strings never contain newlines; wrapping is already measured. */
            while (part.length && part.data[part.length - 1] == '\n') --part.length;
            char encoded[TEXT_UTF8_BYTES];
            if (!text_encode(&part, encoded) || !(s->text = strdup(encoded))) return false;
            snprintf(s->key, sizeof(s->key), "%s", key);
            s->offset = base + first; s->x = x; s->width = width; s->bold = bold;
            r->card = card; r->color = color;
            ++row;
            if (row > b->count) b->count = row;
        }
        base += buffer.length;
    } while (*p);
    *end = row;
    return true;
}
static bool text(message_blocks *b, const char *key, const char *value, int width,
                 bool bold, bool card, unsigned color)
{
    size_t end;
    return column(b, b->count, key, value, card ? 2 : 0, width - (card ? 4 : 0), bold, card, color, &end);
}
static bool labeled(message_blocks *b, const char *key, const char *prefix, const char *value,
                    int width, bool bold, bool card, unsigned color)
{
    size_t length = strlen(prefix) + strlen(value) + 1;
    char *joined = malloc(length);
    if (!joined) return false;
    snprintf(joined, length, "%s%s", prefix, value);
    bool ok = text(b, key, joined, width, bold, card, color);
    free(joined); return ok;
}
static int add_media(message_blocks *b, json_t *message, json_t *entry, const char *role, const char *label, auth_media_kind kind, bool video)
{
    const char *id = str(entry, "id");
    if (strlen(id) != 36) return -1;
    size_t n = b->media_count;
    message_media *items = realloc(b->media, (n + 1) * sizeof(*items));
    if (!items) return -1;
    b->media = items;
    message_media *m = &items[n]; memset(m, 0, sizeof(*m));
    snprintf(m->id, sizeof(m->id), "%s", id);
    snprintf(m->message, sizeof(m->message), "%s", str(message, "id"));
    snprintf(m->role, sizeof(m->role), "%s", role);
    snprintf(m->label, sizeof(m->label), "%s", label);
    const char *mime = str(entry, "mime_type");
    if (kind == AUTH_MEDIA_EMBED) mime = str(json_object_get(entry, role), "mime_type");
    snprintf(m->mime, sizeof(m->mime), "%s", mime);
    m->version = message_media_version(entry);
    m->kind = kind; m->video = video; ++b->media_count;
    return (int)n;
}
static bool media(message_blocks *b, json_t *message, json_t *entry, const char *key,
                  const char *role, const char *label, auth_media_kind kind, bool video,
                  int width, bool graphics, bool card, unsigned color)
{
    int n = add_media(b, message, entry, role, label, kind, video);
    if (n < 0) return strlen(str(entry, "id")) != 36;
    size_t start = b->count;
    if (!labeled(b, key, video ? "[▶ Vídeo / F7] " : "[Imagem / F7] ", label, width, false, card, color)) return false;
    int height = graphics && !video ? (!strcmp(role, "author") ? 2 : 6) : 0;
    for (int i = 0; i < height; ++i) if (!blank(b, card, false, false, color)) return false;
    for (size_t i = start; i < b->count; ++i) {
        b->rows[i].media_index = (int)n;
        b->rows[i].media_row = (int)(i - start) - (int)(b->count - start - (size_t)height);
        b->rows[i].media_height = height;
    }
    return true;
}
bool message_blocks_build(message_blocks *b, json_t *m, const char *author,
                          const char *reply, int width, bool graphics)
{
    memset(b, 0, sizeof(*b));
    if (width < 8) width = 8;
    char label[1600];
    snprintf(label, sizeof(label), "%s · %s%s", str(m, "created_at"), author, *str(m, "edited_at") ? " · editada" : "");
    if (!text(b, "header", label, width, false, false, 0)) goto error;
    if (*str(m, "edited_at") && !labeled(b, "edited", "Editada em: ", str(m, "edited_at"), width, false, false, 0)) goto error;
    if (*str(m, "reply_to") && !labeled(b, "reply", "Resposta: ", reply, width, false, false, 0)) goto error;
    if (!text(b, "content", json_is_string(json_object_get(m, "content")) ? str(m, "content") : "[Sem conteúdo textual]", width, false, false, 0)) goto error;
    size_t index; json_t *item;
    json_array_foreach(json_object_get(m, "attachments"), index, item) {
        char key[128]; snprintf(key, sizeof(key), "attachment:%s:%zu", str(item, "id"), index);
        const char *name = *str(item, "original_file_name") ? str(item, "original_file_name") : "arquivo sem nome";
        if (!labeled(b, key, "Anexo: ", name, width, true, false, 0)) goto error;
        snprintf(label, sizeof(label), "Tipo: %s · %lld bytes · Moderação: %s", str(item, "mime_type"),
                 (long long)json_integer_value(json_object_get(item, "size_bytes")), str(item, "moderation_status"));
        if (!text(b, key, label, width, false, false, 0)) goto error;
        bool video = !strncmp(str(item, "mime_type"), "video/", 6);
        bool image = !strncmp(str(item, "mime_type"), "image/", 6);
        if ((image || video) && !media(b, m, item, key, "attachment", name,
             image && json_is_string(json_object_get(item, "thumbnail_id")) ? AUTH_MEDIA_THUMBNAIL : AUTH_MEDIA_ATTACHMENT,
             video, width, graphics, false, 0)) goto error;
    }
    json_array_foreach(json_object_get(m, "embeds"), index, item) {
        if (index >= 10) break;
        char root[96], key[128];
        if (*str(item, "id")) snprintf(root, sizeof(root), "embed:%s", str(item, "id"));
        else snprintf(root, sizeof(root), "embed:index:%zu", index);
        unsigned color = 0;
        const char *hex = str(item, "color");
        if (strlen(hex) == 7 && hex[0] == '#') {
            char *end; unsigned long value = strtoul(hex + 1, &end, 16);
            if (!*end && value <= 0xffffff) color = (unsigned)value | 0x1000000U;
        }
        if (!blank(b, true, true, false, color)) goto error;
        json_t *a = json_object_get(item, "author");
        bool right_thumbnail = graphics && width >= 44 &&
            json_is_object(json_object_get(item, "thumbnail")) && strlen(str(item, "id")) == 36 &&
            !json_is_object(json_object_get(a, "media"));
        size_t thumbnail_start = b->count;
        int text_width = right_thumbnail ? width - 14 : width;
        snprintf(key, sizeof(key), "%s:author", root);
        if (*str(a, "name") && !text(b, key, str(a, "name"), text_width, true, true, color)) goto error;
        if (json_is_object(json_object_get(a, "media")) && !media(b, m, item, key, "author", "Avatar do autor", AUTH_MEDIA_EMBED, false, text_width, graphics, true, color)) goto error;
        snprintf(key, sizeof(key), "%s:site", root);
        const char *site = *str(item, "site_name") ? str(item, "site_name") : str(item, "provider");
        if (*site && !text(b, key, site, text_width, false, true, color)) goto error;
        snprintf(key, sizeof(key), "%s:title", root);
        if (!text(b, key, *str(item, "title") ? str(item, "title") : "Embed", text_width, true, true, color)) goto error;
        snprintf(key, sizeof(key), "%s:description", root);
        if (*str(item, "description") && !text(b, key, str(item, "description"), text_width, false, true, color)) goto error;
        if (*str(item, "url") && !text(b, root, str(item, "url"), text_width, false, true, color)) goto error;
        if (*str(a, "url") && !text(b, key, str(a, "url"), text_width, false, true, color)) goto error;
        if (right_thumbnail) {
            while (b->count < thumbnail_start + 4) if (!blank(b, true, false, false, color)) goto error;
            int thumb = add_media(b, m, item, "thumbnail", "Miniatura", AUTH_MEDIA_EMBED, false);
            if (thumb < 0) goto error;
            for (size_t r = thumbnail_start; r < thumbnail_start + 4; ++r) {
                b->rows[r].media_index = thumb; b->rows[r].media_row = (int)(r - thumbnail_start);
                b->rows[r].media_height = 4; b->rows[r].media_x = width - 14; b->rows[r].media_width = 12;
            }
        }
        json_t *fields = json_object_get(item, "fields");
        size_t count = json_array_size(fields); if (count > 25) count = 25;
        for (size_t f = 0; f < count;) {
            size_t group = 1;
            int columns = (width - 4) / 20;
            if (columns > 3) columns = 3;
            if (columns < 1) columns = 1;
            if (json_is_true(json_object_get(json_array_get(fields, f), "inline")))
                while (group < (size_t)columns && f + group < count && json_is_true(json_object_get(json_array_get(fields, f + group), "inline"))) ++group;
            size_t start = b->count, end = start;
            int colwidth = (width - 4 - (int)group + 1) / (int)group;
            for (size_t c = 0; c < group; ++c) {
                json_t *field = json_array_get(fields, f + c);
                size_t row;
                snprintf(key, sizeof(key), "%s:field:%zu:name", root, f + c);
                if (!column(b, start, key, str(field, "name"), 2 + (int)c * (colwidth + 1), colwidth, true, true, color, &row)) goto error;
                snprintf(key, sizeof(key), "%s:field:%zu:value", root, f + c);
                if (!column(b, row, key, str(field, "value"), 2 + (int)c * (colwidth + 1), colwidth, false, true, color, &row)) goto error;
                if (row > end) end = row;
            }
            b->count = end; f += group;
        }
        snprintf(key, sizeof(key), "%s:thumbnail", root);
        if (!right_thumbnail && json_is_object(json_object_get(item, "thumbnail")) && !media(b, m, item, key, "thumbnail", "Miniatura", AUTH_MEDIA_EMBED, false, width, graphics, true, color)) goto error;
        if (json_is_object(json_object_get(item, "image")) && !text(b, root, "Imagem adicional indisponível no backend.", width, false, true, color)) goto error;
        json_t *v = json_object_get(item, "video");
        if (json_is_object(v)) {
            snprintf(key, sizeof(key), "%s:video", root);
            bool direct = !strcmp(str(v, "mime_type"), "video/mp4") || !strcmp(str(v, "mime_type"), "video/webm") || !strcmp(str(v, "mime_type"), "video/ogg");
            if (direct) {
                if (!media(b, m, item, key, "video", "Abrir player", AUTH_MEDIA_VIDEO, true, width, graphics, true, color)) goto error;
            } else if (!labeled(b, key, "Vídeo externo: ", str(v, "url"), width, false, true, color)) goto error;
        }
        json_t *footer = json_object_get(item, "footer");
        snprintf(key, sizeof(key), "%s:footer", root);
        if (*str(footer, "text") && !labeled(b, key, "Rodapé: ", str(footer, "text"), width, false, true, color)) goto error;
        if (json_is_object(json_object_get(footer, "icon")) && !text(b, key, "Ícone do rodapé indisponível no backend.", width, false, true, color)) goto error;
        if (!blank(b, true, false, true, color)) goto error;
    }
    if (!blank(b, false, false, false, 0)) goto error;
    return true;
error:
    message_blocks_free(b); return false;
}
void message_blocks_free(message_blocks *b)
{
    /* capacity includes partially constructed rows on an allocation failure. */
    for (size_t i = 0; i < b->capacity; ++i)
        for (int k = 0; k < b->rows[i].count; ++k) free(b->rows[i].segments[k].text);
    free(b->rows); free(b->media); memset(b, 0, sizeof(*b));
}
static struct { int fg, bg; } frame_colors[32];
static int frame_color_count;
void message_colors_begin(void) { frame_color_count = 0; }
static int color_pair(unsigned rgb, int fallback)
{
    if (!(rgb & 0x1000000) || !has_colors() || COLORS < 8 || COLOR_PAIRS < 64) return fallback;
    int r = (int)(rgb >> 16) & 255, g = (int)(rgb >> 8) & 255, b = (int)rgb & 255;
    int fg;
    if (COLORS >= 0x1000000) fg = (int)(rgb & 0xffffff);
    else if (COLORS >= 256) {
        const int level[] = {0, 95, 135, 175, 215, 255};
        int best = 0x7fffffff; fg = 16;
        for (int red = 0; red < 6; ++red) for (int green = 0; green < 6; ++green) for (int blue = 0; blue < 6; ++blue) {
            int dr = r - level[red], dg = g - level[green], db = b - level[blue];
            int distance = dr * dr + dg * dg + db * db;
            if (distance < best) { best = distance; fg = 16 + 36 * red + 6 * green + blue; }
        }
    } else fg = (r > 127 ? COLOR_RED : 0) | (g > 127 ? COLOR_GREEN : 0) | (b > 127 ? COLOR_BLUE : 0);
    int oldfg, background;
    if (extended_pair_content(fallback, &oldfg, &background) == ERR) return fallback;
    for (int i = 0; i < frame_color_count; ++i)
        if (frame_colors[i].fg == fg && frame_colors[i].bg == background) return 32 + i;
    if (frame_color_count == 32) return fallback;
    int pair = 32 + frame_color_count;
    if (init_extended_pair(pair, fg, background) == ERR) return fallback;
    frame_colors[frame_color_count].fg = fg; frame_colors[frame_color_count++].bg = background;
    return pair;
}
void message_row_draw(WINDOW *w, const message_row *row, int y, int x, int width, int pair, int accent)
{
    wattrset(w, COLOR_PAIR(pair));
    if (row->card) {
        wattrset(w, COLOR_PAIR(color_pair(row->color, accent)));
        if (row->top || row->bottom) {
            mvwhline(w, y, x, ACS_HLINE, width);
            mvwaddch(w, y, x, row->top ? ACS_ULCORNER : ACS_LLCORNER);
            mvwaddch(w, y, x + width - 1, row->top ? ACS_URCORNER : ACS_LRCORNER);
        } else {
            mvwaddch(w, y, x, ACS_VLINE); mvwaddch(w, y, x + width - 1, ACS_VLINE);
        }
    }
    for (int i = 0; i < row->count; ++i) {
        const message_segment *s = &row->segments[i];
        wattrset(w, COLOR_PAIR(pair) | (s->bold ? A_BOLD : 0));
        text_label(w, y, x + s->x, s->width, s->text);
    }
}
