#include "ui/screens/conversation/private.h"

static bool graphical_badge(void)
{
    graphics_protocol protocol = graphics_terminal_get().protocol;
    return protocol != GRAPHICS_TEXT && protocol != GRAPHICS_CHAFA;
}
void conversation_notification_dot(view *ui, WINDOW *window, int row, int column, int pair)
{
    attr_t previous = getattrs(window);
    wattrset(window, (has_colors() ? COLOR_PAIR(pair) : 0) | A_BOLD);
    mvwaddstr(window, row, column, "●");
    if (graphical_badge()) {
        mvwaddch(window, row, column + 1, ' ');
        icons_notification(ui->icons, window, row, column, 2, 0, pair);
    }
    wattrset(window, previous);
}
static int notification_badge(view *ui, WINDOW *window, int row, int column, size_t count,
                              bool more, int pair)
{
    char label[32];
    if (!count)
        snprintf(label, sizeof(label), "●");
    else if (count <= 10) {
        int bytes = g_unichar_to_utf8((gunichar)(0x2775 + count), label);
        label[bytes] = 0;
    } else
        snprintf(label, sizeof(label), "(%zu)", count);
    if (more && count)
        strncat(label, "+", sizeof(label) - strlen(label) - 1);
    attr_t previous = window ? getattrs(window) : 0;
    if (window)
        wattrset(window, (has_colors() ? COLOR_PAIR(pair) : 0) | A_BOLD);
    int width = text_label(window, row, column, 24, label);
    if (graphical_badge()) {
        int columns = MAX(2, width - (more && count ? 1 : 0));
        if (window) {
            if (columns > width)
                mvwaddch(window, row, column + width, ' ');
            icons_notification(ui->icons, window, row, column, columns, count, pair);
            if (more && count)
                mvwaddch(window, row, column + columns, '+');
        }
        width = columns + (more && count ? 1 : 0);
    }
    if (window)
        wattrset(window, previous);
    return width;
}
static bool instance_unread(history *h)
{
    if (inbox_unseen_conversations(history_inbox(h)))
        return true;
    for (size_t i = 0; i < history_channel_count(h); ++i)
        if (history_channel_unread(h, history_channel(h, i)))
            return true;
    return false;
}
static int server_badge(view *ui, WINDOW *window, int row, int column, size_t count, bool more,
                        int pair)
{
    graphics_protocol protocol = graphics_terminal_get().protocol;
    if (protocol != GRAPHICS_TEXT && protocol != GRAPHICS_CHAFA)
        return notification_badge(ui, window, row, column, count, more, pair);
    char label[32];
    snprintf(label, sizeof(label), "%zu%s", count, more && count ? "+" : "");
    attr_t previous = window ? getattrs(window) : 0;
    if (window)
        wattrset(window, (has_colors() ? COLOR_PAIR(pair) : 0) | A_BOLD);
    int width = text_label(window, row, column, 24, label);
    if (window)
        wattrset(window, previous);
    return width;
}

void conversation_list_button(view *ui, int button, int column, int width, const char *label)
{
    button_state state = ui->focus == LIST_BUTTONS && ui->list_button == button ? BUTTON_FOCUSED
                         : ui->list_dms == (button != 0)                        ? BUTTON_SELECTED
                                                                                : BUTTON_NORMAL;
    button_draw(ui->list, 0, column, width, label, state, BUTTON_PANEL);
    if (conversation_list_notified(ui, button != 0))
        conversation_notification_dot(ui, ui->list, 0, column + width - (graphical_badge() ? 2 : 1),
                                      state == BUTTON_FOCUSED    ? THEME_NOTIFICATION_BUTTON_FOCUS
                                      : state == BUTTON_SELECTED ? THEME_NOTIFICATION_SELECTED
                                                                 : THEME_NOTIFICATION);
}

int conversation_tab_width(view *ui, size_t index, int available)
{
    int width = text_label(NULL, 0, 0, 128, ui->connections->items[index].name) + 4;
    inbox *box = history_inbox(ui->connections->items[index].model);
    size_t mentions = inbox_unread_mentions(box, NULL);
    if (inbox_unread(box) || instance_unread(ui->connections->items[index].model))
        width += 1 + server_badge(ui, NULL, 0, 0, mentions, inbox_has_more(box), 0);
    return width > available ? available : width;
}

void conversation_draw_tabs(view *ui, auth_client *auth)
{
    bool focused = ui->focus == SERVER_TABS;
    int width = getmaxx(ui->tabs), button_width = 13;
    ui->add_hit = (click_range){width - button_width, button_width};
    int available = ui->add_hit.x - 1;
    wbkgd(ui->tabs, COLOR_PAIR(focused ? THEME_FOCUS_AREA : THEME_PANEL));
    werase(ui->tabs);
    memset(ui->tab_hits, 0, sizeof(ui->tab_hits));
    ui->back_hit = ui->next_hit = ui->channels_hit = ui->conversation_hit = (click_range){0};
    size_t cursor = ui->tab_cursor < ui->connections->count ? ui->tab_cursor : ui->active_tab;
    if (cursor < ui->first_tab)
        ui->first_tab = cursor;
    for (;;) {
        int needed = ui->first_tab ? 2 : 0;
        for (size_t i = ui->first_tab; i <= cursor; ++i)
            needed += conversation_tab_width(ui, i, available - 4);
        if (needed <= available - 2 || ui->first_tab == cursor)
            break;
        ++ui->first_tab;
    }
    int x = ui->first_tab ? 2 : 0;
    if (ui->first_tab) {
        wattrset(ui->tabs, COLOR_PAIR(THEME_PANEL_ACCENT));
        text_label(ui->tabs, 0, 0, 1, "‹");
        ui->back_hit = (click_range){0, 1};
    }
    for (size_t i = ui->first_tab; i < ui->connections->count; ++i) {
        int size = conversation_tab_width(ui, i, available - 4);
        if (x + size > available - 2) {
            wattrset(ui->tabs, COLOR_PAIR(THEME_PANEL_ACCENT));
            text_label(ui->tabs, 0, available - 1, 1, "›");
            ui->next_hit = (click_range){available - 1, 1};
            break;
        }
        bool selected = i == ui->active_tab, active = focused && i == ui->tab_cursor;
        wattrset(ui->tabs, COLOR_PAIR(active     ? THEME_FOCUSED
                                      : selected ? THEME_PANEL_ACTIVE
                                                 : THEME_PANEL_MUTED) |
                               (selected || active ? A_BOLD : A_NORMAL));
        char label[192];
        inbox *box = history_inbox(ui->connections->items[i].model);
        size_t mentions = inbox_unread_mentions(box, NULL);
        bool notified = inbox_unread(box) || instance_unread(ui->connections->items[i].model);
        int badge_width =
            notified ? server_badge(ui, NULL, 0, 0, mentions, inbox_has_more(box), 0) : 0;
        snprintf(label, sizeof(label), "%c%s",
                 selected ? '['
                 : active ? '>'
                          : ' ',
                 ui->connections->items[i].name);
        int used = text_label(ui->tabs, 0, x, size - 3 - badge_width - (notified ? 1 : 0), label);
        if (notified) {
            mvwaddch(ui->tabs, 0, x + used++, ' ');
            used += server_badge(ui, ui->tabs, 0, x + used, mentions, inbox_has_more(box),
                                 active     ? THEME_NOTIFICATION_BUTTON_FOCUS
                                 : selected ? THEME_NOTIFICATION_SELECTED
                                            : THEME_NOTIFICATION);
        }
        mvwaddch(ui->tabs, 0, x + used, selected ? ']' : active ? '<' : ' ');
        ui->tab_hits[i] = (click_range){x, size};
        x += size;
    }
    bool adding = focused && ui->tab_cursor == ui->connections->count;
    button_draw(ui->tabs, 0, ui->add_hit.x, ui->add_hit.width,
                adding ? ">+ Adicionar<" : "[+ Adicionar]", adding ? BUTTON_FOCUSED : BUTTON_NORMAL,
                BUTTON_PANEL);
    const char *areas[] = {"Abas",   "Canais",     "Histórico",    "Mensagem",
                           "Emojis", "Canais/DMs", "Configurações"};
    char summary[1200];
    snprintf(summary, sizeof(summary), "%s · Foco: %s · F2: ajustes · F6: conta",
             auth_username(auth),
             ui->focus == CHANNEL_LIST ? ui->list_dms ? "DMs" : "Canais" : areas[ui->focus]);
    wattrset(ui->tabs, COLOR_PAIR(focused ? THEME_FOCUS_MUTED : THEME_PANEL_MUTED));
    if (COLS < 80) {
        text_label(ui->tabs, 1, 0, width - 20, summary);
        ui->channels_hit = (click_range){width - 19, 8};
        ui->conversation_hit = (click_range){width - 10, 10};
        wattrset(ui->tabs, COLOR_PAIR(ui->show_channels ? THEME_PANEL_ACCENT : THEME_PANEL_MUTED));
        text_label(ui->tabs, 1, ui->channels_hit.x, 8, "[Canais]");
        wattrset(ui->tabs, COLOR_PAIR(ui->show_channels ? THEME_PANEL_MUTED : THEME_PANEL_ACCENT));
        text_label(ui->tabs, 1, ui->conversation_hit.x, 10, "[Conversa]");
    } else
        text_label(ui->tabs, 1, 0, width, summary);
    wattrset(ui->tabs,
             COLOR_PAIR(theme_member_pair(history_member_color(ui->model, auth_user_id(auth)),
                                          focused ? THEME_FOCUS_MUTED : THEME_PANEL_MUTED)));
    text_label(ui->tabs, 1, 0, COLS < 80 ? width - 20 : width, auth_username(auth));
}

bool conversation_collect_notices(view *ui, auth_client *auth)
{
    const char *connection = NULL;
    if (!auth_has_session(auth))
        connection = auth_ws_epoch(auth) && auth_ws_state(auth) != WS_OPEN
                         ? "Desconectado · Revalidando sessão…"
                         : "Revalidando sessão…";
    else if (auth_ws_reconnecting(auth))
        connection =
            auth_ws_epoch(auth) ? "Desconectado · Reconectando…" : "Conectando tempo real…";
    else
        switch (auth_ws_state(auth)) {
        case WS_OPEN:
            connection = "Tempo real conectado";
            break;
        case WS_QUEUED:
        case WS_CONNECTING:
            connection = "Conectando tempo real…";
            break;
        case WS_ERROR:
            connection = "Desconectado · Ctrl+R: reconectar";
            break;
        case WS_CLOSING:
        case WS_CLOSED:
            connection = "Desconectado";
            break;
        default:
            break;
        }
    const char *compact_connection =
        auth_ws_state(auth) == WS_OPEN && auth_has_session(auth) && !auth_ws_reconnecting(auth)
            ? "Conectado"
            : connection;
    bool changed =
        status_bar_set_compact(&ui->status, STATUS_CONNECTION, connection, compact_connection);
    ui->status.connection_critical = auth_ws_epoch(auth) && auth_ws_state(auth) != WS_OPEN;
    if (ui->status.connection_critical)
        changed |=
            status_bar_set_compact(&ui->status, STATUS_CONNECTION, connection, "Desconectado");
    inbox *box = history_inbox(ui->model);
    char notifications[192] = "";
    if (inbox_unread(box))
        snprintf(notifications, sizeof(notifications), "%zu avisos%s · F9", inbox_unread(box),
                 inbox_has_more(box) ? "+" : "");
    changed |= status_bar_set(&ui->status, STATUS_NOTIFICATIONS,
                              *inbox_notice(box) ? inbox_notice(box) : notifications);
    changed |= status_bar_set_compact(&ui->status, STATUS_DESKTOP,
                                      ui->connections->config &&
                                              ui->connections->config->desktop_notifications
                                          ? desktop_notice(ui->connections->desktop)
                                          : "",
                                      "Desktop indisponível");
    changed |= status_bar_set_compact(
        &ui->status, STATUS_SEND, *ui->editor_channel ? send_editor_feedback(ui->editor) : NULL,
        *ui->editor_channel ? send_editor_compact_notice(ui->editor) : NULL);
    /* Apply model notices in the order in which their contexts changed. */
    for (int n = 0; n < HISTORY_NOTICE_COUNT; ++n) {
        int next = -1;
        uint64_t earliest = UINT64_MAX;
        for (int kind = 0; kind < HISTORY_NOTICE_COUNT; ++kind) {
            uint64_t revision = 0;
            history_notice(ui->model, (history_notice_kind)kind, &revision);
            if (revision != ui->notice_revisions[kind] && revision < earliest) {
                next = kind;
                earliest = revision;
            }
        }
        if (next < 0)
            break;
        const char *message = history_notice(ui->model, (history_notice_kind)next, NULL);
        if (next == HISTORY_NOTICE_EVENT) {
            bool replaced = status_bar_set_compact(&ui->status, STATUS_EVENT, message,
                                                   history_event_compact(ui->model));
            changed |= replaced || status_bar_touch(&ui->status, STATUS_EVENT);
        } else
            changed |= status_bar_set(
                &ui->status, next == HISTORY_NOTICE_MUTATION ? STATUS_MUTATION : STATUS_HISTORY,
                message);
        ui->notice_revisions[next] = earliest;
    }
    int paging = history_paging_direction(ui->model);
    if (paging && paging == ui->waiting_page && !ui->at_end && ui->body) {
        static const char *frames[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
        uint64_t now = 0;
        net_now_ms(&now);
        char loading[128];
        snprintf(loading, sizeof(loading), "%s Carregando próximas mensagens",
                 frames[(now / 100) % 10]);
        changed |= status_bar_set(&ui->status, STATUS_HISTORY, loading);
        ui->paging_notice = true;
    } else if (ui->paging_notice) {
        changed |= status_bar_set(&ui->status, STATUS_HISTORY,
                                  history_notice(ui->model, HISTORY_NOTICE_READ, NULL));
        ui->paging_notice = false;
    }
    changed |= status_bar_set(&ui->status, STATUS_EDITOR, ui->feedback);
    bool replaced_copy = status_bar_set(&ui->status, STATUS_CLIPBOARD, clipboard_notice());
    changed |= replaced_copy;
    if (ui->copy_revision != clipboard_revision()) {
        if (!replaced_copy)
            changed |= status_bar_touch(&ui->status, STATUS_CLIPBOARD);
        ui->copy_revision = clipboard_revision();
    }
    char unread[128] = "";
    if (ui->unseen)
        snprintf(unread, sizeof(unread), "%llu novas mensagens · End: ir ao fim",
                 (unsigned long long)ui->unseen);
    changed |= status_bar_set(&ui->status, STATUS_UNREAD, unread);
    return changed;
}

void conversation_draw_footer(view *ui)
{
    const char *full, *shorter, *minimal;
    if (r8_modal_is_open(ui->r8) || ui->settings.open) {
        full = shorter = "↑↓ escolher · Enter abrir · Tab ações · Esc voltar";
        minimal = "Enter · Esc";
    } else if (ui->duplicate_modal) {
        full = "←→ escolher · Enter confirmar · Esc cancelar";
        shorter = "Enter confirmar · Esc cancelar";
        minimal = "Enter · Esc";
    } else if (ui->reactors_open) {
        full = "↑↓ rolar · ←→ colunas · Enter selecionar · Esc fechar";
        shorter = "↑↓ rolar · ←→ colunas · Enter · Esc";
        minimal = "↑↓ · ←→ · Enter · Esc";
    } else if (ui->context_open) {
        full = shorter = "↑↓ escolher · Enter confirmar · m/Esc fechar";
        minimal = "↑↓ · Enter · m/Esc";
    } else if (media_view_modal(ui->media)) {
        full = shorter = minimal = "Esc: voltar da mídia";
    } else if (emoji_picker_open(ui->emojis)) {
        full = "Tab áreas · ↑↓←→ escolher · Enter inserir · F5 atualizar · Esc fechar";
        shorter = "↑↓←→ escolher · Enter inserir · Esc fechar";
        minimal = "Enter inserir · Esc";
    } else if (emoji_picker_suggesting(ui->emojis)) {
        full = shorter = "↑↓ escolher · Tab/Enter inserir emoji · Esc fechar";
        minimal = "↑↓ · Tab/Enter";
    } else if (mention_popup_open(ui->mentions)) {
        full = "↑↓ escolher · Tab/Enter mencionar · Roda: rolar · Esc fechar";
        shorter = "↑↓ escolher · Tab mencionar · Esc fechar";
        minimal = "↑↓ · Tab";
    } else if (ui->navigating) {
        full = "↑↓ mensagens · ←→ mídia · Enter mídia · m menu · r responder · e reagir · E "
               "reações · Esc";
        shorter = "↑↓ mensagens · m menu · r responder · e/E reações";
        minimal = "↑↓ mensagens · m menu · r/e/E";
    } else if (ui->focus == SERVER_TABS) {
        bool adding = ui->tab_cursor == ui->connections->count;
        full = adding ? "←→ servidores · Enter adicionar · ↓ canais · Ctrl+Q sair"
                      : "←→ servidores · Enter ativar · ↓ canais · Ctrl+Q sair";
        shorter = adding ? "←→ servidores · ↓ canais · Enter adicionar"
                         : "←→ servidores · ↓ canais · Enter ativar";
        minimal = "←→ · Enter";
    } else if (ui->focus == LIST_BUTTONS) {
        full = "←→ Canais/DMs · Enter ativar · ↓ lista · F9 avisos · F10 nova DM";
        shorter = "←→ escolher · Enter ativar · ↓ lista";
        minimal = "←→ · Enter";
    } else if (ui->focus == CHANNEL_LIST) {
        bool voice = !strcmp(
            conversation_str(conversation_list_channel(ui, ui->highlighted), "type"), "voice");
        full = voice ? "↑↓ canais · Shift+Enter/F4 histórico · Enter voz (em breve) · ↑ topo"
                     : "↑↓ canais · Enter/→ abrir · ↑ topo · Ctrl+R atualizar";
        shorter = voice ? "↑↓ canais · Shift+Enter/F4 histórico · ↑ topo"
                        : "↑↓ canais · Enter abrir · ↑ topo";
        minimal = voice ? "Shift+Enter/F4 histórico" : "↑↓ · Enter";
    } else if (*ui->edit_message && ui->focus == MESSAGE_INPUT) {
        full = shorter = "Enter editar · Shift+Enter linha · Esc cancelar edição";
        minimal = "Enter editar · Esc cancelar";
    } else if (ui->focus == MESSAGE_INPUT && *conversation_reply_target(ui)) {
        full = "Enter enviar · Shift+Enter linha · F4 emojis · Esc cancelar resposta";
        shorter = "Enter enviar · Esc cancelar resposta";
        minimal = "Esc cancelar resposta";
    } else if (ui->focus == EMOJI_BUTTON) {
        full = shorter = "Enter/F4 emojis · Shift+Tab editor · Esc voltar";
        minimal = "Enter emojis";
    } else if (ui->focus == MESSAGE_INPUT) {
        full = "Enter enviar · Shift+Enter linha · Ctrl+N linha · Shift+setas selecionar · "
               "Ctrl+Shift+C copiar · "
               "Ctrl+C limpar · @ mencionar · F4 emojis · Tab botão · Esc histórico";
        shorter = "Enter enviar · Ctrl+Shift+C copiar · Esc histórico";
        minimal = "Enter";
    } else {
        full = conversation_input(ui)
                   ? "↑↓ rolar · PgUp/PgDn páginas · F7 mensagens · Digite: conversar · ← canais "
                     "· Ctrl+R atualizar"
                   : "↑↓ rolar · ← canais · Ctrl+R atualizar";
        shorter = "↑↓ rolar · F7 mensagens · Ctrl+R atualizar";
        minimal = "↑↓ · F7";
    }
    wbkgd(ui->footer, COLOR_PAIR(THEME_PANEL));
    werase(ui->footer);
    wattrset(ui->footer, COLOR_PAIR(THEME_BORDER));
    mvwhline(ui->footer, 0, 0, ACS_HLINE, getmaxx(ui->footer));
    char full_hints[768], short_hints[512], minimal_hints[256];
    bool normal = !r8_modal_is_open(ui->r8) && !ui->settings.open && !ui->duplicate_modal &&
                  !ui->reactors_open && !ui->context_open && !media_view_modal(ui->media) &&
                  !emoji_picker_open(ui->emojis) && !emoji_picker_suggesting(ui->emojis) &&
                  !mention_popup_open(ui->mentions);
    snprintf(full_hints, sizeof(full_hints), "%s%s", full,
             normal && !strstr(full, "F9") ? " · F9 avisos" : "");
    snprintf(short_hints, sizeof(short_hints), "%s%s", shorter,
             normal && !strstr(shorter, "F9") ? " · F9 avisos" : "");
    snprintf(minimal_hints, sizeof(minimal_hints), "%s%s", minimal,
             normal ? (getmaxx(ui->footer) >= 60 ? " · F9 avisos" : " · F9") : "");
    status_bar_draw(&ui->status, ui->footer, 1, full_hints, short_hints, minimal_hints);
}

void conversation_draw_confirmation(view *ui)
{
    if (!ui->duplicate_modal)
        return;
    if (!ui->confirm) {
        int width = COLS > 60 ? 56 : COLS - 4;
        ui->confirm = newwin(9, width, (LINES - 9) / 2, (COLS - width) / 2);
        if (!ui->confirm) {
            ui->duplicate_modal = false;
            ui->feedback = "Não foi possível abrir a confirmação.";
            return;
        }
    }
    WINDOW *modal = ui->confirm;
    int width = getmaxx(modal);
    wbkgd(modal, COLOR_PAIR(THEME_PANEL));
    werase(modal);
    wattrset(modal, COLOR_PAIR(THEME_BORDER));
    box(modal, 0, 0);
    wattrset(modal, COLOR_PAIR(THEME_PANEL_ACCENT) | A_BOLD);
    text_label(modal, 1, 2, width - 4, "Reenviar mensagem?");
    wattrset(modal, COLOR_PAIR(THEME_PANEL));
    text_label(modal, 3, 2, width - 4, "O servidor pode já ter criado");
    text_label(modal, 4, 2, width - 4, "esta mensagem. Reenviar pode");
    text_label(modal, 5, 2, width - 4, "criar uma duplicata.");
    button_draw(modal, 7, 2, 12, "[Cancelar]", ui->reenviar ? BUTTON_NORMAL : BUTTON_FOCUSED,
                BUTTON_PANEL);
    button_draw(modal, 7, width - 14, 12, "[Reenviar]",
                ui->reenviar ? BUTTON_FOCUSED : BUTTON_NORMAL, BUTTON_PANEL);
    wnoutrefresh(modal);
}

void conversation_draw(view *ui, auth_client *auth, draw_scope scope)
{
    bool paint_history = scope == DRAW_ALL || scope == DRAW_CONVERSATION;
    bool paint_navigation = paint_history || scope == DRAW_NAVIGATION;
    bool paint_media = paint_history || paint_navigation || scope == DRAW_MEDIA ||
                       scope == DRAW_EDITOR || emoji_picker_open(ui->emojis) ||
                       emoji_picker_suggesting(ui->emojis) || ui->reactors_open ||
                       mention_popup_open(ui->mentions) || r8_modal_is_open(ui->r8);
    graphics_update_begin();
    curs_set(0);
    if (paint_history) {
        media_view_begin(ui->media);
        icons_begin(ui->icons);
        chat_assets_visible_begin(history_assets(ui->model));
    }
    if (!paint_history && paint_media) {
        media_view_frame(ui->media);
        icons_begin_overlay(ui->icons);
    }
    if (scope == DRAW_EDITOR)
        icons_begin_editor(ui->icons);
    if (paint_navigation)
        icons_begin_navigation(ui->icons);
    if (!ui->frame) {
        erase();
        text_label(stdscr, 0, 0, COLS, "Amplie o terminal para 44×14. F6: conta.");
        refresh();
        graphics_frame_prepare();
        graphics_frame_flush();
        graphics_update_end();
        return;
    }
    if (scope == DRAW_ALL) {
        werase(ui->frame);
        wattrset(ui->frame, COLOR_PAIR(THEME_BORDER));
        box(ui->frame, 0, 0);
        mvwhline(ui->frame, CONTENT_Y - 2, 1, ACS_HLINE, COLS - 2);
        mvwaddch(ui->frame, CONTENT_Y - 2, 0, ACS_LTEE);
        mvwaddch(ui->frame, CONTENT_Y - 2, COLS - 1, ACS_RTEE);
        mvwhline(ui->frame, LINES - 4, 1, ACS_HLINE, COLS - 2);
        if (COLS >= 80)
            mvwvline(ui->frame, CONTENT_Y, ui->side + 1, ACS_VLINE, LINES - CONTENT_Y - 3);
    }
    if (paint_navigation)
        conversation_draw_tabs(ui, auth);
    conversation_draw_footer(ui);
    if (ui->list && paint_navigation) {
        bool focused = ui->focus == CHANNEL_LIST || ui->focus == LIST_BUTTONS;
        wbkgd(ui->list, COLOR_PAIR(focused ? THEME_FOCUS_AREA : THEME_PANEL));
        werase(ui->list);
        wattrset(ui->list, COLOR_PAIR(focused ? THEME_FOCUS_MUTED : THEME_PANEL_MUTED) | A_BOLD);
        conversation_list_button(ui, 0, 1, 9, "[Canais]");
        conversation_list_button(ui, 1, 11, 8, "[DMs]");
        int rows = getmaxy(ui->list) - 4;
        if (rows < 1)
            rows = 1;
        size_t count = conversation_list_count(ui);
        if (!count)
            text_label(ui->list, 1, 1, ui->side - 2,
                       ui->list_dms ? "Nenhuma DM disponível." : "Nenhum canal disponível.");
        conversation_list_normalize(ui);
        if ((int)ui->highlighted < ui->first_channel)
            ui->first_channel = (int)ui->highlighted;
        if ((int)ui->highlighted >= ui->first_channel + rows)
            ui->first_channel = (int)ui->highlighted - rows + 1;
        if (auth_has_session(auth))
            for (int row = 0; row < rows; ++row) {
                size_t index = (size_t)(ui->first_channel + row);
                json_t *c = conversation_list_channel(ui, index);
                if (!c)
                    break;
                const char *type = conversation_str(c, "type");
                bool unread = history_channel_unread(ui->model, c);
                char label[2200];
                snprintf(label, sizeof(label), "%s%s%s",
                         !strcmp(type, "category") ? "── "
                         : !strcmp(type, "voice")  ? "♪ "
                                                   : "# ",
                         unread ? "* " : "", conversation_str(c, "name"));
                if (ui->list_dms) {
                    json_t *user = json_object_get(c, "user");
                    snprintf(label, sizeof(label), "%s%s · %s (%lld)", unread ? "* " : "",
                             conversation_str(c, "name"),
                             r8_presence_label(inbox_presence(history_inbox(ui->model),
                                                              conversation_str(user, "id"))),
                             (long long)json_integer_value(json_object_get(c, "unread_count")));
                }
                bool selected = index == ui->highlighted && conversation_list_selectable(ui, index);
                size_t mentions =
                    inbox_unread_mentions(history_inbox(ui->model), conversation_str(c, "id"));
                bool notified = unread || mentions;
                int badge_width =
                    notified ? notification_badge(ui, NULL, 0, 0, mentions, false, 0) : 0;
                int pair = selected && focused ? THEME_FOCUSED
                           : selected          ? THEME_PANEL_ACTIVE
                           : focused           ? THEME_FOCUS_AREA
                                               : THEME_PANEL;
                if (ui->list_dms)
                    pair = theme_member_pair(
                        history_member_color(ui->model,
                                             conversation_str(json_object_get(c, "user"), "id")),
                        pair);
                wattrset(ui->list, COLOR_PAIR(pair));
                text_label(ui->list, row + 1, 1, ui->side - 2 - badge_width, label);
                if (notified)
                    notification_badge(ui, ui->list, row + 1, ui->side - 1 - badge_width, mentions,
                                       false,
                                       focused    ? THEME_NOTIFICATION_FOCUS
                                       : selected ? THEME_NOTIFICATION_SELECTED
                                                  : THEME_NOTIFICATION);
            }
    }
    if (ui->list && paint_navigation) {
        int y = getmaxy(ui->list) - 3, w = getmaxx(ui->list);
        wattrset(ui->list, COLOR_PAIR(THEME_BORDER));
        mvwaddch(ui->list, y, 0, ACS_ULCORNER);
        mvwhline(ui->list, y, 1, ACS_HLINE, w - 2);
        mvwaddch(ui->list, y, w - 1, ACS_URCORNER);
        mvwaddch(ui->list, y + 1, 0, ACS_VLINE);
        mvwaddch(ui->list, y + 1, w - 1, ACS_VLINE);
        mvwaddch(ui->list, y + 2, 0, ACS_LLCORNER);
        mvwhline(ui->list, y + 2, 1, ACS_HLINE, w - 2);
        mvwaddch(ui->list, y + 2, w - 1, ACS_LRCORNER);
        wattrset(ui->list, COLOR_PAIR(theme_member_pair(
                               history_member_color(ui->model, auth_user_id(auth)), THEME_PANEL)));
        text_label(ui->list, y + 1, 2, w - 9, auth_username(auth));
        button_draw(ui->list, y + 1, w - 6, 5, papo_unicode_width(0x2699) == 1 ? "[⚙]" : "[*]",
                    ui->focus == SETTINGS_BUTTON ? BUTTON_FOCUSED : BUTTON_NORMAL, BUTTON_PANEL);
    }
    if (ui->body && (paint_history || scope == DRAW_EDITOR)) {
        json_t *c = history_channel(ui->model, history_selected(ui->model));
        if (paint_history) {
            wbkgd(ui->body, COLOR_PAIR(THEME_PANEL));
            werase(ui->body);
            if (ui->focus == HISTORY_PANEL) {
                wattrset(ui->body, COLOR_PAIR(THEME_HISTORY_AREA));
                for (int row = 0; row < ui->editor_border; ++row)
                    mvwhline(ui->body, row, 0, ' ', getmaxx(ui->body));
            }
            char title[2200];
            snprintf(title, sizeof(title), "%s%s · Histórico",
                     c ? json_is_true(json_object_get(c, "_dm"))         ? "DM com "
                         : !strcmp(conversation_str(c, "type"), "voice") ? "♪ "
                                                                         : "# "
                       : "",
                     c ? conversation_str(c, "name") : "Conversa");
            wattrset(ui->body, COLOR_PAIR(ui->focus == HISTORY_PANEL ? THEME_HISTORY_MUTED
                                                                     : THEME_PANEL_MUTED) |
                                   A_BOLD);
            text_label(ui->body, 0, 2, getmaxx(ui->body) - 4, title);
            wattrset(ui->body, COLOR_PAIR(ui->focus == HISTORY_PANEL ? THEME_HISTORY_MUTED
                                                                     : THEME_PANEL_MUTED));
            if (ui->history_top == 3)
                text_label(ui->body, 1, 2, getmaxx(ui->body) - 4,
                           json_is_true(json_object_get(c, "_dm"))
                               ? r8_presence_label(inbox_presence(
                                     history_inbox(ui->model),
                                     conversation_str(json_object_get(c, "user"), "id")))
                               : conversation_str(c, "topic"));
            if (ui->history_top > 1) {
                wattrset(ui->body, COLOR_PAIR(ui->focus == HISTORY_PANEL ? THEME_HISTORY_BORDER
                                                                         : THEME_BORDER));
                mvwhline(ui->body, ui->history_top - 1, 0, ACS_HLINE, getmaxx(ui->body));
            }
            wattrset(ui->body, COLOR_PAIR(ui->focus == HISTORY_PANEL ? THEME_HISTORY_MUTED
                                                                     : THEME_PANEL_MUTED));
            if (!auth_has_session(auth))
                text_label(ui->body, ui->history_top, 2, getmaxx(ui->body) - 4,
                           "Revalidando sessão…");
            else if (history_message_count(ui->model)) {
                int ignored;
                ui->first = conversation_position(ui);
                conversation_walk(ui, ui->first, true, false, &ignored);
            } else {
                text_label(ui->body, ui->history_top, 2, getmaxx(ui->body) - 4,
                           history_status(ui->model));
            }
        }
        text_buffer *editor = conversation_input(ui);
        if (scope == DRAW_EDITOR) {
            wattrset(ui->body, COLOR_PAIR(THEME_PANEL));
            for (int row = ui->editor_border; row < getmaxy(ui->body); ++row)
                mvwhline(ui->body, row, 0, ' ', getmaxx(ui->body));
        }
        bool editing = ui->focus == MESSAGE_INPUT && !ui->duplicate_modal;
        wattrset(ui->body, COLOR_PAIR(editing ? THEME_FOCUS_BORDER : THEME_BORDER));
        mvwhline(ui->body, ui->editor_border, 0, ACS_HLINE, getmaxx(ui->body));
        wattrset(ui->body, COLOR_PAIR(editing ? THEME_FOCUS_MUTED : THEME_PANEL_MUTED));

        if (*ui->edit_message) {
            wattrset(ui->body, COLOR_PAIR(THEME_MUTED));
            text_label(ui->body, ui->editor_border, 2, getmaxx(ui->body) - 8,
                       ui->edit_pending ? "Editando mensagem · aguardando confirmação…"
                                        : "Editando mensagem · Enter editar · Esc cancelar");
            text_label(ui->body, ui->editor_border, getmaxx(ui->body) - 5, 3, "[x]");
        } else if (*conversation_reply_target(ui)) {
            char reply[TEXT_UTF8_BYTES], label[TEXT_UTF8_BYTES + 64];
            conversation_reply_label(ui, conversation_reply_target(ui), getmaxx(ui->body) - 25,
                                     reply, sizeof(reply));
            snprintf(label, sizeof(label), "↰ Respondendo a %s", reply);
            wattrset(ui->body, COLOR_PAIR(THEME_PANEL_MUTED));
            text_label(ui->body, ui->editor_border, 2, getmaxx(ui->body) - 8, label);
            text_label(ui->body, ui->editor_border, getmaxx(ui->body) - 5, 3, "[x]");
        }
        if (editor) {
            int height = conversation_input_height(ui), width = conversation_input_width(ui);
            if (editing)
                for (int row = 0; row < height + 2 * ui->editor_padding; ++row) {
                    wattrset(ui->body, COLOR_PAIR(THEME_FOCUS_AREA));
                    mvwhline(ui->body, ui->editor_border + 1 + row, 0, ' ', getmaxx(ui->body));
                }
            wattrset(ui->body, COLOR_PAIR(editing ? THEME_FOCUS_AREA : THEME_PANEL));
            text_label(ui->body, conversation_input_top(ui), 1, 1, ">");
            text_layout *measured = &ui->measured;
            text_measure(editor, width, measured);
            if (!ui->editor_scrolling)
                text_keep_cursor(editor, measured, height);
            if (!editor->length) {
                char placeholder[2200];
                if (*ui->edit_message)
                    snprintf(placeholder, sizeof(placeholder), "Novo texto da mensagem");
                else
                    snprintf(placeholder, sizeof(placeholder),
                             json_is_true(json_object_get(c, "_dm")) ? "Conversar com %s"
                                                                     : "Conversar em #%s",
                             conversation_str(c, "name"));
                wattrset(ui->body, COLOR_PAIR(editing ? THEME_FOCUS_MUTED : THEME_PANEL_MUTED));
                text_label(ui->body, conversation_input_top(ui), 3, width, placeholder);
            } else {
                char icon_scope[96];
                snprintf(icon_scope, sizeof(icon_scope), "editor:%s", ui->editor_channel);
                icons_scope(ui->icons, icon_scope, 0);
                text_draw(ui->body, editor, measured, editor->first_row, height,
                          conversation_input_top(ui), 3);
            }
            wattrset(ui->body, COLOR_PAIR(editing ? THEME_FOCUS_MUTED : THEME_PANEL_MUTED));
            if (editor->first_row)
                text_label(ui->body, conversation_input_top(ui), getmaxx(ui->body) - 2, 1, "↑");
            if (editor->first_row + height < measured->rows)
                text_label(ui->body, conversation_input_top(ui) + height - 1, getmaxx(ui->body) - 2,
                           1, "↓");
            if (editing && measured->cursor_row >= editor->first_row &&
                measured->cursor_row < editor->first_row + height) {
                wmove(ui->body,
                      conversation_input_top(ui) + measured->cursor_row - editor->first_row,
                      3 + measured->cursor_column);
            }
            wattrset(ui->body, COLOR_PAIR(editing ? THEME_FOCUS_MUTED : THEME_PANEL_MUTED));
            wattrset(ui->body, COLOR_PAIR(ui->focus == EMOJI_BUTTON ? THEME_PROFILE_BUTTON_FOCUS
                                                                    : THEME_PANEL_ACCENT) |
                                   A_BOLD);
            text_buffer smile;
            text_decode(&smile, "[😄]");
            text_icons(&smile, icons_resolve, icons_paint, ui->icons);
            text_measure(&smile, 5, measured);
            icons_scope(ui->icons, "emoji-button", 0);
            text_draw(ui->body, &smile, measured, 0, 1, conversation_input_top(ui),
                      getmaxx(ui->body) - 7);
            wattrset(ui->body, COLOR_PAIR(editing ? THEME_FOCUS_MUTED : THEME_PANEL_MUTED));
        } else {
            wattrset(ui->body, COLOR_PAIR(THEME_PANEL_MUTED));
            text_label(ui->body, conversation_input_top(ui), 3, conversation_input_width(ui),
                       auth_has_session(auth) ? "Abra um canal de texto" : "Confirme a sessão");
        }
        wattrset(ui->body, COLOR_PAIR(editing ? THEME_FOCUS_BORDER : THEME_BORDER));
        mvwhline(ui->body, conversation_input_top(ui) + ui->editor_rows + ui->editor_padding, 0,
                 ACS_HLINE, getmaxx(ui->body));
        char typing[4608];
        history_typing_text(ui->model, typing, sizeof(typing));
        if (*typing) {
            wattrset(ui->body, COLOR_PAIR(THEME_PANEL_ACCENT));
            text_label(ui->body, getmaxy(ui->body) - 1, 3, conversation_input_width(ui), typing);
        }
        if (editing && editor) {
            text_layout *measured = &ui->measured;
            text_measure(editor, conversation_input_width(ui), measured);
            if (measured->cursor_row >= editor->first_row &&
                measured->cursor_row < editor->first_row + conversation_input_height(ui))
                wmove(ui->body,
                      conversation_input_top(ui) + measured->cursor_row - editor->first_row,
                      3 + measured->cursor_column);
        }
    }
    if (scope == DRAW_ALL) {
        touchwin(ui->frame);
        wnoutrefresh(ui->frame);
    }
    if (paint_navigation) {
        wnoutrefresh(ui->tabs);
        if (ui->list)
            wnoutrefresh(ui->list);
    }
    if (ui->body && paint_history) {
        /* Force cell updates in the history viewport rather than allowing the
         * terminal's full-width scrolling region to move adjacent channels. */
        wredrawln(ui->body, ui->history_top, ui->visible);
        wnoutrefresh(ui->body);
    }
    wnoutrefresh(ui->footer);
    if (ui->focus == MESSAGE_INPUT && conversation_input(ui) && ui->body && !ui->duplicate_modal)
        wnoutrefresh(ui->body);
    conversation_draw_confirmation(ui);
    if (paint_media)
        media_view_draw(ui->media);
    if (mention_popup_open(ui->mentions) && ui->body && conversation_input(ui)) {
        text_layout *measured = &ui->measured;
        text_measure(conversation_input(ui), conversation_input_width(ui), measured);
        WINDOW *overlay =
            mention_popup_draw(ui->mentions,
                               getbegy(ui->body) + conversation_input_top(ui) +
                                   measured->cursor_row - conversation_input(ui)->first_row,
                               getbegx(ui->body) + 3 + measured->cursor_column,
                               getbegx(ui->body) + 1, getmaxx(ui->body) - 2);
        if (overlay)
            icons_overlay(ui->icons, getbegy(overlay), getbegx(overlay), getmaxy(overlay),
                          getmaxx(overlay));
    }
    if (emoji_picker_suggesting(ui->emojis) && ui->body && conversation_input(ui)) {
        text_layout *measured = &ui->measured;
        text_buffer *editor = conversation_input(ui);
        text_measure(editor, conversation_input_width(ui), measured);
        emoji_picker_draw_suggestions(ui->emojis,
                                      getbegy(ui->body) + conversation_input_top(ui) +
                                          measured->cursor_row - editor->first_row,
                                      getbegx(ui->body) + 3 + measured->cursor_column,
                                      getbegx(ui->body) + 1, getmaxx(ui->body) - 2);
    }
    bool caret = false;
    if (ui->focus == MESSAGE_INPUT && conversation_input(ui) && ui->body && !ui->duplicate_modal &&
        !(r8_modal_is_open(ui->r8) || ui->settings.open) && !media_view_modal(ui->media) &&
        !emoji_picker_open(ui->emojis)) {
        text_buffer *editor = conversation_input(ui);
        text_layout *measured = &ui->measured;
        text_measure(editor, conversation_input_width(ui), measured);
        caret = measured->cursor_row >= editor->first_row &&
                measured->cursor_row < editor->first_row + conversation_input_height(ui);
        if (caret)
            setsyx(getbegy(ui->body) + conversation_input_top(ui) + measured->cursor_row -
                       editor->first_row,
                   getbegx(ui->body) + 3 + measured->cursor_column);
    }
    if (!caret)
        setsyx(-1, -1);
    if (paint_media && (ui->settings.open || media_view_modal(ui->media) || ui->duplicate_modal))
        icons_begin(ui->icons);
    if (emoji_picker_open(ui->emojis)) {
        emoji_picker_draw(ui->emojis);
        caret = emoji_picker_cursor(ui->emojis);
    }
    conversation_interactions_draw(ui);
    WINDOW *overlay = r8_modal_draw(ui->r8);
    if (overlay)
        icons_overlay(ui->icons, getbegy(overlay), getbegx(overlay), getmaxy(overlay),
                      getmaxx(overlay));
    if (paint_media) {
        if (!ui->duplicate_modal && !ui->settings.open)
            media_view_prepare(ui->media);
        icons_prepare(ui->icons);
        graphics_frame_prepare();
    }
    settings_draw(&ui->settings);
    if (r8_modal_is_open(ui->r8) || ui->settings.open) {
        caret = !ui->settings.open && r8_modal_cursor(ui->r8);
        if (!caret)
            setsyx(-1, -1);
    }
    if (ui->context_open || ui->reactors_open) {
        caret = false;
        setsyx(-1, -1);
    }
    doupdate();
    if (paint_media)
        graphics_frame_flush();
    graphics_update_end();
    /* doupdate may visit every message while painting. Expose only the final
     * caret, after curses and direct graphics output have completed. */
    curs_set(caret ? 1 : 0);
}
