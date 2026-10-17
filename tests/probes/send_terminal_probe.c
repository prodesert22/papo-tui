#define _POSIX_C_SOURCE 200809L
#include "net/clock.h"
#include "ui/components/theme.h"
#include "ui/input/editor.h"
#include <curl/curl.h>
#include <locale.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wctype.h>

#define PASTE_START (KEY_MAX + 1)
#define PASTE_END (KEY_MAX + 2)
static volatile sig_atomic_t stopped;
static void stop_signal(int number)
{
    (void)number;
    stopped = 1;
}
static const char *setting(const char *name, const char *fallback)
{
    const char *value = getenv(name);
    return value ? value : fallback;
}

int main(int argc, char **argv)
{
    if (argc < 2 || !isatty(STDIN_FILENO) || !setlocale(LC_ALL, "") ||
        curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
        return 1;
    auth_client *session = auth_create();
    if (!session)
        return 1;
    bool allow = argc > 2 && !strcmp(argv[2], "--allow-local-http");
    auth_start_with_policy(session, argv[1], setting("PAPO_PROBE_USERNAME", "josé"),
                           setting("PAPO_PROBE_PASSWORD", "Synthetic!123"),
                           setting("PAPO_PROBE_SERVER_PASSWORD", ""), allow);
    struct timespec pause = {.tv_nsec = 1000000};
    while (auth_busy(session)) {
        auth_poll(session);
        nanosleep(&pause, NULL);
    }
    if (!auth_has_session(session)) {
        auth_destroy(session);
        curl_global_cleanup();
        return 2;
    }
    uint64_t permission_start = 0, permission_now = 0;
    net_now_ms(&permission_start);
    do {
        auth_poll(session);
        g_usleep(1000);
        net_now_ms(&permission_now);
    } while (
        permissions_channel(auth_permissions(session),
                            setting("PAPO_SEND_CHANNEL", "00000000-0000-4000-8000-000000000003"),
                            PERMISSION_SEND_MESSAGES) == PERMISSION_UNKNOWN &&
        permission_now - permission_start < 10000 && auth_has_session(session));
    send_editor *editor = send_editor_create(session);
    const char *channels[] = {
        setting("PAPO_SEND_CHANNEL", "00000000-0000-4000-8000-000000000003"),
        setting("PAPO_SEND_SECOND_CHANNEL", "00000000-0000-4000-8000-000000000004")};
    if (!editor || !send_editor_select(editor, channels[0]))
        return 2;
    int selected = 0;
    bool paste = false, cr = false, confirm = false, quitting = false;
    const char *notice = "Enter envia · Tab troca canal · Ctrl+N quebra linha · Ctrl+Q sai";
    char confirmed[TEXT_UTF8_BYTES] = "";
    if (!initscr())
        return 2;
    raw();
    nonl();
    noecho();
    keypad(stdscr, true);
    timeout(20);
    set_escdelay(25);
    mouseinterval(0);
    mousemask(BUTTON1_PRESSED | BUTTON1_CLICKED, NULL);
    define_key("\033[200~", PASTE_START);
    define_key("\033[201~", PASTE_END);
    fputs("\033[?2004h", stdout);
    fflush(stdout);
    ui_theme theme;
    theme_init(&theme);
    struct sigaction action = {0}, previous_int, previous_term;
    action.sa_handler = stop_signal;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, &previous_int);
    sigaction(SIGTERM, &action, &previous_term);
    while (!quitting && !stopped) {
        auth_poll(session);
        send_editor_poll(editor);
        outbox_operation operation;
        json_t *message;
        while ((message = outbox_take_confirmed(send_editor_outbox(editor), &operation))) {
            if (!strcmp(operation.channel_id, channels[selected])) {
                const char *body = json_string_value(json_object_get(message, "content"));
                if (body)
                    snprintf(confirmed, sizeof(confirmed), "%s", body);
            }
            json_decref(message);
        }
        erase();
        curs_set(0);
        text_label(stdscr, 1, 2, COLS - 4, "Validação do envio de texto");
        text_label(stdscr, 3, 2, COLS - 4, selected ? "Canal B" : "Canal A");
        text_label(stdscr, 5, 2, COLS - 4, "Última confirmação:");
        text_label(stdscr, 6, 2, COLS - 4, confirmed);
        text_label(stdscr, 8, 2, COLS - 4, "Mensagem:");
        text_buffer *buffer = send_editor_input(editor);
        text_layout layout;
        if (buffer && LINES >= 14 && COLS >= 44) {
            text_measure(buffer, COLS - 4, &layout);
            text_keep_cursor(buffer, &layout, LINES - 14);
            text_draw(stdscr, buffer, &layout, buffer->first_row, LINES - 14, 9, 2);
        }
        text_label(stdscr, LINES - 4, 2, COLS - 4, send_editor_notice(editor));
        text_label(stdscr, LINES - 2, 2, COLS - 4, notice);
        if (confirm) {
            attron(COLOR_PAIR(THEME_ERROR));
            text_label(stdscr, LINES - 7, 2, COLS - 4, "Pode já existir. Reenviar pode duplicar.");
            text_label(stdscr, LINES - 6, 2, COLS - 4, "Enter: reenviar · Esc: cancelar");
            attroff(COLOR_PAIR(THEME_ERROR));
        } else if (buffer && LINES >= 14 && COLS >= 44) {
            move(9 + layout.cursor_row - buffer->first_row, 2 + layout.cursor_column);
            curs_set(1);
        }
        refresh();
        uint32_t key;
        int type = text_read_raw(&key);
        if (type == ERR)
            continue;
        if (type == KEY_CODE_YES && key == KEY_RESIZE)
            continue;
        if (type == KEY_CODE_YES && key == PASTE_START) {
            paste = true;
            cr = false;
            continue;
        }
        if (type == KEY_CODE_YES && key == PASTE_END) {
            paste = false;
            continue;
        }
        if (!paste && type == OK && (key == 17 || key == 3)) {
            quitting = true;
            continue;
        }
        bool enter = type == OK && (key == '\n' || key == '\r');
        if (confirm) {
            if (!paste && enter) {
                send_editor_submit(editor, true);
                confirm = false;
            } else if (!paste && type == OK && key == 27)
                confirm = false;
            continue;
        }
        if (!paste && type == OK && key == '\t') {
            if (send_editor_select(editor, channels[1 - selected])) {
                selected = 1 - selected;
                confirmed[0] = '\0';
            }
            continue;
        }
        if (!paste && enter) {
            confirm = send_editor_submit(editor, false) == OUTBOX_CONFIRM_DUPLICATION;
            continue;
        }
        if (!buffer || LINES < 14 || COLS < 44)
            continue;
        if (paste) {
            if (key == '\n' && cr) {
                cr = false;
                continue;
            }
            cr = key == '\r';
            if (type == OK)
                text_insert(buffer, key, true);
        } else if (type == KEY_CODE_YES) {
            switch (key) {
            case KEY_LEFT:
                text_horizontal(buffer, false);
                break;
            case KEY_RIGHT:
                text_horizontal(buffer, true);
                break;
            case KEY_UP:
                text_vertical(buffer, COLS - 4, -1);
                break;
            case KEY_DOWN:
                text_vertical(buffer, COLS - 4, 1);
                break;
            case KEY_HOME:
                text_line_edge(buffer, COLS - 4, false);
                break;
            case KEY_END:
                text_line_edge(buffer, COLS - 4, true);
                break;
            case KEY_BACKSPACE:
                text_delete(buffer, true);
                break;
            case KEY_DC:
                text_delete(buffer, false);
                break;
            case KEY_MOUSE: {
                MEVENT event;
                if (getmouse(&event) == OK &&
                    (event.bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED)) && event.y >= 9 &&
                    event.y < LINES - 5)
                    text_click(buffer, COLS - 4, buffer->first_row + event.y - 9,
                               event.x > 2 ? event.x - 2 : 0);
                break;
            }
            default:
                break;
            }
        } else if (key == 21)
            text_clear(buffer);
        else if (key == 127 || key == 8)
            text_delete(buffer, true);
        else if (key == 14)
            text_insert(buffer, '\n', true);
        else if (iswprint(key))
            text_insert(buffer, key, true);
        send_editor_changed(editor);
    }
    send_editor_destroy(editor);
    fputs("\033[?2004l", stdout);
    fflush(stdout);
    theme_restore(&theme);
    mousemask(0, NULL);
    endwin();
    sigaction(SIGINT, &previous_int, NULL);
    sigaction(SIGTERM, &previous_term, NULL);
    auth_destroy(session);
    curl_global_cleanup();
    return 0;
}
