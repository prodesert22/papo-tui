#include "ui/media/resources.h"
#include "platform/runtime.h"
#include <fontconfig/fontconfig.h>
#include <glib.h>
#include <stdlib.h>
#include <string.h>
void media_resources_init(void)
{
    char *executable = papo_executable_path();
    if (!executable)
        return;
    char *directory = g_path_get_dirname(executable);
#ifdef _WIN32
    char *prefix = g_strdup(directory);
#else
    char *name = g_path_get_basename(directory);
    char *prefix = !strcmp(name, "bin") ? g_path_get_dirname(directory) : g_strdup(directory);
    g_free(name);
#endif
#ifdef _WIN32
    char *terminfo = g_build_filename(prefix, "share", "terminfo", NULL);
    if (g_file_test(terminfo, G_FILE_TEST_IS_DIR))
        g_setenv("TERMINFO", terminfo, FALSE);
    char *fontconfig = g_build_filename(prefix, "etc", "fonts", NULL);
    if (g_file_test(fontconfig, G_FILE_TEST_IS_DIR))
        g_setenv("FONTCONFIG_PATH", fontconfig, FALSE);
    g_free(fontconfig);
    g_free(terminfo);
#endif
    char *font = g_build_filename(prefix, "share", "fonts", "papo-tui", "NotoColorEmoji.ttf", NULL);
    if (g_file_test(font, G_FILE_TEST_IS_REGULAR))
        FcConfigAppFontAddFile(NULL, (const FcChar8 *)font);
    g_free(font);
    g_free(prefix);
    g_free(directory);
    free(executable);
}
