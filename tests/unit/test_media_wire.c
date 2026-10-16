#include "ui/media/media_wire.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    image_decode_request request = {.path = "/tmp/synthetic.png",
                                    .width = 32,
                                    .height = 16,
                                    .terminal = {.protocol = GRAPHICS_CHAFA,
                                                 .supported = true,
                                                 .cell_width = 8,
                                                 .cell_height = 16},
                                    .image_id = 123,
                                    .cover = true,
                                    .matte = true,
                                    .background = 0x123456};
    GVariant *value = g_variant_ref_sink(media_wire_request(&request));
    image_decode_request received;
    char *path = NULL;
    assert(media_wire_read_request(value, &received, &path));
    assert(!strcmp(path, request.path) && received.cover && received.matte &&
           received.image_id == 123);
    assert(received.width == 32 && received.terminal.protocol == GRAPHICS_CHAFA);
    g_free(path);
    g_variant_unref(value);
    char long_path[4200];
    memset(long_path, 'x', sizeof(long_path) - 1);
    long_path[sizeof(long_path) - 1] = 0;
    request.path = long_path;
    value = g_variant_ref_sink(media_wire_request(&request));
    assert(!media_wire_read_request(value, &received, &path) && !path);
    g_variant_unref(value);

    graphics_cell cells[2] = {{'x', 1, 2}, {0x2588, 4, 0}};
    graphics_image image = {.data = (char *)cells,
                            .length = sizeof(cells),
                            .protocol = GRAPHICS_CHAFA,
                            .id = 77,
                            .revision = 9,
                            .width = 16,
                            .height = 16,
                            .columns = 2,
                            .rows = 1,
                            .cell_width = 8,
                            .cell_height = 16};
    graphics_image decoded;
    value = g_variant_ref_sink(media_wire_image(&image));
    assert(media_wire_read_image(value, &decoded));
    assert(decoded.data != (char *)cells && decoded.id == 77 && decoded.revision == 9);
    assert(!memcmp(decoded.data, cells, sizeof(cells)));
    graphics_image_free(&decoded);
    g_variant_unref(value);
    image.length = sizeof(cells) - 1;
    value = g_variant_ref_sink(media_wire_image(&image));
    assert(!media_wire_read_image(value, &decoded) && !decoded.data);
    g_variant_unref(value);
    image.length = sizeof(cells);
    image.protocol = (graphics_protocol)99;
    value = g_variant_ref_sink(media_wire_image(&image));
    assert(!media_wire_read_image(value, &decoded));
    g_variant_unref(value);

    animation source = {0}, *frames = NULL;
    source.width = 2;
    source.height = 2;
    source.source_width = 2;
    source.source_height = 2;
    source.count = 1;
    source.rgba[0] = true;
    source.delay[0] = 200;
    source.duration = 200;
    source.bytes = 2 * 2 * 4 + IMAGE_PIXEL_PADDING;
    source.pixels[0] = calloc(1, source.bytes);
    assert(source.pixels[0]);
    source.pixels[0][3] = 255;
    value = g_variant_ref_sink(media_wire_animation(&source));
    assert(media_wire_read_animation(value, &frames));
    assert(frames && frames->pixels[0] != source.pixels[0] && frames->pixels[0][3] == 255 &&
           frames->rgba[0]);
    assert(frames->bytes == source.bytes && animation_frame_at(frames, 1000, 1250) == 0);
    animation_free(frames);
    g_variant_unref(value);
    ++source.bytes;
    value = g_variant_ref_sink(media_wire_animation(&source));
    assert(!media_wire_read_animation(value, &frames) && !frames);
    g_variant_unref(value);
    free(source.pixels[0]);
    value = g_variant_ref_sink(g_variant_new_boolean(false));
    assert(!media_wire_read_animation(value, &frames));
    assert(!media_wire_read_image(value, &decoded));
    g_variant_unref(value);
    return 0;
}
