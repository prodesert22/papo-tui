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
    return app_mode_get() == APP_MODE_NORMAL && media_service_glyph(text, w, h, bg, t, id, out);
}
