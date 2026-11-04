#ifndef PAPO_TERMINAL_FOCUS_H
#define PAPO_TERMINAL_FOCUS_H
#include <gio/gio.h>
/* Brings the terminal running this process to the front after the user
 * clicked one of its desktop notifications. First the tab or pane is selected
 * where the multiplexer or terminal exposes it (tmux, Konsole, WezTerm); then
 * the window manager activates the window owned by the nearest ancestor
 * process (KWin, sway, Hyprland or, with XCB, any EWMH X11 manager). title is
 * the terminal title this process set, used to pick between several windows
 * of one terminal process. Best effort, in a worker thread: never blocks the
 * caller, and bus is only used for the calls of this request. */
void terminal_focus_request(GDBusConnection *bus, const char *title);
#endif
