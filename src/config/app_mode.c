#include "config/app_mode.h"
#include <stdatomic.h>
#include <string.h>
static atomic_int current_mode;
static atomic_bool normal_withdrawn;
const char *app_mode_name(app_mode mode)
{
    static const char *names[] = {"normal", "light", "ultralight"};
    return mode >= APP_MODE_NORMAL && mode <= APP_MODE_ULTRALIGHT ? names[mode] : "normal";
}
const char *app_mode_description(app_mode mode)
{
    static const char *descriptions[] = {
        "Protocolo gráfico ou Chafa; GIFs animados e vídeos sob demanda.",
        "Imagens, GIFs animados e vídeos em caracteres pela Chafa.",
        "Somente texto; sem imagens ou vídeos; libera a pilha de mídia."};
    return descriptions[mode >= APP_MODE_NORMAL && mode <= APP_MODE_ULTRALIGHT ? mode : 0];
}
bool app_mode_parse(const char *name, app_mode *mode)
{
    if (!name || !mode)
        return false;
    for (int i = APP_MODE_NORMAL; i <= APP_MODE_ULTRALIGHT; ++i)
        if (!strcmp(name, app_mode_name((app_mode)i))) {
            *mode = (app_mode)i;
            return true;
        }
    return false;
}
app_mode app_mode_get(void)
{
    app_mode mode = (app_mode)atomic_load(&current_mode);
    return mode == APP_MODE_NORMAL && atomic_load(&normal_withdrawn) ? APP_MODE_LIGHT : mode;
}
void app_mode_withdraw_normal(void)
{
    atomic_store(&normal_withdrawn, true);
}
bool app_mode_normal_available(void)
{
    return !atomic_load(&normal_withdrawn);
}
void app_mode_set(app_mode mode)
{
    if (mode >= APP_MODE_NORMAL && mode <= APP_MODE_ULTRALIGHT)
        atomic_store(&current_mode, mode);
}
