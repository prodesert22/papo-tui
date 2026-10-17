#include "platform/terminal.h"
#include <fcntl.h>
#include <glib.h>
#include <io.h>
#include <signal.h>
#include <stdio.h>
#include <windows.h>
static DWORD input_mode, output_mode, input_cp, output_cp;
static bool active;
static void (*interrupt_handler)(int);
static void (*old_interrupt)(int), (*old_terminate)(int);
static void restore(void)
{
    if (!active)
        return;
    static const char reset[] = "\033\\\033[?2026l\033[?1000l\033[?1002l\033[?1003l\033[?1006l"
                                "\033[?1004l\033[?2004l\033[0m\033[?25h\033[?1049l";
    DWORD written;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), reset, sizeof(reset) - 1, &written, NULL);
    SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), input_mode);
    SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), output_mode);
    SetConsoleCP(input_cp);
    SetConsoleOutputCP(output_cp);
    active = false;
}
static BOOL WINAPI control(DWORD event)
{
    if (event == CTRL_CLOSE_EVENT || event == CTRL_LOGOFF_EVENT || event == CTRL_SHUTDOWN_EVENT) {
        restore();
        return FALSE;
    }
    if (interrupt_handler && (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT)) {
        interrupt_handler(SIGINT);
        return TRUE;
    }
    return FALSE;
}
bool terminal_recovery_begin(void)
{
    if (!GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &input_mode) ||
        !GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &output_mode))
        return false;
    input_cp = GetConsoleCP();
    output_cp = GetConsoleOutputCP();
    if (!SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE),
                        output_mode | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING))
        return false;
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stdin), _O_BINARY);
    if (!g_getenv("TERM"))
        g_setenv("TERM", "xterm-256color", FALSE);
    active = true;
    if (!SetConsoleCtrlHandler(control, TRUE)) {
        restore();
        return false;
    }
    return true;
}
void terminal_recovery_end(void)
{
    restore();
    SetConsoleCtrlHandler(control, FALSE);
}
void terminal_interrupt_begin(void (*handler)(int))
{
    interrupt_handler = handler;
    old_interrupt = signal(SIGINT, handler);
    old_terminate = signal(SIGTERM, handler);
}
void terminal_interrupt_end(void)
{
    signal(SIGINT, old_interrupt);
    signal(SIGTERM, old_terminate);
    interrupt_handler = NULL;
}
