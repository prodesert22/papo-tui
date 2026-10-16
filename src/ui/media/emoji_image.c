#include "ui/media/emoji_image.h"
#include "config/app_mode.h"
#include "ui/media/media_service.h"
bool emoji_image_available(void)
{
    return app_mode_get() == APP_MODE_NORMAL;
}
bool emoji_image_encode(const char *text, int w, int h, uint32_t bg, graphics_terminal t,
                        uint32_t id, graphics_image *out)
{
    return emoji_image_encode_region(text, w, h, bg, t, id, out, 0, 0, 0, 0);
}
bool emoji_image_encode_region(const char *text, int w, int h, uint32_t bg, graphics_terminal t,
                               uint32_t id, graphics_image *out, int top, int bottom, int left,
                               int right)
{
    image_decode_request request = {.width = w,
                                    .height = h,
                                    .terminal = t,
                                    .image_id = id,
                                    .background = bg,
                                    .region = true,
                                    .top = top,
                                    .bottom = bottom,
                                    .left = left,
                                    .right = right};
    return app_mode_get() == APP_MODE_NORMAL && media_service_glyph(text, &request, out);
}
