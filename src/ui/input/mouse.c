#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "ui/input/mouse.h"
#include "net/clock.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <term.h>

static TERMINAL *mouse_terminal;
#endif

/* Termux confirms a single tap only after the platform double-tap timeout and
 * opens the soft keyboard only when mouse reporting is off at that moment. The
 * reporting must stay suspended past the timeout, with room for the delay
 * between the tap and the application handling it. */
#define KEYBOARD_WINDOW_MS 600

/* A click asked for the keyboard and waits for its gesture to end. */
static bool keyboard_pending;
/* The gesture that carried the click already released the button. */
static bool keyboard_released;
static bool keyboard_suspended;
static uint64_t keyboard_until;

/* Termux exports its version to every session it starts. */
static bool termux_active(void)
{
    static bool known;
    static bool active;
    if (!known) {
        const char *version = getenv("TERMUX_VERSION");
        active = version && *version;
        known = true;
    }
    return active;
}

static void keyboard_suspend(void)
{
    /* Reporting off: Termux reads the tap it already delivered as a terminal
     * tap and opens the soft keyboard, instead of forwarding another report. */
    fputs("\033[?1002l\033[?1000l", stdout);
    fflush(stdout);
    keyboard_suspended = true;
    keyboard_pending = false;
    uint64_t now = 0;
    keyboard_until = net_now_ms(&now) ? now + KEYBOARD_WINDOW_MS : 0;
}

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

void ui_mouse_keyboard_request(void)
{
    if (!termux_active() || keyboard_pending || keyboard_suspended)
        return;
    uint64_t now = 0;
    if (!net_now_ms(&now))
        return;
    keyboard_pending = true;
    /* The gesture already ended when the tap delivered its press and release
     * together; the request can suspend the reporting right away. */
    if (keyboard_released)
        keyboard_suspend();
}

void ui_mouse_keyboard_event(mmask_t bstate)
{
    if (bstate & BUTTON1_PRESSED)
        keyboard_released = false;
    if (!(bstate & BUTTON1_RELEASED))
        return;
    keyboard_released = true;
    /* Suspending before the release would lose it, and a lost release leaves
     * the next click acting like an unfinished selection drag. */
    if (keyboard_pending)
        keyboard_suspend();
}

void ui_mouse_keyboard_poll(void)
{
    if (!keyboard_suspended)
        return;
    uint64_t now = 0;
    if (!net_now_ms(&now) || now < keyboard_until)
        return;
    fputs("\033[?1000h\033[?1002h", stdout);
    fflush(stdout);
    keyboard_suspended = false;
}
