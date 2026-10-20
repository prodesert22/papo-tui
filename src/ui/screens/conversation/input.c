#include "ui/screens/conversation/private.h"

const char *conversation_str(const json_t *obj, const char *key)
{
    const char *s = json_string_value(json_object_get(obj, key));
    return s ? s : "";
}

size_t conversation_list_index(view *ui, size_t row)
{
    for (size_t i = 0; i < history_channel_count(ui->model); ++i) {
        json_t *c = history_channel(ui->model, i);
        if (!history_channel_visible(ui->model, i) ||
            json_is_true(json_object_get(c, "_dm")) != ui->list_dms)
            continue;
        if (!row--)
            return i;
    }
    return SIZE_MAX;
}

size_t conversation_list_count(view *ui)
{
    size_t n = 0;
    for (size_t i = 0; i < history_channel_count(ui->model); ++i)
        if (history_channel_visible(ui->model, i) &&
            json_is_true(json_object_get(history_channel(ui->model, i), "_dm")) == ui->list_dms)
            ++n;
    return n;
}

json_t *conversation_list_channel(view *ui, size_t row)
{
    return history_channel(ui->model, conversation_list_index(ui, row));
}

bool conversation_list_selectable(view *ui, size_t row)
{
    json_t *channel = conversation_list_channel(ui, row);
    return channel && strcmp(conversation_str(channel, "type"), "category");
}

size_t conversation_list_find(view *ui, size_t row, int direction)
{
    size_t count = conversation_list_count(ui);
    while (row < count) {
        if (conversation_list_selectable(ui, row))
            return row;
        if (direction < 0) {
            if (!row)
                break;
            --row;
        } else
            ++row;
    }
    return SIZE_MAX;
}

void conversation_list_normalize(view *ui)
{
    size_t count = conversation_list_count(ui);
    if (conversation_list_selectable(ui, ui->highlighted))
        return;
    size_t start = ui->highlighted < count ? ui->highlighted : count ? count - 1 : 0;
    size_t row = conversation_list_find(ui, start, 1);
    if (row == SIZE_MAX)
        row = conversation_list_find(ui, start, -1);
    ui->highlighted = row == SIZE_MAX ? 0 : row;
}

bool conversation_list_notified(view *ui, bool dms)
{
    for (size_t i = 0; i < history_channel_count(ui->model); ++i) {
        json_t *channel = history_channel(ui->model, i);
        if (!inbox_conversation_muted(history_inbox(ui->model), conversation_str(channel, "id")) &&
            json_is_true(json_object_get(channel, "_dm")) == dms &&
            (history_channel_unread(ui->model, channel) ||
             inbox_unread_mentions(history_inbox(ui->model), conversation_str(channel, "id"))))
            return true;
    }
    return false;
}

void conversation_switch_list(view *ui, bool dms)
{
    ui->highlights[ui->list_dms] = ui->highlighted;
    ui->list_dms = dms;
    ui->list_button = dms;
    ui->highlighted = ui->highlights[dms];
    conversation_list_normalize(ui);
    ui->first_channel = 0;
}

bool conversation_hit(click_range range, int x)
{
    return range.width > 0 && x >= range.x && x < range.x + range.width;
}

text_buffer *conversation_input(view *ui)
{
    json_t *channel = history_channel(ui->model, history_selected(ui->model));
    if (!channel ||
        (strcmp(conversation_str(channel, "type"), "text") &&
         strcmp(conversation_str(channel, "type"), "voice")) ||
        strcmp(conversation_str(channel, "id"), ui->editor_channel))
        return NULL;
    text_buffer *draft = send_editor_input(ui->editor);
    return draft && *ui->edit_message ? &ui->edit_input : draft;
}

int conversation_input_width(view *ui)
{
    return ui->body ? MAX(3, getmaxx(ui->body) - conversation_input_left(ui) -
                                 (*ui->edit_message ? 9 : 16))
                    : 3;
}

int conversation_input_left(view *ui)
{
    (void)ui;
    return 3;
}

int conversation_input_height(view *ui)
{
    return ui->editor_rows;
}

int conversation_input_top(view *ui)
{
    return ui->editor_border + 1 + ui->editor_padding;
}

bool conversation_input_row(view *ui, int row)
{
    return row >= conversation_input_top(ui) &&
           row < conversation_input_top(ui) + conversation_input_height(ui);
}

void conversation_set_focus(view *ui, int focus)
{
    if (ui->focus == focus)
        return;
    bool was_channels = ui->show_channels;
    ui->focus = focus;
    if (focus == CHANNEL_LIST || focus == LIST_BUTTONS || focus == SETTINGS_BUTTON)
        ui->show_channels = true;
    if (focus == HISTORY_PANEL || focus == MESSAGE_INPUT || focus == EMOJI_BUTTON ||
        focus == ATTACHMENT_ROW || focus == ATTACH_BUTTON || focus == SEARCH_BUTTON ||
        focus == PINS_BUTTON || focus == MEMBERS_BUTTON)
        ui->show_channels = false;
    if (COLS < 80 && was_channels != ui->show_channels)
        conversation_layout(ui);
}

void conversation_open_emojis(view *ui)
{
    if (!conversation_input(ui) || ui->duplicate_modal ||
        (r8_modal_is_open(ui->r8) || ui->settings.open) || media_view_modal(ui->media))
        return;
    mention_popup_close(ui->mentions);
    ui->dragging = false;
    ui->emoji_return_focus = ui->focus;
    emoji_picker_show(ui->emojis, ui->editor_channel,
                      auth_context_generation(ui->connections->items[ui->active_tab].auth));
}

void conversation_open_channel(view *ui, bool voice_history)
{
    json_t *channel = conversation_list_channel(ui, ui->highlighted);
    if (!channel || !strcmp(conversation_str(channel, "type"), "category"))
        return;
    bool voice = !strcmp(conversation_str(channel, "type"), "voice");
    if (voice && !voice_history) {
        conversation_set_focus(ui, CHANNEL_LIST);
        ui->feedback = "Voz em breve. Shift+Enter/F4 abre o histórico.";
        return;
    }
    if (voice)
        ui->feedback = NULL;
    /* Selecting the displayed channel only changes focus. Preserve pending
     * reads, the reading anchor, the draft and the current message window. */
    if (history_selected(ui->model) == conversation_list_index(ui, ui->highlighted)) {
        conversation_set_focus(ui, HISTORY_PANEL);
        return;
    }
    if (!send_editor_select(ui->editor, conversation_str(channel, "id"))) {
        ui->feedback = "Não foi possível trocar o editor; rascunho preservado.";
        return;
    }
    if (history_select(ui->model, conversation_list_index(ui, ui->highlighted))) {
        for (int context = STATUS_HISTORY; context <= STATUS_CLIPBOARD; ++context)
            status_bar_set(&ui->status, (status_context)context, NULL);
        media_view_suspend(ui->media);
        snprintf(ui->editor_channel, sizeof(ui->editor_channel), "%s",
                 conversation_str(channel, "id"));
        ui->feedback = NULL;
        ui->unseen = 0;
        ui->waiting_page = 0;
        ui->at_end = true;
        ui->anchor[0] = 0;
        conversation_set_focus(ui, HISTORY_PANEL);
    }
}

int conversation_activate_tab(view *ui, size_t index, size_t *selected)
{
    ui->tab_cursor = index;
    ui->focus = SERVER_TABS;
    if (index == ui->connections->count)
        return HISTORY_ADD;
    if (index != ui->active_tab) {
        *selected = index;
        return HISTORY_SWITCH;
    }
    return -1;
}

void conversation_cancel_edit(view *ui)
{
    ui->edit_message[0] = ui->edit_submitted[0] = 0;
    ui->edit_pending = false;
    ui->feedback = NULL;
    text_clear(&ui->edit_input);
    mention_popup_close(ui->mentions);
    emoji_picker_dismiss(ui->emojis);
    ui->dragging = ui->editor_scrolling = false;
}

void conversation_submit(view *ui, uint64_t retry_id, bool duplicate)
{
    if (ui->attachment_task) {
        ui->feedback = "Aguarde a preparação dos anexos; Esc cancela.";
        return;
    }
    if (!conversation_input(ui))
        return;
    ui->feedback = NULL;
    if (*ui->edit_message) {
        if (retry_id || ui->edit_pending) {
            ui->feedback = "Edição em andamento; aguarde ou pressione Esc para cancelar.";
            return;
        }
        char content[TEXT_UTF8_BYTES];
        if (!text_encode(&ui->edit_input, content) || !*content) {
            ui->feedback = "Escreva uma mensagem antes de editar; Esc cancela.";
            return;
        }
        if (!history_edit_message(ui->model, ui->edit_message, content)) {
            ui->feedback = "Não foi possível preparar a edição; aguarde ou tente novamente.";
            return;
        }
        snprintf(ui->edit_submitted, sizeof(ui->edit_submitted), "%s", content);
        ui->edit_pending = true;
        return;
    }
    outbox_submit_result result = retry_id ? send_editor_retry(ui->editor, retry_id, duplicate)
                                           : send_editor_submit(ui->editor, duplicate);
    if (result == OUTBOX_CONFIRM_DUPLICATION) {
        ui->duplicate_modal = true;
        ui->reenviar = false;
        ui->retry_id = retry_id;
    }
}

void conversation_close_confirmation(view *ui)
{
    ui->duplicate_modal = false;
    if (ui->confirm) {
        delwin(ui->confirm);
        ui->confirm = NULL;
    }
}

bool conversation_edit(view *ui, int type, uint32_t key)
{
    text_buffer *editor = conversation_input(ui);
    if (!editor || !ui->body)
        return false;
    text_buffer before = *editor;
    bool handled = text_edit(editor, type, key, conversation_input_width(ui), true);
    if (handled) {
        ui->editor_scrolling = ui->dragging = false;
        ui->feedback =
            send_editor_changed(ui->editor) ? NULL : "Não foi possível salvar o rascunho.";
        if (before.length != editor->length ||
            memcmp(before.data, editor->data, editor->length * sizeof(editor->data[0])))
            history_typing_activity(ui->model);
    }
    return handled;
}

void conversation_handle_confirmation(view *ui, int type, uint32_t key, bool enter)
{
    if (type == OK && key == 27) {
        conversation_close_confirmation(ui);
        return;
    }
    if ((type == OK && key == '\t') || (type == KEY_CODE_YES && key == KEY_BTAB))
        ui->reenviar = !ui->reenviar;
    else if (type == KEY_CODE_YES && key == KEY_LEFT)
        ui->reenviar = false;
    else if (type == KEY_CODE_YES && key == KEY_RIGHT)
        ui->reenviar = true;
    else if (enter) {
        bool retry = ui->reenviar;
        uint64_t id = ui->retry_id;
        conversation_close_confirmation(ui);
        if (retry)
            conversation_submit(ui, id, true);
    } else if (type == KEY_CODE_YES && key == KEY_MOUSE && ui->confirm) {
        MEVENT e;
        if (ui_mouse_get(&e) == OK && (e.bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED))) {
            int y = e.y, x = e.x;
            if (wmouse_trafo(ui->confirm, &y, &x, false) && y == 7) {
                bool cancel = x >= 2 && x < 14;
                bool retry = x >= getmaxx(ui->confirm) - 14 && x < getmaxx(ui->confirm) - 2;
                uint64_t id = ui->retry_id;
                if (cancel || retry) {
                    conversation_close_confirmation(ui);
                    if (retry)
                        conversation_submit(ui, id, true);
                }
            }
        }
    }
    return;
}
