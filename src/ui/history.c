#define _POSIX_C_SOURCE 200809L
#include "ui/history.h"
#include "model/history.h"
#include "ui/text.h"
#include "ui/theme.h"
#include "ui/button.h"
#include "ui/media.h"
#include "ui/graphics.h"
#include "ui/mouse.h"
#include "app/session_store.h"
#include "ui/send_editor.h"
#include "ui/mention_popup.h"
#include "ui/status_bar.h"
#include "ui/clipboard.h"
#include "ui/r8_modal.h"
#include "app/desktop.h"
#include "config/profiles.h"
#include "net/clock.h"
#include <wctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PASTE_START (KEY_MAX + 1)
#define PASTE_END (KEY_MAX + 2)
#define CONTENT_Y 5

enum { SERVER_TABS, CHANNEL_LIST, HISTORY_PANEL, MESSAGE_INPUT, LIST_BUTTONS };
typedef enum { DRAW_ALL, DRAW_CONVERSATION, DRAW_NAVIGATION } draw_scope;
typedef struct { int x, width; } click_range;
typedef struct {
    history *model;
    r8_modal *r8;
    bool list_dms;
    int list_button;
    size_t highlights[2];
    uint64_t inbox_revision;
    uint64_t view_requested_until;
    mention_popup *mentions;
    media_view *media;
    char block[128];
    send_editor *editor; /* Borrowed from the instance connection. */
    instance_connections *connections;
    WINDOW *frame, *tabs, *footer, *list, *body, *confirm;
    int width, height, side, visible, first_channel, focus, first, editor_border, history_top, editor_rows, editor_padding;
    size_t highlighted, active_tab, tab_cursor, first_tab;
    click_range tab_hits[INSTANCE_LIMIT], add_hit, back_hit, next_hit, channels_hit, conversation_hit;
    char anchor[37];
    char editor_channel[64];
    const char *feedback;
    status_bar status;
    uint64_t notice_revisions[HISTORY_NOTICE_COUNT];
    uint64_t copy_revision;
    uint64_t retry_id;
    uint64_t unseen;
    size_t section, offset;
    bool at_end, paste, show_channels, layout_failed, duplicate_modal, reenviar;
    bool dragging, editor_scrolling;
    text_paste paste_data;
} view;
static const char *str(json_t *obj, const char *key)
{
    const char *s = json_string_value(json_object_get(obj, key));
    return s ? s : "";
}
static size_t list_index(view *ui, size_t row)
{
    for (size_t i=0;i<history_channel_count(ui->model);++i) {
        json_t *c=history_channel(ui->model,i);
        if (json_is_true(json_object_get(c,"_dm"))!=ui->list_dms) continue;
        if (!row--) return i;
    }
    return SIZE_MAX;
}
static size_t list_count(view *ui)
{
    size_t n=0;
    for (size_t i=0;i<history_channel_count(ui->model);++i)
        if (json_is_true(json_object_get(history_channel(ui->model,i),"_dm"))==ui->list_dms) ++n;
    return n;
}
static json_t *list_channel(view *ui,size_t row) { return history_channel(ui->model,list_index(ui,row)); }
static bool list_notified(view *ui, bool dms)
{
    for (size_t i=0;i<history_channel_count(ui->model);++i) {
        json_t *channel=history_channel(ui->model,i);
        if (json_is_true(json_object_get(channel,"_dm"))==dms && history_channel_unread(ui->model,channel)) return true;
    }
    return false;
}
static void notification_dot(WINDOW *window, int row, int column, int pair)
{
    attr_t previous=getattrs(window);
    wattrset(window,(has_colors() ? COLOR_PAIR(pair) : 0) | A_BOLD);
    mvwaddstr(window,row,column,"●");
    wattrset(window,previous);
}
static void list_button(view *ui, int button, int column, int width, const char *label)
{
    button_state state=ui->focus==LIST_BUTTONS && ui->list_button==button ? BUTTON_FOCUSED :
                       ui->list_dms==(button!=0) ? BUTTON_SELECTED : BUTTON_NORMAL;
    button_draw(ui->list,0,column,width,label,state,BUTTON_PANEL);
    if (list_notified(ui,button!=0)) notification_dot(ui->list,0,column+width-1,
        state==BUTTON_FOCUSED ? NOTIFICATION_BUTTON_FOCUS : state==BUTTON_SELECTED ? NOTIFICATION_SELECTED : NOTIFICATION);
}
static void switch_list(view *ui,bool dms)
{
    ui->highlights[ui->list_dms]=ui->highlighted;
    ui->list_dms=dms; ui->list_button=dms; ui->highlighted=ui->highlights[dms]; ui->first_channel=0;
}
static void destroy_layout(view *ui)
{
    if (ui->confirm) delwin(ui->confirm);
    if (ui->list) delwin(ui->list);
    if (ui->body) delwin(ui->body);
    if (ui->footer) delwin(ui->footer);
    if (ui->tabs) delwin(ui->tabs);
    if (ui->frame) delwin(ui->frame);
    ui->confirm = ui->list = ui->body = ui->footer = ui->tabs = ui->frame = NULL;
}
static bool layout(view *ui)
{
    graphics_frame_invalidate();
    destroy_layout(ui);
    /* Consume stdscr's resize damage without flushing a blank screen. The
     * replacement windows are composed before the next single doupdate(). */
    erase(); wnoutrefresh(stdscr);
    ui->width = COLS; ui->height = LINES;
    if (COLS < 44 || LINES < 14) return true;
    bool wide = COLS >= 80;
    int rows = LINES - CONTENT_Y - 3;
    ui->side = wide ? 24 : COLS - 2;
    ui->history_top = rows >= 8 ? 3 : rows >= 7 ? 2 : 1;
    /* Reserve a visible history row, both input borders and the typing row. */
    ui->editor_padding = rows - ui->history_top >= 7 ? 1 : 0;
    ui->editor_rows = 1; ui->editor_border = rows - 4 - 2 * ui->editor_padding;
    ui->visible = ui->editor_border - ui->history_top;
    ui->frame = newwin(LINES, COLS, 0, 0);
    if (!ui->frame) return false;
    wbkgd(ui->frame, COLOR_PAIR(NORMAL));
    ui->tabs = derwin(ui->frame, 2, COLS - 2, 1, 1);
    ui->footer = derwin(ui->frame, 2, COLS - 2, LINES - 3, 1);
    if (wide || ui->show_channels) ui->list = derwin(ui->frame, rows, ui->side, CONTENT_Y, 1);
    if (wide || !ui->show_channels) ui->body = derwin(ui->frame, rows, wide ? COLS - 3 - ui->side : COLS - 2, CONTENT_Y, wide ? ui->side + 2 : 1);
    bool ok = ui->tabs && ui->footer && (wide ? ui->list && ui->body : ui->list || ui->body);
    ui->layout_failed = !ok;
    return ok;
}
static bool hit(click_range range, int x)
{
    return range.width > 0 && x >= range.x && x < range.x + range.width;
}
static int tab_width(view *ui, size_t index, int available)
{
    int width = text_label(NULL, 0, 0, 128, ui->connections->items[index].name) + 4;
    inbox *box=history_inbox(ui->connections->items[index].model);
    if (inbox_unread(box) || inbox_has_more(box)) width+=8;
    if (inbox_unseen_conversations(box)) width+=2;
    return width > available ? available : width;
}
static void draw_tabs(view *ui, auth_client *auth)
{
    bool focused = ui->focus == SERVER_TABS;
    int width = getmaxx(ui->tabs), button_width = 13;
    ui->add_hit = (click_range){width - button_width, button_width};
    int available = ui->add_hit.x - 1;
    wbkgd(ui->tabs, COLOR_PAIR(focused ? FOCUS_AREA : PANEL)); werase(ui->tabs);
    memset(ui->tab_hits, 0, sizeof(ui->tab_hits));
    ui->back_hit = ui->next_hit = ui->channels_hit = ui->conversation_hit = (click_range){0};
    size_t cursor = ui->tab_cursor < ui->connections->count ? ui->tab_cursor : ui->active_tab;
    if (cursor < ui->first_tab) ui->first_tab = cursor;
    for (;;) {
        int needed = ui->first_tab ? 2 : 0;
        for (size_t i = ui->first_tab; i <= cursor; ++i) needed += tab_width(ui, i, available - 4);
        if (needed <= available - 2 || ui->first_tab == cursor) break;
        ++ui->first_tab;
    }
    int x = ui->first_tab ? 2 : 0;
    if (ui->first_tab) {
        wattrset(ui->tabs, COLOR_PAIR(PANEL_ACCENT));
        text_label(ui->tabs, 0, 0, 1, "‹"); ui->back_hit = (click_range){0, 1};
    }
    for (size_t i = ui->first_tab; i < ui->connections->count; ++i) {
        int size = tab_width(ui, i, available - 4);
        if (x + size > available - 2) {
            wattrset(ui->tabs, COLOR_PAIR(PANEL_ACCENT));
            text_label(ui->tabs, 0, available - 1, 1, "›"); ui->next_hit = (click_range){available - 1, 1}; break;
        }
        bool selected = i == ui->active_tab, active = focused && i == ui->tab_cursor;
        wattrset(ui->tabs, COLOR_PAIR(active ? FOCUSED : selected ? PANEL_ACTIVE : PANEL_MUTED) | (selected || active ? A_BOLD : A_NORMAL));
        char label[192];
        inbox *box=history_inbox(ui->connections->items[i].model);
        char badge[32]="";
        if (inbox_unread(box) || inbox_has_more(box)) snprintf(badge,sizeof(badge)," · %zu%s",inbox_unread(box),inbox_has_more(box) ? "+" : "");
        snprintf(label, sizeof(label), "%c%s%s%s%c", selected ? '[' : active ? '>' : ' ', ui->connections->items[i].name,badge,
                 inbox_unseen_conversations(box) ? " *" : "",selected ? ']' : active ? '<' : ' ');
        ui->tab_hits[i] = (click_range){x, text_label(ui->tabs, 0, x, size, label)};
        x += size;
    }
    bool adding = focused && ui->tab_cursor == ui->connections->count;
    button_draw(ui->tabs, 0, ui->add_hit.x, ui->add_hit.width, adding ? ">+ Adicionar<" : "[+ Adicionar]",
                adding ? BUTTON_FOCUSED : BUTTON_NORMAL, BUTTON_PANEL);
    const char *areas[] = {"Abas", "Lista", "Histórico", "Mensagem", "Canais/DMs"};
    char summary[1200];
    snprintf(summary, sizeof(summary), "%s · Foco: %s · F6: conta", auth_username(auth),
             ui->focus==CHANNEL_LIST ? ui->list_dms ? "DMs" : "Canais" : areas[ui->focus]);
    wattrset(ui->tabs, COLOR_PAIR(focused ? FOCUS_MUTED : PANEL_MUTED));
    if (COLS < 80) {
        text_label(ui->tabs, 1, 0, width - 20, summary);
        ui->channels_hit = (click_range){width - 19, 8}; ui->conversation_hit = (click_range){width - 10, 10};
        wattrset(ui->tabs, COLOR_PAIR(ui->show_channels ? PANEL_ACCENT : PANEL_MUTED));
        text_label(ui->tabs, 1, ui->channels_hit.x, 8, "[Canais]");
        wattrset(ui->tabs, COLOR_PAIR(ui->show_channels ? PANEL_MUTED : PANEL_ACCENT));
        text_label(ui->tabs, 1, ui->conversation_hit.x, 10, "[Conversa]");
    } else text_label(ui->tabs, 1, 0, width, summary);
}
/* Walk segments with a stable UUID/section/Unicode-offset anchor. Reflow after
 * resize maps the offset back to its new row; prepending pages adds no drift. */
static int walk(view *ui, int first, bool paint, bool save, int *anchor)
{
    uint64_t now=0; net_now_ms(&now);
    bool observed=text_terminal_focused() || (text_terminal_focus_unknown() && now<ui->view_requested_until);
    int total = 0, width = ui->body ? getmaxx(ui->body) - 4 : ui->width - 4;
    *anchor = 0;
    for (size_t i = 0; i < history_message_count(ui->model); ++i) {
        json_t *m = history_message(ui->model, i);
        const char *reply = "mensagem não disponível";
        if (*str(m, "reply_to")) for (size_t j = 0; j < history_message_count(ui->model); ++j)
            if (!strcmp(str(m, "reply_to"), str(history_message(ui->model, j), "id"))) { reply = str(m, "reply_to"); break; }
        message_blocks blocks;
        if (!message_blocks_build_sized_mentions(&blocks, m, history_author(ui->model, str(m, "author_id")), reply, width,
                                  graphics_terminal_get().supported && ui->media, media_view_measure, ui->media,
                                  history_mention_name, ui->model,
                                  auth_user_id(ui->connections->items[ui->active_tab].auth))) continue;
        bool anchored = !strcmp(ui->anchor, str(m, "id"));
        if (anchored) *anchor = total;
        bool *offered = paint && ui->media ? calloc(blocks.media_count + 1, sizeof(*offered)) : NULL;
        size_t rows = blocks.count;
        /* At minimum height, retain the final message instead of its spacing. */
        if (ui->visible == 1 && i + 1 == history_message_count(ui->model) && rows &&
            !blocks.rows[rows - 1].count && !blocks.rows[rows - 1].media_count &&
            !blocks.rows[rows - 1].card) --rows;
        for (size_t row = 0; row < rows; ++row) {
            const message_row *r = &blocks.rows[row];
            char key[128]; size_t offset = 0;
            if (r->count) { snprintf(key, sizeof(key), "%s", r->segments[0].key); offset = r->segments[0].offset; }
            else if (r->media_count) {
                const message_media_region *region = &r->media[0];
                const message_media *item = &blocks.media[region->index];
                snprintf(key, sizeof(key), "media:%s:%s", item->id, item->role);
                offset = (size_t)(region->row < 0 ? 0 : region->row);
            } else snprintf(key, sizeof(key), "gap:%zu", row);
            if (anchored && !strcmp(key, ui->block) && offset <= ui->offset) *anchor = total + (int)row;
            if (save && first == total + (int)row) {
                snprintf(ui->anchor, sizeof(ui->anchor), "%s", str(m, "id"));
                snprintf(ui->block, sizeof(ui->block), "%s", key); ui->offset = offset;
            }
            if (paint && total + (int)row >= first && total + (int)row < first + ui->visible) {
                int y = ui->history_top + total + (int)row - first;
                int pair = ui->focus == HISTORY_PANEL ? HISTORY_AREA : PANEL;
                int accent = ui->focus == HISTORY_PANEL ? HISTORY_ACCENT : PANEL_ACCENT;
                if (r->count && !strcmp(r->segments[0].key, "header")) pair = accent;
                message_row_draw(ui->body, r, y, 2, width, pair, accent);
                if (observed && !r8_modal_is_open(ui->r8) && !media_view_modal(ui->media)) {
                    if ((r->count && strcmp(r->segments[0].key,"header")) || r->media_count)
                        inbox_view_message(history_inbox(ui->model),str(m,"id"));
                    if (ui->at_end && (r->count || r->media_count)) history_read_visible(ui->model,str(m,"id"));
                }
                if (blocks.self_mention && row + 1 < blocks.count) {
                    wattrset(ui->body, COLOR_PAIR(accent) | A_BOLD);
                    mvwaddch(ui->body, y, 0, ACS_VLINE);
                }
                for (int k = 0; k < r->media_count && offered && ui->media; ++k) {
                    const message_media_region *region = &r->media[k];
                    if (offered[region->index]) continue;
                    offered[region->index] = true;
                    int start = total + (int)row - region->row;
                    int top = first > start ? first - start : 0;
                    int bottom = start + region->height > first + ui->visible ? start + region->height - first - ui->visible : 0;
                    if (bottom > region->height) bottom = region->height;
                    int graphics_y = getbegy(ui->body) + ui->history_top + (start > first ? start - first : 0);
                    if (!region->height) graphics_y = getbegy(ui->body) + y;
                    media_view_offer(ui->media, &blocks.media[region->index], graphics_y,
                                     getbegx(ui->body) + 2 + region->x,
                                     region->width, region->height, top, bottom);
                }
            }
        }
        total += (int)rows; free(offered); message_blocks_free(&blocks);
    }
    return total;
}
static int position(view *ui)
{
    int anchor;
    int total = walk(ui, 0, false, false, &anchor);
    int last = total > ui->visible ? total - ui->visible : 0;
    return ui->at_end ? last : anchor > last ? last : anchor;
}
static void scroll_history(view *ui, int delta)
{
    if (!ui->body) return;
    if (!history_message_count(ui->model) && delta<0) {
        ui->at_end=false; ui->anchor[0]=0; return;
    }
    int anchor, total = walk(ui, 0, false, false, &anchor);
    int last = total > ui->visible ? total - ui->visible : 0;
    int first = (ui->at_end ? last : anchor) + delta;
    if (first < 0) first = 0;
    if (first > last) first = last;
    ui->at_end = first == last;
    if (ui->at_end) ui->unseen = 0;
    walk(ui, first, false, true, &anchor);
}
static text_buffer *input(view *ui)
{
    json_t *channel = history_channel(ui->model, history_selected(ui->model));
    return channel && !strcmp(str(channel, "type"), "text") &&
        !strcmp(str(channel, "id"), ui->editor_channel) ? send_editor_input(ui->editor) : NULL;
}
static int input_height(view *ui)
{
    return ui->editor_rows;
}
static int input_top(view *ui)
{
    return ui->editor_border + 1 + ui->editor_padding;
}
static bool input_row(view *ui, int row)
{
    return row >= input_top(ui) && row < input_top(ui) + input_height(ui);
}
static bool editor_geometry(view *ui)
{
    if (!ui->body) return false;
    int rows = 1;
    text_buffer *buffer = input(ui);
    if (buffer) { text_layout measured; text_measure(buffer, getmaxx(ui->body) - 6, &measured); rows = measured.rows; }
    int maximum = getmaxy(ui->body) - ui->history_top - 2 * ui->editor_padding - 4;
    if (maximum > 5) maximum = 5;
    if (maximum < 1) maximum = 1;
    if (rows > maximum) rows = maximum;
    bool changed = rows != ui->editor_rows;
    ui->editor_rows = rows; ui->editor_border = getmaxy(ui->body) - rows - 2 * ui->editor_padding - 3;
    ui->visible = ui->editor_border - ui->history_top;
    return changed;
}
static bool collect_notices(view *ui, auth_client *auth)
{
    const char *connection = NULL;
    if (!auth_has_session(auth))
        connection = auth_ws_epoch(auth) && auth_ws_state(auth)!=WS_OPEN ? "Desconectado · Revalidando sessão…" : "Revalidando sessão…";
    else if (auth_ws_reconnecting(auth))
        connection = auth_ws_epoch(auth) ? "Desconectado · Reconectando…" : "Conectando tempo real…";
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
    const char *compact_connection = auth_ws_state(auth) == WS_OPEN && auth_has_session(auth) &&
                                     !auth_ws_reconnecting(auth) ? "Conectado" : connection;
    bool changed = status_bar_set_compact(&ui->status, STATUS_CONNECTION, connection, compact_connection);
    ui->status.connection_critical=auth_ws_epoch(auth) && auth_ws_state(auth)!=WS_OPEN;
    if (ui->status.connection_critical) changed |= status_bar_set_compact(&ui->status,STATUS_CONNECTION,connection,"Desconectado");
    inbox *box=history_inbox(ui->model);
    char notifications[192]="";
    if (inbox_unread(box)) snprintf(notifications,sizeof(notifications),"%zu avisos%s · F9",inbox_unread(box),inbox_has_more(box) ? "+" : "");
    changed |= status_bar_set(&ui->status,STATUS_NOTIFICATIONS,*inbox_notice(box) ? inbox_notice(box) : notifications);
    changed |= status_bar_set_compact(&ui->status,STATUS_DESKTOP,
        ui->connections->config && ui->connections->config->desktop_notifications ? desktop_notice(ui->connections->desktop) : "","Desktop indisponível");
    changed |= status_bar_set_compact(&ui->status, STATUS_SEND,
                                      *ui->editor_channel ? send_editor_feedback(ui->editor) : NULL,
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
        if (next < 0) break;
        const char *message = history_notice(ui->model, (history_notice_kind)next, NULL);
        if (next == HISTORY_NOTICE_EVENT) {
            bool replaced =
                status_bar_set_compact(&ui->status, STATUS_EVENT, message, history_event_compact(ui->model));
            changed |= replaced || status_bar_touch(&ui->status, STATUS_EVENT);
        } else
            changed |= status_bar_set(&ui->status, STATUS_HISTORY, message);
        ui->notice_revisions[next] = earliest;
    }
    changed |= status_bar_set(&ui->status, STATUS_EDITOR, ui->feedback);
    bool replaced_copy = status_bar_set(&ui->status, STATUS_CLIPBOARD, clipboard_notice());
    changed |= replaced_copy;
    if (ui->copy_revision != clipboard_revision()) {
        if (!replaced_copy) changed |= status_bar_touch(&ui->status, STATUS_CLIPBOARD);
        ui->copy_revision = clipboard_revision();
    }
    char unread[128] = "";
    if (ui->unseen)
        snprintf(unread, sizeof(unread), "%llu novas mensagens · End: ir ao fim",
                 (unsigned long long)ui->unseen);
    changed |= status_bar_set(&ui->status, STATUS_UNREAD, unread);
    return changed;
}
static void draw_footer(view *ui)
{
    const char *full, *shorter, *minimal;
    if (r8_modal_is_open(ui->r8)) {
        full=shorter="↑↓ escolher · Enter abrir · Tab ações · Esc voltar"; minimal="Enter · Esc";
    } else if (ui->duplicate_modal) {
        full = "←→ escolher · Enter confirmar · Esc cancelar";
        shorter = "Enter confirmar · Esc cancelar";
        minimal = "Enter · Esc";
    } else if (media_view_modal(ui->media)) {
        full = shorter = minimal = "Esc: voltar da mídia";
    } else if (mention_popup_open(ui->mentions)) {
        full = "↑↓ escolher · Tab/Enter mencionar · Roda: rolar · Esc fechar";
        shorter = "↑↓ escolher · Tab mencionar · Esc fechar";
        minimal = "↑↓ · Tab";
    } else if (ui->focus == SERVER_TABS) {
        bool adding = ui->tab_cursor == ui->connections->count;
        full = adding ? "←→ servidores · Enter adicionar · ↓ canais · Ctrl+Q sair"
                      : "←→ servidores · Enter ativar · ↓ canais · Ctrl+Q sair";
        shorter =
            adding ? "←→ servidores · ↓ canais · Enter adicionar" : "←→ servidores · ↓ canais · Enter ativar";
        minimal = "←→ · Enter";
    } else if (ui->focus == LIST_BUTTONS) {
        full="←→ Canais/DMs · Enter ativar · ↓ lista · F9 avisos · F10 nova DM";
        shorter="←→ escolher · Enter ativar · ↓ lista"; minimal="←→ · Enter";
    } else if (ui->focus == CHANNEL_LIST) {
        full = "↑↓ canais · Enter/→ abrir · ← abas · Ctrl+R atualizar";
        shorter = "↑↓ canais · Enter abrir · ← abas";
        minimal = "↑↓ · Enter";
    } else if (ui->focus == MESSAGE_INPUT) {
        full = "Enter enviar · Ctrl+N linha · Shift+setas selecionar · Ctrl+Shift+C copiar · Ctrl+C limpar · @ mencionar · Esc histórico";
        shorter = "Enter enviar · Ctrl+Shift+C copiar · Esc histórico";
        minimal = "Enter";
    } else {
        full = input(ui)
                   ? "↑↓ rolar · PgUp/PgDn páginas · F7 mídia · Digite: conversar · ← canais · Ctrl+R atualizar"
                   : "↑↓ rolar · ← canais · Ctrl+R atualizar";
        shorter = "↑↓ rolar · F7 mídia · Ctrl+R atualizar";
        minimal = "↑↓ · F7";
    }
    wbkgd(ui->footer, COLOR_PAIR(PANEL));
    werase(ui->footer);
    wattrset(ui->footer, COLOR_PAIR(BORDER));
    mvwhline(ui->footer, 0, 0, ACS_HLINE, getmaxx(ui->footer));
    status_bar_draw(&ui->status, ui->footer, 1, full, shorter, minimal);
}

static void draw_confirmation(view *ui)
{
    if (!ui->duplicate_modal) return;
    if (!ui->confirm) {
        int width = COLS > 60 ? 56 : COLS - 4;
        ui->confirm = newwin(9, width, (LINES - 9) / 2, (COLS - width) / 2);
        if (!ui->confirm) { ui->duplicate_modal = false; ui->feedback = "Não foi possível abrir a confirmação."; return; }
    }
    WINDOW *modal = ui->confirm;
    int width = getmaxx(modal);
    wbkgd(modal, COLOR_PAIR(PANEL)); werase(modal);
    wattrset(modal, COLOR_PAIR(BORDER)); box(modal, 0, 0);
    wattrset(modal, COLOR_PAIR(PANEL_ACCENT) | A_BOLD);
    text_label(modal, 1, 2, width - 4, "Reenviar mensagem?");
    wattrset(modal, COLOR_PAIR(PANEL));
    text_label(modal, 3, 2, width - 4, "O servidor pode já ter criado");
    text_label(modal, 4, 2, width - 4, "esta mensagem. Reenviar pode");
    text_label(modal, 5, 2, width - 4, "criar uma duplicata.");
    button_draw(modal, 7, 2, 12, "[Cancelar]", ui->reenviar ? BUTTON_NORMAL : BUTTON_FOCUSED, BUTTON_PANEL);
    button_draw(modal, 7, width - 14, 12, "[Reenviar]", ui->reenviar ? BUTTON_FOCUSED : BUTTON_NORMAL, BUTTON_PANEL);
    wnoutrefresh(modal);
}
static void draw(view *ui, auth_client *auth, draw_scope scope)
{
    graphics_update_begin();
    curs_set(0);
    if (scope != DRAW_NAVIGATION) {
        media_view_begin(ui->media);
        message_colors_begin();
    }
    if (!ui->frame) {
        erase(); text_label(stdscr, 0, 0, COLS, "Amplie o terminal para 44×14. F6: conta."); refresh(); graphics_frame_prepare(); graphics_frame_flush(); graphics_update_end(); return;
    }
    if (scope == DRAW_ALL) {
        werase(ui->frame); wattrset(ui->frame, COLOR_PAIR(BORDER)); box(ui->frame, 0, 0);
        mvwhline(ui->frame, CONTENT_Y - 2, 1, ACS_HLINE, COLS - 2);
        mvwaddch(ui->frame, CONTENT_Y - 2, 0, ACS_LTEE);
        mvwaddch(ui->frame, CONTENT_Y - 2, COLS - 1, ACS_RTEE);
        mvwhline(ui->frame, LINES - 4, 1, ACS_HLINE, COLS - 2);
        if (COLS >= 80) mvwvline(ui->frame, CONTENT_Y, ui->side + 1, ACS_VLINE, LINES - CONTENT_Y - 3);
    }
    if (scope != DRAW_CONVERSATION) draw_tabs(ui, auth);
    draw_footer(ui);
    if (ui->list && scope != DRAW_CONVERSATION) {
        bool focused=ui->focus==CHANNEL_LIST || ui->focus==LIST_BUTTONS;
        wbkgd(ui->list, COLOR_PAIR(focused ? FOCUS_AREA : PANEL)); werase(ui->list);
        wattrset(ui->list, COLOR_PAIR(focused ? FOCUS_MUTED : PANEL_MUTED) | A_BOLD);
        list_button(ui,0,1,9,"[Canais]");
        list_button(ui,1,11,8,"[DMs]");
        int rows = getmaxy(ui->list) - 2;
        size_t count = list_count(ui);
        if (!count) text_label(ui->list,1,1,ui->side-2,ui->list_dms ? "Nenhuma DM disponível." : "Nenhum canal disponível.");
        if (ui->highlighted >= count) ui->highlighted = count ? count - 1 : 0;
        if ((int)ui->highlighted < ui->first_channel) ui->first_channel = (int)ui->highlighted;
        if ((int)ui->highlighted >= ui->first_channel + rows) ui->first_channel = (int)ui->highlighted - rows + 1;
        if (auth_has_session(auth)) for (int row = 0; row < rows; ++row) {
            size_t index = (size_t)(ui->first_channel + row);
            json_t *c = list_channel(ui, index);
            if (!c) break;
            const char *type = str(c, "type");
            bool unread = history_channel_unread(ui->model,c);
            char label[2200];
            snprintf(label, sizeof(label), "%c %s%s%s", list_index(ui,index) == history_selected(ui->model) ? '>' : index == ui->highlighted ? '*' : ' ',
                     !strcmp(type, "category") ? "── " : !strcmp(type, "voice") ? "♪ " : "# ", unread ? "* " : "", str(c, "name"));
            if (ui->list_dms) {
                json_t *user=json_object_get(c,"user");
                snprintf(label,sizeof(label),"%s%s · %s (%lld)",unread ? "* " : "",
                    str(c,"name"),r8_presence_label(inbox_presence(history_inbox(ui->model),str(user,"id"))),
                    (long long)json_integer_value(json_object_get(c,"unread_count")));
            }
            bool selected = index == ui->highlighted;
            bool notified=unread;
            wattrset(ui->list, COLOR_PAIR(selected && focused ? FOCUSED : selected ? PANEL_ACTIVE : focused ? FOCUS_AREA : PANEL));
            text_label(ui->list, row + 1, 1, ui->side - 2 - (notified ? 1 : 0), label);
            if (notified) notification_dot(ui->list,row+1,ui->side-2,
                focused ? NOTIFICATION_FOCUS : selected ? NOTIFICATION_SELECTED : NOTIFICATION);
        }
    }
    if (ui->body && scope != DRAW_NAVIGATION) {
        wbkgd(ui->body, COLOR_PAIR(PANEL)); werase(ui->body);
        if (ui->focus == HISTORY_PANEL) {
            wattrset(ui->body, COLOR_PAIR(HISTORY_AREA));
            for (int row = 0; row < ui->editor_border; ++row)
                mvwhline(ui->body, row, 0, ' ', getmaxx(ui->body));
        }
        json_t *c = history_channel(ui->model, history_selected(ui->model));
        char title[2200]; snprintf(title, sizeof(title), "%s%s · Histórico", c ? json_is_true(json_object_get(c,"_dm")) ? "DM com " : "# " : "", c ? str(c, "name") : "Conversa");
        wattrset(ui->body, COLOR_PAIR(ui->focus == HISTORY_PANEL ? HISTORY_MUTED : PANEL_MUTED) | A_BOLD);
        text_label(ui->body, 0, 2, getmaxx(ui->body) - 4, title);
        wattrset(ui->body, COLOR_PAIR(ui->focus == HISTORY_PANEL ? HISTORY_MUTED : PANEL_MUTED));
        if (ui->history_top == 3) text_label(ui->body, 1, 2, getmaxx(ui->body) - 4, json_is_true(json_object_get(c,"_dm")) ? r8_presence_label(inbox_presence(history_inbox(ui->model),str(json_object_get(c,"user"),"id"))) : str(c, "topic"));
        if (ui->history_top > 1) {
            wattrset(ui->body, COLOR_PAIR(ui->focus == HISTORY_PANEL ? HISTORY_BORDER : BORDER));
            mvwhline(ui->body, ui->history_top - 1, 0, ACS_HLINE, getmaxx(ui->body));
        }
        wattrset(ui->body, COLOR_PAIR(ui->focus == HISTORY_PANEL ? HISTORY_MUTED : PANEL_MUTED));
        if (!auth_has_session(auth)) text_label(ui->body, ui->history_top, 2, getmaxx(ui->body) - 4, "Revalidando sessão…");
        else if (history_message_count(ui->model)) {
            int ignored; ui->first = position(ui); walk(ui, ui->first, true, false, &ignored);
        } else {
            text_label(ui->body, ui->history_top, 2, getmaxx(ui->body) - 4, history_status(ui->model));
        }
        text_buffer *editor = input(ui);
        bool editing = ui->focus == MESSAGE_INPUT && !ui->duplicate_modal;
        wattrset(ui->body, COLOR_PAIR(editing ? FOCUS_BORDER : BORDER));
        mvwhline(ui->body, ui->editor_border, 0, ACS_HLINE, getmaxx(ui->body));
        wattrset(ui->body, COLOR_PAIR(editing ? FOCUS_MUTED : PANEL_MUTED));

        if (editor) {
            int height = input_height(ui), width = getmaxx(ui->body) - 6;
            if (editing) for (int row = 0; row < height + 2 * ui->editor_padding; ++row) {
                wattrset(ui->body, COLOR_PAIR(FOCUS_AREA));
                mvwhline(ui->body, ui->editor_border + 1 + row, 0, ' ', getmaxx(ui->body));
            }
            wattrset(ui->body, COLOR_PAIR(editing ? FOCUS_AREA : PANEL));
            text_label(ui->body, input_top(ui), 1, 1, ">");
            text_layout measured; text_measure(editor, width, &measured);
            if (!ui->editor_scrolling) text_keep_cursor(editor, &measured, height);
            if (!editor->length) {
                char placeholder[2200]; snprintf(placeholder, sizeof(placeholder), json_is_true(json_object_get(c,"_dm")) ? "Conversar com %s" : "Conversar em #%s", str(c, "name"));
                wattrset(ui->body, COLOR_PAIR(editing ? FOCUS_MUTED : PANEL_MUTED));
                text_label(ui->body, input_top(ui), 3, width, placeholder);
            } else text_draw(ui->body, editor, &measured, editor->first_row, height, input_top(ui), 3);
            wattrset(ui->body, COLOR_PAIR(editing ? FOCUS_MUTED : PANEL_MUTED));
            if (editor->first_row) text_label(ui->body, input_top(ui), getmaxx(ui->body) - 2, 1, "↑");
            if (editor->first_row + height < measured.rows) text_label(ui->body, input_top(ui) + height - 1, getmaxx(ui->body) - 2, 1, "↓");
            if (editing && measured.cursor_row >= editor->first_row && measured.cursor_row < editor->first_row + height) {
                wmove(ui->body, input_top(ui) + measured.cursor_row - editor->first_row, 3 + measured.cursor_column);
            }
        } else {
            wattrset(ui->body, COLOR_PAIR(PANEL_MUTED));
            text_label(ui->body, input_top(ui), 3, getmaxx(ui->body) - 6,
                       auth_has_session(auth) ? "Abra um canal de texto" : "Confirme a sessão");
        }
        wattrset(ui->body, COLOR_PAIR(editing ? FOCUS_BORDER : BORDER));
        mvwhline(ui->body, input_top(ui) + ui->editor_rows + ui->editor_padding,
                 0, ACS_HLINE, getmaxx(ui->body));
        char typing[4608];
        history_typing_text(ui->model, typing, sizeof(typing));
        if (*typing) {
            wattrset(ui->body, COLOR_PAIR(PANEL_ACCENT));
            text_label(ui->body, getmaxy(ui->body) - 1, 3, getmaxx(ui->body) - 6, typing);
        }
        if (editing && editor) {
            text_layout measured; text_measure(editor, getmaxx(ui->body) - 6, &measured);
            if (measured.cursor_row >= editor->first_row && measured.cursor_row < editor->first_row + input_height(ui))
                wmove(ui->body, input_top(ui) + measured.cursor_row - editor->first_row, 3 + measured.cursor_column);
        }
    }
    if (scope == DRAW_ALL) { touchwin(ui->frame); wnoutrefresh(ui->frame); }
    if (scope != DRAW_CONVERSATION) {
        wnoutrefresh(ui->tabs);
        if (ui->list) wnoutrefresh(ui->list);
    }
    if (ui->body && scope != DRAW_NAVIGATION) {
        /* Force cell updates in the history viewport rather than allowing the
         * terminal's full-width scrolling region to move adjacent channels. */
        wredrawln(ui->body, ui->history_top, ui->visible);
        wnoutrefresh(ui->body);
    }
    wnoutrefresh(ui->footer);
    if (ui->focus == MESSAGE_INPUT && input(ui) && ui->body && !ui->duplicate_modal) wnoutrefresh(ui->body);
    draw_confirmation(ui);
    if (scope != DRAW_NAVIGATION) media_view_draw(ui->media);
    if (mention_popup_open(ui->mentions) && ui->body && input(ui)) {
        text_layout measured; text_measure(input(ui), getmaxx(ui->body) - 6, &measured);
        mention_popup_draw(ui->mentions, getbegy(ui->body) + input_top(ui) + measured.cursor_row - input(ui)->first_row,
                           getbegx(ui->body) + 3 + measured.cursor_column, getbegx(ui->body) + 1, getmaxx(ui->body) - 2);
    }
    bool caret = false;
    if (ui->focus == MESSAGE_INPUT && input(ui) && ui->body && !ui->duplicate_modal && !r8_modal_is_open(ui->r8) && !media_view_modal(ui->media)) {
        text_buffer *editor = input(ui); text_layout measured;
        text_measure(editor, getmaxx(ui->body) - 6, &measured);
        caret = measured.cursor_row >= editor->first_row && measured.cursor_row < editor->first_row + input_height(ui);
        if (caret) setsyx(getbegy(ui->body) + input_top(ui) + measured.cursor_row - editor->first_row,
                          getbegx(ui->body) + 3 + measured.cursor_column);
    }
    if (!caret) setsyx(-1, -1);
    if (scope != DRAW_NAVIGATION) {
        if (!ui->duplicate_modal && !r8_modal_is_open(ui->r8) && !mention_popup_open(ui->mentions)) media_view_prepare(ui->media);
        graphics_frame_prepare();
    }
    r8_modal_draw(ui->r8);
    if (r8_modal_is_open(ui->r8)) { caret=false; setsyx(-1,-1); }
    doupdate();
    if (scope != DRAW_NAVIGATION) graphics_frame_flush();
    graphics_update_end();
    /* doupdate may visit every message while painting. Expose only the final
     * caret, after curses and direct graphics output have completed. */
    curs_set(caret ? 1 : 0);
}
static void set_focus(view *ui, int focus)
{
    if (ui->focus == focus) return;
    bool was_channels = ui->show_channels;
    ui->focus = focus;
    if (focus == CHANNEL_LIST || focus == LIST_BUTTONS) ui->show_channels = true;
    if (focus == HISTORY_PANEL || focus == MESSAGE_INPUT) ui->show_channels = false;
    if (COLS < 80 && was_channels != ui->show_channels) layout(ui);
}
static void open_channel(view *ui)
{
    /* Selecting the displayed channel only changes focus. Preserve pending
     * reads, the reading anchor, the draft and the current message window. */
    if (history_selected(ui->model) == list_index(ui,ui->highlighted)) { set_focus(ui, HISTORY_PANEL); return; }
    json_t *channel = list_channel(ui, ui->highlighted);
    bool text = !strcmp(str(channel, "type"), "text");
    if (text && !send_editor_select(ui->editor, str(channel, "id"))) {
        ui->feedback = "Não foi possível trocar o editor; rascunho preservado."; return;
    }
    if (history_select(ui->model, list_index(ui,ui->highlighted))) {
        for (int context = STATUS_HISTORY; context <= STATUS_CLIPBOARD; ++context)
            status_bar_set(&ui->status, (status_context)context, NULL);
        media_view_reset(ui->media);
        snprintf(ui->editor_channel, sizeof(ui->editor_channel), "%s", text ? str(channel, "id") : "");
        ui->feedback = NULL;
        ui->unseen = 0;
        ui->at_end = true; ui->anchor[0] = 0; set_focus(ui, HISTORY_PANEL);
    }
}
static int activate_tab(view *ui, size_t index, size_t *selected)
{
    ui->tab_cursor = index; ui->focus = SERVER_TABS;
    if (index == ui->connections->count) return HISTORY_ADD;
    if (index != ui->active_tab) { *selected = index; return HISTORY_SWITCH; }
    return -1;
}
static void submit(view *ui, uint64_t retry_id, bool duplicate)
{
    if (!input(ui)) return;
    ui->feedback = NULL;
    outbox_submit_result result = retry_id ? send_editor_retry(ui->editor, retry_id, duplicate) :
                                          send_editor_submit(ui->editor, duplicate);
    if (result == OUTBOX_CONFIRM_DUPLICATION) {
        ui->duplicate_modal = true; ui->reenviar = false; ui->retry_id = retry_id;
    }
}
static void close_confirmation(view *ui)
{
    ui->duplicate_modal = false;
    if (ui->confirm) { delwin(ui->confirm); ui->confirm = NULL; }
}
static bool poll_editor(view *ui, auth_client *auth)
{
    const char *notice = send_editor_notice(ui->editor);
    text_buffer *before = input(ui);
    size_t length = before ? before->length : 0;
    send_editor_poll(ui->editor);
    bool changed = notice != send_editor_notice(ui->editor);
    text_buffer *after = input(ui);
    changed |= length != (after ? after->length : 0) || !!before != !!after;
    outbox_operation operation;
    json_t *message;
    while ((message = outbox_take_confirmed(send_editor_outbox(ui->editor), &operation))) {
        if (operation.generation == auth_context_generation(auth) &&
            !strcmp(operation.channel_id, ui->editor_channel)) {
            if (!history_confirm(ui->model, message, operation.generation))
                ui->feedback = "Envio confirmado; Ctrl+R para consultar o histórico.";
        }
        json_decref(message); changed = true;
    }
    if (!auth_has_session(auth) && ui->duplicate_modal) { close_confirmation(ui); changed = true; }
    return changed;
}
static bool poll_model(view *ui, auth_client *auth)
{
    char highlighted[37];
    snprintf(highlighted, sizeof(highlighted), "%s", str(list_channel(ui, ui->highlighted), "id"));
    /* Retain IDs around the reading anchor, never borrowed model pointers. */
    char neighbors[HISTORY_MESSAGE_LIMIT][37]; size_t count = 0, anchored = 0;
    if (!ui->at_end) for (size_t i = 0; i < history_message_count(ui->model); ++i) {
        snprintf(neighbors[count], sizeof(neighbors[count]), "%s", str(history_message(ui->model, i), "id"));
        if (!strcmp(neighbors[count], ui->anchor)) anchored = count;
        ++count;
    }
    uint64_t inserted = history_insertions(ui->model);
    ws_state before = auth_ws_state(auth);
    auth_poll(auth);
    bool changed = poll_editor(ui, auth);
    changed |= history_poll(ui->model) || before != auth_ws_state(auth);
    uint64_t delta = history_insertions(ui->model) - inserted;
    if (!ui->at_end) ui->unseen += delta;
    for (size_t i = 0; i < list_count(ui); ++i)
        if (!strcmp(highlighted, str(list_channel(ui, i), "id"))) ui->highlighted = i;
    if (!ui->at_end && count) {
        bool found = false;
        for (size_t i = 0; i < history_message_count(ui->model); ++i)
            found |= !strcmp(ui->anchor, str(history_message(ui->model, i), "id"));
        if (!found) {
            for (size_t step = 0; step < count && !found; ++step) {
                size_t index = anchored + step < count ? anchored + step : count - 1 - (anchored + step - count);
                for (size_t i = 0; i < history_message_count(ui->model); ++i)
                    if (!strcmp(neighbors[index], str(history_message(ui->model, i), "id"))) {
                        snprintf(ui->anchor, sizeof(ui->anchor), "%s", neighbors[index]);
                        ui->section = ui->offset = 0; found = true; break;
                    }
            }
        }
    }
    return changed;
}
static bool edit(view *ui, int type, wint_t key)
{
    text_buffer *editor = input(ui);
    if (!editor || !ui->body) return false;
    text_buffer before = *editor;
    bool handled = text_edit(editor, type, key, getmaxx(ui->body) - 6, true);
    if (handled) {
        ui->editor_scrolling = ui->dragging = false;
        ui->feedback = send_editor_changed(ui->editor) ? NULL : "Não foi possível salvar o rascunho.";
        if (before.length != editor->length || memcmp(before.data, editor->data, editor->length * sizeof(editor->data[0])))
            history_typing_activity(ui->model);
    }
    return handled;
}
int history_run(auth_client *auth, instance_connections *connections, size_t *selected,
                bool focus_tabs, volatile sig_atomic_t *interrupted)
{
    text_input_init();
    view *ui = calloc(1, sizeof(*ui));
    if (!ui) return HISTORY_ACCOUNT;
    ui->connections = connections; ui->active_tab = ui->tab_cursor = instances_index(connections, auth);
    ui->editor = instances_editor(connections, auth);
    ui->media = media_view_create(auth);
    ui->model = instances_history(connections,auth);
    if (ui->model) history_active(ui->model,true);
    ui->r8 = ui->model ? r8_modal_create(ui->model,auth,connections->config) : NULL;
    ui->mentions = ui->model ? mention_popup_create(ui->model) : NULL;
    ui->at_end = ui->show_channels = true; ui->focus = focus_tabs ? SERVER_TABS : CHANNEL_LIST;
    if (!ui->model || !ui->r8 || !ui->mentions || !ui->editor || ui->active_tab == SIZE_MAX || !layout(ui)) { mention_popup_destroy(ui->mentions); media_view_destroy(ui->media); if (ui->model) history_active(ui->model,false); r8_modal_destroy(ui->r8); destroy_layout(ui); free(ui); return HISTORY_ACCOUNT; }
    mmask_t previous_mouse;
    /* A separately delivered release must reach getmouse(). If it is filtered
     * out, ncurses can wait for another gesture inside get_wch(), stalling HTTP
     * and media updates even with timeout(30) and mouseinterval(0). */
    mousemask(BUTTON1_PRESSED | BUTTON1_RELEASED | BUTTON1_CLICKED | BUTTON3_PRESSED | BUTTON3_CLICKED | REPORT_MOUSE_POSITION | BUTTON4_PRESSED | BUTTON5_PRESSED, &previous_mouse);
    ui_mouse_drag_tracking(true);
    bool redraw = true; draw_scope scope = DRAW_ALL; int result = HISTORY_ACCOUNT;
    while (!*interrupted) {
        if (ui->layout_failed) break;
        uint64_t visible_now=0; net_now_ms(&visible_now);
        if (!ui->body || !ui->at_end || r8_modal_is_open(ui->r8) || ui->duplicate_modal ||
            mention_popup_open(ui->mentions) || media_view_modal(ui->media) ||
            !(text_terminal_focused() || (text_terminal_focus_unknown() && visible_now<ui->view_requested_until)))
            history_read_visible(ui->model,NULL);
        auth_state before = auth_get_state(auth);
        if (session_store_poll(connections->sessions, auth)) { redraw = true; scope = DRAW_ALL; }
        if (poll_model(ui, auth) || before != auth_get_state(auth)) { redraw = true; scope = DRAW_ALL; }
        if (instances_poll(connections, auth)) { redraw = true; scope = DRAW_ALL; }
        if (media_view_poll(ui->media, ui->model)) {
            if (!redraw) scope = DRAW_CONVERSATION;
            else if (scope == DRAW_NAVIGATION) scope = DRAW_ALL;
            redraw = true;
        }
        if (clipboard_poll()) redraw = true;
        if (input(ui)) text_mentions(input(ui), history_mention_name, ui->model);
        if (editor_geometry(ui)) { redraw = true; scope = DRAW_ALL; }
        if (!r8_modal_is_open(ui->r8) && mention_popup_update(ui->mentions, input(ui), ui->focus == MESSAGE_INPUT && !ui->paste &&
                                 !ui->duplicate_modal && !r8_modal_is_open(ui->r8) && !media_view_modal(ui->media))) {
            redraw = true; scope = DRAW_ALL;
        }
        inbox *box=history_inbox(ui->model);
        if (ui->inbox_revision!=inbox_revision(box)) { ui->inbox_revision=inbox_revision(box); redraw=true; scope=DRAW_ALL; }
        const char *opened=inbox_opened(box);
        const char *destination=*opened ? opened : r8_modal_destination(ui->r8);
        if (*destination) {
            bool found=false;
            for (size_t i=0;i<history_channel_count(ui->model);++i) {
                json_t *c=history_channel(ui->model,i);
                if (strcmp(destination,str(c,"id"))) continue;
                switch_list(ui,json_is_true(json_object_get(c,"_dm")));
                for (size_t j=0;j<list_count(ui);++j) if (list_index(ui,j)==i) ui->highlighted=j;
                open_channel(ui); found=true; break;
            }
            if (!found) inbox_resolve_dm(box,destination);
            inbox_opened_clear(box); r8_modal_destination_clear(ui->r8); redraw=true;
        }
        instances_visible(connections,auth,ui->body && ui->at_end && !r8_modal_is_open(ui->r8) && !media_view_modal(ui->media) ? str(history_channel(ui->model,history_selected(ui->model)),"id") : "");
        if (collect_notices(ui, auth)) redraw = true;
        if (!auth_has_session(auth) && !auth_busy(auth)) break;
        if (redraw) { draw(ui, auth, scope); redraw = false; scope = DRAW_ALL; }
        wint_t key; int type = ui->paste ? text_paste_read(&ui->paste_data, &key, true, graphics_terminal_input, 30) : graphics_terminal_input(&key);
        if (type == ERR) continue;
        if (type==KEY_CODE_YES && (key==TEXT_FOCUS_IN || key==TEXT_FOCUS_OUT)) {
            ui->view_requested_until=0;
            redraw=true; scope=DRAW_ALL; continue;
        }
        if (!(type==KEY_CODE_YES && (key==PASTE_START || key==PASTE_END || key==KEY_RESIZE || key==TEXT_IGNORED)) && !ui->paste) {
            uint64_t now; if (net_now_ms(&now)) ui->view_requested_until=now+2000;
            history_activity(ui->model);
        }
        if (type == KEY_CODE_YES && key == PASTE_START) {
            ui->paste = true; text_paste_begin(&ui->paste_data); ui->dragging = false;
            if (!ui->duplicate_modal && !r8_modal_is_open(ui->r8) && !media_view_modal(ui->media) && input(ui) && ui->focus != SERVER_TABS) { set_focus(ui, MESSAGE_INPUT); redraw = true; }
            continue;
        }
        if (type == KEY_CODE_YES && key == PASTE_END) {
            ui->paste = false;
            if (!ui->duplicate_modal && !r8_modal_is_open(ui->r8) && !media_view_modal(ui->media) && ui->focus == MESSAGE_INPUT && input(ui)) {
                if (!text_paste_commit(&ui->paste_data, input(ui))) ui->feedback = "Colagem excede o limite; rascunho preservado.";
                else {
                    ui->feedback = send_editor_changed(ui->editor) ? NULL : "Não foi possível salvar o rascunho.";
                    history_typing_activity(ui->model); history_activity(ui->model);
                    uint64_t now; if (net_now_ms(&now)) ui->view_requested_until=now+2000;
                }
                ui->editor_scrolling = false; redraw = true; scope = DRAW_ALL;
            } else text_paste_cancel(&ui->paste_data);
            mention_popup_update(ui->mentions, input(ui), ui->focus == MESSAGE_INPUT);
            mention_popup_close(ui->mentions); continue;
        }
        if (type == KEY_CODE_YES && key == KEY_RESIZE) { layout(ui); redraw = true; continue; }
        if (ui->paste) continue;
        redraw = true;
        if (type == KEY_CODE_YES && key == TEXT_COPY) {
            if (ui->focus == MESSAGE_INPUT && input(ui) && !ui->duplicate_modal && !r8_modal_is_open(ui->r8) && !media_view_modal(ui->media))
                clipboard_copy(input(ui));
            continue;
        }
        if (type == OK && (key == 17 || (key == 3 && !r8_modal_is_open(ui->r8) && (ui->focus != MESSAGE_INPUT || ui->duplicate_modal || media_view_modal(ui->media))))) { result = HISTORY_EXIT; break; }
        bool accepted = false;
        if (mention_popup_key(ui->mentions, input(ui), type, key, &accepted)) {
            if (accepted) { send_editor_changed(ui->editor); history_typing_activity(ui->model); }
            scope = DRAW_ALL; continue;
        }
        bool enter = (type == OK && (key == '\r' || key == '\n')) || (type == KEY_CODE_YES && key == KEY_ENTER);
        if (ui->duplicate_modal) {
            if (type == OK && key == 27) { close_confirmation(ui); continue; }
            if ((type == OK && key == '\t') || (type == KEY_CODE_YES && key == KEY_BTAB)) ui->reenviar = !ui->reenviar;
            else if (type == KEY_CODE_YES && key == KEY_LEFT) ui->reenviar = false;
            else if (type == KEY_CODE_YES && key == KEY_RIGHT) ui->reenviar = true;
            else if (enter) {
                bool retry = ui->reenviar; uint64_t id = ui->retry_id;
                close_confirmation(ui); if (retry) submit(ui, id, true);
            } else if (type == KEY_CODE_YES && key == KEY_MOUSE && ui->confirm) {
                MEVENT e;
                if (ui_mouse_get(&e) == OK && (e.bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED))) {
                    int y = e.y, x = e.x;
                    if (wmouse_trafo(ui->confirm, &y, &x, false) && y == 7) {
                        bool cancel = x >= 2 && x < 14;
                        bool retry = x >= getmaxx(ui->confirm) - 14 && x < getmaxx(ui->confirm) - 2;
                        uint64_t id = ui->retry_id;
                        if (cancel || retry) { close_confirmation(ui); if (retry) submit(ui, id, true); }
                    }
                }
            }
            continue;
        }
        if (type == KEY_CODE_YES && key == KEY_F(6)) break;
        if (type == OK && key == 16) { result = HISTORY_PROFILES; break; }
        if (type == OK && key == 12) { auth_logout(auth); break; }
        if (r8_modal_is_open(ui->r8)) { r8_modal_key(ui->r8,type,key); scope=DRAW_ALL; continue; }
        if (type==KEY_CODE_YES && (key==KEY_F(9) || key==KEY_F(10) || key==KEY_F(1))) {
            if (!media_view_modal(ui->media)) {
                mention_popup_close(ui->mentions);
                r8_modal_open(ui->r8,key==KEY_F(9) ? R8_NOTIFICATIONS : key==KEY_F(10) ? R8_USERS : R8_HELP);
            }
            scope=DRAW_ALL; continue;
        }
        if (type==OK && key==4 && ui->focus==CHANNEL_LIST && ui->list_dms) {
            json_t *dm=list_channel(ui,ui->highlighted); outbox_operation operation;
            bool busy=outbox_latest(send_editor_outbox(ui->editor),str(dm,"id"),&operation) &&
                (operation.state==OUTBOX_QUEUED || operation.state==OUTBOX_SENDING);
            if (busy) ui->feedback="Aguarde o envio antes de fechar a DM.";
            else if (dm) inbox_close_dm(history_inbox(ui->model),str(dm,"id"));
            continue;
        }
        if (media_view_modal(ui->media) && type == KEY_CODE_YES && key == KEY_MOUSE) {
            MEVENT e;
            if (ui_mouse_get(&e) == OK) {
                if (e.bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED)) media_view_click(ui->media, e.y, e.x);
                else if (e.bstate & (BUTTON4_PRESSED | BUTTON5_PRESSED))
                    media_view_scroll(ui->media, e.y, e.x, e.bstate & BUTTON4_PRESSED ? -3 : 3);
            }
            continue;
        }
        if ((media_view_modal(ui->media) || ui->focus == HISTORY_PANEL) && media_view_key(ui->media, type, key)) continue;
        if (type == OK && key == 20 && input(ui)) {
            outbox_operation latest;
            if (outbox_latest(send_editor_outbox(ui->editor), ui->editor_channel, &latest) &&
                (latest.state == OUTBOX_FAILED || latest.state == OUTBOX_UNCERTAIN)) submit(ui, latest.id, false);
            else ui->feedback = "Nenhum envio falhou nesta conversa.";
            continue;
        }
        if (type == OK && key == 18) {
            if (auth_ws_reconnecting(auth)) { auth_connect_ws(auth); continue; }
            inbox_retry(history_inbox(ui->model));
            if (history_message_count(ui->model)) {
                if (!history_retry(ui->model)) { ui->at_end = true; ui->anchor[0] = 0; }
            } else if (history_selected(ui->model) != SIZE_MAX) history_recent(ui->model);
            else history_reload_channels(ui->model);
            continue;
        }
        if ((type==OK && key=='\t') || (type==KEY_CODE_YES && key==KEY_BTAB)) {
            int order[]={SERVER_TABS,LIST_BUTTONS,CHANNEL_LIST,HISTORY_PANEL,MESSAGE_INPUT};
            int count=input(ui) ? 5 : 4, at=0;
            while (at<count && order[at]!=ui->focus) ++at;
            set_focus(ui,order[(at+(type==OK ? 1 : count-1))%count]); continue;
        }
        if (type == OK && key == 27) { set_focus(ui, ui->focus == MESSAGE_INPUT ? HISTORY_PANEL : ui->focus == HISTORY_PANEL ? CHANNEL_LIST : HISTORY_PANEL); continue; }
        if (type == OK && key == 14 && ui->focus != SERVER_TABS && input(ui)) {
            set_focus(ui, MESSAGE_INPUT); edit(ui, type, key); continue;
        }
        if (type == KEY_CODE_YES && key == KEY_MOUSE) {
            MEVENT events[64]; size_t count = ui_mouse_events(events, 64);
            for (size_t event_index = 0; event_index < count; ++event_index) {
                MEVENT e = events[event_index];
                if (!ui->frame) continue;
                if (ui->dragging && input(ui) && ui->body &&
                    (e.bstate & (BUTTON1_PRESSED | BUTTON1_RELEASED | REPORT_MOUSE_POSITION))) {
                    text_buffer *buffer = input(ui);
                    int row = e.y - getbegy(ui->body) - input_top(ui);
                    int column = e.x - getbegx(ui->body) - 3;
                    text_layout measured; text_measure(buffer, getmaxx(ui->body) - 6, &measured);
                    if (row < 0 && buffer->first_row > 0) --buffer->first_row;
                    if (row >= input_height(ui) && buffer->first_row + input_height(ui) < measured.rows) ++buffer->first_row;
                    if (row < 0) row = 0;
                    if (row >= input_height(ui)) row = input_height(ui) - 1;
                    if (column < 0) column = 0;
                    if (column > getmaxx(ui->body) - 7) column = getmaxx(ui->body) - 7;
                    text_drag(buffer, getmaxx(ui->body) - 6, buffer->first_row + row, column);
                    send_editor_changed(ui->editor); ui->editor_scrolling = false;
                    if (e.bstate & BUTTON1_RELEASED) ui->dragging = false;
                    continue;
                }
                if (e.bstate & (BUTTON3_PRESSED | BUTTON3_CLICKED)) {
                    if (ui->body && input(ui) && e.x >= getbegx(ui->body) && e.x < getbegx(ui->body) + getmaxx(ui->body) &&
                        input_row(ui, e.y - getbegy(ui->body)))
                        clipboard_copy(input(ui));
                    continue;
                }
                if (e.bstate & REPORT_MOUSE_POSITION) continue;
                if (mention_popup_mouse(ui->mentions, input(ui), &e, &accepted)) {
                    if (accepted) { send_editor_changed(ui->editor); history_typing_activity(ui->model); }
                    scope = DRAW_ALL; continue;
                }
                if (e.bstate & (BUTTON4_PRESSED | BUTTON5_PRESSED)) {
                    scope = DRAW_CONVERSATION;
                    if (ui->body && e.x >= getbegx(ui->body) && e.x < getbegx(ui->body) + getmaxx(ui->body) && e.y >= getbegy(ui->body) + ui->history_top && e.y < getbegy(ui->body) + ui->editor_border)
                        scroll_history(ui, e.bstate & BUTTON4_PRESSED ? -3 : 3);
                    else if (ui->body && input(ui) && e.x >= getbegx(ui->body) && e.x < getbegx(ui->body) + getmaxx(ui->body) &&
                             input_row(ui, e.y - getbegy(ui->body))) {
                        text_buffer *buffer = input(ui); text_layout measured;
                        text_measure(buffer, getmaxx(ui->body) - 6, &measured);
                        int last = measured.rows - input_height(ui); if (last < 0) last = 0;
                        buffer->first_row += e.bstate & BUTTON4_PRESSED ? -3 : 3;
                        if (buffer->first_row < 0) buffer->first_row = 0;
                        if (buffer->first_row > last) buffer->first_row = last;
                        ui->editor_scrolling = true;
                    }
                    continue;
                }
                if (!(e.bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED))) continue;
                if (media_view_click(ui->media, e.y, e.x)) continue;
                if (ui->unseen && e.y == getbegy(ui->footer) + 1 &&
                    status_bar_hit(&ui->status, STATUS_UNREAD, e.x - getbegx(ui->footer))) {
                    ui->at_end = true; ui->unseen = 0; ui->anchor[0] = 0; continue;
                }
                int y = e.y, x = e.x;
                if (wmouse_trafo(ui->tabs, &y, &x, false)) {
                    if (y == 0 && hit(ui->add_hit, x)) { result = HISTORY_ADD; goto finished; }
                    if (y == 0 && hit(ui->back_hit, x)) { if (ui->first_tab) --ui->first_tab; ui->tab_cursor = ui->first_tab; ui->focus = SERVER_TABS; continue; }
                    if (y == 0 && hit(ui->next_hit, x)) { if (ui->first_tab + 1 < connections->count) ++ui->first_tab; ui->tab_cursor = ui->first_tab; ui->focus = SERVER_TABS; continue; }
                    if (y == 0) for (size_t i = 0; i < connections->count; ++i) if (hit(ui->tab_hits[i], x)) {
                        int action = activate_tab(ui, i, selected); if (action >= 0) { result = action; goto finished; } break;
                    }
                    if (y == 1 && hit(ui->channels_hit, x)) set_focus(ui, CHANNEL_LIST);
                    if (y == 1 && hit(ui->conversation_hit, x)) set_focus(ui, HISTORY_PANEL);
                    continue;
                }
                y = e.y; x = e.x;
                if (ui->list && wmouse_trafo(ui->list, &y, &x, false)) {
                    size_t index = y >= 1 ? (size_t)(ui->first_channel + y - 1) : SIZE_MAX;
                    if (y==0 && x>=1 && x<19) {
                        switch_list(ui,x>=11); set_focus(ui,LIST_BUTTONS); scope=DRAW_ALL;
                    } else if (y < getmaxy(ui->list) - 1 && index < list_count(ui)) {
                        ui->highlighted = index; open_channel(ui);
                    } else {
                        if (ui->focus == CHANNEL_LIST || ui->focus == SERVER_TABS) scope = DRAW_NAVIGATION;
                        set_focus(ui, CHANNEL_LIST);
                    }
                    continue;
                }
                y = e.y; x = e.x;
                if (ui->body && wmouse_trafo(ui->body, &y, &x, false)) {
                    if (y < ui->editor_border) set_focus(ui, HISTORY_PANEL);
                    else if (input(ui) && input_row(ui, y)) {
                        text_buffer *editor = input(ui);
                        text_click(editor, getmaxx(ui->body) - 6, y - input_top(ui) + editor->first_row, x - 3);
                        send_editor_changed(ui->editor); set_focus(ui, MESSAGE_INPUT);
                        ui->editor_scrolling = false; ui->dragging = (e.bstate & BUTTON1_PRESSED) != 0;
                    }
                }
            }
            continue;
        }
        if (ui->focus==LIST_BUTTONS) {
            if (type==KEY_CODE_YES && (key==KEY_LEFT || key==KEY_RIGHT)) ui->list_button=key==KEY_RIGHT;
            else if (enter || (type==OK && key==' ')) switch_list(ui,ui->list_button!=0);
            else if (type==KEY_CODE_YES && key==KEY_DOWN) { switch_list(ui,ui->list_button!=0); set_focus(ui,CHANNEL_LIST); }
            else if (type==KEY_CODE_YES && key==KEY_UP) set_focus(ui,SERVER_TABS);
            scope=DRAW_ALL; continue;
        }
        if (ui->focus != SERVER_TABS && input(ui) && type == OK && iswprint(key)) {
            if (ui->focus != MESSAGE_INPUT) set_focus(ui, MESSAGE_INPUT);
            edit(ui, type, key); continue;
        }
        if (ui->focus == MESSAGE_INPUT) {
            if (enter) submit(ui, 0, false);
            else edit(ui, type, key);
            continue;
        }
        if (ui->focus == SERVER_TABS) {
            if (type == KEY_CODE_YES && key == KEY_DOWN) set_focus(ui, CHANNEL_LIST);
            else if (type == KEY_CODE_YES && (key == KEY_LEFT || key == KEY_RIGHT)) {
                size_t index = ui->tab_cursor;
                if (key == KEY_LEFT && index) --index;
                if (key == KEY_RIGHT && index < connections->count) ++index;
                ui->tab_cursor = index;
                if (index < connections->count) { int action = activate_tab(ui, index, selected); if (action >= 0) { result = action; break; } }
            } else if (enter) { int action = activate_tab(ui, ui->tab_cursor, selected); if (action >= 0) { result = action; break; } }
        } else if (ui->focus == CHANNEL_LIST) {
            scope = DRAW_NAVIGATION;
            size_t count = list_count(ui);
            if (type==KEY_CODE_YES && key==KEY_LEFT) set_focus(ui,SERVER_TABS);
            else if (type==KEY_CODE_YES && key==KEY_UP && !ui->highlighted) set_focus(ui,LIST_BUTTONS);
            else if (type == KEY_CODE_YES && key == KEY_UP && ui->highlighted) --ui->highlighted;
            else if (type == KEY_CODE_YES && key == KEY_DOWN && ui->highlighted + 1 < count) ++ui->highlighted;
            else if (type == KEY_CODE_YES && key == KEY_HOME) ui->highlighted = 0;
            else if (type == KEY_CODE_YES && key == KEY_END && count) ui->highlighted = count - 1;
            else if (type == KEY_CODE_YES && key == KEY_PPAGE)
                ui->highlighted = ui->highlighted > (size_t)(LINES - CONTENT_Y - 5) ? ui->highlighted - (size_t)(LINES - CONTENT_Y - 5) : 0;
            else if (type == KEY_CODE_YES && key == KEY_NPAGE && count) {
                ui->highlighted += (size_t)(LINES - CONTENT_Y - 5); if (ui->highlighted >= count) ui->highlighted = count - 1;
            } else if (enter || (type == KEY_CODE_YES && key == KEY_RIGHT)) { scope = DRAW_ALL; open_channel(ui); }
        } else {
            if (type == KEY_CODE_YES && key == KEY_LEFT) set_focus(ui, CHANNEL_LIST);
            else if (type == KEY_CODE_YES && (key == KEY_PPAGE || key == KEY_UP || key == KEY_HOME)) {
                scope = DRAW_CONVERSATION;
                int first = position(ui);
                scroll_history(ui, key == KEY_HOME ? -2147483647 : key == KEY_UP ? -1 : -ui->visible);
                if (first == 0 && key != KEY_HOME && history_has_more(ui->model)) { ui->at_end = false; history_older(ui->model); }
            } else if (type == KEY_CODE_YES && (key == KEY_DOWN || key == KEY_NPAGE)) {
                scope = DRAW_CONVERSATION; scroll_history(ui, key == KEY_DOWN ? 1 : ui->visible);
            }
            else if (type == KEY_CODE_YES && key == KEY_END) {
                scope = DRAW_CONVERSATION;
                if (!ui->unseen || history_evicted(ui->model)) history_recent(ui->model);
                ui->at_end = true; ui->unseen = 0; ui->anchor[0] = 0;
            }
            else if (enter && input(ui)) set_focus(ui, MESSAGE_INPUT);
            else if (enter && !history_retry(ui->model)) { ui->at_end = true; ui->anchor[0] = 0; }
        }
    }
finished:
    instances_visible(connections,NULL,NULL);
    text_paste_cancel(&ui->paste_data);
    mousemask(previous_mouse, NULL);
    ui_mouse_drag_tracking((previous_mouse & REPORT_MOUSE_POSITION) != 0);
    mention_popup_destroy(ui->mentions);
    text_buffer *draft = send_editor_input(ui->editor);
    if (draft) text_mentions(draft, NULL, NULL);
    media_view_destroy(ui->media); destroy_layout(ui); if (ui->model) history_active(ui->model,false); r8_modal_destroy(ui->r8); free(ui); touchwin(stdscr); return result;
}
