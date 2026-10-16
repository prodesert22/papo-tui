#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "ui/input/mouse.h"
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <term.h>

static TERMINAL *mouse_terminal;
#endif

void ui_mouse_initialize(mmask_t mask)
{
#ifndef _WIN32
    const char *name = getenv("TERM"), *prefix = tigetstr("kmous");
    /* Some Kitty installations still ship a terminfo entry using X10 mouse
     * reports, which cannot address columns beyond 223. Let ncurses initialize
     * its SGR decoder using xterm's capabilities, retaining Kitty's own terminal
     * description for everything else. No hand-written mouse parser is needed.
     * Keep the auxiliary terminfo alive: ncurses borrows its XM string. */
    if (name && !strcmp(name, "xterm-kitty") && prefix && prefix != (char *)-1 &&
        !strcmp(prefix, "\033[M")) {
        TERMINAL *original = cur_term;
        int status = 0;
        if (setupterm("xterm-256color", fileno(stdout), &status) == OK) {
            TERMINAL *candidate = cur_term;
            const char *sgr = tigetstr("kmous");
            if (sgr && sgr != (char *)-1 && !strcmp(sgr, "\033[<")) {
                define_key("\033[<", KEY_MOUSE);
                mousemask(mask, NULL);
                mouse_terminal = candidate;
            }
            set_curterm(original);
            if (!mouse_terminal)
                del_curterm(candidate);
        } else
            set_curterm(original);
    }
#endif
    mousemask(mask, NULL);
}

void ui_mouse_shutdown(void)
{
#ifndef _WIN32
    if (mouse_terminal) {
        del_curterm(mouse_terminal);
        mouse_terminal = NULL;
    }
#endif
}
