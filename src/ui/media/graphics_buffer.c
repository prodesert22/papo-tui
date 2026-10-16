#include "ui/media/graphics.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
static atomic_uint_fast32_t next_id = 0x50510000U;
uint32_t graphics_image_next_id(void)
{
    uint32_t id = (uint32_t)atomic_fetch_add(&next_id, 1);
    return id ? id : (uint32_t)atomic_fetch_add(&next_id, 1);
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
