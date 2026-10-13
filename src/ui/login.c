#define _POSIX_C_SOURCE 200809L
#include "ui/login.h"
#include "ui/theme.h"
#include "net/auth.h"

#include <form.h>
#include <ncurses.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

#define FIELD_COUNT 4
#define CARD_HEIGHT 22
#define MIN_COLS 44
#define KEY_PASTE_START (KEY_MAX + 1)
#define KEY_PASTE_END (KEY_MAX + 2)

typedef struct {
    WINDOW *card;
    WINDOW *header;
    WINDOW *body;
    WINDOW *footer;
    WINDOW *input;
    FORM *form;
    FIELD *fields[FIELD_COUNT + 1];
    char saved[FIELD_COUNT][8193];
    int used_columns[FIELD_COUNT];
    int focus;
    int width;
    bool paste;
} login_ui;

static volatile sig_atomic_t interrupted;
static void on_signal(int signal_number) { (void)signal_number; interrupted = 1; }

static void erase_text(char *value, size_t length)
{
    volatile unsigned char *p = (volatile unsigned char *)value;
    while (length--) *p++ = 0;
}

static void snapshot(login_ui *ui)
{
    if (!ui->form) return;
    form_driver(ui->form, REQ_VALIDATION);
    for (int i = 0; i < FIELD_COUNT; ++i) {
        const char *buffer = field_buffer(ui->fields[i], 0);
        /* formw pads buffers with spaces. Keep typed trailing spaces distinct
         * from padding, particularly for passwords. Editing remains in formw. */
        mbstate_t state = {0};
        size_t length = 0;
        int columns = 0;
        while (buffer[length]) {
            wchar_t character;
            size_t bytes = mbrtowc(&character, buffer + length, MB_CUR_MAX, &state);
            if (!bytes || bytes == (size_t)-1 || bytes == (size_t)-2) break;
            int width = wcwidth(character);
            if (width < 0 || columns + width > ui->used_columns[i] ||
                (width && columns == ui->used_columns[i])) break;
            length += bytes;
            columns += width;
        }
        if (length >= sizeof(ui->saved[i])) length = sizeof(ui->saved[i]) - 1;
        erase_text(ui->saved[i], sizeof(ui->saved[i]));
        memcpy(ui->saved[i], buffer, length);
    }
}

static void destroy_layout(login_ui *ui)
{
    if (ui->form) {
        unpost_form(ui->form);
        free_form(ui->form);
        ui->form = NULL;
    }
    for (int i = 0; i < FIELD_COUNT; ++i) {
        if (ui->fields[i]) {
            set_field_buffer(ui->fields[i], 0, "");
            free_field(ui->fields[i]);
            ui->fields[i] = NULL;
        }
    }
    if (ui->input) delwin(ui->input);
    if (ui->footer) delwin(ui->footer);
    if (ui->body) delwin(ui->body);
    if (ui->header) delwin(ui->header);
    if (ui->card) delwin(ui->card);
    ui->input = ui->footer = ui->body = ui->header = ui->card = NULL;
}

static bool layout(login_ui *ui)
{
    curs_set(0);
    snapshot(ui);
    destroy_layout(ui);
    erase();
    refresh();
    if (LINES < CARD_HEIGHT || COLS < MIN_COLS) return true;
    ui->width = COLS >= 70 ? 64 : COLS - 4;
    ui->card = newwin(CARD_HEIGHT, ui->width, (LINES - CARD_HEIGHT) / 2, (COLS - ui->width) / 2);
    if (!ui->card) return false;
    wbkgd(ui->card, COLOR_PAIR(NORMAL));
    /* Keep the prototype's three terminal regions separate: identity, form,
     * and feedback/controls. The form remains a subwindow of the outer card. */
    ui->header = derwin(ui->card, 3, ui->width - 4, 1, 2);
    ui->body = derwin(ui->card, 12, ui->width - 4, 4, 2);
    ui->footer = derwin(ui->card, 5, ui->width - 4, 16, 2);
    if (!ui->header || !ui->body || !ui->footer) return false;
    wbkgd(ui->header, COLOR_PAIR(NORMAL));
    wbkgd(ui->body, COLOR_PAIR(NORMAL));
    wbkgd(ui->footer, COLOR_PAIR(NORMAL));
    ui->input = derwin(ui->card, 10, ui->width - 8, 5, 4);
    if (!ui->input) return false;
    const int limits[FIELD_COUNT] = {2048, 256, 1024, 1024};
    for (int i = 0; i < FIELD_COUNT; ++i) {
        ui->fields[i] = new_field(1, ui->width - 8, i * 3, 0, 0, 0);
        if (!ui->fields[i]) return false;
        field_opts_off(ui->fields[i], O_AUTOSKIP | O_STATIC | O_BLANK | O_WRAP);
        field_opts_on(ui->fields[i], O_NO_LEFT_STRIP);
        if (i >= 2) field_opts_off(ui->fields[i], O_PUBLIC);
        set_max_field(ui->fields[i], limits[i]);
        set_field_buffer(ui->fields[i], 0, ui->saved[i]);
        set_field_fore(ui->fields[i], COLOR_PAIR(NORMAL));
        set_field_back(ui->fields[i], COLOR_PAIR(INPUT));
    }
    ui->form = new_form(ui->fields);
    if (!ui->form) return false;
    /* Backspace must never become navigation at the beginning of a field. */
    form_opts_off(ui->form, O_BS_OVERLOAD | O_NL_OVERLOAD);
    set_form_win(ui->form, ui->card);
    set_form_sub(ui->form, ui->input);
    if (post_form(ui->form) != E_OK) return false;
    form_driver(ui->form, REQ_INS_MODE);
    set_current_field(ui->form, ui->fields[ui->focus < FIELD_COUNT ? ui->focus : 0]);
    form_driver(ui->form, REQ_BEG_FIELD);
    while (ui->form->curcol < ui->used_columns[ui->focus < FIELD_COUNT ? ui->focus : 0])
        if (form_driver(ui->form, REQ_RIGHT_CHAR) != E_OK) break;
    return true;
}

/* Limit by display cells and discard controls, including remote terminal escapes. */
static void text(WINDOW *window, int y, int x, int width, const char *value)
{
    mbstate_t state = {0};
    int cells = 0;
    while (*value && cells < width) {
        wchar_t character;
        size_t bytes = mbrtowc(&character, value, MB_CUR_MAX, &state);
        if (bytes == (size_t)-1 || bytes == (size_t)-2) break;
        if (!bytes) break;
        value += bytes;
        if (!iswprint(character)) continue;
        int character_width = wcwidth(character);
        if (character_width < 0 || cells + character_width > width) break;
        wchar_t output[] = {character, 0};
        mvwaddwstr(window, y, x + cells, output);
        cells += character_width;
    }
}

static void draw(login_ui *ui, auth_client *client)
{
    /* Painting positions must not appear as the focused input's caret. */
    curs_set(0);
    if (!ui->card) {
        erase();
        text(stdscr, 0, 0, COLS, "Amplie o terminal para 44 colunas e 22 linhas.");
        if (LINES > 1) text(stdscr, 1, 0, COLS, "Ctrl+Q: sair");
        curs_set(0);
        refresh();
        return;
    }
    snapshot(ui);
    const bool busy = auth_busy(client);
    const bool ready = auth_get_state(client) == AUTH_READY;
    const char *labels[FIELD_COUNT] = {"URL do servidor", "Usuário (username)", "Senha (password)", "Senha do servidor"};
    wattrset(ui->card, COLOR_PAIR(BORDER));
    box(ui->card, 0, 0);
    wattrset(ui->header, COLOR_PAIR(ACCENT) | A_BOLD);
    mvwaddstr(ui->header, 0, 2, "papo");
    wattrset(ui->header, COLOR_PAIR(NORMAL));
    text(ui->header, 1, 2, ui->width - 8, "Entre na sua comunidade");
    wattrset(ui->header, COLOR_PAIR(BORDER));
    mvwhline(ui->header, 2, 0, ACS_HLINE, ui->width - 4);

    for (int i = 0; i < FIELD_COUNT; ++i) {
        bool selected = ui->focus == i && !busy && !ready;
        chtype attributes = COLOR_PAIR(selected ? FOCUSED : INPUT);
        if (field_back(ui->fields[i]) != attributes) set_field_back(ui->fields[i], attributes);
        if (field_fore(ui->fields[i]) != attributes) set_field_fore(ui->fields[i], attributes);
        wattrset(ui->body, COLOR_PAIR(selected ? ACCENT : MUTED));
        text(ui->body, 3 * i, 2, ui->width - 8, labels[i]);
        if (i >= 2) {
            wattrset(ui->body, COLOR_PAIR(selected ? FOCUSED : INPUT));
            mvwhline(ui->body, 1 + 3 * i, 2, ' ', ui->width - 8);
            if (*ui->saved[i]) {
                /* Private fields are never made public: overlay only a mask. */
                size_t count = 0;
                for (const unsigned char *p = (const unsigned char *)ui->saved[i]; *p; ++p)
                    if ((*p & 0xc0) != 0x80) ++count;
                int shown = count < (size_t)(ui->width - 9) ? (int)count : ui->width - 9;
                mvwhline(ui->body, 1 + 3 * i, 2, '*', shown);
            }
        }
    }
    wattrset(ui->body, COLOR_PAIR(MUTED));
    text(ui->body, 11, 2, ui->width - 8, "Preencha a senha do servidor se a instância for privada.");

    wattrset(ui->footer, COLOR_PAIR(BORDER));
    mvwhline(ui->footer, 0, 0, ACS_HLINE, ui->width - 4);
    wattrset(ui->footer, COLOR_PAIR(ui->focus == FIELD_COUNT ? ACCENT : NORMAL) | A_BOLD);
    mvwhline(ui->footer, 1, 2, ' ', ui->width - 8);
    const char *button = ready ? "[ Sair da conta ]" : busy ? "[ Conectando… ]" : "[ Entrar ]";
    mvwaddstr(ui->footer, 1, 2, button);
    int status_color = auth_get_state(client) == AUTH_ERROR || auth_ws_state(client) == WS_ERROR ? ERROR : ready ? SUCCESS : MUTED;
    wattrset(ui->footer, COLOR_PAIR(status_color));
    mvwhline(ui->footer, 2, 2, ' ', ui->width - 8);
    mvwhline(ui->footer, 3, 2, ' ', ui->width - 8);
    text(ui->footer, 2, 2, ui->width - 8, auth_message(client));
    if (ready) {
        char summary[300];
        snprintf(summary, sizeof(summary), "%s · %s", auth_username(client), auth_server_name(client));
        text(ui->footer, 3, 2, ui->width - 8, summary);
    }
    wattrset(ui->footer, COLOR_PAIR(MUTED));
    text(ui->footer, 4, 0, ui->width - 4, "Tab: foco  Enter: entrar  Ctrl+Q: sair");
    if (ui->focus < FIELD_COUNT && !busy && !ready) {
        pos_form_cursor(ui->form);
    }
    touchwin(ui->card);
    wnoutrefresh(ui->card);
    wnoutrefresh(ui->header);
    wnoutrefresh(ui->body);
    wnoutrefresh(ui->footer);
    if (ui->focus < FIELD_COUNT && !busy && !ready) wnoutrefresh(ui->input);
    doupdate();
    curs_set(ui->focus < FIELD_COUNT && !busy && !ready ? 1 : 0);
}

static void select_focus(login_ui *ui, int selected)
{
    if (selected < FIELD_COUNT && set_current_field(ui->form, ui->fields[selected]) != E_OK) return;
    ui->focus = selected;
    if (ui->focus < FIELD_COUNT) {
        form_driver(ui->form, REQ_BEG_FIELD);
        while (ui->form->curcol < ui->used_columns[ui->focus])
            if (form_driver(ui->form, REQ_RIGHT_CHAR) != E_OK) break;
    }
}

static void focus(login_ui *ui, int direction)
{
    select_focus(ui, (ui->focus + direction + FIELD_COUNT + 1) % (FIELD_COUNT + 1));
}

static void submit(login_ui *ui, auth_client *client)
{
    if (auth_get_state(client) == AUTH_READY) auth_logout(client);
    else if (!auth_busy(client)) {
        snapshot(ui);
        auth_start(client, ui->saved[0], ui->saved[1], ui->saved[2], ui->saved[3]);
    }
}

static void click(login_ui *ui, auth_client *client)
{
    MEVENT event;
    if (getmouse(&event) != OK || ui->paste || !ui->card || !ui->form ||
        auth_busy(client) || !(event.bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED))) return;
    int y = event.y, x = event.x;
    if (!wmouse_trafo(ui->card, &y, &x, false)) return;
    bool ready = auth_get_state(client) == AUTH_READY;
    int button_width = ready ? (int)strlen("[ Sair da conta ]") : (int)strlen("[ Entrar ]");
    if (y == 17 && x >= 4 && x < 4 + button_width) {
        select_focus(ui, FIELD_COUNT);
        submit(ui, client);
        return;
    }
    if (ready || x < 4 || x >= ui->width - 4) return;
    for (int i = 0; i < FIELD_COUNT; ++i) {
        if (y != 4 + 3 * i && y != 5 + 3 * i) continue;
        if (set_current_field(ui->form, ui->fields[i]) != E_OK) return;
        ui->focus = i;
        /* Preserve the viewport's offset when clicking a scrolled input. */
        int target = y == 4 + 3 * i ? ui->used_columns[i] : ui->form->begincol + x - 4;
        if (target > ui->used_columns[i]) target = ui->used_columns[i];
        form_driver(ui->form, REQ_BEG_FIELD);
        while (ui->form->curcol < target)
            if (form_driver(ui->form, REQ_RIGHT_CHAR) != E_OK) break;
        return;
    }
}

static void edit_request(login_ui *ui, int request)
{
    int before = ui->form->curcol;
    if (request == REQ_DEL_PREV && before == 0) return;
    int removed = 1;
    if (request == REQ_DEL_CHAR) {
        snapshot(ui);
        mbstate_t state = {0};
        const char *p = ui->saved[ui->focus];
        int columns = 0;
        while (*p) {
            wchar_t character;
            size_t bytes = mbrtowc(&character, p, MB_CUR_MAX, &state);
            if (!bytes || bytes == (size_t)-1 || bytes == (size_t)-2) break;
            int width = wcwidth(character);
            if (columns >= before) { removed = width > 0 ? width : 1; break; }
            columns += width;
            p += bytes;
        }
    }
    if (form_driver(ui->form, request) != E_OK) return;
    int *end = &ui->used_columns[ui->focus];
    if (request == REQ_CLR_FIELD) *end = 0;
    else if (request == REQ_DEL_PREV && ui->form->curcol < *end) {
        int actual = before < *end ? before : *end;
        *end -= actual - ui->form->curcol;
    } else if (request == REQ_DEL_CHAR && before < *end) {
        *end -= removed < *end - before ? removed : *end - before;
    }
}

static void edit_character(login_ui *ui, wchar_t character)
{
    /* curcol is the absolute cursor column in ncurses' public FORM structure,
     * independent of horizontal scrolling. Only read it; formw owns editing. */
    int before = ui->form->curcol;
    int width = wcwidth(character);
    if (width < 0 || form_driver_w(ui->form, OK, character) != E_OK) return;
    int *end = &ui->used_columns[ui->focus];
    *end = before < *end ? *end + width : before + width;
}

static void clear_passwords(login_ui *ui)
{
    for (int i = 2; i < FIELD_COUNT; ++i) {
        erase_text(ui->saved[i], sizeof(ui->saved[i]));
        ui->used_columns[i] = 0;
        if (ui->fields[i]) set_field_buffer(ui->fields[i], 0, "");
    }
}

int login_run(void)
{
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        fputs("Abra o papo-tui em um terminal interativo.\n", stderr);
        return 1;
    }
    auth_client *client = auth_create();
    if (!client) {
        fputs("Não foi possível preparar a comunicação com o servidor.\n", stderr);
        return 1;
    }
    login_ui ui = {0};
    if (!initscr()) { auth_destroy(client); return 1; }
    /* raw also disables IXON, so Ctrl+Q reaches the application. */
    raw();
    noecho();
    keypad(stdscr, true);
    mouseinterval(0); /* Act on presses, without a delayed second click event. */
    mousemask(BUTTON1_PRESSED | BUTTON1_CLICKED, NULL);
    timeout(30);
    set_escdelay(25);
    ui_theme theme;
    theme_init(&theme);
    struct sigaction action = {0}, previous_int, previous_term;
    action.sa_handler = on_signal;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, &previous_int);
    sigaction(SIGTERM, &action, &previous_term);
    define_key("\033[200~", KEY_PASTE_START);
    define_key("\033[201~", KEY_PASTE_END);
    fputs("\033[?2004h", stdout);
    fflush(stdout);
    int result = layout(&ui) ? 0 : 1;
    bool redraw = true;
    while (!result && !interrupted) {
        auth_state before = auth_get_state(client);
        ws_state ws_before = auth_ws_state(client);
        auth_poll(client);
        if (before != auth_get_state(client)) redraw = true;
        if (ws_before != auth_ws_state(client)) redraw = true;
        json_t *event;
        while ((event = auth_next_event(client))) json_decref(event);
        if (before != AUTH_READY && auth_get_state(client) == AUTH_READY) {
            clear_passwords(&ui);
            ui.focus = FIELD_COUNT;
            auth_connect_ws(client);
        }
        if (redraw) {
            draw(&ui, client);
            redraw = false;
        }
        wint_t key;
        int type = get_wch(&key);
        if (type == ERR) continue;
        redraw = true;
        if (type == KEY_CODE_YES && key == KEY_RESIZE) {
            if (!layout(&ui)) result = 1;
            continue;
        }
        if (type == KEY_CODE_YES && key == KEY_PASTE_START) { ui.paste = true; continue; }
        if (type == KEY_CODE_YES && key == KEY_PASTE_END) { ui.paste = false; continue; }
        if (type == KEY_CODE_YES && key == KEY_MOUSE) {
            click(&ui, client);
            continue;
        }
        if (type == OK && !ui.paste && (key == 17 || key == 3)) break;
        if (!ui.form) continue;
        if (type == OK && !ui.paste && key == 27) {
            if (auth_busy(client)) { auth_cancel(client); clear_passwords(&ui); }
            continue;
        }
        if (!ui.paste && ((type == OK && key == '\t') || (type == KEY_CODE_YES && key == KEY_DOWN))) {
            focus(&ui, 1);
            continue;
        }
        if (!ui.paste && type == KEY_CODE_YES && (key == KEY_BTAB || key == KEY_UP)) {
            focus(&ui, -1);
            continue;
        }
        if (!ui.paste && ((type == OK && (key == '\n' || key == '\r')) ||
                         (type == KEY_CODE_YES && key == KEY_ENTER))) {
            submit(&ui, client);
            continue;
        }
        if (auth_busy(client) || auth_get_state(client) == AUTH_READY || ui.focus >= FIELD_COUNT) continue;
        if (type == KEY_CODE_YES) {
            int request = 0;
            switch (key) {
                case KEY_LEFT: request = REQ_LEFT_CHAR; break;
                case KEY_RIGHT: request = REQ_RIGHT_CHAR; break;
                case KEY_HOME: request = REQ_BEG_FIELD; break;
                case KEY_END: request = REQ_END_FIELD; break;
                case KEY_BACKSPACE: request = REQ_DEL_PREV; break;
                case KEY_DC: request = REQ_DEL_CHAR; break;
                default: break;
            }
            if (request && !ui.paste) edit_request(&ui, request);
        } else if (!ui.paste && (key == 127 || key == 8)) edit_request(&ui, REQ_DEL_PREV);
        else if (!ui.paste && key == 21) edit_request(&ui, REQ_CLR_FIELD);
        else if (iswprint(key)) edit_character(&ui, (wchar_t)key);
    }
    destroy_layout(&ui);
    erase_text((char *)ui.saved, sizeof(ui.saved));
    fputs("\033[?2004l", stdout);
    fflush(stdout);
    mousemask(0, NULL);
    theme_restore(&theme);
    endwin();
    sigaction(SIGINT, &previous_int, NULL);
    sigaction(SIGTERM, &previous_term, NULL);
    auth_destroy(client);
    if (result) fputs("Não foi possível criar a tela de login.\n", stderr);
    return result;
}
