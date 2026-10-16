#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "platform/terminal.h"
#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <termios.h>
#include <unistd.h>

static const int fatal_signals[] = {SIGABRT, SIGSEGV, SIGBUS, SIGILL, SIGFPE};
static struct sigaction previous[sizeof(fatal_signals) / sizeof(fatal_signals[0])];
static struct termios original;
static volatile sig_atomic_t active;

static void recover(int number, siginfo_t *info, void *context)
{
    int saved_errno = errno;
    if (active) {
        tcsetattr(STDIN_FILENO, TCSANOW, &original);
        /* Terminate an interrupted graphics string before resetting modes.
         * Leave the alternate screen and clear the application's focus marker. */
        static const char reset[] =
            "\033\\\033[?2026l\033[?1000l\033[?1002l\033[?1003l"
            "\033[?1006l\033[?1004l\033[?2004l\033[?1l\033>\033[0m\033[?25h\033[?1049l"
            "\033]1337;SetUserVar=papo_tui\a";
        ssize_t ignored = write(STDOUT_FILENO, reset, sizeof(reset) - 1);
        (void)ignored;
    }
    for (size_t i = 0; i < sizeof(fatal_signals) / sizeof(fatal_signals[0]); ++i) {
        if (fatal_signals[i] != number)
            continue;
        sigaction(number, &previous[i], NULL);
        /* Preserve sanitizer diagnostics with the original fault context. */
        if (previous[i].sa_handler != SIG_DFL && previous[i].sa_handler != SIG_IGN) {
            if (previous[i].sa_flags & SA_SIGINFO)
                previous[i].sa_sigaction(number, info, context);
            else
                previous[i].sa_handler(number);
        }
        break;
    }
    struct sigaction action = {0};
    action.sa_handler = SIG_DFL;
    sigemptyset(&action.sa_mask);
    sigaction(number, &action, NULL);
    errno = saved_errno;
    kill(getpid(), number); /* Retain the fatal status and ordinary core dump. */
}
bool terminal_recovery_begin(void)
{
    if (tcgetattr(STDIN_FILENO, &original))
        return false;
    struct sigaction action = {0};
    action.sa_sigaction = recover;
    action.sa_flags = SA_SIGINFO;
    sigfillset(&action.sa_mask);
    size_t i = 0;
    for (; i < sizeof(fatal_signals) / sizeof(fatal_signals[0]); ++i) {
        if (!sigaction(fatal_signals[i], &action, &previous[i]))
            continue;
        while (i) {
            --i;
            sigaction(fatal_signals[i], &previous[i], NULL);
        }
        return false;
    }
    active = 1;
    return true;
}
void terminal_recovery_end(void)
{
    active = 0;
    for (size_t i = 0; i < sizeof(fatal_signals) / sizeof(fatal_signals[0]); ++i)
        sigaction(fatal_signals[i], &previous[i], NULL);
}

static struct sigaction old_interrupt, old_terminate;
void terminal_interrupt_begin(void (*handler)(int))
{
    struct sigaction action = {0};
    action.sa_handler = handler;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, &old_interrupt);
    sigaction(SIGTERM, &action, &old_terminate);
}
void terminal_interrupt_end(void)
{
    sigaction(SIGINT, &old_interrupt, NULL);
    sigaction(SIGTERM, &old_terminate, NULL);
}
