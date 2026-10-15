#define _POSIX_C_SOURCE 200809L
#include "ui/message_blocks.h"
#include "ui/sixel.h"
#include "ui/graphics.h"
#include "ui/media_layout.h"
#include "ui/text.h"
#include <assert.h>
#include <locale.h>
#include <sixel.h>
#include <libavutil/base64.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void cards(void)
{
    json_error_t error;
    json_t *m = json_loads("{\"id\":\"00000000-0000-4000-8000-000000000001\",\"content\":null,\"embeds\":[{\"id\":\"00000000-0000-4000-8000-000000000002\",\"title\":\"Title\\u001b[31m\",\"description\":\"A long description with Unicode café and 界\",\"color\":\"#abcdef\",\"fields\":[{\"name\":\"First\",\"value\":\"Value one\",\"inline\":true},{\"name\":\"Second\",\"value\":\"Value two\",\"inline\":true},{\"name\":\"Third\",\"value\":\"Value three\",\"inline\":true}],\"footer\":{\"text\":\"Footer\"},\"image\":{\"mime_type\":\"image/png\"}}]}", 0, &error);
    assert(m);
    message_blocks wide, narrow;
    assert(message_blocks_build(&wide, m, "Author", "Reply", 80, false));
    assert(message_blocks_build(&narrow, m, "Author", "Reply", 24, false));
    bool columns = false, unavailable = false, title = false, footer = false;
    for (size_t i = 0; i < wide.count; ++i) {
        columns |= wide.rows[i].count == 3;
        for (int j = 0; j < wide.rows[i].count; ++j) {
            message_segment *s = &wide.rows[i].segments[j];
            assert(!strchr(s->text, '\033'));
            assert(!strstr(s->text, "Sem conteúdo textual"));
            assert(!strstr(s->text, "Rodapé:"));
            unavailable |= strstr(s->text, "indisponível no backend") != NULL;
            title |= s->bold && strstr(s->text, "Title") != NULL;
            footer |= s->muted && !s->bold && !strcmp(s->text, "Footer");
        }
    }
    assert(columns && title && footer && unavailable);
    for (size_t i = 0; i < narrow.count; ++i) if (!narrow.rows[i].header) assert(narrow.rows[i].count <= 1);
    assert(narrow.count > wide.count);
    message_blocks_free(&wide); message_blocks_free(&narrow); json_decref(m);
}
static void embed_layout(void)
{
    json_error_t error;
    json_t *m = json_loads("{\"id\":\"00000000-0000-4000-8000-000000000001\",\"content\":\"\",\"embeds\":[{\"id\":\"00000000-0000-4000-8000-000000000002\",\"source_type\":\"custom\",\"color\":\"#00b0f4\",\"author\":{\"name\":\"Info\",\"media\":{\"mime_type\":\"image/png\"}},\"thumbnail\":{\"mime_type\":\"image/png\"},\"fields\":[{\"name\":\"Full field\",\"value\":\"Full value\"},{\"name\":\"First\",\"value\":\"One\",\"inline\":true},{\"name\":\"Second\",\"value\":\"Two\",\"inline\":true},{\"name\":\"Third\",\"value\":\"Three\",\"inline\":true},{\"name\":\"Fourth\",\"value\":\"Four\",\"inline\":true}],\"footer\":{\"text\":\"Example Footer\"}}]}", 0, &error);
    assert(m);
    message_blocks b;
    assert(message_blocks_build(&b, m, "User", "", 120, true));
    assert(b.media_count == 2);
    assert(!b.media[0].full_width && !b.media[1].full_width);
    bool simultaneous = false, author = false;
    size_t group_row = 0, fourth_row = 0;
    for (size_t r = 0; r < b.count; ++r) {
        message_row *row = &b.rows[r];
        if (row->card) assert(row->card_width == 76 && row->color == 0x100b0f4U);
        simultaneous |= row->media_count == 2;
        for (int k = 0; k < row->media_count; ++k) {
            message_media_region *region = &row->media[k];
            assert(region->x >= 2 && region->x + region->width <= row->card_width - 2);
            for (int s = 0; s < row->count; ++s) {
                message_segment *segment = &row->segments[s];
                assert(segment->x + segment->width <= region->x || segment->x >= region->x + region->width);
            }
        }
        for (int k = 0; k < row->count; ++k) {
            message_segment *s = &row->segments[k];
            assert(strcmp(s->text, "Embed") && !strstr(s->text, "Sem conteúdo textual"));
            author |= !strcmp(s->text, "Info") && s->muted && !s->bold && s->x == 7;
            if (!strcmp(s->text, "One")) { group_row = r; assert(row->count == 3); }
            if (!strcmp(s->text, "Four")) { fourth_row = r; assert(row->count == 1); }
        }
    }
    assert(simultaneous && author && group_row && fourth_row > group_row);
    message_blocks_free(&b);
    assert(message_blocks_build(&b, m, "User", "", 24, false));
    for (size_t r = 0; r < b.count; ++r) if (!b.rows[r].header) assert(b.rows[r].count <= 1);
    message_blocks_free(&b); json_decref(m);
}
static void protocol(void)
{
    sixel_terminal t = {0};
    assert(sixel_terminal_reply(&t, "\033[?62;4;22c") && t.supported);
    assert(sixel_terminal_reply(&t, "\033[6;16;8t") && t.cell_width == 8 && t.cell_height == 16);
    assert(!sixel_terminal_reply(&t, "\033[6;-1;8t"));
    assert(!sixel_terminal_reply(&t, "\033[?62;4cINJECT"));
    assert(sixel_terminal_reply(&t, "\033[?4;22c") && !t.supported);
    unsigned char pixels[12 * 12 * 3];
    for (size_t i = 0; i < sizeof(pixels); i += 3) { pixels[i] = 255; pixels[i + 1] = pixels[i + 2] = 0; }
    sixel_image image;
    assert(sixel_image_encode(pixels, 12, 12, &image));
    assert(image.length > 10 && !memcmp(image.data, "\033P", 2));
    unsigned char *decoded = NULL, *palette = NULL; int w = 0, h = 0, colors = 0;
    assert(SIXEL_SUCCEEDED(sixel_decode_raw((unsigned char *)image.data, (int)image.length, &decoded, &w, &h, &palette, &colors, NULL)));
    assert(w == 12 && h == 12 && colors > 0);
    unsigned color = decoded[0];
    assert(color < (unsigned)colors && palette[color * 3] > 240 && palette[color * 3 + 1] < 16 && palette[color * 3 + 2] < 16);
    free(decoded); free(palette); sixel_image_free(&image);
    assert(!sixel_image_encode(pixels, 100000, 100000, &image));
}
static void graphics_protocols(void)
{
    graphics_terminal t = {0};
    char reply[80];
    snprintf(reply, sizeof(reply), "\033_Gi=%u;OK\033\\", GRAPHICS_PROBE_ID);
    assert(graphics_terminal_reply(&t, reply) && t.protocol == GRAPHICS_KITTY);
    assert(graphics_terminal_reply(&t, "\033[?62;4;22c") && t.protocol == GRAPHICS_KITTY);
    assert(graphics_terminal_reply(&t, "\033[6;6;4t") && t.cell_width == 4 && t.cell_height == 6);
    assert(!graphics_terminal_reply(&t, "\033_Gi=1;OK\033\\"));
    assert(!graphics_terminal_reply(&t, "\033[6;129;4t"));
    assert(graphics_terminal_reply(&t, "\033[?2026;2$y") && t.synchronized);
    assert(graphics_terminal_reply(&t, "\033[?2026;0$y") && !t.synchronized);
    unsigned char pixels[12 * 12 * 3];
    for (size_t i = 0; i < sizeof(pixels); i += 3) { pixels[i] = 255; pixels[i + 1] = pixels[i + 2] = 0; }
    graphics_image image;
    assert(graphics_image_encode(pixels, 12, 12, t, &image));
    assert(image.protocol == GRAPHICS_KITTY && image.id && image.columns == 3 && image.rows == 2);
    char *encoded = calloc(image.length + 1, 1);
    assert(encoded); memcpy(encoded, image.data, image.length);
    assert(strstr(encoded, "a=t,f=32,s=12,v=12,c=3,r=2,i="));
    assert(strstr(encoded, "q=2,C=1"));
    char payload[2048] = "";
    size_t used = 0;
    for (char *p = encoded; (p = strstr(p, "\033_Gm=1;")); ) {
        p += 7;
        char *end = strstr(p, "\033\\"); assert(end);
        size_t length = (size_t)(end - p); assert(length < sizeof(payload) - used);
        memcpy(payload + used, p, length); used += length; p = end + 2;
    }
    unsigned char rgba[12 * 12 * 4 + 1];
    assert(av_base64_decode(rgba, payload, sizeof(rgba)) == 12 * 12 * 4);
    for (size_t i = 0; i < 12 * 12 * 4; i += 4)
        assert(rgba[i] > 240 && rgba[i + 1] < 16 && rgba[i + 2] < 16 && rgba[i + 3] == 255);
    free(encoded); graphics_image_free(&image);
    t.protocol = GRAPHICS_ITERM2;
    assert(graphics_image_encode(pixels, 12, 12, t, &image));
    assert(image.protocol == GRAPHICS_ITERM2 && !memcmp(image.data, "\033]1337;File=inline=1;", 21));
    assert(image.data[image.length - 1] == '\a'); graphics_image_free(&image);
    assert(!graphics_image_encode(pixels, 100000, 100000, t, &image));
}
static void preview_crops(void)
{
    unsigned char pixels[12 * 36 * 3];
    memset(pixels, 127, sizeof(pixels));
    graphics_terminal t = {.protocol = GRAPHICS_SIXEL, .cell_width = 4, .cell_height = 6};
    for (int mode = 0; mode < 2; ++mode) {
        graphics_image image;
        assert(graphics_image_encode_preview(pixels, 12, 36, t, 123, &image));
        assert(image.crop_count == 20 && graphics_image_bytes(&image) > image.length);
        const graphics_image *crop = graphics_image_crop(&image, 6, 12);
        assert(crop && crop->height == 18 && crop->rows == 3 && crop->id == image.id);
        assert(crop->revision != image.revision && crop->data);
        assert(!graphics_image_crop(&image, 36, 0));
        graphics_image_free(&image);
        assert(!image.data && !image.crops);
        /* A partially filled last cell must disappear when clipping that row. */
        t.cell_height = 16;
        assert(graphics_image_encode_preview(pixels, 12, 36, t, 123, &image));
        crop = graphics_image_crop(&image, 0, 4);
        assert(crop && crop->height == 32 && crop->rows == 2);
        crop = graphics_image_crop(&image, 16, 4);
        assert(crop && crop->height == 16 && crop->rows == 1);
        graphics_image_free(&image);
        t.cell_height = 6;
        t.protocol = GRAPHICS_ITERM2;
    }
}
static void measured_media(void *context, const message_media *item, int available_width, int *width, int *height)
{
    (void)context;
    assert(item->kind == AUTH_MEDIA_ATTACHMENT && available_width > 8);
    *width = 8; *height = 10;
}
static void modal_geometry(void)
{
    media_modal_layout g;
    int widths[] = {44, 79, 80, 100, 160}, heights[] = {14, 24, 30, 50};
    for (size_t c = 0; c < sizeof(widths) / sizeof(*widths); ++c)
        for (size_t r = 0; r < sizeof(heights) / sizeof(*heights); ++r)
            for (int info = 0; info < 2; ++info) {
                assert(media_modal_geometry(widths[c], heights[r], info != 0, 2, 1, &g));
                assert(g.x >= 4 && g.y >= 2 && g.x + g.width <= widths[c] - 4);
                if (widths[c] >= 80) assert(g.x >= 30);
                assert(g.image_x >= g.area_x && g.image_x + 2 <= g.area_x + g.area_width);
                assert(g.image_y >= g.area_y && g.image_y + 1 <= g.area_y + g.area_height);
                int before = g.image_x - g.area_x, after = g.area_x + g.area_width - (g.image_x + 2);
                assert(before == after || before + 1 == after);
                int above = g.image_y - g.area_y, below = g.area_y + g.area_height - (g.image_y + 1);
                assert(above == below || above + 1 == below);
                assert(media_modal_contains(&g, g.y, g.x));
                assert(!media_modal_contains(&g, g.y - 1, g.x));
                assert(!media_modal_contains(&g, g.y, g.x + g.width));
                if (info) assert(g.info_x > g.button_x + g.button_width - 1);
                media_modal_layout toggled;
                assert(media_modal_geometry(widths[c], heights[r], !info, 2, 1, &toggled));
                assert(g.button_x == toggled.button_x && g.button_y == toggled.button_y &&
                       g.button_width == toggled.button_width);
            }
    assert(!media_modal_geometry(29, 24, false, 1, 1, &g));
    assert(!media_modal_geometry(80, 11, false, 1, 1, &g));
}
static void attachment_layout(void)
{
    json_error_t error;
    json_t *m = json_loads("{\"id\":\"00000000-0000-4000-8000-000000000001\",\"attachments\":[{\"id\":\"00000000-0000-4000-8000-000000000002\",\"thumbnail_id\":\"00000000-0000-4000-8000-000000000003\",\"mime_type\":\"image/gif\",\"original_file_name\":\"example.gif\",\"size_bytes\":43562,\"moderation_status\":\"clean\"}]}", 0, &error);
    assert(m);
    message_blocks b;
    assert(message_blocks_build_sized(&b, m, "Sender", "", 80, true, measured_media, NULL));
    assert(b.media_count == 1 && b.media[0].kind == AUTH_MEDIA_ATTACHMENT);
    assert(b.media[0].size_known && b.media[0].size_bytes == 43562);
    assert(!strcmp(b.media[0].original_file_name, "example.gif"));
    assert(!strcmp(b.media[0].moderation, "clean"));
    assert(!strcmp(b.media[0].thumbnail_id, "00000000-0000-4000-8000-000000000003"));
    int media_rows = 0;
    for (size_t r = 0; r < b.count; ++r) {
        for (int c = 0; c < b.rows[r].count; ++c) {
            const char *text = b.rows[r].segments[c].text;
            assert(!strstr(text, "Anexo:") && !strstr(text, "Tipo:") && !strstr(text, "Moderação:") && !strstr(text, "[Imagem / F7]"));
        }
        for (int k = 0; k < b.rows[r].media_count; ++k) {
            assert(b.rows[r].media[k].width == 8 && b.rows[r].media[k].height == 10);
            ++media_rows;
        }
    }
    assert(media_rows == 10);
    message_blocks_free(&b);
    assert(message_blocks_build(&b, m, "Sender", "", 80, false));
    bool metadata = false;
    for (size_t r = 0; r < b.count; ++r)
        for (int c = 0; c < b.rows[r].count; ++c)
            metadata |= strstr(b.rows[r].segments[c].text, "Anexo: example.gif") != NULL;
    assert(metadata);
    message_blocks_free(&b); json_decref(m);
}
static void gallery_measure(void *context, const message_media *item, int available_width, int *width, int *height)
{
    const int (*sizes)[2] = context;
    assert(item->position < 4);
    int w = sizes[item->position][0], h = sizes[item->position][1];
    *width = w < available_width ? w : available_width;
    *height = h * *width / w;
    if (*height < 1) *height = 1;
}
static void gallery_regions(const message_blocks *b, int positions[4][4])
{
    int found = 0;
    for (size_t r = 0; r < b->count; ++r) {
        const message_row *row = &b->rows[r];
        assert(row->y >= 0 && row->y < b->height);
        for (int k = 0; k < row->media_count; ++k) {
            const message_media_region *region = &row->media[k];
            if (region->row) continue;
            assert(region->index >= 0 && region->index < 4);
            int *p = positions[region->index];
            p[0] = row->indent + row->x + region->x; p[1] = row->y; p[2] = region->width; p[3] = region->height;
            ++found;
        }
    }
    assert(found == 4);
    assert(b->rows[b->count - 1].y == b->height - 1);
}
static void image_gallery(void)
{
    json_t *m = json_pack("{s:s,s:[]}", "id", "00000000-0000-4000-8000-000000000001", "attachments");
    assert(m);
    for (int i = 0; i < 4; ++i) {
        char id[37]; snprintf(id, sizeof(id), "00000000-0000-4000-8000-%012d", i + 10);
        assert(!json_array_append_new(json_object_get(m, "attachments"),
            json_pack("{s:s,s:s}", "id", id, "mime_type", "image/png")));
    }
    const int sizes[4][2] = {{8, 4}, {12, 2}, {10, 6}, {20, 3}};
    const int widths[] = {34, 33, 12}, heights[] = {12, 13, 18};
    const int expected[3][4][2] = {{{0, 1}, {10, 1}, {24, 1}, {0, 8}},
        {{0, 1}, {10, 1}, {0, 6}, {12, 6}}, {{0, 1}, {0, 6}, {0, 9}, {0, 16}}};
    for (size_t w = 0; w < sizeof(widths) / sizeof(*widths); ++w) {
        message_blocks b; int positions[4][4] = {{0}};
        assert(message_blocks_build_sized(&b, m, "Sender", "", widths[w] + 6, true, gallery_measure, (void *)sizes));
        int headers = 0;
        for (size_t i = 0; i < b.count; ++i) headers += b.rows[i].header;
        assert(b.height == heights[w] - 1 + headers); gallery_regions(&b, positions);
        for (int i = 0; i < 4; ++i) {
            assert(positions[i][0] == expected[w][i][0] + 6 && positions[i][1] == expected[w][i][1] - 1 + headers);
            assert(positions[i][0] + positions[i][2] <= widths[w] + 6);
        }
        message_blocks_free(&b);
    }
    message_blocks b;
    assert(message_blocks_build(&b, m, "Sender", "", 34, false));
    assert(b.height == (int)b.count);
    for (size_t r = 0; r < b.count; ++r) assert(b.rows[r].x == 0 && b.rows[r].y == (int)r);
    message_blocks_free(&b); json_decref(m);
}
static void embed_gallery(void)
{
    json_t *m = json_pack("{s:s,s:[]}", "id", "00000000-0000-4000-8000-000000000001", "embeds");
    assert(m);
    for (int i = 0; i < 4; ++i) {
        char id[37]; snprintf(id, sizeof(id), "00000000-0000-4000-8000-%012d", i + 10);
        assert(!json_array_append_new(json_object_get(m, "embeds"), json_pack("{s:s,s:s,s:s,s:{s:s}}",
            "id", id, "source_type", "link", "title", "Prévia 界", "thumbnail", "mime_type", "image/png")));
    }
    const int sizes[4][2] = {{8, 4}, {8, 4}, {8, 4}, {8, 4}};
    message_blocks b; int positions[4][4] = {{0}};
    assert(message_blocks_build_sized(&b, m, "Sender", "", 49, true, gallery_measure, (void *)sizes));
    gallery_regions(&b, positions);
    assert(positions[0][1] == positions[1][1] && positions[1][1] == positions[2][1]);
    assert(positions[1][0] - positions[0][0] == 15 && positions[2][0] - positions[1][0] == 15);
    assert(positions[3][0] == positions[0][0] && positions[3][1] == positions[0][1] + 9);
    int titles = 0;
    for (size_t r = 0; r < b.count; ++r)
        for (int s = 0; s < b.rows[r].count; ++s) {
            const message_segment *segment = &b.rows[r].segments[s];
            if (!strcmp(segment->text, "Prévia 界")) {
                assert(segment->bold && b.rows[r].x + segment->x < 43); ++titles;
            }
        }
    assert(titles == 4 && b.height < (int)b.count);
    message_blocks_free(&b); json_decref(m);
}
static void embed_author_unique(void)
{
    const char *fixtures[] = {
        "{\"embeds\":[{\"author\":{\"name\":\"Author\"},\"site_name\":\"Author\"}]}",
        "{\"embeds\":[{\"author\":{\"name\":\"Author\"},\"provider\":\"Author\"}]}",
        "{\"embeds\":[{\"author\":{\"name\":\"Author\"},\"site_name\":\"Publisher\"}]}"
    };
    for (size_t f = 0; f < sizeof(fixtures) / sizeof(*fixtures); ++f) {
        json_error_t error;
        json_t *m = json_loads(fixtures[f], 0, &error);
        assert(m);
        for (int graphics = 0; graphics < 2; ++graphics) {
            message_blocks blocks;
            assert(message_blocks_build(&blocks, m, "Sender", "", 80, graphics != 0));
            int authors = 0, publishers = 0;
            for (size_t r = 0; r < blocks.count; ++r)
                for (int c = 0; c < blocks.rows[r].count; ++c) {
                    authors += !strcmp(blocks.rows[r].segments[c].text, "Author");
                    publishers += !strcmp(blocks.rows[r].segments[c].text, "Publisher");
                }
            assert(authors == 1 && publishers == (f == 2));
            message_blocks_free(&blocks);
        }
        json_decref(m);
    }
}
int main(void)
{
    assert(setlocale(LC_ALL, "C.UTF-8")); init_graphemebreak(); cards(); embed_layout(); protocol(); graphics_protocols();
    preview_crops(); embed_author_unique(); attachment_layout(); image_gallery(); embed_gallery(); modal_geometry();
    puts("cards, terminal negotiation and Sixel/Kitty/iTerm2 pixels verified"); return 0;
}
