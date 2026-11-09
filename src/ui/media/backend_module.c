#include "ui/media/backend_module.h"
#include "platform/runtime.h"
#include <glib.h>
#include <stdlib.h>

GModule *backend_module_open(const char *name)
{
    if (!g_module_supported())
        return NULL;
    char *executable = papo_executable_path();
    char *directory = executable ? g_path_get_dirname(executable) : NULL;
    char *base = directory ? g_path_get_basename(directory) : NULL;
    char *local = directory ? (!g_strcmp0(base, "papo-backends")
                                   ? g_build_filename(directory, name, NULL)
                                   : g_build_filename(directory, "papo-backends", name, NULL))
                            : NULL;
    g_free(base);
    GModule *module = local ? g_module_open(local, G_MODULE_BIND_LOCAL) : NULL;
    if (!local) {
        char *installed = g_build_filename(PAPO_BACKEND_INSTALL_DIR, name, NULL);
        module = g_module_open(installed, G_MODULE_BIND_LOCAL);
        g_free(installed);
    }
    g_free(local);
    g_free(directory);
    free(executable);
    return module;
}
