#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "platform/browser.h"
#include <glib.h>
#include <unistd.h>

/* Runs in the child before it becomes the opener. Outside the terminal's
 * session, a browser started by it survives the client and its terminal and
 * takes no keyboard signal meant for them. */
static void detach(gpointer context)
{
    (void)context;
    setsid();
}
bool browser_open(const char *url)
{
    if (!url || (g_ascii_strncasecmp(url, "https://", 8) && g_ascii_strncasecmp(url, "http://", 7)))
        return false;
    /* xdg-open is the desktop's own opener: it follows the user's default
     * browser, reaches the host through the portal inside Flatpak and is
     * provided by Termux. GLib starts it without waiting for it and leaves no
     * child to reap; its input is /dev/null, and so is its output, which
     * would otherwise be drawn over the interface. */
    char *arguments[] = {"xdg-open", (char *)url, NULL};
    return g_spawn_async(NULL, arguments, NULL,
                         G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL |
                             G_SPAWN_STDERR_TO_DEV_NULL,
                         detach, NULL, NULL, NULL);
}
