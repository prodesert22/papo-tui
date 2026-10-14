#define _POSIX_C_SOURCE 200809L
#include "ui/message_blocks.h"
#include "ui/sixel.h"
#include "ui/text.h"
#include <assert.h>
#include <locale.h>
#include <sixel.h>
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
int main(void)
{
    assert(setlocale(LC_ALL, "C.UTF-8")); init_graphemebreak(); cards(); protocol();
    puts("cards, terminal negotiation and Sixel pixels verified"); return 0;
}
