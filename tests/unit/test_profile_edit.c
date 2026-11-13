#include "ui/input/profile_commands.h"
#include "ui/media/profile_image.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <zlib.h>

#define CHECK(value)                                                                               \
    do {                                                                                           \
        if (!(value)) {                                                                            \
            fprintf(stderr, "check failed at line %d\n", __LINE__);                                \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static profile_command command;

static int commands(void)
{
    profile_command_parse("olá /nick x", &command);
    CHECK(command.kind == PROFILE_COMMAND_NONE);
    profile_command_parse("/nickname x", &command);
    CHECK(command.kind == PROFILE_COMMAND_NONE);
    profile_command_parse("/comando", &command);
    CHECK(command.kind == PROFILE_COMMAND_NONE);
    profile_command_parse("//nick x", &command);
    CHECK(command.kind == PROFILE_COMMAND_LITERAL);
    profile_command_parse("/nick   Ana Clara  ", &command);
    CHECK(command.kind == PROFILE_COMMAND_NICK && !strcmp(command.argument, "Ana Clara"));
    profile_command_parse("/NICK", &command);
    CHECK(command.kind == PROFILE_COMMAND_NICK && !*command.argument);
    profile_command_parse("/bio\nduas\nlinhas\n", &command);
    CHECK(command.kind == PROFILE_COMMAND_BIO && !strcmp(command.argument, "duas\nlinhas"));
    profile_command_parse("/status Ausente", &command);
    CHECK(command.kind == PROFILE_COMMAND_STATUS && !strcmp(command.status, "away"));
    profile_command_parse("/status busy", &command);
    CHECK(command.kind == PROFILE_COMMAND_STATUS && !strcmp(command.status, "busy"));
    profile_command_parse("/status online", &command);
    CHECK(command.kind == PROFILE_COMMAND_STATUS && !*command.status);
    profile_command_parse("/status", &command);
    CHECK(command.kind == PROFILE_COMMAND_USAGE && command.usage);
    profile_command_parse("/status talvez", &command);
    CHECK(command.kind == PROFILE_COMMAND_USAGE);
    profile_command_parse("/avatar", &command);
    CHECK(command.kind == PROFILE_COMMAND_AVATAR);
    profile_command_parse("/avatar foto.png", &command);
    CHECK(command.kind == PROFILE_COMMAND_USAGE);
    profile_command_parse("/banner", &command);
    CHECK(command.kind == PROFILE_COMMAND_BANNER);
    profile_command_parse("/banner remover", &command);
    CHECK(command.kind == PROFILE_COMMAND_BANNER_REMOVE);
    profile_command_parse("/banner talvez", &command);
    CHECK(command.kind == PROFILE_COMMAND_USAGE);
    /* A long argument is cut at a whole UTF-8 sequence. */
    static char text[sizeof(command.argument) + 64];
    strcpy(text, "/bio ");
    for (size_t at = 5; at + 2 < sizeof(text) - 1; at += 2)
        memcpy(text + at, "é", 2);
    text[sizeof(text) - 1] = 0;
    profile_command_parse(text, &command);
    CHECK(command.kind == PROFILE_COMMAND_BIO && strlen(command.argument) % 2 == 0);
    return 0;
}

static size_t be(unsigned char *at, uint32_t value)
{
    at[0] = (unsigned char)(value >> 24);
    at[1] = (unsigned char)(value >> 16);
    at[2] = (unsigned char)(value >> 8);
    at[3] = (unsigned char)value;
    return 4;
}
static int inspection(void)
{
    profile_image_info info;
    unsigned char png[64] = "\x89PNG\r\n\x1a\n";
    be(png + 8, 13);
    memcpy(png + 12, "IHDR", 4);
    be(png + 16, 640);
    be(png + 20, 480);
    CHECK(profile_image_inspect(png, 33, &info) && info.format == PROFILE_FORMAT_PNG &&
          info.width == 640 && info.height == 480 && !info.animated);
    /* APNG: acTL before the image data. */
    be(png + 33, 8);
    memcpy(png + 37, "acTL", 4);
    CHECK(profile_image_inspect(png, 53, &info) && info.animated);

    /* GIF with a global table and two image descriptors. */
    unsigned char gif[128] = "GIF89a";
    gif[6] = 0x58, gif[7] = 0x02, gif[8] = 0x10, gif[9] = 0x00; /* 600 x 16 */
    gif[10] = 0x80;                                             /* 2-color table */
    size_t at = 13 + 6;
    for (int frame = 0; frame < 2; ++frame) {
        gif[at] = 0x21, gif[at + 1] = 0xf9, gif[at + 2] = 4, gif[at + 7] = 0;
        at += 8;
        gif[at] = 0x2c;
        at += 10;
        gif[at++] = 2;                         /* LZW minimum code size */
        gif[at++] = 2, at += 2, gif[at++] = 0; /* One data sub-block */
    }
    gif[at++] = 0x3b;
    CHECK(profile_image_inspect(gif, at, &info) && info.format == PROFILE_FORMAT_GIF &&
          info.width == 600 && info.height == 16 && info.animated);
    CHECK(profile_image_inspect(gif, 13 + 6 + 8 + 14, &info) && !info.animated);

    unsigned char jpeg[] = {0xff, 0xd8, 0xff, 0xe0, 0x00, 0x04, 0x00, 0x00, 0xff,
                            0xc0, 0x00, 0x11, 0x08, 0x01, 0x2c, 0x02, 0x58, 0x03};
    CHECK(profile_image_inspect(jpeg, sizeof(jpeg), &info) && info.format == PROFILE_FORMAT_JPEG &&
          info.width == 600 && info.height == 300);

    unsigned char webp[32] = "RIFF\0\0\0\0WEBPVP8X";
    webp[20] = 0x02;                  /* Animation flag */
    webp[24] = 0xff, webp[25] = 0x03; /* Width 1024 */
    webp[27] = 0x7f, webp[28] = 0x00; /* Height 128 */
    CHECK(profile_image_inspect(webp, sizeof(webp), &info) && info.format == PROFILE_FORMAT_WEBP &&
          info.width == 1024 && info.height == 128 && info.animated);

    CHECK(!profile_image_inspect((const unsigned char *)"texto qualquer", 14, &info));
    CHECK(!profile_image_inspect(png, 20, &info));
    CHECK(!strcmp(profile_image_format_name(PROFILE_FORMAT_JPEG), "JPEG"));
    return 0;
}
static int writing(void)
{
    unsigned char pixels[3 * 4 * 2];
    for (size_t i = 0; i < sizeof(pixels); ++i)
        pixels[i] = (unsigned char)(i * 11);
    unsigned char *data;
    size_t length;
    CHECK(profile_image_png(pixels, 3, 2, true, &data, &length));
    profile_image_info info;
    CHECK(profile_image_inspect(data, length, &info) && info.width == 3 && info.height == 2);
    /* IHDR: 8 bits, RGBA; the image data inflates back to the filtered rows. */
    CHECK(data[24] == 8 && data[25] == 6);
    uint32_t idat =
        (uint32_t)data[33] << 24 | (uint32_t)data[34] << 16 | (uint32_t)data[35] << 8 | data[36];
    CHECK(!memcmp(data + 37, "IDAT", 4));
    unsigned char rows[2 * (1 + 3 * 4)];
    uLongf rows_length = sizeof(rows);
    CHECK(uncompress(rows, &rows_length, data + 41, idat) == Z_OK && rows_length == sizeof(rows));
    CHECK(rows[0] == 0 && !memcmp(rows + 1, pixels, 12) && rows[13] == 0 &&
          !memcmp(rows + 14, pixels + 12, 12));
    CHECK(!memcmp(data + length - 8, "IEND", 4));
    g_free(data);
    CHECK(!profile_image_png(pixels, 0, 2, false, &data, &length));
    return 0;
}

int main(void)
{
    int failed = commands();
    failed |= inspection();
    failed |= writing();
    return failed;
}
