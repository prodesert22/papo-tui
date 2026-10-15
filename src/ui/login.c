#define _POSIX_C_SOURCE 200809L
#include "ui/graphics.h"
#include "ui/mouse.h"
#include "ui/login.h"
#include "ui/theme.h"
#include "ui/button.h"
#include "app/session_store.h"
#include "ui/profiles.h"
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
#include "ui/history.h"
#include "ui/send_editor.h"
#include "ui/text.h"
#include "ui/clipboard.h"
#include "ui/terminal.h"

#define FIELD_COUNT 5
#define CONFIRM_FIELD 4
#define LOGIN_FIELD_COUNT 4
#define EXTRA_FOCUS (FIELD_COUNT + 1)
#define PROFILE_FOCUS (FIELD_COUNT + 2)
#define CHAT_FOCUS (FIELD_COUNT + 3)
#define FOCUS_COUNT (FIELD_COUNT + 4)
#define PROFILE_BUTTON "[Selecionar perfil]"
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
    FIELD *form_fields[FIELD_COUNT + 1];
    char saved[FIELD_COUNT][8193];
    int focus;
    int width;
    bool paste, values_ready, dragging;
    text_paste paste_data;
    text_buffer values[FIELD_COUNT]; /* Owned UTF-32; saved is the auth snapshot. */
    int scroll[FIELD_COUNT];
    bool open_chat;
    size_t previous_instance;
    bool profile_active;
    bool allow_local_http;
    bool server_password_error;
    bool registering, mode_changed, session_present, layout_failed;
    char validation_message[192];
    int validation_field;
    char profile_id[PROFILE_ID_BYTES], profile_name[PROFILE_NAME_BYTES];
} login_ui;

/* Keep transport field IDs stable; confirmation belongs only to the UI. */
static int field_row(const login_ui *ui, int field)
{
    if (!ui->registering) return field * 3;
    if (field == CONFIRM_FIELD) return 6;
    return field == 3 ? 8 : field * 2;
}

static int visible_field_count(const login_ui *ui) { return ui->registering ? FIELD_COUNT : LOGIN_FIELD_COUNT; }

static volatile sig_atomic_t interrupted;
/* Owned for this invocation of the login/application loop. */
static instance_connections *connections;
static void on_signal(int signal_number) { (void)signal_number; interrupted = 1; }

static void erase_text(char *value, size_t length)
{
    volatile unsigned char *p = (volatile unsigned char *)value;
    while (length--) *p++ = 0;
}

static void snapshot(login_ui *ui)
{
    if (!ui->values_ready) {
        for (int i = 0; i < FIELD_COUNT; ++i) text_decode(&ui->values[i], ui->saved[i]);
        ui->values_ready = true;
    }
    for (int i = 0; i < FIELD_COUNT; ++i) {
        erase_text(ui->saved[i], sizeof(ui->saved[i]));
        text_encode(&ui->values[i], ui->saved[i]);
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
    memset(ui->form_fields, 0, sizeof(ui->form_fields));
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
    const int limits[FIELD_COUNT] = {2048, 256, 1024, 1024, 1024};
    memset(ui->form_fields, 0, sizeof(ui->form_fields));
    const int order[FIELD_COUNT] = {0, 1, 2, CONFIRM_FIELD, 3};
    for (int position = 0; position < visible_field_count(ui); ++position) {
        int i = ui->registering ? order[position] : position;
        ui->fields[i] = new_field(1, ui->width - 8, field_row(ui, i), 0, 0, 0);
        ui->form_fields[position] = ui->fields[i];
        if (!ui->fields[i]) return false;
        field_opts_off(ui->fields[i], O_AUTOSKIP | O_STATIC | O_BLANK | O_WRAP);
        field_opts_on(ui->fields[i], O_NO_LEFT_STRIP);
        if (i >= 2) field_opts_off(ui->fields[i], O_PUBLIC);
        if (i == 0 && ui->profile_active) field_opts_off(ui->fields[i], O_EDIT);
        set_max_field(ui->fields[i], limits[i]);
        set_field_buffer(ui->fields[i], 0, ui->saved[i]);
        set_field_fore(ui->fields[i], COLOR_PAIR(NORMAL));
        set_field_back(ui->fields[i], COLOR_PAIR(INPUT));
    }
    ui->form = new_form(ui->form_fields);
    if (!ui->form) return false;
    /* Backspace must never become navigation at the beginning of a field. */
    form_opts_off(ui->form, O_BS_OVERLOAD | O_NL_OVERLOAD);
    set_form_win(ui->form, ui->card);
    set_form_sub(ui->form, ui->input);
    if (post_form(ui->form) != E_OK) return false;
    form_driver(ui->form, REQ_INS_MODE);
    set_current_field(ui->form, ui->fields[ui->focus < FIELD_COUNT ? ui->focus : 0]);

    return true;
}

/* Limit by display cells and discard controls, including remote terminal escapes. */
static const char *text(WINDOW *window, int y, int x, int width, const char *value)
{
    mbstate_t state = {0};
    int cells = 0;
    while (*value && cells < width) {
        wchar_t character;
        size_t bytes = mbrtowc(&character, value, MB_CUR_MAX, &state);
        if (bytes == (size_t)-1 || bytes == (size_t)-2) break;
        if (!bytes) break;
        if (!iswprint(character)) { value += bytes; continue; }
        int character_width = wcwidth(character);
        if (character_width < 0 || cells + character_width > width) break;
        wchar_t output[] = {character, 0};
        mvwaddwstr(window, y, x + cells, output);
        cells += character_width;
        value += bytes;
    }
    return value;
}

static const char *primary_button(const login_ui *ui, auth_client *client)
{
    return auth_has_session(client) ? "[ Sair da conta ]" : auth_busy(client) ? "[ Conectando… ]" :
           ui->registering ? "[Criar conta]" : "[ Entrar ]";
}

static const char *extra_button(const login_ui *ui, auth_client *client)
{
    if (!auth_has_session(client)) return ui->registering ? "[Login]" : "[Cadastro]";
    ws_state state = auth_ws_state(client);
    return state == WS_ERROR || state == WS_CLOSED ? "[Reconectar]" : "[Renovar]";
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
    const bool ready = auth_has_session(client);
    const char *labels[FIELD_COUNT] = {"URL do servidor", "Usuário (username)", "Senha (password)", "Senha do servidor", "Confirmar senha"};
    wattrset(ui->card, COLOR_PAIR(BORDER));
    box(ui->card, 0, 0);
    wattrset(ui->header, COLOR_PAIR(NORMAL));
    mvwhline(ui->header, 0, 0, ' ', ui->width - 4);
    wattrset(ui->header, COLOR_PAIR(ACCENT) | A_BOLD);
    mvwaddstr(ui->header, 0, 2, "papo");
    button_draw(ui->header, 0, ui->width - 6 - (int)strlen(PROFILE_BUTTON), (int)strlen(PROFILE_BUTTON),
                PROFILE_BUTTON, ui->focus == PROFILE_FOCUS ? BUTTON_FOCUSED : BUTTON_NORMAL, BUTTON_CANVAS);
    wattrset(ui->header, COLOR_PAIR(NORMAL));
    mvwhline(ui->header, 1, 2, ' ', ui->width - 8);
    text(ui->header, 1, 2, ui->width - 8, ui->registering ? "Crie sua conta nesta instância" :
         ui->profile_active ? ui->profile_name : "Entre na sua comunidade");
    wattrset(ui->header, COLOR_PAIR(BORDER));
    mvwhline(ui->header, 2, 0, ACS_HLINE, ui->width - 4);

    for (int i = 0; i < FIELD_COUNT; ++i) {
        if (!ui->fields[i]) continue;
        bool selected = ui->focus == i && !busy && !ready;
        chtype attributes = COLOR_PAIR(selected ? FOCUSED : INPUT);
        if (field_back(ui->fields[i]) != attributes) set_field_back(ui->fields[i], attributes);
        if (field_fore(ui->fields[i]) != attributes) set_field_fore(ui->fields[i], attributes);
        wattrset(ui->body, COLOR_PAIR((i == 3 && ui->server_password_error) || (*ui->validation_message && i == ui->validation_field) ? ERROR : selected ? ACCENT : MUTED));
        text(ui->body, field_row(ui, i), 2, ui->width - 8, labels[i]);

    }
    wattrset(ui->body, COLOR_PAIR(MUTED));
    mvwhline(ui->body, 11, 1, ' ', ui->width - 6);
    /* The private-server error frame follows its input in either mode. */
    int server_row = field_row(ui, 3);
    wattrset(ui->body, COLOR_PAIR(NORMAL));
    mvwaddch(ui->body, server_row + 1, 1, ' ');
    mvwaddch(ui->body, server_row + 1, ui->width - 6, ' ');
    if (ui->server_password_error) {
        wattrset(ui->body, COLOR_PAIR(ERROR));
        mvwhline(ui->body, server_row, 2, ACS_HLINE, ui->width - 8);
        mvwhline(ui->body, server_row + 2, 2, ACS_HLINE, ui->width - 8);
        mvwaddch(ui->body, server_row, 1, ACS_ULCORNER);
        mvwaddch(ui->body, server_row, ui->width - 6, ACS_URCORNER);
        mvwaddch(ui->body, server_row + 2, 1, ACS_LLCORNER);
        mvwaddch(ui->body, server_row + 2, ui->width - 6, ACS_LRCORNER);
        mvwaddch(ui->body, server_row + 1, 1, ACS_VLINE);
        mvwaddch(ui->body, server_row + 1, ui->width - 6, ACS_VLINE);
        text(ui->body, server_row, 3, ui->width - 10, "Senha do servidor *");
    } else {
        /* Clear the previous error frame before restoring the normal label. */
        wattrset(ui->body, COLOR_PAIR(ui->focus == 3 && !busy && !ready ? ACCENT : MUTED));
        mvwhline(ui->body, server_row, 1, ' ', ui->width - 6);
        mvwhline(ui->body, server_row + 2, 1, ' ', ui->width - 6);
        text(ui->body, server_row, 2, ui->width - 8, labels[3]);
        wattrset(ui->body, COLOR_PAIR(MUTED));
        if (ready) {
            char summary[300];
            snprintf(summary, sizeof(summary), "%s · %s", auth_username(client), auth_server_name(client));
            wattrset(ui->body, COLOR_PAIR(NORMAL));
            text(ui->body, 11, 2, ui->width - 8, summary);
        } else text(ui->body, 11, 2, ui->width - 8, ui->registering ?
             "Senha: 8+ caracteres, maiúscula e símbolo." : "Preencha a senha do servidor se a instância for privada.");
    }

    wattrset(ui->footer, COLOR_PAIR(BORDER));
    mvwhline(ui->footer, 0, 0, ACS_HLINE, ui->width - 4);
    wattrset(ui->footer, COLOR_PAIR(NORMAL));
    mvwhline(ui->footer, 1, 2, ' ', ui->width - 8);
    const char *button = primary_button(ui, client);
    button_draw(ui->footer, 1, 2, ui->width - 8, button,
                ui->focus == FIELD_COUNT ? BUTTON_FOCUSED : BUTTON_NORMAL, BUTTON_CANVAS);
    if (!busy) {
        int column = 2 + (int)strlen(button) + 2;
        button_draw(ui->footer, 1, column, ui->width - 6 - column, extra_button(ui, client),
                    ui->focus == EXTRA_FOCUS ? BUTTON_FOCUSED : BUTTON_NORMAL, BUTTON_CANVAS);
    }
    int status_color = *ui->validation_message || auth_get_state(client) == AUTH_ERROR || auth_ws_state(client) == WS_ERROR ? ERROR :
                       ready || auth_get_state(client) == AUTH_REGISTERED ? SUCCESS : MUTED;
    wattrset(ui->footer, COLOR_PAIR(NORMAL));
    mvwhline(ui->footer, 2, 2, ' ', ui->width - 8);
    mvwhline(ui->footer, 3, 2, ' ', ui->width - 8);
    wattrset(ui->footer, COLOR_PAIR(status_color));
    const char *message = *ui->validation_message ? ui->validation_message :
        ui->mode_changed && auth_get_state(client) == AUTH_IDLE ?
        ui->registering ? "Preencha os dados para criar sua conta." : "Preencha os dados para entrar." : auth_message(client);
    char status_message[384];
    if (clipboard_notice()) {
        snprintf(status_message, sizeof(status_message), "%s | %s", message, clipboard_notice());
        message = status_message;
    }
    const char *remaining = text(ui->footer, 2, 2, ui->width - 8, message);
    if (*remaining) {
        const char *space = NULL;
        for (const char *p = message; p < remaining; ++p) if (*p == ' ') space = p;
        if (space && space > message) {
            char line[256];
            size_t length = (size_t)(space - message);
            if (length < sizeof(line)) {
                memcpy(line, message, length);
                line[length] = '\0';
                mvwhline(ui->footer, 2, 2, ' ', ui->width - 8);
                text(ui->footer, 2, 2, ui->width - 8, line);
                remaining = space + 1;
            }
        }
    }
    while (*remaining == ' ') ++remaining;
    text(ui->footer, 3, 2, ui->width - 8, remaining);
    if (!*ui->validation_message && auth_get_state(client) == AUTH_IDLE && session_store_restoring(connections->sessions))
        text(ui->footer, 2, 2, ui->width - 8, "Recuperando sessão… Esc: abrir login");
    else if (ready && !*remaining)
        text(ui->footer, 3, 2, ui->width - 8, session_store_notice(connections->sessions));
    wattrset(ui->footer, COLOR_PAIR(MUTED));
    if (ready) {
        button_draw(ui->footer, 4, 0, 13, "[Canais / F6]",
                    ui->focus == CHAT_FOCUS ? BUTTON_FOCUSED : BUTTON_NORMAL, BUTTON_CANVAS);
        text(ui->footer, 4, 13, ui->width - 17, "  Ctrl+R: sessão · Ctrl+P");
    } else text(ui->footer, 4, 0, ui->width - 4, ui->width >= 64 ?
         "Ctrl+N: cadastro · Ctrl+R: sessão · Ctrl+P: perfis · Ctrl+Q" : "Ctrl+N · Ctrl+R · Ctrl+P · Ctrl+Q");
    for (int i = 0; i < FIELD_COUNT; ++i) if (ui->fields[i]) {
        wattrset(ui->input, COLOR_PAIR(ui->focus == i && !busy && !ready ? FOCUSED : INPUT));
        mvwhline(ui->input, field_row(ui, i), 0, ' ', ui->width - 8);
        text_draw_field(ui->input, &ui->values[i], &ui->scroll[i], field_row(ui, i), 0, ui->width - 8, i >= 2);
    }
    if (ui->focus < FIELD_COUNT && !busy && !ready) {
        text_layout measured; text_measure(&ui->values[ui->focus], TEXT_LIMIT * 2 + 1, &measured);
        wmove(ui->input, field_row(ui, ui->focus), measured.cursor_column - ui->scroll[ui->focus]);
    }
    touchwin(ui->card);
    wnoutrefresh(ui->card);
    wnoutrefresh(ui->header);
    wnoutrefresh(ui->body);
    wnoutrefresh(ui->footer);
    wnoutrefresh(ui->input);
    doupdate();
    curs_set(ui->focus < FIELD_COUNT && !busy && !ready ? 1 : 0);
}

static void select_focus(login_ui *ui, int selected)
{
    if (ui->profile_active && selected == 0) selected = 1;
    if (selected < FIELD_COUNT && set_current_field(ui->form, ui->fields[selected]) != E_OK) return;
    if (selected != ui->focus && ui->focus < FIELD_COUNT) text_unselect(&ui->values[ui->focus]);
    ui->dragging = false; ui->focus = selected;
}

static bool focus_available(const login_ui *ui, auth_client *client, int selected)
{
    if (selected < FIELD_COUNT)
        return ui->fields[selected] && !auth_busy(client) && !auth_has_session(client) && !(selected == 0 && ui->profile_active);
    if (selected == EXTRA_FOCUS) return !auth_busy(client);
    if (selected == FIELD_COUNT) return !auth_busy(client) || auth_has_session(client);
    if (selected == CHAT_FOCUS) return auth_has_session(client);
    return selected == PROFILE_FOCUS;
}

/* Tab follows visual reading order; arrows follow rows and columns. */
static void focus(login_ui *ui, auth_client *client, int direction)
{
    const int order[FOCUS_COUNT] = {0, 1, 2, CONFIRM_FIELD, 3, FIELD_COUNT, EXTRA_FOCUS, CHAT_FOCUS, PROFILE_FOCUS};
    int position = 0;
    while (position < FOCUS_COUNT && order[position] != ui->focus) ++position;
    for (int i = 0; i < FOCUS_COUNT; ++i) {
        position = (position + direction + FOCUS_COUNT) % FOCUS_COUNT;
        if (focus_available(ui, client, order[position])) { select_focus(ui, order[position]); return; }
    }
}

static void focus_vertical(login_ui *ui, auth_client *client, int direction)
{
    int selected = ui->focus;
    if (selected < FIELD_COUNT) {
        if (!focus_available(ui, client, selected)) selected = PROFILE_FOCUS;
        else {
            const int order[FIELD_COUNT + 1] = {0, 1, 2, CONFIRM_FIELD, 3, FIELD_COUNT};
            int position = 0;
            while (position < FIELD_COUNT && order[position] != selected) ++position;
            for (int next = position + direction; next >= 0 && next <= FIELD_COUNT; next += direction) {
                if (focus_available(ui, client, order[next])) { selected = order[next]; break; }
            }
        }
    } else if (selected == PROFILE_FOCUS) {
        if (direction < 0 && focus_available(ui, client, FIELD_COUNT)) selected = FIELD_COUNT;
    } else if (direction > 0) selected = PROFILE_FOCUS;
    else if (focus_available(ui, client, 3)) selected = 3;
    if (focus_available(ui, client, selected)) select_focus(ui, selected);
}

static void focus_horizontal(login_ui *ui, auth_client *client, int direction)
{
    int selected = ui->focus;
    if (selected == FIELD_COUNT && direction > 0) selected = EXTRA_FOCUS;
    else if (selected == EXTRA_FOCUS && direction < 0) selected = FIELD_COUNT;
    if (focus_available(ui, client, selected)) select_focus(ui, selected);
}

static void apply_input_error(login_ui *ui, auth_client *client)
{
    auth_input_error error = auth_error_input(client);
    if (auth_get_state(client) != AUTH_ERROR || error == AUTH_INPUT_NONE) return;
    int field = (int)error - 1;
    if (field < 0 || field >= FIELD_COUNT) return;
    if (error == AUTH_INPUT_SERVER_PASSWORD) {
        ui->server_password_error = true;
        erase_text(ui->saved[3], sizeof(ui->saved[3]));
        erase_text((char *)&ui->values[3], sizeof(ui->values[3])); text_clear(&ui->values[3]);
        if (ui->fields[3]) set_field_buffer(ui->fields[3], 0, "");
    }
    ui->focus = field;
    if (ui->form) select_focus(ui, field);
}

static void submit(login_ui *ui, auth_client *client)
{
    if (auth_has_session(client)) auth_logout(client);
    else if (!auth_busy(client)) {
        snapshot(ui);
        ui->mode_changed = false;
        ui->server_password_error = false;
        ui->validation_message[0] = '\0';
        if (ui->registering) {
            auth_input_error error;
            if (!auth_validate_registration(ui->saved[1], ui->saved[2], &error,
                                            ui->validation_message, sizeof(ui->validation_message))) {
                ui->mode_changed = true;
                ui->validation_field = (int)error - 1;
                select_focus(ui, ui->validation_field);
                return;
            }
            if (!*ui->saved[CONFIRM_FIELD] || strcmp(ui->saved[2], ui->saved[CONFIRM_FIELD]) != 0) {
                snprintf(ui->validation_message, sizeof(ui->validation_message), "%s", !*ui->saved[CONFIRM_FIELD] ?
                         "Confirme sua senha." : "As senhas não coincidem.");
                ui->mode_changed = true;
                ui->validation_field = CONFIRM_FIELD;
                select_focus(ui, CONFIRM_FIELD);
                return;
            }
            auth_register_with_policy(client, ui->saved[0], ui->saved[1], ui->saved[2], ui->saved[3], ui->allow_local_http);
        } else auth_start_with_policy(client, ui->saved[0], ui->saved[1], ui->saved[2], ui->saved[3], ui->allow_local_http);
        apply_input_error(ui, client);
    }
}

static void extra_action(login_ui *ui, auth_client *client);

/* Return true when the profile selector was requested. */
static bool click_event(login_ui *ui, auth_client *client, MEVENT event)
{
    if (ui->paste || !ui->card || !ui->form) return false;
    if (ui->dragging && ui->focus < FIELD_COUNT && !auth_busy(client) && !auth_has_session(client)) {
        if (event.bstate & (BUTTON1_PRESSED | BUTTON1_RELEASED | REPORT_MOUSE_POSITION)) {
            int column = event.x - getbegx(ui->input);
            if (column < 0 && ui->scroll[ui->focus]) --ui->scroll[ui->focus];
            if (column >= getmaxx(ui->input)) ++ui->scroll[ui->focus];
            if (column < 0) column = 0;
            if (column >= getmaxx(ui->input)) column = getmaxx(ui->input) - 1;
            text_drag(&ui->values[ui->focus], TEXT_LIMIT * 2 + 1, 0, ui->scroll[ui->focus] + column);
            if (event.bstate & BUTTON1_RELEASED) ui->dragging = false;
            return false;
        }
    }
    if (!(event.bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED | BUTTON3_PRESSED | BUTTON3_CLICKED)) ||
        (event.bstate & REPORT_MOUSE_POSITION)) return false;
    int y = event.y, x = event.x;
    if (!wmouse_trafo(ui->card, &y, &x, false)) return false;
    if (event.bstate & (BUTTON3_PRESSED | BUTTON3_CLICKED)) {
        if (!auth_busy(client) && !auth_has_session(client) && x >= 4 && x < ui->width - 4)
            for (int i = 0; i < FIELD_COUNT; ++i)
                if (ui->fields[i] && y == 5 + field_row(ui, i)) { clipboard_copy(&ui->values[i]); break; }
        return false;
    }
    int profile_width = (int)strlen(PROFILE_BUTTON);
    int profile_x = ui->width - 4 - profile_width;
    if (y == 1 && x >= profile_x && x < profile_x + profile_width) {
        select_focus(ui, PROFILE_FOCUS);
        return true;
    }
    bool ready = auth_has_session(client);
    if (ready && y == 20 && x >= 2 && x < 15) {
        ui->open_chat = true;
        return true;
    }
    if (auth_busy(client) && !ready) return false;
    int button_width = (int)strlen(primary_button(ui, client));
    if (y == 17 && x >= 4 && x < 4 + button_width) {
        select_focus(ui, FIELD_COUNT);
        submit(ui, client);
        return false;
    }
    int extra_x = 4 + button_width + 2;
    if (!auth_busy(client) && y == 17 && x >= extra_x && x < extra_x + (int)strlen(extra_button(ui, client))) {
        select_focus(ui, EXTRA_FOCUS);
        extra_action(ui, client);
        return false;
    }
    if (ready || x < 4 || x >= ui->width - 4) return false;
    for (int i = 0; i < FIELD_COUNT; ++i) {
        if (!ui->fields[i] || (i == 0 && ui->profile_active)) continue;
        int row = field_row(ui, i);
        if (y != 4 + row && y != 5 + row) continue;
        select_focus(ui, i);
        if (y == 4 + row) text_edit(&ui->values[i], KEY_CODE_YES, TEXT_CTRL_END, TEXT_LIMIT * 2 + 1, false);
        else text_click(&ui->values[i], TEXT_LIMIT * 2 + 1, 0, ui->scroll[i] + x - 4);
        ui->dragging = (event.bstate & BUTTON1_PRESSED) != 0 && y == 5 + row;
        return false;
    }
    return false;
}

static bool click(login_ui *ui, auth_client *client)
{
    MEVENT events[64]; size_t count = ui_mouse_events(events, 64);
    for (size_t i = 0; i < count; ++i) if (click_event(ui, client, events[i])) return true;
    return false;
}

static void edit_field(login_ui *ui, int type, wint_t key)
{
    static const size_t limits[FIELD_COUNT] = {2048, 256, 1024, 1024, 1024};
    text_buffer *buffer = &ui->values[ui->focus], prior = *buffer;
    if (text_edit(buffer, type, key, TEXT_LIMIT * 2 + 1, false)) {
        if (buffer->length > limits[ui->focus]) *buffer = prior;
        else { ui->validation_message[0] = 0; if (ui->focus == 3) ui->server_password_error = false; }
    }
    erase_text((char *)&prior, sizeof(prior));
}

static void clear_passwords(login_ui *ui)
{
    ui->dragging = false;
    for (int i = 2; i < FIELD_COUNT; ++i) {
        erase_text(ui->saved[i], sizeof(ui->saved[i]));
        erase_text((char *)&ui->values[i], sizeof(ui->values[i])); text_clear(&ui->values[i]);
        ui->scroll[i] = 0;
        if (ui->fields[i]) set_field_buffer(ui->fields[i], 0, "");
    }
}

static void extra_action(login_ui *ui, auth_client *client)
{
    if (auth_busy(client)) return;
    if (auth_has_session(client)) {
        ws_state state = auth_ws_state(client);
        if (state == WS_ERROR || state == WS_CLOSED) auth_connect_ws(client);
        else auth_refresh(client);
        return;
    }
    snapshot(ui);
    clear_passwords(ui);
    auth_cancel(client);
    ui->validation_message[0] = '\0';
    ui->registering = !ui->registering;
    ui->mode_changed = true;
    ui->server_password_error = false;
    ui->focus = *ui->saved[0] ? 1 : 0;
    ui->layout_failed = !layout(ui);
    if (ui->form) select_focus(ui, ui->focus);
}

static int manage_profiles(login_ui *ui, auth_client **client, profile_config **config, bool allow_http, bool chooser)
{
    if (!*config) *config = profiles_create();
    if (!*config) return -1;
    /* Remove all transfers, events and cookies before entering another
     * profile context. Recreating the client leaves the global budget intact. */
    instances_detach(connections, *client);
    session_store_unwatch(connections->sessions, *client);
    auth_destroy(*client);
    *client = NULL;
    destroy_layout(ui);
    erase_text((char *)ui->saved, sizeof(ui->saved));
    erase_text((char *)ui->values, sizeof(ui->values)); ui->values_ready = false;
    memset(ui->scroll, 0, sizeof(ui->scroll)); text_paste_cancel(&ui->paste_data); ui->paste = ui->dragging = false;
    ui->server_password_error = false;
    ui->registering = ui->mode_changed = ui->session_present = false;
    ui->validation_message[0] = '\0';
    server_profile profile;
    int result = chooser ? profiles_choose(*config, allow_http, &interrupted, &profile) :
                           profiles_manage(*config, allow_http, &interrupted, &profile);
    if (result == 3) result = profiles_manage(*config, allow_http, &interrupted, &profile);
    if (result < 0 || result == 2) return result;
    const server_profile *active = result == 1 ? &profile : profiles_find(*config, ui->profile_id);
    if (active && !allow_http && strncmp(active->url, "http://", 7) == 0) active = NULL;
    ui->profile_active = active != NULL;
    ui->profile_id[0] = ui->profile_name[0] = '\0';
    if (active) {
        snprintf(ui->profile_id, sizeof(ui->profile_id), "%s", active->id);
        snprintf(ui->profile_name, sizeof(ui->profile_name), "%s", active->name);
        snprintf(ui->saved[0], sizeof(ui->saved[0]), "%s", active->url);
    }
    ui->focus = active ? 1 : 0;
    *client = auth_create();
    return *client && layout(ui) ? 0 : -1;
}

/* Returns 0 to the account screen, 1 to exit, or -1 on layout/profile failure. */
static bool instance_form(login_ui *ui, auth_client *client, const char *url)
{
    destroy_layout(ui);
    erase_text((char *)ui->saved, sizeof(ui->saved));
    erase_text((char *)ui->values, sizeof(ui->values)); ui->values_ready = false;
    memset(ui->scroll, 0, sizeof(ui->scroll)); text_paste_cancel(&ui->paste_data); ui->paste = ui->dragging = false;
    ui->profile_active = ui->registering = ui->mode_changed = ui->server_password_error = false;
    ui->profile_id[0] = ui->profile_name[0] = ui->validation_message[0] = '\0';
    ui->session_present = auth_has_session(client);
    ui->focus = url && *url ? 1 : 0;
    if (url) {
        snprintf(ui->saved[0], sizeof(ui->saved[0]), "%s", url);
    }
    return layout(ui);
}

static int open_history(login_ui *ui, auth_client **client, profile_config **config)
{
    ui->open_chat = false;
    if (!instances_attach(connections, *client)) return -1;
    int action;
    bool focus_tabs = true;
    for (;;) {
        size_t selected = SIZE_MAX;
        action = history_run(*client, connections, &selected, focus_tabs, &interrupted);
        if (action != HISTORY_SWITCH) break;
        if (selected >= connections->count) return -1;
        *client = connections->items[selected].auth;
        focus_tabs = true;
        if (!instance_form(ui, *client, connections->items[selected].url)) return -1;
        if (!auth_has_session(*client) && !auth_busy(*client)) return 0;
    }
    if (action == HISTORY_ADD) {
        if (connections->count == INSTANCE_LIMIT) {
            snprintf(ui->validation_message, sizeof(ui->validation_message), "Limite de 64 conexões abertas. Saia de uma conta antes de adicionar outra.");
            return layout(ui) ? 0 : -1;
        }
        auth_client *next = auth_create();
        if (!next) return -1;
        ui->previous_instance = instances_index(connections, *client);
        *client = next;
        return instance_form(ui, next, NULL) ? 0 : -1;
    }
    if (!layout(ui)) return -1;
    if (action == 1) return 1;
    if (action == 2) {
        int selected = manage_profiles(ui, client, config, ui->allow_local_http, true);
        if (selected < 0) return -1;
        if (selected == 2) return 1;
    }
    return 0;
}

int login_run_profiles(bool open_profiles, bool allow_local_http)
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
    login_ui ui = {.previous_instance = SIZE_MAX};
    connections = instances_create();
    if (!connections) { auth_destroy(client); return 1; }
    ui.allow_local_http = allow_local_http;
    profile_config *config = profiles_create();
    if (!config) { auth_destroy(client); instances_destroy(connections); connections = NULL; fputs("Não foi possível carregar os perfis locais.\n", stderr); return 1; }
    connections->sessions = session_store_create(config->directory, !open_profiles);
    interrupted = 0;
    if (!terminal_recovery_begin()) {
        auth_destroy(client); instances_destroy(connections); connections = NULL; profiles_destroy(config);
        fputs("Não foi possível preservar o estado do terminal.\n", stderr); return 1;
    }
    if (!initscr()) { terminal_recovery_end(); auth_destroy(client); instances_destroy(connections); connections = NULL; profiles_destroy(config); return 1; }
    /* raw also disables IXON, so Ctrl+Q reaches the application. */
    raw(); nonl();
    noecho();
    keypad(stdscr, true);
    mouseinterval(0); /* Act on presses, without a delayed second click event. */
    mousemask(BUTTON1_PRESSED | BUTTON1_RELEASED | BUTTON1_CLICKED | BUTTON3_PRESSED | BUTTON3_CLICKED | REPORT_MOUSE_POSITION, NULL);
    ui_mouse_drag_tracking(true);
    text_input_init();
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
    /* Accept cursor keys from terminals that retain normal cursor mode even
     * while keypad(stdscr, true) requests application mode. */
    define_key("\033[A", KEY_UP);
    define_key("\033[B", KEY_DOWN);
    define_key("\033[C", KEY_RIGHT);
    define_key("\033[D", KEY_LEFT);
    /* Keep the terminal's keyboard protocol unchanged. Enabling CSI-u would
     * require complete support for all keys, modifiers and event forms. */
    fputs("\033[?2004h", stdout);
    fflush(stdout);
    text_input_terminal_active(true);
    int result = layout(&ui) ? 0 : 1;
    bool startup_restore = !open_profiles;
    if (!result && !open_profiles && *session_store_last_url(connections->sessions) &&
        !instance_form(&ui, client, session_store_last_url(connections->sessions))) result = 1;
    bool quit = false;
    if (!result && open_profiles) {
        int selected = manage_profiles(&ui, &client, &config, allow_local_http, false);
        if (selected < 0) result = 1;
        else if (selected == 2) quit = true;
    }
    bool redraw = true;
    while (!result && !quit && !interrupted) {
        if (ui.layout_failed) { result = 1; break; }
        if (session_store_poll(connections->sessions, client)) redraw = true;
        if (startup_restore) {
            char *snapshot = session_store_take_resume(connections->sessions);
            if (snapshot) {
                auth_session_restore(client, snapshot, session_store_last_url(connections->sessions), allow_local_http);
                auth_session_free(&snapshot); startup_restore = false; redraw = true;
            } else if (!session_store_restoring(connections->sessions)) startup_restore = false;
        }
        if (instances_poll(connections, client)) redraw = true;
        auth_state before = auth_get_state(client);
        bool before_busy = auth_busy(client);
        bool before_session = auth_has_session(client);
        ws_state ws_before = auth_ws_state(client);
        auth_realtime_active(client, true);
        auth_poll(client);
        send_editor_poll(instances_editor(connections, client));
        if (before != auth_get_state(client) || before_busy != auth_busy(client)) redraw = true;
        if (before != AUTH_ERROR && auth_get_state(client) == AUTH_ERROR) apply_input_error(&ui, client);
        if (ws_before != auth_ws_state(client)) redraw = true;
        json_t *event;
        while ((event = auth_next_event(client))) { instances_media_event(connections, client, event); json_decref(event); }
        if (auth_response_ready(client)) json_decref(auth_take_response(client, NULL, NULL));
        if (!before_session && auth_has_session(client)) {
            /* Only a new login opens channels. Returning with F6 or verifying
             * an existing session after renewal must keep the account view. */
            if (!ui.session_present) ui.open_chat = true;
            ui.server_password_error = false;
            clear_passwords(&ui);
            ui.focus = FIELD_COUNT;
            auth_connect_ws(client);
        }
        if (before != AUTH_REGISTERED && auth_get_state(client) == AUTH_REGISTERED) {
            clear_passwords(&ui);
            ui.registering = ui.mode_changed = ui.server_password_error = false;
            ui.validation_message[0] = '\0';
            ui.focus = 2;
            if (!layout(&ui)) { result = 1; break; }
            if (ui.form) select_focus(&ui, 2);
        }
        if (auth_has_session(client)) ui.session_present = true;
        if (ui.session_present && !auth_has_session(client) && !auth_busy(client)) {
            if (auth_get_state(client) == AUTH_IDLE) instances_detach(connections, client);
            ui.session_present = false;
            redraw = true;
            clear_passwords(&ui);
            ui.focus = 2;
            if (ui.form) select_focus(&ui, 2);
        }
        if (clipboard_poll()) redraw = true;
        if (redraw) {
            draw(&ui, client);
            graphics_terminal_probe();
            redraw = false;
        }
        if (ui.open_chat && auth_has_session(client) && !ui.paste) {
            int action = open_history(&ui, &client, &config);
            if (action < 0) result = 1;
            else if (action == 1) quit = true;
            redraw = true;
            continue;
        }
        wint_t key;
        int type = ui.paste ? text_paste_read(&ui.paste_data, &key, false, graphics_terminal_input, 30) : graphics_terminal_input(&key);
        if (type == ERR) continue;
        if (startup_restore && !(type == KEY_CODE_YES && key == KEY_RESIZE)) {
            session_store_cancel_resume(connections->sessions); startup_restore = false;
        }
        redraw = true;
        if (type == KEY_CODE_YES && key == KEY_RESIZE) {
            if (!layout(&ui)) result = 1;
            continue;
        }
        if (type == KEY_CODE_YES && key == KEY_PASTE_START) { ui.paste = true; text_paste_begin(&ui.paste_data); ui.dragging = false; continue; }
        if (type == KEY_CODE_YES && key == KEY_PASTE_END) {
            static const size_t limits[FIELD_COUNT] = {2048, 256, 1024, 1024, 1024};
            ui.paste = false;
            if (ui.focus < FIELD_COUNT && !auth_busy(client) && !auth_has_session(client) && !(ui.profile_active && ui.focus == 0)) {
                text_buffer *buffer = &ui.values[ui.focus]; size_t lo = buffer->cursor, hi = lo;
                text_selection(buffer, &lo, &hi);
                if (buffer->length - (hi - lo) + ui.paste_data.text.length > limits[ui.focus]) ui.paste_data.truncated = true;
                if (!text_paste_commit(&ui.paste_data, buffer)) snprintf(ui.validation_message, sizeof(ui.validation_message), "Colagem excede o limite do campo; texto preservado.");
                else ui.validation_message[0] = 0;
            } else text_paste_cancel(&ui.paste_data);
            continue;
        }
        if (!ui.paste && type == KEY_CODE_YES && key == TEXT_COPY) {
            if (ui.focus < FIELD_COUNT && !auth_busy(client) && !auth_has_session(client))
                clipboard_copy(&ui.values[ui.focus]);
            continue;
        }
        bool choose_profile = type == KEY_CODE_YES && key == KEY_MOUSE;
        if (choose_profile && !click(&ui, client)) continue;
        bool chat_enter = (type == OK && (key == '\n' || key == '\r')) || (type == KEY_CODE_YES && key == KEY_ENTER);
        if (!ui.paste && auth_has_session(client) && (ui.open_chat ||
            (type == KEY_CODE_YES && key == KEY_F(6)) || (ui.focus == CHAT_FOCUS && chat_enter))) {
            int action = open_history(&ui, &client, &config);
            if (action < 0) result = 1;
            else if (action == 1) quit = true;
            continue;
        }
        if (type == OK && !ui.paste && (key == 17 || (key == 3 && (ui.focus >= FIELD_COUNT || auth_has_session(client) || auth_busy(client))))) break;
        if (type == OK && !ui.paste && ((key == 14 && !auth_has_session(client)) ||
            (key == 18 && auth_has_session(client)))) {
            extra_action(&ui, client);
            continue;
        }
        bool enter = !ui.paste && ((type == OK && (key == '\n' || key == '\r')) ||
                                  (type == KEY_CODE_YES && key == KEY_ENTER));
        if (choose_profile || (type == OK && !ui.paste && key == 16) ||
            (ui.form && ui.focus == PROFILE_FOCUS && enter)) {
            int selected = manage_profiles(&ui, &client, &config, allow_local_http, true);
            if (selected < 0) result = 1;
            else if (selected == 2) quit = true;
            continue;
        }
        if (!ui.form) continue;
        if (type == OK && !ui.paste && key == 27) {
            if (auth_busy(client)) { auth_cancel(client); clear_passwords(&ui); }
            else if (!auth_has_session(client) && !instances_contains(connections, client) &&
                     ui.previous_instance < connections->count &&
                     auth_has_session(connections->items[ui.previous_instance].auth)) {
                session_store_unwatch(connections->sessions, client); auth_destroy(client);
                client = connections->items[ui.previous_instance].auth;
                if (!instance_form(&ui, client, connections->items[ui.previous_instance].url)) { result = 1; break; }
                ui.open_chat = true;
            }
            continue;
        }
        if (!ui.paste && type == OK && key == '\t') {
            focus(&ui, client, 1);
            continue;
        }
        if (!ui.paste && type == KEY_CODE_YES && key == KEY_BTAB) {
            focus(&ui, client, -1);
            continue;
        }
        if (!ui.paste && type == KEY_CODE_YES && (key == KEY_DOWN || key == KEY_UP)) {
            focus_vertical(&ui, client, key == KEY_DOWN ? 1 : -1);
            continue;
        }
        if (!ui.paste && type == KEY_CODE_YES && ui.focus >= FIELD_COUNT &&
            (key == KEY_LEFT || key == KEY_RIGHT)) {
            focus_horizontal(&ui, client, key == KEY_RIGHT ? 1 : -1);
            continue;
        }
        if (enter) {
            if (ui.focus == EXTRA_FOCUS) extra_action(&ui, client);
            else submit(&ui, client);
            continue;
        }
        if (auth_busy(client) || auth_has_session(client) || ui.focus >= FIELD_COUNT ||
            (ui.profile_active && ui.focus == 0)) continue;
        edit_field(&ui, type, key);
    }
    destroy_layout(&ui);
    erase_text((char *)ui.saved, sizeof(ui.saved));
    erase_text((char *)ui.values, sizeof(ui.values)); text_paste_cancel(&ui.paste_data);
    clipboard_shutdown();
    text_input_terminal_active(false);
    fputs("\033[?2004l", stdout);
    fflush(stdout);
    mousemask(0, NULL);
    ui_mouse_drag_tracking(false);
    theme_restore(&theme);
    endwin();
    terminal_recovery_end();
    sigaction(SIGINT, &previous_int, NULL);
    sigaction(SIGTERM, &previous_term, NULL);
    if (!instances_contains(connections, client)) {
        session_store_unwatch(connections->sessions, client); auth_destroy(client);
    }
    instances_destroy(connections);
    connections = NULL;
    profiles_destroy(config);
    if (result) fputs("Não foi possível criar a tela de login.\n", stderr);
    return result;
}

int login_run(void) { return login_run_profiles(false, false); }
