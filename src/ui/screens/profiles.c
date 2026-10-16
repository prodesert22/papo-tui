#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "ui/screens/profiles.h"
#include "net/auth/client.h"
#include "net/rate_limit.h"
#include "platform/clipboard.h"
#include "ui/components/button.h"
#include "ui/components/theme.h"
#include "ui/input/mouse.h"
#include "ui/input/text.h"
#include "ui/media/graphics.h"
#include <stdint.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>

#define KEY_PASTE_START (KEY_MAX + 1)
#define KEY_PASTE_END (KEY_MAX + 2)

enum { TAB_AREA, LIST_AREA, SELECT, EDIT, REMOVE, LIMITS, VERIFY, AREA_COUNT };
#define ACTION_COUNT (AREA_COUNT - SELECT)
enum { MANAGE = AREA_COUNT, CANCEL, ADD };
enum { LIST_TAB, ADD_TAB };
enum { NO_DIALOG, EDIT_DIALOG, REMOVE_DIALOG, RATE_DIALOG };
typedef struct {
    int row, column, width, index;
} hit;
typedef struct {
    WINDOW *window, *dialog;
    int cursor, first_tab, focus, dialog_type, field, button, top_tab;
    bool paste, dragging;
    text_paste paste_data;
    char editing[PROFILE_ID_BYTES];
    text_buffer name, url;
    hit top_tabs[2], tabs[PROFILE_LIMIT + 1], actions[ACTION_COUNT], buttons[3];
    int tab_count, action_count, dialog_width, dialog_height;
    int scroll[2];
    profile_config *config;
    bool allow_http;
    bool chooser, selection_error;
    int width, height;
    auth_client *discovery;
    char discovery_id[PROFILE_ID_BYTES], discovery_url[PROFILE_URL_BYTES];
} profile_ui;

static const server_profile *selected(profile_ui *ui)
{
    if (!ui->chooser && ui->top_tab == ADD_TAB)
        return NULL;
    return ui->cursor >= 0 && ui->cursor < (int)ui->config->count
               ? &ui->config->profiles[ui->cursor]
               : NULL;
}

static bool start_discovery(profile_ui *ui, const server_profile *profile)
{
    /* A fresh transport/cookie store acts as the discovery generation. Late
     * responses from a destroyed profile can never reach the current UI. */
    auth_destroy(ui->discovery);
    ui->discovery = auth_create();
    if (!ui->discovery)
        return false;
    snprintf(ui->discovery_id, sizeof(ui->discovery_id), "%s", profile->id);
    snprintf(ui->discovery_url, sizeof(ui->discovery_url), "%s", profile->url);
    auth_discover(ui->discovery, profile->url, ui->allow_http);
    return true;
}

static bool synchronize_discovery(profile_ui *ui)
{
    const server_profile *profile = selected(ui);
    if (!profile) {
        if (!ui->chooser)
            ui->focus = TAB_AREA;
        if (*ui->discovery_id)
            auth_cancel(ui->discovery);
        ui->discovery_id[0] = ui->discovery_url[0] = '\0';
        return true;
    }
    if (strcmp(profile->id, ui->discovery_id) == 0 && strcmp(profile->url, ui->discovery_url) == 0)
        return true;
    return start_discovery(ui, profile);
}

static void close_dialog(profile_ui *ui)
{
    if (ui->dialog)
        delwin(ui->dialog);
    ui->dialog = NULL;
    ui->dialog_type = NO_DIALOG;
    if (!ui->chooser && !selected(ui))
        ui->focus = TAB_AREA;
}

static bool layout(profile_ui *ui)
{
    if (ui->dialog)
        delwin(ui->dialog);
    if (ui->window)
        delwin(ui->window);
    ui->dialog = ui->window = NULL;
    erase();
    refresh();
    if (LINES < 14 || COLS < 44)
        return true;
    ui->width = ui->chooser ? (COLS >= 70 ? 64 : COLS - 4) : COLS;
    ui->height = ui->chooser ? (LINES >= 20 ? 18 : LINES - 2) : LINES;
    ui->window = newwin(ui->height, ui->width, (LINES - ui->height) / 2, (COLS - ui->width) / 2);
    if (!ui->window)
        return false;
    wbkgd(ui->window, COLOR_PAIR(ui->chooser ? THEME_PROFILE_PANEL : THEME_PROFILE_TEXT));
    if (ui->dialog_type != NO_DIALOG) {
        ui->dialog_width = COLS > 76 ? 72 : COLS - 4;
        ui->dialog_height = 12;
        ui->dialog = newwin(ui->dialog_height, ui->dialog_width, (LINES - ui->dialog_height) / 2,
                            (COLS - ui->dialog_width) / 2);
        if (!ui->dialog)
            return false;
        wbkgd(ui->dialog, COLOR_PAIR(THEME_PROFILE_PANEL));
    }
    return true;
}

static chtype attributes(int color)
{
    chtype result = COLOR_PAIR(color);
    if (!has_colors()) {
        if (color == THEME_PROFILE_FOCUS || color == THEME_PROFILE_BUTTON_FOCUS)
            result |= A_REVERSE;
        else if (color == THEME_PROFILE_SELECTED || color == THEME_PROFILE_BUTTON_SELECTED)
            result |= A_BOLD;
    }
    return result;
}

static void label(WINDOW *window, int row, int column, int width, int color, const char *value)
{
    wattrset(window, attributes(color));
    text_label(window, row, column, width, value);
}

static void draw_tabs(profile_ui *ui)
{
    const char *titles[] = {"[Listar]", "[+ Adicionar]"};
    int x = 2;
    for (int i = 0; i < 2; ++i) {
        int width = (int)strlen(titles[i]);
        bool current = i == ui->top_tab;
        button_state state =
            current ? (ui->focus == TAB_AREA && !ui->dialog_type ? BUTTON_FOCUSED : BUTTON_SELECTED)
                    : BUTTON_NORMAL;
        button_draw(ui->window, 2, x, width, titles[i], state, BUTTON_CANVAS);
        ui->top_tabs[i] = (hit){2, x, width, i};
        x += width + 2;
    }
}

static void draw_profile_list(profile_ui *ui)
{
    ui->tab_count = 0;
    if (ui->top_tab != LIST_TAB)
        return;
    int visible = LINES - 13;
    if (ui->cursor < ui->first_tab)
        ui->first_tab = ui->cursor;
    if (ui->cursor >= ui->first_tab + visible)
        ui->first_tab = ui->cursor - visible + 1;
    int last = (int)ui->config->count > visible ? (int)ui->config->count - visible : 0;
    if (ui->first_tab > last)
        ui->first_tab = last;
    if (!ui->config->count) {
        label(ui->window, 4, 2, COLS - 4, THEME_MUTED,
              "Nenhum perfil salvo. Use Adicionar no topo.");
        return;
    }
    for (int i = ui->first_tab, row = 4; i < (int)ui->config->count && row < 4 + visible;
         ++i, ++row) {
        char name[PROFILE_NAME_BYTES + 4];
        snprintf(name, sizeof(name), "%c %s", ui->cursor == i ? '>' : ' ',
                 ui->config->profiles[i].name);
        bool focused = ui->focus == LIST_AREA && !ui->dialog_type;
        int color = ui->cursor == i
                        ? (focused ? THEME_PROFILE_BUTTON_FOCUS : THEME_PROFILE_BUTTON_SELECTED)
                        : THEME_PROFILE_TEXT;
        wattrset(ui->window, attributes(color));
        mvwhline(ui->window, row, 2, ' ', COLS - 4);
        text_label(ui->window, row, 2, COLS - 4, name);
        ui->tabs[ui->tab_count++] = (hit){row, 2, COLS - 4, i};
    }
    if (ui->first_tab)
        label(ui->window, 4, COLS - 2, 1, THEME_MUTED, "↑");
    if (ui->first_tab + visible < (int)ui->config->count)
        label(ui->window, 3 + visible, COLS - 2, 1, THEME_MUTED, "↓");
}

static void field_draw(profile_ui *ui, text_buffer *buffer, int index, int row, const char *name)
{
    label(ui->dialog, row - 1, 2, ui->dialog_width - 4, THEME_PROFILE_PANEL, name);
    int color = ui->field == index ? THEME_PROFILE_FOCUS : THEME_PROFILE_PANEL;
    wattrset(ui->dialog, attributes(color));
    mvwhline(ui->dialog, row, 2, ' ', ui->dialog_width - 4);
    text_draw_field(ui->dialog, buffer, &ui->scroll[index], row, 2, ui->dialog_width - 4, false);
}

static void draw_dialog(profile_ui *ui)
{
    if (!ui->dialog)
        return;
    werase(ui->dialog);
    wattrset(ui->dialog, COLOR_PAIR(THEME_BORDER));
    box(ui->dialog, 0, 0);
    label(ui->dialog, 0, 2, ui->dialog_width - 4, THEME_PROFILE_PANEL,
          ui->dialog_type == REMOVE_DIALOG ? " Remover perfil "
          : ui->dialog_type == RATE_DIALOG ? " Limites de requisições "
          : *ui->editing                   ? " Editar perfil "
                                           : " Adicionar perfil ");
    if (ui->dialog_type == EDIT_DIALOG || ui->dialog_type == RATE_DIALOG) {
        field_draw(ui, &ui->name, 0, 3,
                   ui->dialog_type == RATE_DIALOG ? "Requisições por segundo (positivo e finito)"
                                                  : "Nome local (até 128 bytes UTF-8)");
        field_draw(ui, &ui->url, 1, 6,
                   ui->dialog_type == RATE_DIALOG ? "Burst (inteiro positivo)"
                                                  : "URL base (HTTPS)");
        label(ui->dialog, 7, 2, ui->dialog_width - 4, THEME_PANEL_MUTED,
              ui->dialog_type == RATE_DIALOG ? "Padrão preenche 10/25; Salvar aplica globalmente."
              : ui->allow_http ? "HTTP permitido somente em localhost, 127.0.0.1 ou [::1]."
                               : "HTTP local: use --allow-local-http ao iniciar.");
    } else {
        const server_profile *profile = profiles_find(ui->config, ui->editing);
        label(ui->dialog, 2, 2, ui->dialog_width - 4, THEME_PROFILE_PANEL,
              "Remover este perfil salvo?");
        label(ui->dialog, 4, 2, ui->dialog_width - 4, THEME_PROFILE_PANEL,
              profile ? profile->name : "Perfil indisponível");
        label(ui->dialog, 6, 2, ui->dialog_width - 4, THEME_PANEL_MUTED,
              "A remoção só acontece após confirmar.");
    }
    ui->buttons[0] = (hit){8, 2, 12, 0};
    ui->buttons[1] = (hit){8, 17, 12, 1};
    ui->buttons[2] = (hit){8, 29, 8, 2};
    button_draw(ui->dialog, 8, 2, 12, "[Cancelar]",
                ui->field == 2 && !ui->button ? BUTTON_FOCUSED : BUTTON_NORMAL, BUTTON_PANEL);
    button_draw(ui->dialog, 8, 17, 12, ui->dialog_type == REMOVE_DIALOG ? "[Remover]" : "[Salvar]",
                ui->field == 2 && ui->button == 1 ? BUTTON_FOCUSED : BUTTON_NORMAL, BUTTON_PANEL);
    if (ui->dialog_type == RATE_DIALOG)
        button_draw(ui->dialog, 8, 29, 8, "[Padrão]",
                    ui->field == 2 && ui->button == 2 ? BUTTON_FOCUSED : BUTTON_NORMAL,
                    BUTTON_PANEL);
    label(ui->dialog, 9, 2, ui->dialog_width - 4,
          ui->config->writable ? THEME_PANEL_MUTED : THEME_ERROR, ui->config->message);
    label(ui->dialog, 10, 2, ui->dialog_width - 4, THEME_PANEL_MUTED,
          clipboard_notice() ? clipboard_notice() : "Tab: foco · Enter: confirmar · Esc: cancelar");
    if ((ui->dialog_type == EDIT_DIALOG || ui->dialog_type == RATE_DIALOG) && ui->field < 2) {
        text_buffer *buffer = ui->field ? &ui->url : &ui->name;
        text_layout view;
        text_measure(buffer, TEXT_LIMIT * 2 + 1, &view);
        wmove(ui->dialog, ui->field ? 6 : 3, 2 + view.cursor_column - ui->scroll[ui->field]);
    }
    wnoutrefresh(ui->dialog);
}

static void draw_discovery(profile_ui *ui, int row, int width)
{
    const server_profile *profile = selected(ui);
    if (profile && auth_get_state(ui->discovery) == AUTH_IDENTIFIED) {
        const char *mode = auth_server_public(ui->discovery) ? "Pública · " : "Privada · ";
        int prefix = text_label(NULL, 0, 0, width, mode);
        label(ui->window, row, 2, prefix, ui->chooser ? THEME_PROFILE_PANEL : THEME_PROFILE_TEXT,
              mode);
        wattrset(ui->window, COLOR_PAIR(ui->chooser ? THEME_PROFILE_PANEL : THEME_PROFILE_TEXT));
        if (!text_label(ui->window, row, 2 + prefix, width - prefix,
                        auth_server_name(ui->discovery)))
            label(ui->window, row, 2 + prefix, width - prefix,
                  ui->chooser ? THEME_PROFILE_PANEL : THEME_PROFILE_TEXT, "Nome indisponível");
    } else
        label(ui->window, row, 2, width,
              auth_get_state(ui->discovery) == AUTH_ERROR ? THEME_ERROR : THEME_MUTED,
              profile ? auth_message(ui->discovery)
                      : "HTTPS · Nenhuma senha ou cookie é persistido.");
}

static void draw_chooser(profile_ui *ui)
{
    werase(ui->window);
    wattrset(ui->window, COLOR_PAIR(THEME_BORDER));
    box(ui->window, 0, 0);
    label(ui->window, 1, 2, ui->width - 4, THEME_PROFILE_PANEL, "Selecione um perfil");
    label(ui->window, 2, 2, ui->width - 4, THEME_PANEL_MUTED,
          "↑↓: escolher · Enter: abrir · Esc: login manual");
    wattrset(ui->window, COLOR_PAIR(THEME_BORDER));
    mvwhline(ui->window, 3, 1, ACS_HLINE, ui->width - 2);
    int visible = ui->height - 10;
    if (ui->cursor < ui->first_tab)
        ui->first_tab = ui->cursor;
    if (ui->cursor >= ui->first_tab + visible)
        ui->first_tab = ui->cursor - visible + 1;
    int last = (int)ui->config->count > visible ? (int)ui->config->count - visible : 0;
    if (ui->first_tab > last)
        ui->first_tab = last;
    ui->tab_count = 0;
    for (int i = ui->first_tab, row = 4; i < (int)ui->config->count && row < 4 + visible;
         ++i, ++row) {
        char name[PROFILE_NAME_BYTES + 4];
        snprintf(name, sizeof(name), "%c %s", ui->cursor == i ? '>' : ' ',
                 ui->config->profiles[i].name);
        int color = ui->cursor == i ? (ui->focus == TAB_AREA ? THEME_PROFILE_BUTTON_FOCUS
                                                             : THEME_PROFILE_BUTTON_SELECTED)
                                    : THEME_PROFILE_PANEL;
        wattrset(ui->window, attributes(color));
        mvwhline(ui->window, row, 2, ' ', ui->width - 4);
        text_label(ui->window, row, 2, ui->width - 4, name);
        ui->tabs[ui->tab_count++] = (hit){row, 2, ui->width - 4, i};
    }
    const server_profile *profile = selected(ui);
    label(ui->window, ui->height - 6, 2, ui->width - 4,
          profile ? THEME_PROFILE_PANEL : THEME_PANEL_MUTED,
          profile ? profile->url : "Nenhum perfil salvo. Abra Gerenciar.");
    if (ui->selection_error || !ui->config->writable)
        label(ui->window, ui->height - 5, 2, ui->width - 4, THEME_ERROR, ui->config->message);
    else
        draw_discovery(ui, ui->height - 5, ui->width - 4);
    const char *buttons[] = {"[Selecionar]", "[Gerenciar]", "[Cancelar]"};
    const int actions[] = {SELECT, MANAGE, CANCEL};
    ui->action_count = 0;
    int row = ui->height - 4, column = 2;
    for (int i = 0; i < 3; ++i) {
        int width = (int)strlen(buttons[i]);
        if (column + width >= ui->width - 1) {
            ++row;
            column = 2;
        }
        button_draw(ui->window, row, column, width, buttons[i],
                    ui->focus == actions[i] ? BUTTON_FOCUSED : BUTTON_NORMAL, BUTTON_PANEL);
        ui->actions[ui->action_count++] = (hit){row, column, width, actions[i]};
        column += width + 2;
    }
    label(ui->window, ui->height - 2, 2, ui->width - 4, THEME_PANEL_MUTED,
          "Tab: foco · Ctrl+P: gerenciar · Ctrl+Q: sair");
    wnoutrefresh(ui->window);
    doupdate();
}

static void draw(profile_ui *ui)
{
    curs_set(0);
    if (!ui->window) {
        erase();
        label(stdscr, 0, 0, COLS, THEME_MUTED, "Amplie o terminal para 44 colunas e 14 linhas.");
        if (LINES > 1)
            label(stdscr, 1, 0, COLS, THEME_MUTED, "Ctrl+Q: sair");
        refresh();
        return;
    }
    if (ui->chooser) {
        draw_chooser(ui);
        return;
    }
    werase(ui->window);
    wattrset(ui->window, COLOR_PAIR(THEME_BORDER));
    box(ui->window, 0, 0);
    label(ui->window, 1, 2, COLS - 4, THEME_PROFILE_TEXT, "papo · Servidores salvos");
    draw_tabs(ui);
    wattrset(ui->window, COLOR_PAIR(THEME_BORDER));
    mvwhline(ui->window, 3, 1, ACS_HLINE, COLS - 2);
    draw_profile_list(ui);
    const server_profile *profile = selected(ui);
    if (ui->top_tab == ADD_TAB) {
        label(ui->window, 4, 2, COLS - 4, THEME_PROFILE_TEXT, "Adicionar uma instância");
        label(ui->window, 5, 2, COLS - 4, THEME_MUTED, "Enter abre o cadastro de nome e URL.");
    }
    char summary[192];
    snprintf(summary, sizeof(summary), "Perfil %d/%zu · %.8g req/s · burst %u",
             profile ? ui->cursor + 1 : 0, ui->config->count, ui->config->requests_per_second,
             ui->config->request_burst);
    label(ui->window, LINES - 9, 2, COLS - 4, profile ? THEME_PROFILE_TEXT : THEME_MUTED,
          profile ? profile->url : "HTTPS · Nenhuma senha ou cookie é persistido.");
    if (profile)
        draw_discovery(ui, LINES - 8, COLS - 4);
    label(ui->window, LINES - 7, 2, COLS - 4, THEME_PROFILE_TEXT, summary);
    static const char *buttons[] = {"[Selecionar]", "[Editar]", "[Remover]", "[Limites]",
                                    "[Identificar]"};
    int row = LINES - 6, column = 2;
    ui->action_count = 0;
    for (int i = 0; profile && i < ACTION_COUNT; ++i) {
        int width = (int)strlen(buttons[i]);
        if (column + width >= COLS - 1) {
            column = 2;
            ++row;
        }
        button_draw(ui->window, row, column, width, buttons[i],
                    ui->focus == i + SELECT && !ui->dialog_type ? BUTTON_FOCUSED : BUTTON_NORMAL,
                    BUTTON_CANVAS);
        ui->actions[ui->action_count++] = (hit){row, column, width, i + SELECT};
        column += width + 2;
    }
    if (!profile)
        label(ui->window, row, column, COLS - 4, THEME_MUTED,
              "Selecione um perfil para habilitar as ações.");
    label(ui->window, LINES - 4, 2, COLS - 4, ui->config->writable ? THEME_MUTED : THEME_ERROR,
          ui->config->message);
    const char *navigation = ui->focus == TAB_AREA    ? "←→: opções · ↓/Enter: abrir · Tab: foco"
                             : ui->focus == LIST_AREA ? "↑↓: perfis · →/Enter: ações · Tab: foco"
                                                      : "←→: ações · ↑: lista · Enter: confirmar";
    label(ui->window, LINES - 3, 2, COLS - 4, THEME_MUTED, navigation);
    label(ui->window, LINES - 2, 2, COLS - 4, THEME_MUTED,
          profile
              ? (COLS >= 80
                     ? "Ctrl+L: limites · F5: retry · Ctrl+R: reler · Esc: voltar · Ctrl+Q: sair"
                     : "F5 · Ctrl+L · Ctrl+R · Esc · Ctrl+Q")
              : "Ctrl+R: recarregar · Esc: voltar · Ctrl+Q: sair");
    wnoutrefresh(ui->window);
    draw_dialog(ui);
    doupdate();
    curs_set(ui->dialog && (ui->dialog_type == EDIT_DIALOG || ui->dialog_type == RATE_DIALOG) &&
                     ui->field < 2
                 ? 1
                 : 0);
}

static bool open_dialog(profile_ui *ui, int kind)
{
    if (kind == ADD && !ui->chooser) {
        ui->top_tab = ADD_TAB;
        ui->focus = TAB_AREA;
    }
    const server_profile *profile = selected(ui);
    if (kind != ADD && kind != LIMITS && !profile)
        return true;
    ui->editing[0] = '\0';
    text_clear(&ui->name);
    text_clear(&ui->url);
    ui->scroll[0] = ui->scroll[1] = 0;
    if (kind == LIMITS) {
        char number[64];
        /* JSON formatting is locale-independent, matching the numeric input
         * parser even when the terminal locale uses a decimal comma. */
        json_t *rate = json_real(ui->config->requests_per_second);
        char *formatted = rate ? json_dumps(rate, JSON_ENCODE_ANY) : NULL;
        json_decref(rate);
        if (!formatted)
            return false;
        text_decode(&ui->name, formatted);
        free(formatted);
        snprintf(number, sizeof(number), "%u", ui->config->request_burst);
        text_decode(&ui->url, number);
    } else if (kind != ADD) {
        snprintf(ui->editing, sizeof(ui->editing), "%s", profile->id);
        text_decode(&ui->name, profile->name);
        text_decode(&ui->url, profile->url);
    }
    ui->dialog_type = kind == REMOVE ? REMOVE_DIALOG : kind == LIMITS ? RATE_DIALOG : EDIT_DIALOG;
    ui->field = kind == REMOVE ? 2 : 0;
    ui->button = 0;
    snprintf(ui->config->message, sizeof(ui->config->message), "%s",
             kind == REMOVE   ? "Cancelar mantém o perfil."
             : kind == LIMITS ? "Alterações não recarregam tokens nem limpam Retry-After."
                              : "Informe somente nome e URL; não inclua credenciais.");
    return layout(ui);
}

static bool confirm(profile_ui *ui)
{
    if (ui->dialog_type == REMOVE_DIALOG) {
        if (!profiles_remove(ui->config, ui->editing))
            return true;
        if (ui->cursor >= (int)ui->config->count)
            ui->cursor = ui->config->count ? (int)ui->config->count - 1 : 0;
    } else if (ui->dialog_type == RATE_DIALOG) {
        char rate[TEXT_UTF8_BYTES], burst[TEXT_UTF8_BYTES];
        if (!text_encode(&ui->name, rate) || !text_encode(&ui->url, burst))
            return true;
        json_error_t diagnostic;
        json_t *rate_value = json_loads(rate, JSON_DECODE_ANY, &diagnostic);
        json_t *burst_value = json_loads(burst, JSON_DECODE_ANY, &diagnostic);
        bool valid = json_is_number(rate_value) && json_is_integer(burst_value) &&
                     json_integer_value(burst_value) > 0 &&
                     json_integer_value(burst_value) <= UINT_MAX;
        if (valid)
            valid = profiles_set_rate(ui->config, json_number_value(rate_value),
                                      (unsigned)json_integer_value(burst_value));
        else
            snprintf(ui->config->message, sizeof(ui->config->message),
                     "Use taxa positiva e finita; burst inteiro entre 1 e 4294967295.");
        json_decref(rate_value);
        json_decref(burst_value);
        if (!valid)
            return true;
    } else {
        char name[TEXT_UTF8_BYTES], url[TEXT_UTF8_BYTES], id[PROFILE_ID_BYTES];
        if (!text_encode(&ui->name, name) || !text_encode(&ui->url, url))
            return true;
        if (!profiles_update(ui->config, ui->editing, name, url, ui->allow_http, id))
            return true;
        const server_profile *profile = profiles_find(ui->config, id);
        if (profile)
            ui->cursor = (int)(profile - ui->config->profiles);
        ui->top_tab = LIST_TAB;
        ui->focus = LIST_AREA;
    }
    close_dialog(ui);
    return true;
}

static int activate(profile_ui *ui, server_profile *output)
{
    if (ui->chooser && ui->focus == MANAGE)
        return 3;
    if (ui->chooser && ui->focus == CANCEL)
        return 4;
    if (ui->chooser && !selected(ui))
        return 3;
    if (!ui->chooser) {
        if (ui->focus == TAB_AREA) {
            if (ui->top_tab == ADD_TAB)
                return open_dialog(ui, ADD) ? 0 : -1;
            if (selected(ui))
                ui->focus = LIST_AREA;
            return 0;
        }
        if (!selected(ui)) {
            ui->focus = TAB_AREA;
            return 0;
        }
        if (ui->focus == LIST_AREA) {
            ui->focus = SELECT;
            return 0;
        }
    }
    if (ui->focus == EDIT || ui->focus == REMOVE)
        return open_dialog(ui, ui->focus) ? 0 : -1;
    if (ui->focus == LIMITS)
        return open_dialog(ui, LIMITS) ? 0 : -1;
    if (ui->focus == VERIFY) {
        const server_profile *profile = selected(ui);
        if (profile && !start_discovery(ui, profile))
            return -1;
        return 0;
    }
    const server_profile *profile = selected(ui);
    if (profile && profiles_select(ui->config, profile->id, ui->allow_http)) {
        *output = ui->config->profiles[ui->cursor];
        return 1;
    }
    ui->selection_error = true;
    return 0;
}

static bool inside(hit region, int row, int column)
{
    return region.row == row && column >= region.column && column < region.column + region.width;
}

static void restore_rate_fields(profile_ui *ui)
{
    text_decode(&ui->name, "10");
    text_decode(&ui->url, "25");
    ui->scroll[0] = ui->scroll[1] = 0;
    ui->field = 0;
    snprintf(ui->config->message, sizeof(ui->config->message),
             "Padrão preenchido. Salvar aplica 10 req/s e burst 25.");
}

static int click_event(profile_ui *ui, server_profile *output, MEVENT mouse)
{
    if (ui->paste || !ui->window)
        return 0;
    if (ui->dragging && ui->dialog && ui->field < 2 && ui->dialog_type != REMOVE_DIALOG) {
        if (mouse.bstate & (BUTTON1_PRESSED | BUTTON1_RELEASED | REPORT_MOUSE_POSITION)) {
            int column = mouse.x - getbegx(ui->dialog) - 2, width = ui->dialog_width - 4;
            if (column < 0 && ui->scroll[ui->field])
                --ui->scroll[ui->field];
            if (column >= width)
                ++ui->scroll[ui->field];
            if (column < 0)
                column = 0;
            if (column >= width)
                column = width - 1;
            text_drag(ui->field ? &ui->url : &ui->name, TEXT_LIMIT * 2 + 1, 0,
                      ui->scroll[ui->field] + column);
            if (mouse.bstate & BUTTON1_RELEASED)
                ui->dragging = false;
            return 0;
        }
    }
    if ((mouse.bstate & REPORT_MOUSE_POSITION) ||
        !(mouse.bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED | BUTTON3_PRESSED | BUTTON3_CLICKED)))
        return 0;
    int row = mouse.y, column = mouse.x;
    if (!wmouse_trafo(ui->window, &row, &column, false))
        return 0;
    if (ui->dialog) {
        row = mouse.y;
        column = mouse.x;
        if (!wmouse_trafo(ui->dialog, &row, &column, false))
            return 0;
        if (mouse.bstate & (BUTTON3_PRESSED | BUTTON3_CLICKED)) {
            if (ui->dialog_type != REMOVE_DIALOG && (row == 3 || row == 6) && column >= 2 &&
                column < ui->dialog_width - 2)
                clipboard_copy(row == 6 ? &ui->url : &ui->name);
            return 0;
        }
        for (int i = 0; i < (ui->dialog_type == RATE_DIALOG ? 3 : 2); ++i)
            if (inside(ui->buttons[i], row, column)) {
                ui->field = 2;
                ui->button = i;
                if (!i)
                    close_dialog(ui);
                else if (i == 2)
                    restore_rate_fields(ui);
                else if (!confirm(ui))
                    return -1;
                return 0;
            }
        if ((ui->dialog_type == EDIT_DIALOG || ui->dialog_type == RATE_DIALOG) && column >= 2 &&
            column < ui->dialog_width - 2 && (row == 3 || row == 6)) {
            ui->field = row == 6 ? 1 : 0;
            text_buffer *buffer = ui->field ? &ui->url : &ui->name;
            text_click(buffer, TEXT_LIMIT * 2 + 1, 0, ui->scroll[ui->field] + column - 2);
            ui->dragging = (mouse.bstate & BUTTON1_PRESSED) != 0;
        }
        return 0;
    }
    if (mouse.bstate & (BUTTON3_PRESSED | BUTTON3_CLICKED))
        return 0;
    if (!ui->chooser) {
        for (int i = 0; i < 2; ++i)
            if (inside(ui->top_tabs[i], row, column)) {
                ui->top_tab = i;
                ui->focus = i == LIST_TAB && selected(ui) ? LIST_AREA : TAB_AREA;
                return i == ADD_TAB ? activate(ui, output) : 0;
            }
    }
    for (int i = 0; i < ui->tab_count; ++i)
        if (inside(ui->tabs[i], row, column)) {
            ui->cursor = ui->tabs[i].index;
            ui->focus = TAB_AREA;
            if (ui->chooser)
                return activate(ui, output);
            ui->focus = LIST_AREA;
            return 0;
        }
    if (!ui->chooser && !selected(ui))
        return 0;
    for (int i = 0; i < ui->action_count; ++i)
        if (inside(ui->actions[i], row, column)) {
            ui->focus = ui->actions[i].index;
            return activate(ui, output);
        }
    return 0;
}

static int click(profile_ui *ui, server_profile *output)
{
    MEVENT events[64];
    size_t count = ui_mouse_events(events, 64);
    for (size_t i = 0; i < count; ++i) {
        int result = click_event(ui, output, events[i]);
        if (result)
            return result;
    }
    return 0;
}
static void edit(text_buffer *buffer, int type, uint32_t key)
{
    text_edit(buffer, type, key, TEXT_LIMIT * 2 + 1, false);
}

static int profiles_run(profile_config *config, bool allow_local_http,
                        const volatile sig_atomic_t *interrupted, server_profile *output,
                        bool chooser)
{
    profile_ui *ui = calloc(1, sizeof(*ui));
    if (!ui)
        return -1;
    ui->config = config;
    ui->allow_http = allow_local_http;
    ui->chooser = chooser;
    ui->discovery = auth_create();
    if (!ui->discovery) {
        free(ui);
        return -1;
    }
    const server_profile *active = profiles_find(config, config->selected);
    if (active)
        ui->cursor = (int)(active - config->profiles);
    if (!chooser && config->count)
        ui->focus = LIST_AREA;
    text_input_init();
    int result = layout(ui) ? 0 : -1;
    bool done = result < 0;
    bool redraw = true;

    while (!done && !*interrupted) {
        auth_state before = auth_get_state(ui->discovery);
        if (!synchronize_discovery(ui)) {
            result = -1;
            break;
        }
        auth_poll(ui->discovery);
        if (before != auth_get_state(ui->discovery))
            redraw = true;
        if (clipboard_poll())
            redraw = true;
        if (redraw) {
            draw(ui);
            redraw = false;
        }
        uint32_t key;
        int type = ui->paste
                       ? text_paste_read(&ui->paste_data, &key, false, graphics_terminal_input, 30)
                       : graphics_terminal_input(&key);
        if (type == ERR) {
            if (redraw) {
                draw(ui);
                redraw = false;
            }
            continue;
        }
        redraw = true;
        if (type == KEY_CODE_YES && key == KEY_RESIZE) {
            if (!layout(ui)) {
                result = -1;
                break;
            }
            continue;
        }
        if (type == KEY_CODE_YES && key == KEY_PASTE_START) {
            ui->paste = true;
            text_paste_begin(&ui->paste_data);
            ui->dragging = false;
            continue;
        }
        if (type == KEY_CODE_YES && key == KEY_PASTE_END) {
            ui->paste = false;
            if ((ui->dialog_type == EDIT_DIALOG || ui->dialog_type == RATE_DIALOG) &&
                ui->field < 2) {
                if (!text_paste_commit(&ui->paste_data, ui->field ? &ui->url : &ui->name))
                    snprintf(config->message, sizeof(config->message),
                             "Colagem excede o limite do campo; texto preservado.");
            } else
                text_paste_cancel(&ui->paste_data);
            continue;
        }
        if (type == KEY_CODE_YES && key == KEY_MOUSE) {
            result = click(ui, output);
            if (result)
                break;
            continue;
        }
        if (ui->paste)
            continue;
        if (type == KEY_CODE_YES && key == TEXT_COPY) {
            if (ui->dialog && ui->dialog_type != REMOVE_DIALOG && ui->field < 2)
                clipboard_copy(ui->field ? &ui->url : &ui->name);
            continue;
        }
        if (type == OK &&
            (key == 17 ||
             (key == 3 && !(ui->dialog && ui->dialog_type != REMOVE_DIALOG && ui->field < 2)))) {
            result = 2;
            break;
        }
        if (type == OK && key == 27) {
            if (ui->dialog_type)
                close_dialog(ui);
            else
                break;
            continue;
        }
        if (!ui->window)
            continue;
        bool enter = (type == OK && (key == '\n' || key == '\r')) ||
                     (type == KEY_CODE_YES && key == KEY_ENTER);
        int tab = type == OK && key == '\t' ? 1 : type == KEY_CODE_YES && key == KEY_BTAB ? -1 : 0;
        if (ui->chooser) {
            const int areas[] = {TAB_AREA, SELECT, MANAGE, CANCEL};
            if (type == OK && (key == 16 || key == 14 || key == 5 || key == 4 || key == 12)) {
                result = 3;
                break;
            }
            if (tab) {
                int index = 0;
                while (index < 3 && areas[index] != ui->focus)
                    ++index;
                ui->focus = areas[(index + tab + 4) % 4];
            } else if (type == KEY_CODE_YES && (key == KEY_LEFT || key == KEY_RIGHT)) {
                if (ui->focus == TAB_AREA) {
                    if (key == KEY_RIGHT)
                        ui->focus = selected(ui) ? SELECT : MANAGE;
                } else {
                    int index = 1;
                    while (index < 3 && areas[index] != ui->focus)
                        ++index;
                    if (key == KEY_LEFT && index > 1)
                        --index;
                    if (key == KEY_RIGHT && index < 3)
                        ++index;
                    ui->focus = areas[index];
                }
            } else if (type == KEY_CODE_YES && (key == KEY_UP || key == KEY_DOWN)) {
                if (ui->focus != TAB_AREA) {
                    if (key == KEY_UP)
                        ui->focus = TAB_AREA;
                } else if (key == KEY_UP && ui->cursor > 0)
                    --ui->cursor;
                else if (key == KEY_DOWN && ui->cursor + 1 < (int)config->count)
                    ++ui->cursor;
                ui->selection_error = false;
            } else if (type == OK && key == 18) {
                profiles_reload(config);
                ui->cursor = ui->first_tab = 0;
                ui->selection_error = false;
            } else if (type == KEY_CODE_YES && key == KEY_F(5)) {
                const server_profile *profile = selected(ui);
                if (profile && !start_discovery(ui, profile)) {
                    result = -1;
                    break;
                }
            } else if (enter) {
                result = activate(ui, output);
                if (result)
                    done = true;
            }
            continue;
        }
        if (ui->dialog_type) {
            if (tab) {
                if (ui->dialog_type == REMOVE_DIALOG)
                    ui->button = !ui->button;
                else {
                    ui->field = (ui->field + tab + 3) % 3;
                    if (ui->field == 2)
                        ui->button = 1;
                }
            } else if (ui->field == 2 && type == KEY_CODE_YES &&
                       (key == KEY_LEFT || key == KEY_RIGHT)) {
                int last = ui->dialog_type == RATE_DIALOG ? 2 : 1;
                if (key == KEY_LEFT && ui->button > 0)
                    --ui->button;
                if (key == KEY_RIGHT && ui->button < last)
                    ++ui->button;
            } else if (enter) {
                if (ui->dialog_type != REMOVE_DIALOG && ui->field < 2) {
                    if (++ui->field == 2)
                        ui->button = 1;
                } else if (!ui->button)
                    close_dialog(ui);
                else if (ui->button == 2)
                    restore_rate_fields(ui);
                else if (!confirm(ui)) {
                    result = -1;
                    break;
                }
            } else if (ui->dialog_type != REMOVE_DIALOG && ui->field < 2)
                edit(ui->field ? &ui->url : &ui->name, type, key);
            continue;
        }
        if (tab)
            ui->focus = selected(ui) ? (ui->focus + tab + AREA_COUNT) % AREA_COUNT : TAB_AREA;
        else if (type == KEY_CODE_YES && (key == KEY_LEFT || key == KEY_RIGHT)) {
            if (ui->focus == TAB_AREA) {
                if (key == KEY_LEFT)
                    ui->top_tab = LIST_TAB;
                else
                    ui->top_tab = ADD_TAB;
            } else if (ui->focus == LIST_AREA) {
                if (key == KEY_RIGHT)
                    ui->focus = SELECT;
            } else {
                if (key == KEY_LEFT && ui->focus > SELECT)
                    --ui->focus;
                if (key == KEY_RIGHT && ui->focus < VERIFY)
                    ++ui->focus;
            }
        } else if (type == KEY_CODE_YES && (key == KEY_DOWN || key == KEY_UP)) {
            if (ui->focus == LIST_AREA) {
                if (key == KEY_UP && ui->cursor == 0)
                    ui->focus = TAB_AREA;
                else if (key == KEY_UP)
                    --ui->cursor;
                else if (ui->cursor + 1 < (int)config->count)
                    ++ui->cursor;
            } else if (ui->focus == TAB_AREA) {
                if (key == KEY_DOWN && selected(ui))
                    ui->focus = LIST_AREA;
            } else if (key == KEY_UP)
                ui->focus = LIST_AREA;
        } else if (type == KEY_CODE_YES && ui->focus == LIST_AREA &&
                   (key == KEY_PPAGE || key == KEY_NPAGE || key == KEY_HOME || key == KEY_END)) {
            if (key == KEY_HOME)
                ui->cursor = 0;
            else if (key == KEY_END)
                ui->cursor = (int)config->count - 1;
            else
                ui->cursor += (key == KEY_NPAGE ? 1 : -1) * (LINES - 13);
            if (ui->cursor < 0)
                ui->cursor = 0;
            if (ui->cursor >= (int)config->count)
                ui->cursor = (int)config->count - 1;
        } else if (type == OK && key == 18) {
            profiles_reload(config);
            ui->cursor = ui->first_tab = 0;
            ui->focus = selected(ui) ? LIST_AREA : TAB_AREA;
        } else if (type == OK && key == 12) {
            if (selected(ui) && !open_dialog(ui, LIMITS)) {
                result = -1;
                break;
            }
        } else if (type == KEY_CODE_YES && key == KEY_F(5)) {
            const server_profile *profile = selected(ui);
            if (profile && !start_discovery(ui, profile)) {
                result = -1;
                break;
            }
        } else if (type == OK && (key == 14 || key == 5 || key == 4)) {
            if (!open_dialog(ui, key == 14 ? ADD : key == 5 ? EDIT : REMOVE)) {
                result = -1;
                break;
            }
        } else if (enter) {
            result = activate(ui, output);
            if (result)
                done = true;
        }
    }
    close_dialog(ui);
    if (ui->window)
        delwin(ui->window);
    auth_destroy(ui->discovery);
    text_paste_cancel(&ui->paste_data);
    free(ui);
    curs_set(0);
    return *interrupted ? 2 : result == 4 ? 0 : result;
}

int profiles_manage(profile_config *config, bool allow_local_http,
                    const volatile sig_atomic_t *interrupted, server_profile *output)
{
    return profiles_run(config, allow_local_http, interrupted, output, false);
}

int profiles_choose(profile_config *config, bool allow_local_http,
                    const volatile sig_atomic_t *interrupted, server_profile *output)
{
    return profiles_run(config, allow_local_http, interrupted, output, true);
}
