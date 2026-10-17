/* Synthetic ConPTY input diagnostics: compare Win32 UTF-16 with ncurses. */
#include "platform/terminal.h"
#include "ui/input/text.h"
#include <locale.h>
#include <stdio.h>
#include <wctype.h>
#include <windows.h>

int main(void)
{
    if (!setlocale(LC_ALL, ".UTF8") || !setlocale(LC_NUMERIC, "C") || !terminal_recovery_begin())
        return 1;
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;
    if (!GetConsoleMode(input, &mode) ||
        !SetConsoleMode(input, mode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT))) {
        terminal_recovery_end();
        return 1;
    }
    unsigned native[32], keys[32];
    size_t count = 0, length = 0;
    puts("PAPO_NATIVE_READY");
    fflush(stdout);
    ULONGLONG until = GetTickCount64() + 5000;
    while (count < 32 && GetTickCount64() < until) {
        INPUT_RECORD event;
        DWORD read;
        if (WaitForSingleObject(input, 20) != WAIT_OBJECT_0)
            continue;
        if (!ReadConsoleInputW(input, &event, 1, &read) || !read)
            break;
        if (event.EventType != KEY_EVENT || !event.Event.KeyEvent.bKeyDown)
            continue;
        unsigned key = event.Event.KeyEvent.uChar.UnicodeChar;
        if (key == '\r')
            break;
        if (key)
            native[count++] = key;
    }
    if (!initscr()) {
        terminal_recovery_end();
        return 1;
    }
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    text_input_init();
    timeout(20);
    printw("PAPO_CURSES_READY");
    refresh();
    until = GetTickCount64() + 5000;
    while (length < 32 && GetTickCount64() < until) {
        uint32_t key;
        int type = text_read_raw(&key);
        if (type == ERR)
            continue;
        if (type == OK && key == 27)
            break;
        if (type == OK)
            keys[length++] = key;
    }
    /* Queue coalesced records explicitly so repeat handling is exercised even
     * when ConPTY chooses to send adjacent equal keys as separate events. */
    INPUT_RECORD repeated[] = {
        {.EventType = KEY_EVENT,
         .Event.KeyEvent = {.bKeyDown = TRUE, .wRepeatCount = 3, .uChar.UnicodeChar = 'a'}},
        {.EventType = KEY_EVENT,
         .Event.KeyEvent = {.bKeyDown = TRUE, .wRepeatCount = 2, .uChar.UnicodeChar = 0xe9}},
        {.EventType = KEY_EVENT,
         .Event.KeyEvent = {.bKeyDown = TRUE, .wRepeatCount = 1, .uChar.UnicodeChar = 'z'}}};
    unsigned repeats[6] = {0};
    DWORD written;
    DWORD repeat_mode = 0;
    /* With VT enabled, Windows converts WriteConsoleInput key records to VT
     * before queuing them, dropping their native repeat counts. Test native
     * records in native mode, and actual VT stream repeats in the next phase. */
    bool queued = GetConsoleMode(input, &repeat_mode) &&
                  SetConsoleMode(input, repeat_mode & ~ENABLE_VIRTUAL_TERMINAL_INPUT) &&
                  WriteConsoleInputW(input, repeated, 3, &written) && written == 3;
    size_t repeat_count = 0;
    until = GetTickCount64() + 5000;
    while (queued && repeat_count < 6 && GetTickCount64() < until) {
        uint32_t key;
        int type = text_read_raw(&key);
        if (type != ERR)
            repeats[repeat_count++] = key;
    }
    SetConsoleMode(input, repeat_mode);
    unsigned commands[16];
    size_t command_count = 0;
    printw("\nPAPO_COMMANDS_READY");
    refresh();
    until = GetTickCount64() + 5000;
    while (command_count < 16 && GetTickCount64() < until) {
        uint32_t key;
        int type = text_read_raw(&key);
        if (type == ERR)
            continue;
        if (type == OK && key == 4)
            break;
        commands[command_count++] = type == KEY_CODE_YES && key == KEY_UP             ? 0x110000
                                    : type == KEY_CODE_YES && key == TEXT_PASTE_START ? 0x110001
                                    : type == KEY_CODE_YES && key == TEXT_PASTE_END   ? 0x110002
                                    : key == '\r'                                     ? '\n'
                                                                                      : key;
    }
    DWORD flags = 0;
    GetConsoleMode(input, &flags);
    endwin();
    terminal_recovery_end();
    printf("\nPAPO_PROBE_RESULT={\"native\":[");
    for (size_t i = 0; i < count; ++i)
        printf("%s%u", i ? "," : "", native[i]);
    printf("],\"curses\":[");
    for (size_t i = 0; i < length; ++i)
        printf("%s%u", i ? "," : "", keys[i]);
    printf("],\"repeats\":[");
    for (size_t i = 0; i < repeat_count; ++i)
        printf("%s%u", i ? "," : "", repeats[i]);
    printf("],\"commands\":[");
    for (size_t i = 0; i < command_count; ++i)
        printf("%s%u", i ? "," : "", commands[i]);
    printf("],\"vt_input\":%d,\"iswprint_e_acute\":%d}\n",
           !!(flags & ENABLE_VIRTUAL_TERMINAL_INPUT), !!iswprint(0xe9));
    return 0;
}
