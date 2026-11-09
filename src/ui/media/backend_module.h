#ifndef PAPO_BACKEND_MODULE_H
#define PAPO_BACKEND_MODULE_H
#include <gmodule.h>
/* Opens a bundled backend module by file name: next to the executable in
 * papo-backends/, or in the installed backend directory. Never searches the
 * working directory, PATH or a remotely supplied path. */
GModule *backend_module_open(const char *name);
#endif
