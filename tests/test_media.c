#define _POSIX_C_SOURCE 200809L
#include "ui/message_blocks.h"
#include "ui/sixel.h"
#include "ui/graphics.h"
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
            unavailable |= strstr(s->text, "indisponível no backend") != NULL;
            title |= s->bold && strstr(s->text, "Title") != NULL;
            footer |= strstr(s->text, "Footer") != NULL;
        }
    }
    assert(columns && title && footer && unavailable);
    for (size_t i = 0; i < narrow.count; ++i) assert(narrow.rows[i].count <= 1);
    assert(narrow.count > wide.count);
    message_blocks_free(&wide); message_blocks_free(&narrow); json_decref(m);
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
    assert(!sixel_image_encode(pixels, 100000, 12, &image));
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
    assert(!graphics_image_encode(pixels, 641, 12, t, &image));
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
        t.protocol = GRAPHICS_ITERM2;
    }
}
static void measured_media(void *context, const message_media *item, int available_width, int *width, int *height)
{
    (void)context;
    assert(item->kind == AUTH_MEDIA_ATTACHMENT && available_width > 8);
    *width = 8; *height = 10;
}
static void attachment_layout(void)
{
    json_error_t error;
    json_t *m = json_loads("{\"id\":\"00000000-0000-4000-8000-000000000001\",\"attachments\":[{\"id\":\"00000000-0000-4000-8000-000000000002\",\"thumbnail_id\":\"00000000-0000-4000-8000-000000000003\",\"mime_type\":\"image/gif\",\"original_file_name\":\"example.gif\",\"size_bytes\":43562,\"moderation_status\":\"clean\"}]}", 0, &error);
    assert(m);
    message_blocks b;
    assert(message_blocks_build_sized(&b, m, "Sender", "", 80, true, measured_media, NULL));
    assert(b.media_count == 1 && b.media[0].kind == AUTH_MEDIA_ATTACHMENT);
    int media_rows = 0;
    for (size_t r = 0; r < b.count; ++r) {
        for (int c = 0; c < b.rows[r].count; ++c) {
            const char *text = b.rows[r].segments[c].text;
            assert(!strstr(text, "Anexo:") && !strstr(text, "Tipo:") && !strstr(text, "Moderação:") && !strstr(text, "[Imagem / F7]"));
        }
        if (b.rows[r].media_index >= 0) {
            assert(b.rows[r].media_width == 8 && b.rows[r].media_height == 10);
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
int main(void)
{
    assert(setlocale(LC_ALL, "C.UTF-8")); init_graphemebreak(); cards(); protocol(); graphics_protocols();
    preview_crops(); attachment_layout();
    puts("cards, terminal negotiation and Sixel/Kitty/iTerm2 pixels verified"); return 0;
}
