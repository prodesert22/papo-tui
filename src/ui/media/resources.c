#include "ui/media/resources.h"
#include "platform/install.h"
#ifdef PAPO_MEDIA_RESOURCES_WORKER
#include <fontconfig/fontconfig.h>
#endif
#include <glib.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
/* ncurses checks terminfo entries with access() but reads them with stat()
 * and fopen(), and after setlocale(".UTF8") those calls decode a path with
 * accents differently. The folder's 8.3 short name is plain ASCII for all of
 * them; keep the full path when the volume has no short names. */
static char *ascii_path(const char *path)
{
    char *result = NULL;
    wchar_t *wide = g_utf8_to_utf16(path, -1, NULL, NULL, NULL);
    DWORD size = wide ? GetShortPathNameW(wide, NULL, 0) : 0;
    if (size) {
        wchar_t *shortened = g_new(wchar_t, size);
        if (GetShortPathNameW(wide, shortened, size))
            result = g_utf16_to_utf8((const gunichar2 *)shortened, -1, NULL, NULL, NULL);
        g_free(shortened);
    }
    g_free(wide);
    if (!result || !g_str_is_ascii(result)) {
        g_free(result);
        result = g_strdup(path);
    }
    return result;
}
#endif
void media_resources_init(void)
{
    char *prefix = install_prefix();
    if (!prefix)
        return;
#ifdef _WIN32
    char *terminfo = g_build_filename(prefix, "share", "terminfo", NULL);
    if (g_file_test(terminfo, G_FILE_TEST_IS_DIR)) {
        char *path = ascii_path(terminfo);
        g_setenv("TERMINFO", path, FALSE);
        g_free(path);
    }
    char *fontconfig = g_build_filename(prefix, "etc", "fonts", NULL);
    if (g_file_test(fontconfig, G_FILE_TEST_IS_DIR))
        g_setenv("FONTCONFIG_PATH", fontconfig, FALSE);
    g_free(fontconfig);
    g_free(terminfo);
#endif
#ifdef PAPO_MEDIA_RESOURCES_WORKER
    char *font = g_build_filename(prefix, "share", "fonts", "papo-tui", "NotoColorEmoji.ttf", NULL);
    if (g_file_test(font, G_FILE_TEST_IS_REGULAR))
        FcConfigAppFontAddFile(NULL, (const FcChar8 *)font);
    g_free(font);
#endif
    g_free(prefix);
}
