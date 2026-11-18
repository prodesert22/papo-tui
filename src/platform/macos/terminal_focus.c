#include "platform/linux/terminal_focus.h"

/* Raising the terminal's window needs LaunchServices, which is not wired up yet. Clicking a
 * notification still reaches the program when the terminal reports it; the window stays where it
 * is. The stub keeps the notification code shared with Linux. */
void terminal_focus_request(GDBusConnection *bus, const char *title)
{
    (void)bus;
    (void)title;
}
