#include "ui/media/profile_image.h"
#include "config/app_mode.h"
#include "model/text/translation.h"
#include "ui/media/image_decode.h"
#include <glib/gstdio.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <zlib.h>

#define READ_LIMIT (64U * 1024U * 1024U)

static uint32_t be32(const unsigned char *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static unsigned be16(const unsigned char *p)
{
    return (unsigned)p[0] << 8 | p[1];
}
static unsigned le16(const unsigned char *p)
{
    return (unsigned)p[1] << 8 | p[0];
}
static unsigned le24(const unsigned char *p)
{
    return (unsigned)p[2] << 16 | (unsigned)p[1] << 8 | p[0];
}

static bool png_info(const unsigned char *d, size_t n, profile_image_info *info)
{
    if (n < 24 || memcmp(d, "\x89PNG\r\n\x1a\n", 8) || memcmp(d + 12, "IHDR", 4))
        return false;
    info->width = (int)MIN(be32(d + 16), (uint32_t)INT32_MAX);
    info->height = (int)MIN(be32(d + 20), (uint32_t)INT32_MAX);
    /* APNG declares acTL before the first image data. */
    for (size_t at = 8; at + 8 <= n;) {
        uint32_t length = be32(d + at);
        if (!memcmp(d + at + 4, "acTL", 4))
            info->animated = true;
        if (!memcmp(d + at + 4, "IDAT", 4) || length > n - at - 8 || n - at - 8 - length < 4)
            break;
        at += 12 + (size_t)length;
    }
    return true;
}
static size_t gif_blocks(const unsigned char *d, size_t n, size_t at)
{
    while (at < n && d[at])
        at += 1 + (size_t)d[at];
    return at < n ? at + 1 : n;
}
static bool gif_info(const unsigned char *d, size_t n, profile_image_info *info)
{
    if (n < 13 || (memcmp(d, "GIF87a", 6) && memcmp(d, "GIF89a", 6)))
        return false;
    info->width = (int)le16(d + 6);
    info->height = (int)le16(d + 8);
    size_t at = 13 + ((d[10] & 0x80) ? 3U << ((d[10] & 7) + 1) : 0);
    unsigned images = 0;
    while (at < n && images < 2) {
        if (d[at] == 0x2c) { /* Image descriptor */
            if (n - at < 11)
                break;
            unsigned char packed = d[at + 9];
            at += 10 + ((packed & 0x80) ? 3U << ((packed & 7) + 1) : 0) + 1;
            at = gif_blocks(d, n, at);
            ++images;
        } else if (d[at] == 0x21 && n - at >= 2) /* Extension */
            at = gif_blocks(d, n, at + 2);
        else
            break;
    }
    info->animated = images > 1;
    return true;
}
static bool jpeg_info(const unsigned char *d, size_t n, profile_image_info *info)
{
    if (n < 4 || d[0] != 0xff || d[1] != 0xd8)
        return false;
    for (size_t at = 2; at + 4 <= n;) {
        if (d[at] != 0xff) {
            ++at;
            continue;
        }
        unsigned char marker = d[at + 1];
        if (marker == 0xff) {
            ++at;
            continue;
        }
        if (marker == 0x01 || (marker >= 0xd0 && marker <= 0xd9)) {
            at += 2;
            continue;
        }
        size_t length = be16(d + at + 2);
        bool frame =
            marker >= 0xc0 && marker <= 0xcf && marker != 0xc4 && marker != 0xc8 && marker != 0xcc;
        if (frame && length >= 7 && at + 9 <= n) {
            info->height = (int)be16(d + at + 5);
            info->width = (int)be16(d + at + 7);
            return info->width > 0 && info->height > 0;
        }
        if (length < 2)
            return false;
        at += 2 + length;
    }
    return false;
}
static bool webp_info(const unsigned char *d, size_t n, profile_image_info *info)
{
    if (n < 30 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "WEBP", 4))
        return false;
    if (!memcmp(d + 12, "VP8X", 4)) {
        info->animated = (d[20] & 0x02) != 0;
        info->width = (int)le24(d + 24) + 1;
        info->height = (int)le24(d + 27) + 1;
    } else if (!memcmp(d + 12, "VP8 ", 4) && d[23] == 0x9d && d[24] == 0x01 && d[25] == 0x2a) {
        info->width = (int)(le16(d + 26) & 0x3fff);
        info->height = (int)(le16(d + 28) & 0x3fff);
    } else if (!memcmp(d + 12, "VP8L", 4) && d[20] == 0x2f) {
        info->width = 1 + (int)(d[21] | (d[22] & 0x3f) << 8);
        info->height = 1 + (int)((d[22] >> 6) | d[23] << 2 | (d[24] & 0x0f) << 10);
    } else
        return false;
    return true;
}
bool profile_image_inspect(const unsigned char *data, size_t length, profile_image_info *info)
{
    *info = (profile_image_info){0};
    if (!data)
        return false;
    bool ok = false;
    if (png_info(data, length, info))
        info->format = PROFILE_FORMAT_PNG, ok = true;
    else if (gif_info(data, length, info))
        info->format = PROFILE_FORMAT_GIF, ok = true;
    else if (jpeg_info(data, length, info))
        info->format = PROFILE_FORMAT_JPEG, ok = true;
    else if (webp_info(data, length, info))
        info->format = PROFILE_FORMAT_WEBP, ok = true;
    if (!ok || info->width < 1 || info->height < 1) {
        *info = (profile_image_info){0};
        return false;
    }
    return true;
}
const char *profile_image_format_name(profile_format format)
{
    return format == PROFILE_FORMAT_GIF    ? "GIF"
           : format == PROFILE_FORMAT_JPEG ? "JPEG"
           : format == PROFILE_FORMAT_PNG  ? "PNG"
           : format == PROFILE_FORMAT_WEBP ? "WEBP"
                                           : "";
}

/* The TUI process links no image library: decoding stays in the media
 * helper. Writing already decoded pixels as PNG only needs zlib for the
 * compressed stream and chunk checksums. */
static void chunk(GByteArray *out, const char *type, const unsigned char *data, size_t length)
{
    unsigned char header[8] = {(unsigned char)(length >> 24), (unsigned char)(length >> 16),
                               (unsigned char)(length >> 8), (unsigned char)length};
    memcpy(header + 4, type, 4);
    g_byte_array_append(out, header, 8);
    if (length)
        g_byte_array_append(out, data, (guint)length);
    uLong crc = crc32(0L, (const Bytef *)type, 4);
    if (length)
        crc = crc32(crc, data, (uInt)length);
    unsigned char sum[4] = {(unsigned char)(crc >> 24), (unsigned char)(crc >> 16),
                            (unsigned char)(crc >> 8), (unsigned char)crc};
    g_byte_array_append(out, sum, 4);
}
bool profile_image_png(const unsigned char *pixels, int width, int height, bool alpha,
                       unsigned char **png, size_t *length)
{
    *png = NULL;
    *length = 0;
    if (!pixels || width < 1 || height < 1 || width > 16384 || height > 16384)
        return false;
    size_t channels = alpha ? 4 : 3, row = (size_t)width * channels;
    size_t raw_length = (row + 1) * (size_t)height;
    unsigned char *raw = g_try_malloc(raw_length);
    uLongf packed_length = compressBound((uLong)raw_length);
    unsigned char *packed = raw ? g_try_malloc(packed_length) : NULL;
    if (!packed) {
        g_free(raw);
        return false;
    }
    for (int y = 0; y < height; ++y) {
        raw[(size_t)y * (row + 1)] = 0; /* Filter: none. */
        memcpy(raw + (size_t)y * (row + 1) + 1, pixels + (size_t)y * row, row);
    }
    bool ok = compress2(packed, &packed_length, raw, (uLong)raw_length, 9) == Z_OK;
    g_free(raw);
    if (ok) {
        GByteArray *out = g_byte_array_new();
        g_byte_array_append(out, (const guint8 *)"\x89PNG\r\n\x1a\n", 8);
        unsigned char header[13] = {(unsigned char)(width >> 24),
                                    (unsigned char)(width >> 16),
                                    (unsigned char)(width >> 8),
                                    (unsigned char)width,
                                    (unsigned char)(height >> 24),
                                    (unsigned char)(height >> 16),
                                    (unsigned char)(height >> 8),
                                    (unsigned char)height,
                                    8,
                                    alpha ? 6 : 2,
                                    0,
                                    0,
                                    0};
        chunk(out, "IHDR", header, sizeof(header));
        chunk(out, "IDAT", packed, packed_length);
        chunk(out, "IEND", NULL, 0);
        *length = out->len;
        *png = g_byte_array_free(out, FALSE);
    }
    g_free(packed);
    return ok;
}

static bool cancelled(void *context)
{
    return g_cancellable_is_cancelled(context);
}
static bool fail(profile_image_result *result, const char *message)
{
    snprintf(result->error, sizeof(result->error), "%s", message);
    return false;
}
/* Decodes the first frame at most side pixels per side and writes it as PNG. */
static bool reduce(const char *path, int side, unsigned char **png, size_t *length,
                   GCancellable *cancel)
{
    image_decode_request request = {.path = path,
                                    .width = side,
                                    .height = side,
                                    .native = true,
                                    .decode_only = true,
                                    .static_only = true,
                                    .retain_static = true};
    graphics_image image;
    animation *frames = NULL;
    bool ok = image_decode_file(&request, &image, &frames, cancelled, cancel) && frames &&
              frames->count && frames->pixels[0] &&
              profile_image_png(frames->pixels[0], frames->width, frames->height, frames->rgba[0],
                                png, length);
    graphics_image_free(&image);
    animation_free(frames);
    return ok;
}
bool profile_image_prepare(const char *path, int max_side, profile_image_result *result,
                           GCancellable *cancel)
{
    *result = (profile_image_result){0};
    GStatBuf status;
    if (!path || g_stat(path, &status) || !S_ISREG(status.st_mode))
        return fail(result, _("Arquivo de imagem não encontrado."));
    if ((guint64)status.st_size > READ_LIMIT)
        return fail(result, _("Arquivo grande demais; use uma imagem de até 2 MB."));
    gchar *data = NULL;
    gsize length = 0;
    if (!g_file_get_contents(path, &data, &length, NULL))
        return fail(result, _("Não foi possível ler a imagem."));
    profile_image_info info;
    if (!profile_image_inspect((const unsigned char *)data, length, &info)) {
        g_free(data);
        return fail(result, _("Use uma imagem PNG, JPEG, GIF ou WEBP."));
    }
    if (length <= PROFILE_IMAGE_BYTES && info.width <= max_side && info.height <= max_side) {
        result->base64 = g_base64_encode((const guchar *)data, length);
        snprintf(result->format, sizeof(result->format), "%s",
                 profile_image_format_name(info.format));
        g_free(data);
        return result->base64 != NULL;
    }
    g_free(data);
    char limits[96];
    snprintf(limits, sizeof(limits), "2 MB e %d px", max_side);
    if (info.animated) {
        snprintf(result->error, sizeof(result->error),
                 _("Animação acima do limite (%s); reduza o arquivo."), limits);
        return false;
    }
    int side = MIN(max_side, MAX(info.width, info.height));
    for (int attempt = 0; attempt < 5 && side >= 16; ++attempt) {
        unsigned char *png = NULL;
        size_t png_length = 0;
        if (!reduce(path, side, &png, &png_length, cancel)) {
            if (g_cancellable_is_cancelled(cancel))
                return fail(result, _("Envio da imagem cancelado."));
            /* Ultralight runs no media helper, so nothing can be decoded. */
            snprintf(result->error, sizeof(result->error),
                     _("Imagem acima do limite (%s) e não foi possível reduzi-la%s."), limits,
                     app_mode_get() == APP_MODE_ULTRALIGHT ? _(" no modo Ultralight") : "");
            return false;
        }
        if (png_length <= PROFILE_IMAGE_BYTES) {
            result->base64 = g_base64_encode(png, png_length);
            g_free(png);
            snprintf(result->format, sizeof(result->format), "PNG");
            result->reduced = true;
            return result->base64 != NULL;
        }
        g_free(png);
        side = side * 3 / 4;
    }
    snprintf(result->error, sizeof(result->error), _("Não foi possível reduzir a imagem para %s."),
             limits);
    return false;
}
