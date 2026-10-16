#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "ui/media/message_blocks.h"
#include "config/app_mode.h"
#include "model/conversation/history.h"
#include "ui/components/theme.h"
#include "ui/input/text.h"
#include "ui/media/graphics.h"
#include "ui/media/message_time.h"
#include <glib.h>
#include <limits.h>
#include <linebreak.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define IMAGE_GAP_COLUMNS 2
static const char *str(const json_t *obj, const char *key)
{
    const char *s = json_string_value(json_object_get(obj, key));
    return s ? s : "";
}
bool message_same_author(const json_t *previous, const json_t *message)
{
    if (!json_is_object(previous) || !json_is_object(message) ||
        strcmp(str(previous, "channel_id"), str(message, "channel_id")))
        return false;
    bool app = history_message_webhook(message);
    if (app != history_message_webhook(previous))
        return false;
    if (app) {
        if (strcmp(str(previous, "webhook_id"), str(message, "webhook_id")))
            return false;
        json_t *a = json_object_get(previous, "external_author"),
               *b = json_object_get(message, "external_author");
        const char *fields[] = {"source", "id", "name", "avatar_url"};
        for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i)
            if (strcmp(str(a, fields[i]), str(b, fields[i])))
                return false;
        return true;
    }
    const char *id = str(message, "author_id");
    if (*str(message, "edited_at"))
        return false;
    return history_uuid(id) && !strcmp(str(previous, "author_id"), id);
}
uint64_t message_media_version(const json_t *entry)
{
    char *json = json_dumps(entry, JSON_COMPACT | JSON_SORT_KEYS);
    if (!json)
        return 0;
    uint64_t hash = UINT64_C(14695981039346656037);
    for (const unsigned char *p = (const unsigned char *)json; *p; ++p) {
        hash ^= *p;
        hash *= UINT64_C(1099511628211);
    }
    free(json);
    return hash;
}
static bool reserve(message_blocks *b, size_t count)
{
    if (count <= b->capacity)
        return true;
    if (count > 262144)
        return false;
    size_t capacity = count + 32;
    message_row *rows = realloc(b->rows, capacity * sizeof(*rows));
    if (!rows)
        return false;
    memset(rows + b->capacity, 0, (capacity - b->capacity) * sizeof(*rows));
    b->rows = rows;
    b->capacity = capacity;
    return true;
}
static bool blank(message_blocks *b, bool card, bool top, bool bottom, unsigned color)
{
    if (!reserve(b, b->count + 1))
        return false;
    message_row *r = &b->rows[b->count++];
    r->card = card;
    r->top = top;
    r->bottom = bottom;
    r->color = color;
    return true;
}
/* libunibreak chooses word/Unicode line boundaries; the existing text
 * decoder supplies sanitized graphemes and their actual terminal widths. */
static void card_measure(const text_buffer *buffer, int width, text_layout *layout)
{
    char breaks[TEXT_LIMIT];
    text_buffer visible;
    text_project(buffer, &visible);
    init_linebreak();
    set_linebreaks_utf32(visible.data, visible.length, NULL, breaks);
    text_measure(buffer, TEXT_LIMIT * 2 + 1, layout);
    size_t count = layout->count - 1;
    int row = 0, column = 0;
    for (size_t start = 0; start < count;) {
        size_t end = start, boundary = start;
        column = 0;
        while (end < count && !layout->cells[end].newline &&
               column + layout->cells[end].width <= width) {
            const text_cell *cell = &layout->cells[end++];
            column += cell->width;
            if (breaks[cell->display_end - 1] == LINEBREAK_ALLOWBREAK)
                boundary = end;
        }
        if (end < count && !layout->cells[end].newline && boundary > start)
            end = boundary;
        if (end == start && !layout->cells[end].newline)
            ++end;
        column = 0;
        for (size_t k = start; k < end; ++k) {
            layout->cells[k].row = row;
            layout->cells[k].column = column;
            column += layout->cells[k].width;
        }
        if (end < count && layout->cells[end].newline) {
            layout->cells[end].row = row;
            layout->cells[end].column = column;
            ++end;
            ++row;
            column = 0;
        } else if (end < count) {
            ++row;
            column = 0;
        }
        start = end;
    }
    layout->cells[count].row = row;
    layout->cells[count].column = column;
    layout->rows = row + 1;
}
/* Appends one wrapped column at a specified row. Other columns can occupy the
 * same rows. All terminal controls pass through the shared Unicode decoder. */
static bool column(message_blocks *b, size_t start, const char *key, const char *value, int x,
                   int width, bool bold, bool card, unsigned color, size_t *end)
{
    if (width < 1)
        width = 1;
    size_t base = 0, row = start;
    const char *p = value;
    do {
        text_buffer buffer;
        text_layout layout;
        const char *begin = p;
        mention_range first_mention;
        bool styled = b->resolver && (card || !strcmp(key, "content")) &&
                      mention_next(value, 0, &first_mention);
        if (!text_decode_chunk(&buffer, &p))
            return false;
        if (styled && *p) {
            mention_range range;
            size_t from = 0, cut = (size_t)(p - begin);
            while (mention_next(begin, from, &range)) {
                if (range.start >= cut)
                    break;
                if (range.end > cut) {
                    char *prefix = g_strndup(begin, range.start);
                    if (!prefix)
                        return false;
                    bool ok = text_decode(&buffer, prefix);
                    g_free(prefix);
                    if (!ok)
                        return false;
                    p = begin + range.start;
                    break;
                }
                from = range.end;
            }
        }
        if (styled)
            text_mentions(&buffer, b->resolver, b->resolver_context);
        if (b->icon_resolver && (card || !strcmp(key, "content")))
            text_icons(&buffer, b->icon_resolver, b->icon_painter, b->icon_context);
        text_buffer visible;
        text_project(&buffer, &visible);
        if (card)
            card_measure(&buffer, width, &layout);
        else
            text_measure(&buffer, width, &layout);
        for (int line = 0; line < layout.rows; ++line) {
            if (!reserve(b, row + 1))
                return false;
            message_row *r = &b->rows[row];
            if (r->count == MESSAGE_COLUMNS)
                return false;
            message_segment *s = &r->segments[r->count++];
            size_t first = visible.length, last = first, source_first = buffer.length;
            for (size_t k = 0; k < layout.count; ++k)
                if (layout.cells[k].row == line) {
                    if (first == visible.length) {
                        first = layout.cells[k].display_start;
                        source_first = layout.cells[k].start;
                    }
                    last = layout.cells[k].display_end;
                }
            text_buffer part = {0};
            part.length = last - first;
            memcpy(part.data, visible.data + first, part.length * sizeof(part.data[0]));
            /* Row strings never contain newlines; wrapping is already measured. */
            while (part.length && part.data[part.length - 1] == '\n')
                --part.length;
            char encoded[TEXT_UTF8_BYTES];
            if (!text_encode(&part, encoded) || !(s->text = strdup(encoded)))
                return false;
            snprintf(s->key, sizeof(s->key), "%s", key);
            s->offset = base + source_first;
            s->x = x;
            s->width = width;
            s->bold = bold;
            if (styled) {
                s->highlight = calloc(part.length + 1, 1);
                s->highlight_length = part.length;
                if (!s->highlight)
                    return false;
                for (size_t k = 0; k < layout.count; ++k)
                    if (layout.cells[k].row == line && layout.cells[k].mention)
                        for (size_t q = layout.cells[k].display_start;
                             q < layout.cells[k].display_end && q < first + part.length; ++q)
                            if (q >= first)
                                s->highlight[q - first] = 1;
            }
            for (size_t k = 0; k < layout.count; ++k)
                if (layout.cells[k].row == line && layout.cells[k].icon) {
                    message_icon_region *icons =
                        realloc(r->icons, (r->icon_count + 1) * sizeof(*icons));
                    if (!icons)
                        return false;
                    r->icons = icons;
                    message_icon_region *icon = &r->icons[r->icon_count++];
                    icon->id = layout.cells[k].icon;
                    icon->x = x + layout.cells[k].column;
                    icon->offset = base + layout.cells[k].start;
                    snprintf(icon->key, sizeof(icon->key), "%s", key);
                    r->icon_painter = b->icon_painter;
                    r->icon_context = b->icon_context;
                }
            r->card = card;
            r->color = color;
            ++row;
            if (row > b->count)
                b->count = row;
        }
        base += buffer.length;
    } while (*p);
    *end = row;
    return true;
}
static bool text(message_blocks *b, const char *key, const char *value, int width, bool bold,
                 bool card, unsigned color)
{
    size_t end;
    return column(b, b->count, key, value, card ? 2 : 0, width - (card ? 4 : 0), bold, card, color,
                  &end);
}
static bool labeled(message_blocks *b, const char *key, const char *prefix, const char *value,
                    int width, bool bold, bool card, unsigned color)
{
    size_t length = strlen(prefix) + strlen(value) + 1;
    char *joined = malloc(length);
    if (!joined)
        return false;
    snprintf(joined, length, "%s%s", prefix, value);
    bool ok = text(b, key, joined, width, bold, card, color);
    free(joined);
    return ok;
}
static void style(message_blocks *b, size_t first, bool muted, bool link)
{
    for (size_t i = first; i < b->count; ++i)
        for (int j = 0; j < b->rows[i].count; ++j) {
            b->rows[i].segments[j].muted = muted;
            b->rows[i].segments[j].link = link;
        }
}
static bool region(message_blocks *b, int index, size_t start, size_t end, size_t image_start,
                   int x, int width, int height)
{
    for (size_t i = start; i < end; ++i) {
        message_row *r = &b->rows[i];
        if (r->media_count == 2)
            return false;
        r->media[r->media_count++] =
            (message_media_region){index, (int)i - (int)image_start, height, x, width};
    }
    return true;
}
static int add_media(message_blocks *b, json_t *message, json_t *entry, const char *role,
                     const char *label, auth_media_kind kind, bool video)
{
    const char *id = str(entry, "id");
    if (strlen(id) != 36)
        return -1;
    size_t n = b->media_count;
    message_media *items = realloc(b->media, (n + 1) * sizeof(*items));
    if (!items)
        return -1;
    b->media = items;
    message_media *m = &items[n];
    memset(m, 0, sizeof(*m));
    snprintf(m->id, sizeof(m->id), "%s", id);
    snprintf(m->message, sizeof(m->message), "%s", str(message, "id"));
    snprintf(m->role, sizeof(m->role), "%s", role);
    snprintf(m->label, sizeof(m->label), "%s", label);
    json_t *properties = entry;
    if (kind == AUTH_MEDIA_EMBED) {
        properties = json_object_get(entry, role);
        if (!strcmp(role, "author"))
            properties = json_object_get(properties, "media");
    }
    snprintf(m->mime, sizeof(m->mime), "%s", str(properties, "mime_type"));
    snprintf(m->original_file_name, sizeof(m->original_file_name), "%s",
             str(properties, "original_file_name"));
    json_t *size = json_object_get(properties, "size_bytes");
    m->size_known = json_is_integer(size) && json_integer_value(size) >= 0;
    if (m->size_known)
        m->size_bytes = json_integer_value(size);
    json_int_t w = json_integer_value(json_object_get(properties, "width"));
    json_int_t h = json_integer_value(json_object_get(properties, "height"));
    m->source_width = w > 0 && w <= INT_MAX ? (int)w : 0;
    m->source_height = h > 0 && h <= INT_MAX ? (int)h : 0;
    snprintf(m->moderation, sizeof(m->moderation), "%s", str(entry, "moderation_status"));
    snprintf(m->created_at, sizeof(m->created_at), "%s", str(entry, "created_at"));
    snprintf(m->thumbnail_id, sizeof(m->thumbnail_id), "%s", str(entry, "thumbnail_id"));
    m->version = message_media_version(entry);
    m->position = (unsigned)n;
    m->kind = kind;
    m->video = video;
    ++b->media_count;
    return (int)n;
}
static bool media(message_blocks *b, json_t *message, json_t *entry, const char *key,
                  const char *role, const char *label, auth_media_kind kind, bool video, int width,
                  bool graphics, bool card, unsigned color)
{
    if (app_mode_get() == APP_MODE_ULTRALIGHT)
        return labeled(b, key, video ? "Vídeo: " : "Imagem: ", label, width, false, card, color);
    int n = add_media(b, message, entry, role, label, kind, video);
    if (n < 0)
        return strlen(str(entry, "id")) != 36;
    b->media[n].full_width = true;
    size_t start = b->count;
    /* A graphical preview is the image itself; F7 is documented in the UI. */
    if ((!graphics || video) && !labeled(b, key, video ? "[▶ Vídeo / F7] " : "[Imagem / F7] ",
                                         label, width, false, card, color))
        return false;
    size_t image_start = b->count;
    int height = graphics && !video ? 6 : 0;
    int media_width = width - (card ? 4 : 0);
    if (height && b->measure)
        b->measure(b->measure_context, &b->media[n], media_width, &media_width, &height);
    for (int i = 0; i < height; ++i)
        if (!blank(b, card, false, false, color))
            return false;
    if (height > 0 && !card) {
        b->rows[start].tile_width = media_width;
        b->rows[start].tile_rows = (int)(b->count - start);
    }
    return region(b, n, start, b->count, image_start, card ? 2 : 0, media_width, height);
}
/* Keep each tile's rows, strings and hit regions owned by the message. Position
 * them without merging rows or imposing a fixed number of images per line. */
static bool wrap_images(message_blocks *b, int width)
{
    size_t count = b->count;
    size_t tiles = 0;
    for (size_t i = 0; i < count; ++i)
        tiles += b->rows[i].tile_rows > 0;
    if (tiles > 1)
        for (size_t i = 0; i < count;) {
            int rows = b->rows[i].tile_rows;
            if (!rows) {
                ++i;
                continue;
            }
            if (b->rows[i].card) {
                /* Preserve each preview's text and padding, but don't reserve
                 * unused card width between neighboring images. */
                int used = 8;
                for (int j = 0; j < rows; ++j) {
                    message_row *r = &b->rows[i + j];
                    for (int k = 0; k < r->count; ++k) {
                        message_segment *s = &r->segments[k];
                        int end = s->x + text_label(NULL, 0, 0, s->width, s->text) + 2;
                        if (end > used)
                            used = end;
                    }
                    for (int k = 0; k < r->media_count; ++k) {
                        int end = r->media[k].x + r->media[k].width + 2;
                        if (end > used)
                            used = end;
                    }
                }
                if (used < b->rows[i].tile_width) {
                    b->rows[i].tile_width = used;
                    for (int j = 0; j < rows; ++j) {
                        message_row *r = &b->rows[i + j];
                        r->card_width = used;
                        for (int k = 0; k < r->count; ++k)
                            if (r->segments[k].width > used - r->segments[k].x - 2)
                                r->segments[k].width = used - r->segments[k].x - 2;
                    }
                }
            }
            i += (size_t)rows;
        }
    int y = 0, line_y = 0, right = 0, line_height = 0;
    bool line = false;
    for (size_t i = 0; i < count;) {
        int tile_width = b->rows[i].tile_width, tile_rows = b->rows[i].tile_rows;
        if (!tile_rows) {
            if (line) {
                y = line_y + line_height;
                line = false;
            }
            b->rows[i].y = y++;
            ++i;
            continue;
        }
        if (tile_width < 1 || tile_width > width || tile_rows < 1 || (size_t)tile_rows > count - i)
            return false;
        int x = 0;
        if (line) {
            int remaining = width - right;
            if (remaining < IMAGE_GAP_COLUMNS || tile_width > remaining - IMAGE_GAP_COLUMNS) {
                y = line_y + line_height;
                size_t gap = b->count;
                if (!blank(b, false, false, false, 0))
                    return false;
                b->rows[gap].y = y++;
                line = false;
            } else
                x = right + IMAGE_GAP_COLUMNS;
        }
        if (!line) {
            line_y = y;
            line_height = 0;
            line = true;
        }
        for (int j = 0; j < tile_rows; ++j) {
            b->rows[i + j].x = x;
            b->rows[i + j].y = line_y + j;
        }
        right = x + tile_width;
        if (tile_rows > line_height)
            line_height = tile_rows;
        i += (size_t)tile_rows;
    }
    b->height = line ? line_y + line_height : y;
    /* Preserve the final message separator as the last logical row. */
    if (b->count > count) {
        message_row separator = b->rows[count - 1];
        memmove(b->rows + count - 1, b->rows + count, (b->count - count) * sizeof(*b->rows));
        b->rows[b->count - 1] = separator;
    }
    return true;
}
bool message_blocks_build_mentions(message_blocks *b, json_t *m, const char *author,
                                   const char *reply, int width, bool graphics,
                                   mention_resolver resolver, void *context, const char *self)
{
    return message_blocks_build_sized_mentions(b, m, author, reply, width, graphics, NULL, NULL,
                                               resolver, context, self);
}
bool message_blocks_build_sized(message_blocks *b, json_t *m, const char *author, const char *reply,
                                int width, bool graphics, message_media_measure measure,
                                void *context)
{
    return message_blocks_build_sized_mentions(b, m, author, reply, width, graphics, measure,
                                               context, NULL, NULL, NULL);
}
bool message_blocks_build_sized_mentions(message_blocks *b, json_t *m, const char *author,
                                         const char *reply, int width, bool graphics,
                                         message_media_measure measure, void *measure_context,
                                         mention_resolver resolver, void *resolver_context,
                                         const char *self)
{
    return message_blocks_build_visual(b, m, author, reply, width, graphics, measure,
                                       measure_context, resolver, resolver_context, self, NULL,
                                       NULL, NULL, false);
}
bool message_blocks_build_visual(message_blocks *b, json_t *m, const char *author,
                                 const char *reply, int width, bool graphics,
                                 message_media_measure measure, void *measure_context,
                                 mention_resolver resolver, void *resolver_context,
                                 const char *self, text_icon_resolver icons,
                                 text_icon_painter painter, void *icon_context, bool continuation)
{
    memset(b, 0, sizeof(*b));
    b->measure = measure;
    b->measure_context = measure_context;
    b->resolver = resolver;
    b->resolver_context = resolver_context;
    b->icon_resolver = icons;
    b->icon_painter = painter;
    b->icon_context = icon_context;
    b->self_mention = mention_message_contains(m, self);
    if (width < 8)
        width = 8;
    graphics_terminal terminal = graphics_terminal_get();
    int author_x = graphics && terminal.cell_width > 0
                       ? (MESSAGE_AVATAR_PIXELS + terminal.cell_width - 1) / terminal.cell_width
                       : 6;
    if (app_mode_get() == APP_MODE_ULTRALIGHT)
        author_x = 0;
    if (author_x > width - 8)
        author_x = width - 8;
    char label[1600];
    if (!continuation) {
        char timestamp[192];
        message_time_format(str(m, "created_at"), time(NULL), timestamp, sizeof(timestamp));
        if (!blank(b, false, false, false, 0))
            goto error;
        bool app = history_message_webhook(m);
        int metadata = text_label(NULL, 0, 0, 200, timestamp) + (*str(m, "edited_at") ? 12 : 3) +
                       (app ? 6 : 0);
        int author_width = width - author_x - metadata;
        if (author_width < 4)
            author_width = 4;
        text_buffer name;
        if (!text_decode(&name, author))
            text_decode(&name, "usuário");
        text_layout name_layout;
        text_measure(&name, author_width + 1, &name_layout);
        size_t cut = name.length;
        for (size_t k = 0; k + 1 < name_layout.count; ++k)
            if (name_layout.cells[k].row) {
                cut = name_layout.cells[k].start;
                break;
            }
        if (cut < name.length && cut)
            name.data[cut - 1] = 0x2026;
        name.length = cut;
        name.data[cut] = 0;
        char author_text[TEXT_UTF8_BYTES];
        if (!text_encode(&name, author_text))
            goto error;
        snprintf(label, sizeof(label), "%.*s%s · %s%s", 1024, author_text, app ? " [APP]" : "",
                 timestamp, *str(m, "edited_at") ? " · editado" : "");
        size_t header_end;
        if (!column(b, 0, "header", label, author_x, width - author_x, true, false, 0, &header_end))
            goto error;
        text_buffer initials = {0}, decoded;
        if (text_decode(&decoded, author)) {
            for (size_t k = 0; k < decoded.length && initials.length < 2; ++k)
                if ((k == 0 || decoded.data[k - 1] == ' ') && decoded.data[k] > ' ')
                    initials.data[initials.length++] = decoded.data[k];
        }
        char initial_text[TEXT_UTF8_BYTES];
        if (!initials.length) {
            initials.data[0] = '?';
            initials.length = 1;
        }
        if (!text_encode(&initials, initial_text))
            goto error;
        if (author_x > 1 && !column(b, 0, "header:avatar", initial_text, 1, author_x - 1, false,
                                    false, 0, &header_end))
            goto error;
        for (size_t i = 0; i < b->count; ++i)
            b->rows[i].header = true;
    }
    size_t content_start = b->count;
    width -= author_x;
    if (*str(m, "reply_to") && !labeled(b, "reply", "Resposta: ", reply, width, false, false, 0))
        goto error;
    if (*str(m, "content") && !text(b, "content", str(m, "content"), width, false, false, 0))
        goto error;
    size_t index;
    json_t *item;
    json_array_foreach(json_object_get(m, "attachments"), index, item)
    {
        char key[128];
        snprintf(key, sizeof(key), "attachment:%s:%zu", str(item, "id"), index);
        const char *name =
            *str(item, "original_file_name") ? str(item, "original_file_name") : "arquivo sem nome";
        if (app_mode_get() == APP_MODE_ULTRALIGHT) {
            const char *mime = str(item, "mime_type");
            const char *kind = !strcmp(mime, "image/gif")    ? "GIF"
                               : !strncmp(mime, "image/", 6) ? "Imagem"
                               : !strncmp(mime, "video/", 6) ? "Vídeo"
                                                             : "Anexo";
            json_t *size = json_object_get(item, "size_bytes");
            size_t length = strlen(name);
            if (length > SIZE_MAX - 128)
                goto error;
            char *description = malloc(length + 128);
            if (!description)
                goto error;
            if (json_is_integer(size) && json_integer_value(size) >= 0)
                snprintf(description, length + 128, "%s: %s · %lld bytes", kind, name,
                         (long long)json_integer_value(size));
            else
                snprintf(description, length + 128, "%s: %s", kind, name);
            bool ok = text(b, key, description, width, false, false, 0);
            free(description);
            if (!ok)
                goto error;
            continue;
        }
        bool video = !strncmp(str(item, "mime_type"), "video/", 6);
        bool image = !strncmp(str(item, "mime_type"), "image/", 6);
        if (!graphics || !image) {
            if (!labeled(b, key, "Anexo: ", name, width, true, false, 0))
                goto error;
            snprintf(label, sizeof(label), "Tipo: %s · %lld bytes · Moderação: %s",
                     str(item, "mime_type"),
                     (long long)json_integer_value(json_object_get(item, "size_bytes")),
                     str(item, "moderation_status"));
            if (!text(b, key, label, width, false, false, 0))
                goto error;
        }
        if ((image || video) && !media(b, m, item, key, "attachment", name, AUTH_MEDIA_ATTACHMENT,
                                       video, width, graphics, false, 0))
            goto error;
    }
    json_array_foreach(json_object_get(m, "embeds"), index, item)
    {
        if (index >= 10)
            break;
        int card_width = width > 76 ? 76 : width;
        size_t card_start = b->count;
        char root[96], key[128];
        if (*str(item, "id"))
            snprintf(root, sizeof(root), "embed:%s", str(item, "id"));
        else
            snprintf(root, sizeof(root), "embed:index:%zu", index);
        unsigned color = 0;
        const char *hex = str(item, "color");
        if (strlen(hex) == 7 && hex[0] == '#') {
            char *end;
            unsigned long value = strtoul(hex + 1, &end, 16);
            if (!*end && value <= 0xffffff)
                color = (unsigned)value | 0x1000000U;
        }
        if (!blank(b, true, true, false, color))
            goto error;
        json_t *a = json_object_get(item, "author");
        bool right_thumbnail = graphics && card_width >= 44 &&
                               json_is_object(json_object_get(item, "thumbnail")) &&
                               strlen(str(item, "id")) == 36 &&
                               (!strcmp(str(item, "source_type"), "custom") ||
                                json_is_object(json_object_get(item, "image")));
        size_t thumbnail_start = b->count;
        int text_width = right_thumbnail ? card_width - 14 : card_width;
        snprintf(key, sizeof(key), "%s:author", root);
        size_t author_start = b->count, author_end;
        bool avatar = graphics && json_is_object(json_object_get(a, "media")) &&
                      strlen(str(item, "id")) == 36;
        if (*str(a, "name") &&
            !column(b, author_start, key, str(a, "name"), avatar ? 7 : 2,
                    text_width - (avatar ? 9 : 4), false, true, color, &author_end))
            goto error;
        style(b, author_start, true, false);
        if (avatar) {
            while (b->count < author_start + 2)
                if (!blank(b, true, false, false, color))
                    goto error;
            int n = add_media(b, m, item, "author", "Avatar do autor", AUTH_MEDIA_EMBED, false);
            if (n < 0)
                goto error;
            if (!region(b, n, author_start, author_start + 2, author_start, 2, 4, 2))
                goto error;
        }
        snprintf(key, sizeof(key), "%s:site", root);
        const char *site = *str(item, "site_name") ? str(item, "site_name") : str(item, "provider");
        size_t site_start = b->count;
        if (*site && strcmp(site, str(a, "name")) &&
            !text(b, key, site, text_width, false, true, color))
            goto error;
        style(b, site_start, true, false);
        snprintf(key, sizeof(key), "%s:title", root);
        size_t title_start = b->count;
        if (*str(item, "title") && !text(b, key, str(item, "title"), text_width, true, true, color))
            goto error;
        style(b, title_start, false, *str(item, "url") != '\0');
        snprintf(key, sizeof(key), "%s:description", root);
        if (*str(item, "description")) {
            if (!blank(b, true, false, false, color) ||
                !text(b, key, str(item, "description"), text_width, false, true, color))
                goto error;
        }
        if (!*str(item, "title") && *str(item, "url")) {
            size_t url_start = b->count;
            if (!text(b, root, str(item, "url"), text_width, false, true, color))
                goto error;
            style(b, url_start, false, true);
        }
        if (right_thumbnail) {
            while (b->count < thumbnail_start + 4)
                if (!blank(b, true, false, false, color))
                    goto error;
            int thumb = add_media(b, m, item, "thumbnail", "Miniatura", AUTH_MEDIA_EMBED, false);
            if (thumb < 0)
                goto error;
            if (!region(b, thumb, thumbnail_start, thumbnail_start + 4, thumbnail_start,
                        card_width - 14, 12, 4))
                goto error;
        }
        json_t *fields = json_object_get(item, "fields");
        size_t count = json_array_size(fields);
        if (count > 25)
            count = 25;
        for (size_t f = 0; f < count;) {
            size_t group = 1;
            int columns = (card_width - 4) / 20;
            if (columns > 3)
                columns = 3;
            if (columns < 1)
                columns = 1;
            if (json_is_true(json_object_get(json_array_get(fields, f), "inline")))
                while (group < (size_t)columns && f + group < count &&
                       json_is_true(json_object_get(json_array_get(fields, f + group), "inline")))
                    ++group;
            if (!blank(b, true, false, false, color))
                goto error;
            size_t start = b->count, end = start;
            int colwidth = (card_width - 4 - (int)group + 1) / (int)group;
            for (size_t c = 0; c < group; ++c) {
                json_t *field = json_array_get(fields, f + c);
                size_t row;
                snprintf(key, sizeof(key), "%s:field:%zu:name", root, f + c);
                if (!column(b, start, key, str(field, "name"), 2 + (int)c * (colwidth + 1),
                            colwidth, true, true, color, &row))
                    goto error;
                snprintf(key, sizeof(key), "%s:field:%zu:value", root, f + c);
                if (!column(b, row, key, str(field, "value"), 2 + (int)c * (colwidth + 1), colwidth,
                            false, true, color, &row))
                    goto error;
                if (row > end)
                    end = row;
            }
            b->count = end;
            f += group;
        }
        snprintf(key, sizeof(key), "%s:thumbnail", root);
        if (!right_thumbnail && json_is_object(json_object_get(item, "thumbnail"))) {
            if (!blank(b, true, false, false, color) ||
                !media(b, m, item, key, "thumbnail", "Miniatura", AUTH_MEDIA_EMBED, false,
                       card_width, graphics, true, color))
                goto error;
        }
        if (json_is_object(json_object_get(item, "image"))) {
            if (!blank(b, true, false, false, color))
                goto error;
            size_t unavailable_start = b->count;
            if (!text(b, root, "Imagem adicional indisponível no backend.", card_width, false, true,
                      color))
                goto error;
            style(b, unavailable_start, true, false);
        }
        json_t *v = json_object_get(item, "video");
        if (json_is_object(v)) {
            snprintf(key, sizeof(key), "%s:video", root);
            bool direct = !strcmp(str(v, "mime_type"), "video/mp4") ||
                          !strcmp(str(v, "mime_type"), "video/webm") ||
                          !strcmp(str(v, "mime_type"), "video/ogg");
            if (direct) {
                if (!media(b, m, item, key, "video", "Abrir player", AUTH_MEDIA_VIDEO, true,
                           card_width, graphics, true, color))
                    goto error;
            } else if (!labeled(b, key, "Vídeo externo: ", str(v, "url"), card_width, false, true,
                                color))
                goto error;
        }
        json_t *footer = json_object_get(item, "footer");
        snprintf(key, sizeof(key), "%s:footer", root);
        if (*str(footer, "text")) {
            if (!blank(b, true, false, false, color))
                goto error;
            size_t footer_start = b->count;
            if (!text(b, key, str(footer, "text"), card_width, false, true, color))
                goto error;
            style(b, footer_start, true, false);
        }
        if (!blank(b, true, false, true, color))
            goto error;
        for (size_t r = card_start; r < b->count; ++r)
            b->rows[r].card_width = card_width;
        if (graphics && json_is_object(json_object_get(item, "thumbnail")) &&
            strlen(str(item, "id")) == 36) {
            b->rows[card_start].tile_width = card_width;
            b->rows[card_start].tile_rows = (int)(b->count - card_start);
        }
    }
    if (continuation && *str(m, "edited_at")) {
        size_t marker = b->count;
        if (!text(b, "edited", "editado", width, false, false, 0))
            goto error;
        style(b, marker, true, false);
    }
    if (!blank(b, false, false, false, 0))
        goto error;
    for (size_t i = content_start; i < b->count; ++i)
        b->rows[i].indent = author_x;
    if (!wrap_images(b, width))
        goto error;
    return true;
error:
    message_blocks_free(b);
    return false;
}
bool message_blocks_build(message_blocks *b, json_t *m, const char *a, const char *r, int w, bool g)
{
    return message_blocks_build_mentions(b, m, a, r, w, g, NULL, NULL, NULL);
}
void message_blocks_free(message_blocks *b)
{
    /* capacity includes partially constructed rows on an allocation failure. */
    for (size_t i = 0; i < b->capacity; ++i)
        for (int k = 0; k < b->rows[i].count; ++k) {
            free(b->rows[i].segments[k].text);
            free(b->rows[i].segments[k].highlight);
        }
    for (size_t i = 0; i < b->capacity; ++i)
        free(b->rows[i].icons);
    free(b->rows);
    free(b->media);
    memset(b, 0, sizeof(*b));
}
static struct {
    int fg, bg;
} frame_colors[32];
static int frame_color_count;
void message_colors_begin(void)
{
    frame_color_count = 0;
}
static int color_pair(unsigned rgb, int fallback)
{
    if (!(rgb & 0x1000000) || !has_colors() || COLORS < 8 || COLOR_PAIRS < 64)
        return fallback;
    int r = (int)(rgb >> 16) & 255, g = (int)(rgb >> 8) & 255, b = (int)rgb & 255;
    int fg;
    if (COLORS >= 0x1000000)
        fg = (int)(rgb & 0xffffff);
    else if (COLORS >= 256) {
        const int level[] = {0, 95, 135, 175, 215, 255};
        int best = 0x7fffffff;
        fg = 16;
        for (int red = 0; red < 6; ++red)
            for (int green = 0; green < 6; ++green)
                for (int blue = 0; blue < 6; ++blue) {
                    int dr = r - level[red], dg = g - level[green], db = b - level[blue];
                    int distance = dr * dr + dg * dg + db * db;
                    if (distance < best) {
                        best = distance;
                        fg = 16 + 36 * red + 6 * green + blue;
                    }
                }
    } else
        fg = (r > 127 ? COLOR_RED : 0) | (g > 127 ? COLOR_GREEN : 0) | (b > 127 ? COLOR_BLUE : 0);
    int oldfg, background;
    if (extended_pair_content(fallback, &oldfg, &background) == ERR)
        return fallback;
    for (int i = 0; i < frame_color_count; ++i)
        if (frame_colors[i].fg == fg && frame_colors[i].bg == background)
            return THEME_PAIR_END + i;
    if (frame_color_count == 32 || THEME_PAIR_END + frame_color_count >= COLOR_PAIRS)
        return fallback;
    int pair = THEME_PAIR_END + frame_color_count;
    if (init_extended_pair(pair, fg, background) == ERR)
        return fallback;
    frame_colors[frame_color_count].fg = fg;
    frame_colors[frame_color_count++].bg = background;
    return pair;
}
void message_row_draw(WINDOW *w, const message_row *row, int y, int x, int width, int pair,
                      int accent)
{
    x += row->indent;
    width -= row->indent;
    if (width < 1)
        return;
    wattrset(w, COLOR_PAIR(pair));
    if (row->card) {
        if (row->card_width > 0 && row->card_width < width)
            width = row->card_width;
        pair = THEME_EMBED_AREA;
        accent = color_pair(row->color, THEME_EMBED_ACCENT);
        wattrset(w, COLOR_PAIR(pair));
        mvwhline(w, y, x, ' ', width);
        wattrset(w, COLOR_PAIR(accent));
        mvwaddstr(w, y, x, "▎");
    }
    for (int i = 0; i < row->count; ++i) {
        const message_segment *s = &row->segments[i];
        int text_pair = pair;
        if (row->card)
            text_pair = s->muted ? THEME_EMBED_MUTED : s->link ? accent : pair;
        wattrset(w, COLOR_PAIR(text_pair) | (s->bold ? A_BOLD : 0));
        if (s->highlight) {
            text_buffer buffer;
            text_layout layout;
            if (text_decode(&buffer, s->text)) {
                text_measure(&buffer, s->width, &layout);
                text_draw_styled(w, &buffer, &layout, s->highlight, s->highlight_length, 0, 1, y,
                                 x + s->x, text_pair, row->card ? THEME_EMBED_ACCENT : accent);
            }
        } else
            text_label(w, y, x + s->x, s->width, s->text);
    }
}
