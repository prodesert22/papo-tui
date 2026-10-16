#ifndef PAPO_MEDIA_SERVICE_H
#define PAPO_MEDIA_SERVICE_H
#include "ui/media/media_backend.h"
/* Stop is nonblocking and cancels outstanding RPCs before retiring the child.
 * Expensive calls below run on existing media workers, never the TUI thread. */
void media_service_stop(void);
const char *media_service_error(void);
bool media_service_iterm2(void);
bool media_service_glyph(const char *, int, int, uint32_t, graphics_terminal, uint32_t,
                         graphics_image *);
#endif
