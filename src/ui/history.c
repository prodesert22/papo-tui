#define _POSIX_C_SOURCE 200809L
#include "ui/history.h"
#include "model/history.h"
#include "ui/text.h"
#include "ui/theme.h"
#include "ui/button.h"
#include "app/session_store.h"
#include "ui/send_editor.h"
#include <wctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PASTE_START (KEY_MAX + 1)
#define PASTE_END (KEY_MAX + 2)
#define CONTENT_Y 5

enum { SERVER_TABS, CHANNEL_LIST, HISTORY_PANEL, MESSAGE_INPUT };
typedef struct { int x, width; } click_range;
typedef struct {
    history *model;
    send_editor *editor; /* Borrowed from the instance connection. */
    instance_connections *connections;
    WINDOW *frame, *tabs, *footer, *list, *body, *confirm;
    int width, height, side, visible, first_channel, focus, first, editor_border, history_top;
    size_t highlighted, active_tab, tab_cursor, first_tab;
    click_range tab_hits[INSTANCE_LIMIT], add_hit, back_hit, next_hit, channels_hit, conversation_hit;
    char anchor[37];
    char editor_channel[64];
    const char *feedback;
    uint64_t retry_id;
    uint64_t unseen;
    size_t section, offset;
    bool at_end, paste, paste_cr, show_channels, layout_failed, duplicate_modal, reenviar;
} view;
static const char *str(json_t *obj, const char *key)
{
    const char *s = json_string_value(json_object_get(obj, key));
    return s ? s : "";
}
static bool line(json_t *out, const char *prefix, const char *value)
{
    size_t a = strlen(prefix), b = strlen(value);
    char *s = malloc(a + b + 1);
    if (!s) return false;
    memcpy(s, prefix, a); memcpy(s + a, value, b + 1);
    json_t *v = json_string(s); free(s);
    return v && json_array_append_new(out, v) == 0;
}
static json_t *presentation(view *ui, json_t *message)
{
    json_t *out = json_array();
    if (!out) return NULL;
    char header[1200];
    snprintf(header, sizeof(header), "%s · %s%s", str(message, "created_at"),
             history_author(ui->model, str(message, "author_id")), *str(message, "edited_at") ? " · editada" : "");
    if (!line(out, "", header)) goto fail;
    if (*str(message, "edited_at") && !line(out, "Editada em: ", str(message, "edited_at"))) goto fail;
    const char *reply = str(message, "reply_to");
    if (*reply) {
        json_t *target = NULL;
        for (size_t i = 0; i < history_message_count(ui->model); ++i) {
            json_t *m = history_message(ui->model, i);
            if (!strcmp(reply, str(m, "id"))) { target = m; break; }
        }
        if (!line(out, "Resposta: ", target ? str(target, "id") : "mensagem não disponível")) goto fail;
    }
    json_t *content = json_object_get(message, "content");
    if (!line(out, "", json_is_string(content) ? json_string_value(content) : "[Sem conteúdo textual]")) goto fail;
    size_t i; json_t *item;
    json_array_foreach(json_object_get(message, "attachments"), i, item) {
        if (!line(out, "Anexo: ", *str(item, "original_file_name") ? str(item, "original_file_name") : "arquivo sem nome") ||
            !line(out, "Tipo: ", str(item, "mime_type"))) goto fail;
        char size[80];
        snprintf(size, sizeof(size), "%lld bytes", (long long)json_integer_value(json_object_get(item, "size_bytes")));
        if (!line(out, "Tamanho: ", size) || !line(out, "Moderação: ", str(item, "moderation_status"))) goto fail;
    }
    json_array_foreach(json_object_get(message, "embeds"), i, item) {
        if (!line(out, "Embed: ", *str(item, "title") ? str(item, "title") : "sem título")) goto fail;
        const char *keys[] = {"description", "url", "provider", "site_name", "source_type", "fetch_method"};
        const char *labels[] = {"", "URL: ", "Provedor: ", "Site: ", "Origem: ", "Método: "};
        for (size_t k = 0; k < 6; ++k)
            if (*str(item, keys[k]) && !line(out, labels[k], str(item, keys[k]))) goto fail;
        json_t *author = json_object_get(item, "author"), *footer = json_object_get(item, "footer");
        if (*str(author, "name") && !line(out, "Autor do embed: ", str(author, "name"))) goto fail;
        if (*str(author, "url") && !line(out, "URL do autor: ", str(author, "url"))) goto fail;
        if (*str(footer, "text") && !line(out, "Rodapé: ", str(footer, "text"))) goto fail;
        size_t field_index; json_t *field;
        json_array_foreach(json_object_get(item, "fields"), field_index, field) {
            if (field_index >= 25) break;
            if (!line(out, "Campo: ", str(field, "name")) || !line(out, "", str(field, "value"))) goto fail;
        }
        const char *media_keys[] = {"thumbnail", "image", "video"};
        const char *media_labels[] = {"Miniatura: ", "Imagem: ", "Vídeo: "};
        for (size_t k = 0; k < 3; ++k) {
            json_t *media = json_object_get(item, media_keys[k]);
            if (!json_is_object(media)) continue;
            if (!line(out, media_labels[k], str(media, "mime_type"))) goto fail;
            if (*str(media, "url") && !line(out, "URL de mídia: ", str(media, "url"))) goto fail;
        }
    }
    if (!line(out, "", "")) goto fail;
    return out;
fail:
    json_decref(out); return NULL;
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
    destroy_layout(ui);
    erase(); refresh();
    ui->width = COLS; ui->height = LINES;
    if (COLS < 44 || LINES < 14) return true;
    bool wide = COLS >= 80;
    int rows = LINES - CONTENT_Y - 3;
    ui->side = wide ? 24 : COLS - 2;
    ui->editor_border = rows >= 10 ? rows - 5 : rows - 2;
    ui->history_top = rows >= 8 ? 3 : 2;
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
        char label[160];
        snprintf(label, sizeof(label), "%c%s%c", selected ? '[' : active ? '>' : ' ', ui->connections->items[i].name, selected ? ']' : active ? '<' : ' ');
        ui->tab_hits[i] = (click_range){x, text_label(ui->tabs, 0, x, size, label)};
        x += size;
    }
    bool adding = focused && ui->tab_cursor == ui->connections->count;
    button_draw(ui->tabs, 0, ui->add_hit.x, ui->add_hit.width, adding ? ">+ Adicionar<" : "[+ Adicionar]",
                adding ? BUTTON_FOCUSED : BUTTON_NORMAL, BUTTON_PANEL);
    const char *areas[] = {"Abas", "Canais", "Histórico", "Editor"};
    char summary[1200];
    snprintf(summary, sizeof(summary), "%s · Foco: %s · F6: conta", auth_username(auth), areas[ui->focus]);
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
    int total = 0, width = ui->body ? getmaxx(ui->body) - 4 : ui->width - 4;
    *anchor = 0;
    for (size_t i = 0; i < history_message_count(ui->model); ++i) {
        json_t *m = history_message(ui->model, i), *sections = presentation(ui, m);
        if (!sections) continue;
        if (!strcmp(ui->anchor, str(m, "id")) && ui->section >= json_array_size(sections)) {
            ui->section = 0; ui->offset = 0;
        }
        size_t section; json_t *field;
        json_array_foreach(sections, section, field) {
            const char *p = json_string_value(field);
            size_t offset = 0;
            do {
                text_buffer buffer; text_layout measured;
                if (!text_decode_chunk(&buffer, &p)) {
                    text_decode(&buffer, "Texto indisponível.");
                    p += strlen(p);
                }
                text_measure(&buffer, width, &measured);
                if (!strcmp(ui->anchor, str(m, "id")) && ui->section == section &&
                    ui->offset >= offset && (ui->offset < offset + buffer.length + 1 || !*p)) {
                    if (ui->offset > offset + buffer.length) ui->offset = offset + buffer.length;
                    int row = 0;
                    for (size_t k = 0; k < measured.count; ++k)
                        if (measured.cells[k].start <= ui->offset - offset) row = measured.cells[k].row;
                    *anchor = total + row;
                }
                if (save && first >= total && first < total + measured.rows) {
                    snprintf(ui->anchor, sizeof(ui->anchor), "%s", str(m, "id"));
                    ui->section = section; ui->offset = offset;
                    for (size_t k = 0; k < measured.count; ++k)
                        if (measured.cells[k].row == first - total) { ui->offset += measured.cells[k].start; break; }
                }
                if (paint && total < first + ui->visible && total + measured.rows > first) {
                    wattrset(ui->body, COLOR_PAIR(section == 0 ? (ui->focus == HISTORY_PANEL ? HISTORY_ACCENT : PANEL_ACCENT) : ui->focus == HISTORY_PANEL ? HISTORY_AREA : PANEL));
                    text_draw(ui->body, &buffer, &measured, first - total, ui->visible, ui->history_top, 2);
                }
                total += measured.rows; offset += buffer.length;
            } while (*p);
        }
        json_decref(sections);
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
    return getmaxy(ui->body) - ui->editor_border - 1 - (getmaxy(ui->body) >= 10 ? 1 : 0);
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
static void draw(view *ui, auth_client *auth)
{
    curs_set(0);
    if (!ui->frame) {
        erase(); text_label(stdscr, 0, 0, COLS, "Amplie o terminal para 44×14. F6: conta."); refresh(); return;
    }
    werase(ui->frame); wattrset(ui->frame, COLOR_PAIR(BORDER)); box(ui->frame, 0, 0);
    mvwhline(ui->frame, CONTENT_Y - 2, 1, ACS_HLINE, COLS - 2);
    mvwaddch(ui->frame, CONTENT_Y - 2, 0, ACS_LTEE);
    mvwaddch(ui->frame, CONTENT_Y - 2, COLS - 1, ACS_RTEE);
    mvwhline(ui->frame, LINES - 4, 1, ACS_HLINE, COLS - 2);
    if (COLS >= 80) mvwvline(ui->frame, CONTENT_Y, ui->side + 1, ACS_VLINE, LINES - CONTENT_Y - 3);
    draw_tabs(ui, auth);
    wbkgd(ui->footer, COLOR_PAIR(PANEL)); werase(ui->footer);
    wattrset(ui->footer, COLOR_PAIR(PANEL_MUTED));
    char progress[192];
    const char *connection = auth_ws_reconnecting(auth) ? auth_message(auth) :
                             history_recovering(ui->model) ? "Recuperando histórico…" : NULL;
    if (ui->unseen) {
        snprintf(progress, sizeof(progress), "%llu novas mensagens · End: ir ao fim%s",
                 (unsigned long long)ui->unseen, connection ? " · Reconectando" : "");
        text_label(ui->footer, 0, 1, COLS - 4, progress);
    } else text_label(ui->footer, 0, 1, COLS - 4, connection ? connection : history_status(ui->model));
    text_label(ui->footer, 1, 1, COLS - 4, ui->focus == SERVER_TABS ? "←→ servidores · Enter: adicionar · ↓ canais · Ctrl+Q: sair" :
               ui->focus == CHANNEL_LIST ? "↑↓ canais · ← abas · → conversa · Ctrl+L: sair da conta" :
               ui->focus == MESSAGE_INPUT ? "Enter enviar · Ctrl+N nova linha · Esc histórico · Ctrl+T tentar" :
               "↑↓ rolar · ← canais · Digite: editor · Ctrl+R atualizar");
    if (ui->list) {
        wbkgd(ui->list, COLOR_PAIR(ui->focus == CHANNEL_LIST ? FOCUS_AREA : PANEL)); werase(ui->list);
        wattrset(ui->list, COLOR_PAIR(ui->focus == CHANNEL_LIST ? FOCUS_MUTED : PANEL_MUTED) | A_BOLD);
        text_label(ui->list, 0, 1, ui->side - 2, "Canais");
        int rows = getmaxy(ui->list) - 2;
        size_t count = history_channel_count(ui->model);
        if (ui->highlighted >= count) ui->highlighted = count ? count - 1 : 0;
        if ((int)ui->highlighted < ui->first_channel) ui->first_channel = (int)ui->highlighted;
        if ((int)ui->highlighted >= ui->first_channel + rows) ui->first_channel = (int)ui->highlighted - rows + 1;
        if (auth_has_session(auth)) for (int row = 0; row < rows; ++row) {
            size_t index = (size_t)(ui->first_channel + row);
            json_t *c = history_channel(ui->model, index);
            if (!c) break;
            const char *type = str(c, "type"), *last = str(json_object_get(c, "last_message"), "id");
            bool unread = *last && strcmp(last, str(c, "last_read_message"));
            char label[2200];
            snprintf(label, sizeof(label), "%c %s%s%s", index == history_selected(ui->model) ? '>' : index == ui->highlighted ? '*' : ' ',
                     !strcmp(type, "category") ? "── " : !strcmp(type, "voice") ? "♪ " : "# ", unread ? "* " : "", str(c, "name"));
            bool selected = index == ui->highlighted;
            wattrset(ui->list, COLOR_PAIR(selected && ui->focus == CHANNEL_LIST ? FOCUSED : selected ? PANEL_ACTIVE : ui->focus == CHANNEL_LIST ? FOCUS_AREA : PANEL));
            text_label(ui->list, row + 1, 1, ui->side - 2, label);
        }
        wattrset(ui->list, COLOR_PAIR(ui->focus == CHANNEL_LIST ? FOCUS_MUTED : PANEL_MUTED));
        text_label(ui->list, getmaxy(ui->list) - 1, 1, ui->side - 2, "↑↓ escolher · Enter abrir");
    }
    if (ui->body) {
        wbkgd(ui->body, COLOR_PAIR(PANEL)); werase(ui->body);
        if (ui->focus == HISTORY_PANEL) {
            wattrset(ui->body, COLOR_PAIR(HISTORY_AREA));
            for (int row = 0; row < ui->editor_border; ++row)
                mvwhline(ui->body, row, 0, ' ', getmaxx(ui->body));
        }
        json_t *c = history_channel(ui->model, history_selected(ui->model));
        char title[2200]; snprintf(title, sizeof(title), "%s%s · Histórico", c ? "# " : "", c ? str(c, "name") : "Conversa");
        wattrset(ui->body, COLOR_PAIR(ui->focus == HISTORY_PANEL ? HISTORY_MUTED : PANEL_MUTED) | A_BOLD);
        text_label(ui->body, 0, 2, getmaxx(ui->body) - 4, title);
        wattrset(ui->body, COLOR_PAIR(ui->focus == HISTORY_PANEL ? HISTORY_MUTED : PANEL_MUTED));
        if (ui->history_top == 3) text_label(ui->body, 1, 2, getmaxx(ui->body) - 4, str(c, "topic"));
        wattrset(ui->body, COLOR_PAIR(ui->focus == HISTORY_PANEL ? HISTORY_BORDER : BORDER)); mvwhline(ui->body, ui->history_top - 1, 0, ACS_HLINE, getmaxx(ui->body));
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
        text_label(ui->body, ui->editor_border, 2, getmaxx(ui->body) - 4, " Editor ");
        if (editor) {
            int height = input_height(ui), width = getmaxx(ui->body) - 6;
            if (editing) for (int row = 0; row < height; ++row) {
                wattrset(ui->body, COLOR_PAIR(FOCUS_AREA));
                mvwhline(ui->body, ui->editor_border + 1 + row, 0, ' ', getmaxx(ui->body));
            }
            wattrset(ui->body, COLOR_PAIR(editing ? FOCUS_AREA : PANEL));
            text_label(ui->body, ui->editor_border + 1, 1, 1, ">");
            text_layout measured; text_measure(editor, width, &measured);
            text_keep_cursor(editor, &measured, height);
            text_draw(ui->body, editor, &measured, editor->first_row, height, ui->editor_border + 1, 3);
            if (editing) {
                wmove(ui->body, ui->editor_border + 1 + measured.cursor_row - editor->first_row, 3 + measured.cursor_column);
                curs_set(1);
            }
        } else {
            wattrset(ui->body, COLOR_PAIR(PANEL_MUTED));
            text_label(ui->body, ui->editor_border + 1, 3, getmaxx(ui->body) - 6,
                       auth_has_session(auth) ? "Abra um canal de texto" : "Confirme a sessão");
        }
        if (getmaxy(ui->body) >= 10) {
            wattrset(ui->body, COLOR_PAIR(PANEL_MUTED));
            text_label(ui->body, getmaxy(ui->body) - 1, 2, getmaxx(ui->body) - 4,
                       ui->feedback ? ui->feedback : editor ? send_editor_notice(ui->editor) : "Selecione uma conversa para escrever.");
        } else if (editor || ui->feedback) {
            wattrset(ui->footer, COLOR_PAIR(PANEL_MUTED));
            text_label(ui->footer, 0, 1, COLS - 4, ui->feedback ? ui->feedback : send_editor_notice(ui->editor));
        }
        if (editing && editor) {
            text_layout measured; text_measure(editor, getmaxx(ui->body) - 6, &measured);
            wmove(ui->body, ui->editor_border + 1 + measured.cursor_row - editor->first_row, 3 + measured.cursor_column);
        }
    }
    touchwin(ui->frame); wnoutrefresh(ui->frame); wnoutrefresh(ui->tabs);
    if (ui->list) wnoutrefresh(ui->list);
    if (ui->body) wnoutrefresh(ui->body);
    wnoutrefresh(ui->footer);
    if (ui->focus == MESSAGE_INPUT && input(ui) && ui->body && !ui->duplicate_modal) wnoutrefresh(ui->body);
    draw_confirmation(ui); doupdate();
}
static void set_focus(view *ui, int focus)
{
    ui->focus = focus;
    if (focus == CHANNEL_LIST) ui->show_channels = true;
    if (focus == HISTORY_PANEL || focus == MESSAGE_INPUT) ui->show_channels = false;
    layout(ui);
}
static void open_channel(view *ui)
{
    json_t *channel = history_channel(ui->model, ui->highlighted);
    bool text = !strcmp(str(channel, "type"), "text");
    if (text && !send_editor_select(ui->editor, str(channel, "id"))) {
        ui->feedback = "Não foi possível trocar o editor; rascunho preservado."; return;
    }
    if (history_select(ui->model, ui->highlighted)) {
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
    snprintf(highlighted, sizeof(highlighted), "%s", str(history_channel(ui->model, ui->highlighted), "id"));
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
    for (size_t i = 0; i < history_channel_count(ui->model); ++i)
        if (!strcmp(highlighted, str(history_channel(ui->model, i), "id"))) ui->highlighted = i;
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
    int width = getmaxx(ui->body) - 6;
    bool handled = true;
    if (type == OK && (key == 8 || key == 127)) text_delete(editor, true);
    else if (type == KEY_CODE_YES && key == KEY_BACKSPACE) text_delete(editor, true);
    else if (type == KEY_CODE_YES && key == KEY_DC) text_delete(editor, false);
    else if (type == KEY_CODE_YES && (key == KEY_LEFT || key == KEY_RIGHT)) text_horizontal(editor, key == KEY_RIGHT);
    else if (type == KEY_CODE_YES && (key == KEY_UP || key == KEY_DOWN)) text_vertical(editor, width, key == KEY_UP ? -1 : 1);
    else if (type == KEY_CODE_YES && (key == KEY_HOME || key == KEY_END)) text_line_edge(editor, width, key == KEY_END);
    else if (type == OK && key == 21) text_clear(editor);
    else if (type == OK && key == 14) text_insert(editor, '\n', true);
    else if (type == OK && iswprint(key)) text_insert(editor, key, true);
    else handled = false;
    if (handled) {
        ui->feedback = send_editor_changed(ui->editor) ? NULL : "Não foi possível salvar o rascunho.";
    }
    return handled;
}
int history_run(auth_client *auth, instance_connections *connections, size_t *selected,
                bool focus_tabs, volatile sig_atomic_t *interrupted)
{
    init_graphemebreak();
    view *ui = calloc(1, sizeof(*ui));
    if (!ui) return HISTORY_ACCOUNT;
    ui->connections = connections; ui->active_tab = ui->tab_cursor = instances_index(connections, auth);
    ui->editor = instances_editor(connections, auth);
    ui->model = history_create(auth); ui->at_end = ui->show_channels = true; ui->focus = focus_tabs ? SERVER_TABS : CHANNEL_LIST;
    if (!ui->model || !ui->editor || ui->active_tab == SIZE_MAX || !layout(ui)) { history_destroy(ui->model); destroy_layout(ui); free(ui); return HISTORY_ACCOUNT; }
    mmask_t previous_mouse;
    mousemask(BUTTON1_PRESSED | BUTTON1_CLICKED | BUTTON4_PRESSED | BUTTON5_PRESSED, &previous_mouse);
    bool redraw = true; int result = HISTORY_ACCOUNT;
    while (!*interrupted) {
        if (ui->layout_failed) break;
        auth_state before = auth_get_state(auth);
        if (session_store_poll(connections->sessions, auth)) redraw = true;
        if (instances_poll(connections, auth)) redraw = true;
        if (poll_model(ui, auth) || before != auth_get_state(auth)) redraw = true;
        if (!auth_has_session(auth) && !auth_busy(auth)) break;
        if (redraw) { draw(ui, auth); redraw = false; }
        wint_t key; int type = get_wch(&key);
        if (type == ERR) continue;
        if (type == KEY_CODE_YES && key == PASTE_START) {
            ui->paste = true; ui->paste_cr = false;
            if (!ui->duplicate_modal && input(ui) && ui->focus != SERVER_TABS) { set_focus(ui, MESSAGE_INPUT); redraw = true; }
            continue;
        }
        if (type == KEY_CODE_YES && key == PASTE_END) { ui->paste = false; continue; }
        if (type == KEY_CODE_YES && key == KEY_RESIZE) { layout(ui); redraw = true; continue; }
        if (ui->paste) {
            if (!ui->duplicate_modal && ui->focus == MESSAGE_INPUT && type == OK && input(ui)) {
                text_buffer *editor = input(ui);
                if (key == '\n' && ui->paste_cr) { ui->paste_cr = false; continue; }
                ui->paste_cr = key == '\r';
                if (ui->paste_cr) key = '\n';
                if (key == '\t') key = ' ';
                if (key == '\n' || iswprint(key)) {
                    text_insert(editor, key, true);
                    ui->feedback = send_editor_changed(ui->editor) ? NULL : "Não foi possível salvar o rascunho.";
                    redraw = true;
                }
            }
            continue;
        }
        redraw = true;
        if (type == OK && (key == 17 || key == 3)) { result = HISTORY_EXIT; break; }
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
                if (getmouse(&e) == OK && (e.bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED))) {
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
        if (type == OK && key == 20 && input(ui)) {
            outbox_operation latest;
            if (outbox_latest(send_editor_outbox(ui->editor), ui->editor_channel, &latest) &&
                (latest.state == OUTBOX_FAILED || latest.state == OUTBOX_UNCERTAIN)) submit(ui, latest.id, false);
            else ui->feedback = "Nenhum envio falhou nesta conversa.";
            continue;
        }
        if (type == OK && key == 18) {
            if (auth_ws_reconnecting(auth)) { auth_connect_ws(auth); continue; }
            if (history_message_count(ui->model)) {
                if (!history_retry(ui->model)) { ui->at_end = true; ui->anchor[0] = 0; }
            } else if (history_selected(ui->model) != SIZE_MAX) history_recent(ui->model);
            else history_reload_channels(ui->model);
            continue;
        }
        if ((type == OK && key == '\t') || (type == KEY_CODE_YES && key == KEY_BTAB)) {
            if (COLS < 80) {
                if (ui->focus == SERVER_TABS) set_focus(ui, CHANNEL_LIST);
                else if (ui->focus == HISTORY_PANEL || ui->focus == MESSAGE_INPUT) set_focus(ui, CHANNEL_LIST);
                else if (history_selected(ui->model) != ui->highlighted) open_channel(ui);
                else set_focus(ui, HISTORY_PANEL);
            } else {
                int count = input(ui) ? 4 : 3;
                set_focus(ui, type == KEY_CODE_YES ? (ui->focus + count - 1) % count : (ui->focus + 1) % count);
            }
            continue;
        }
        if (type == OK && key == 27) { set_focus(ui, ui->focus == MESSAGE_INPUT ? HISTORY_PANEL : ui->focus == HISTORY_PANEL ? CHANNEL_LIST : HISTORY_PANEL); continue; }
        if (type == OK && key == 14 && ui->focus != SERVER_TABS && input(ui)) {
            set_focus(ui, MESSAGE_INPUT); edit(ui, type, key); continue;
        }
        if (type == KEY_CODE_YES && key == KEY_MOUSE) {
            MEVENT e;
            if (getmouse(&e) != OK || !ui->frame) continue;
            if (e.bstate & (BUTTON4_PRESSED | BUTTON5_PRESSED)) {
                if (ui->body && e.x >= getbegx(ui->body) && e.x < getbegx(ui->body) + getmaxx(ui->body) && e.y >= getbegy(ui->body) + ui->history_top && e.y < getbegy(ui->body) + ui->editor_border)
                    scroll_history(ui, e.bstate & BUTTON4_PRESSED ? -3 : 3);
                continue;
            }
            if (!(e.bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED))) continue;
            if (ui->unseen && e.y == getbegy(ui->footer)) {
                ui->at_end = true; ui->unseen = 0; ui->anchor[0] = 0; continue;
            }
            int y = e.y, x = e.x;
            if (wmouse_trafo(ui->tabs, &y, &x, false)) {
                if (y == 0 && hit(ui->add_hit, x)) { result = HISTORY_ADD; break; }
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
            if (ui->list && wmouse_trafo(ui->list, &y, &x, false) && y >= 1 && y < getmaxy(ui->list) - 1) {
                size_t index = (size_t)(ui->first_channel + y - 1);
                if (index < history_channel_count(ui->model)) { ui->highlighted = index; open_channel(ui); }
                continue;
            }
            y = e.y; x = e.x;
            if (ui->body && wmouse_trafo(ui->body, &y, &x, false)) {
                if (y < ui->editor_border) set_focus(ui, HISTORY_PANEL);
                else if (input(ui) && y > ui->editor_border && y <= ui->editor_border + input_height(ui)) {
                    text_buffer *editor = input(ui);
                    text_click(editor, getmaxx(ui->body) - 6, y - ui->editor_border - 1 + editor->first_row, x - 3);
                    send_editor_changed(ui->editor); set_focus(ui, MESSAGE_INPUT);
                }
            }
            continue;
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
            size_t count = history_channel_count(ui->model);
            if (type == KEY_CODE_YES && (key == KEY_LEFT || (key == KEY_UP && !ui->highlighted))) set_focus(ui, SERVER_TABS);
            else if (type == KEY_CODE_YES && key == KEY_UP && ui->highlighted) --ui->highlighted;
            else if (type == KEY_CODE_YES && key == KEY_DOWN && ui->highlighted + 1 < count) ++ui->highlighted;
            else if (type == KEY_CODE_YES && key == KEY_HOME) ui->highlighted = 0;
            else if (type == KEY_CODE_YES && key == KEY_END && count) ui->highlighted = count - 1;
            else if (type == KEY_CODE_YES && key == KEY_PPAGE)
                ui->highlighted = ui->highlighted > (size_t)(LINES - CONTENT_Y - 5) ? ui->highlighted - (size_t)(LINES - CONTENT_Y - 5) : 0;
            else if (type == KEY_CODE_YES && key == KEY_NPAGE && count) {
                ui->highlighted += (size_t)(LINES - CONTENT_Y - 5); if (ui->highlighted >= count) ui->highlighted = count - 1;
            } else if (enter || (type == KEY_CODE_YES && key == KEY_RIGHT)) open_channel(ui);
        } else {
            if (type == KEY_CODE_YES && key == KEY_LEFT) set_focus(ui, CHANNEL_LIST);
            else if (type == KEY_CODE_YES && (key == KEY_PPAGE || key == KEY_UP || key == KEY_HOME)) {
                int first = position(ui);
                scroll_history(ui, key == KEY_HOME ? -2147483647 : key == KEY_UP ? -1 : -ui->visible);
                if (first == 0 && key != KEY_HOME && history_has_more(ui->model)) { ui->at_end = false; history_older(ui->model); }
            } else if (type == KEY_CODE_YES && (key == KEY_DOWN || key == KEY_NPAGE)) scroll_history(ui, key == KEY_DOWN ? 1 : ui->visible);
            else if (type == KEY_CODE_YES && key == KEY_END) {
                if (!ui->unseen || history_evicted(ui->model)) history_recent(ui->model);
                ui->at_end = true; ui->unseen = 0; ui->anchor[0] = 0;
            }
            else if (enter && input(ui)) set_focus(ui, MESSAGE_INPUT);
            else if (enter && !history_retry(ui->model)) { ui->at_end = true; ui->anchor[0] = 0; }
        }
    }
finished:
    mousemask(previous_mouse, NULL);
    destroy_layout(ui); history_destroy(ui->model); free(ui); touchwin(stdscr); return result;
}
