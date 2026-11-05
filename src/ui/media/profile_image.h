#ifndef PAPO_PROFILE_IMAGE_H
#define PAPO_PROFILE_IMAGE_H
#include <gio/gio.h>
#include <stdbool.h>
#include <stddef.h>
/* Avatar and banner uploads: the server takes GIF, JPEG, PNG or WEBP up to
 * 2 MB, avatars up to 512 px and banners up to 2048 px per side. */
#define PROFILE_IMAGE_BYTES 2000000U
#define PROFILE_AVATAR_SIDE 512
#define PROFILE_BANNER_SIDE 2048
typedef enum {
    PROFILE_FORMAT_NONE,
    PROFILE_FORMAT_GIF,
    PROFILE_FORMAT_JPEG,
    PROFILE_FORMAT_PNG,
    PROFILE_FORMAT_WEBP
} profile_format;
typedef struct {
    profile_format format;
    int width, height;
    bool animated;
} profile_image_info;
/* Reads only the container headers; never decodes pixels. */
bool profile_image_inspect(const unsigned char *data, size_t length, profile_image_info *info);
const char *profile_image_format_name(profile_format format);
/* PNG from packed RGB or RGBA rows (8 bits per channel). Owned by g_free. */
bool profile_image_png(const unsigned char *pixels, int width, int height, bool alpha,
                       unsigned char **png, size_t *length);
typedef struct {
    char *base64; /* Owned by g_free on success. */
    char format[8], error[192];
    bool reduced;
} profile_image_result;
/* Blocking, worker thread only. A file within the limits is sent unchanged;
 * a larger still image is decoded by the media helper, reduced to fit and
 * written as PNG. A larger animation is refused. */
bool profile_image_prepare(const char *path, int max_side, profile_image_result *result,
                           GCancellable *cancel);
#endif
