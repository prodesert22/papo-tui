#ifndef PAPO_APP_MODE_H
#define PAPO_APP_MODE_H
#include <stdbool.h>
typedef enum { APP_MODE_NORMAL, APP_MODE_LIGHT, APP_MODE_ULTRALIGHT } app_mode;
const char *app_mode_name(app_mode mode);
const char *app_mode_description(app_mode mode);
bool app_mode_parse(const char *name, app_mode *mode);
/* The effective mode: Normal becomes Light once Normal is unavailable. */
app_mode app_mode_get(void);
void app_mode_set(app_mode mode);
/* Normal needs a graphics protocol; without one it is withdrawn for the rest
 * of the process. The saved preference is left unchanged. */
void app_mode_withdraw_normal(void);
bool app_mode_normal_available(void);
#endif
