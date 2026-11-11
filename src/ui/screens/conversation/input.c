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

static const json_t *participants(view *ui, size_t row)
{
    json_t *channel = conversation_list_channel(ui, row);
    return !ui->list_dms && !strcmp(conversation_str(channel, "type"), "voice")
               ? voice_members(history_voice(ui->model), conversation_str(channel, "id"))
               : NULL;
}

size_t conversation_list_visual_row(view *ui, size_t channel)
{
    size_t row = 0;
    for (size_t i = 0; i < channel && i < conversation_list_count(ui); ++i)
        row += 1 + json_array_size(participants(ui, i));
    return row;
}

size_t conversation_list_visual_hit(view *ui, size_t row, const char **participant)
{
    if (participant)
        *participant = NULL;
    for (size_t i = 0; i < conversation_list_count(ui); ++i) {
        if (!row)
            return i;
        --row;
        const json_t *members = participants(ui, i);
        size_t count = json_array_size(members);
        if (row < count) {
            if (participant)
                *participant = json_string_value(json_array_get(members, row));
            return i;
        }
        row -= count;
    }
    return SIZE_MAX;
}

int conversation_list_rows(view *ui)
{
    return ui->list ? MAX(1, getmaxy(ui->list) - 5 - (voice_active() ? 2 : 0)) : 0;
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
    if (focus == CHANNEL_LIST || focus == LIST_BUTTONS || focus == SETTINGS_BUTTON ||
        focus == VOICE_OUTPUT_BUTTON || focus == VOICE_MICROPHONE_BUTTON ||
        focus == VOICE_LEAVE_BUTTON || focus == VOICE_PTT_BUTTON)
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
        (r8_modal_is_open(ui->r8) || conversation_panel_open(ui)) || media_view_modal(ui->media))
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
        /* The voice status slot already reports the result of this attempt. */
        voice_join(history_voice(ui->model), conversation_str(channel, "id"));
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
    /* The reading position is cached with the conversation being left. */
    history_reading reading = {.offset = ui->offset, .at_end = ui->at_end};
    snprintf(reading.anchor, sizeof(reading.anchor), "%s", ui->anchor);
    snprintf(reading.block, sizeof(reading.block), "%s", ui->block);
    history_keep_reading(ui->model, &reading);
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
        /* A cached conversation reopens where the reader left it. */
        if (history_restored_reading(ui->model, &reading) && !reading.at_end && *reading.anchor) {
            ui->at_end = false;
            snprintf(ui->anchor, sizeof(ui->anchor), "%s", reading.anchor);
            snprintf(ui->block, sizeof(ui->block), "%s", reading.block);
            ui->offset = reading.offset;
        }
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

void conversation_open_switcher(view *ui)
{
    instance_connections *connections = ui->connections;
    size_t count = 0, capacity = 0;
    switcher_entry *entries = NULL;
    for (size_t tab = 0; tab < connections->count; ++tab) {
        history *model = connections->items[tab].model;
        if (!model)
            continue;
        inbox *box = history_inbox(model);
        /* The conversation on screen is not a destination. */
        const json_t *current =
            tab == ui->active_tab ? history_channel(ui->model, history_selected(ui->model)) : NULL;
        for (size_t i = 0; i < history_channel_count(model); ++i) {
            json_t *channel = history_channel(model, i);
            const char *type = conversation_str(channel, "type"),
                       *id = conversation_str(channel, "id");
            if (channel == current || !strcmp(type, "category") || !history_uuid(id))
                continue;
            if (count == capacity) {
                size_t grown = capacity ? capacity * 2 : 64;
                switcher_entry *larger = realloc(entries, grown * sizeof(*entries));
                if (!larger) {
                    free(entries);
                    ui->feedback = _("Memória insuficiente para listar as conversas.");
                    return;
                }
                entries = larger;
                capacity = grown;
            }
            switcher_entry *entry = &entries[count++];
            memset(entry, 0, sizeof(*entry));
            entry->instance = tab;
            snprintf(entry->channel, sizeof(entry->channel), "%s", id);
            snprintf(entry->name, sizeof(entry->name), "%s", conversation_str(channel, "name"));
            snprintf(entry->server, sizeof(entry->server), "%s", connections->items[tab].name);
            snprintf(entry->last, sizeof(entry->last), "%s",
                     conversation_str(json_object_get(channel, "last_message"), "created_at"));
            entry->dm = json_is_true(json_object_get(channel, "_dm"));
            entry->voice = !strcmp(type, "voice");
            entry->muted = inbox_conversation_muted(box, id);
            entry->unread = !entry->muted && history_channel_unread(model, channel);
            entry->mentions = inbox_unread_mentions(box, id);
        }
    }
    if (!quick_switcher_open(&ui->switcher, entries, count))
        ui->feedback = _("Memória insuficiente para listar as conversas.");
}

int conversation_switch_to(view *ui, const switcher_entry *entry, size_t *selected)
{
    if (entry->instance >= ui->connections->count)
        return -1;
    if (entry->instance != ui->active_tab) {
        /* The other tab opens it as it would a clicked desktop notification
         * without a message: the conversation at its latest messages. */
        instance_connection *item = &ui->connections->items[entry->instance];
        snprintf(item->open_channel, sizeof(item->open_channel), "%s", entry->channel);
        item->open_message[0] = item->open_time[0] = 0;
        return conversation_activate_tab(ui, entry->instance, selected);
    }
    for (size_t i = 0; i < history_channel_count(ui->model); ++i) {
        json_t *channel = history_channel(ui->model, i);
        if (strcmp(entry->channel, conversation_str(channel, "id")))
            continue;
        conversation_switch_list(ui, json_is_true(json_object_get(channel, "_dm")));
        for (size_t j = 0; j < conversation_list_count(ui); ++j)
            if (conversation_list_index(ui, j) == i)
                ui->highlighted = j;
        conversation_open_channel(ui, true);
        if (history_selected(ui->model) == i && conversation_input(ui))
            conversation_set_focus(ui, MESSAGE_INPUT);
        return -1;
    }
    ui->feedback = _("A conversa não está mais disponível.");
    return -1;
}

void conversation_cancel_edit(view *ui)
{
    ui->edit_message[0] = 0;
    ui->feedback = NULL;
    text_clear(&ui->edit_input);
    mention_popup_close(ui->mentions);
    emoji_picker_dismiss(ui->emojis);
    ui->dragging = ui->editor_scrolling = false;
    conversation_selection_clear(ui);
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
        if (retry_id) {
            ui->feedback = "Edição aberta; Enter edita ou Esc cancela.";
            return;
        }
        char content[TEXT_UTF8_BYTES];
        bool encoded = text_encode(&ui->edit_input, content);
        if (encoded && !*content && conversation_confirm_delete_edit(ui))
            return;
        if (!encoded || !*content) {
            send_editor_refuse(ui->editor, true);
            ui->feedback = "Escreva uma mensagem antes de editar; Esc cancela.";
            return;
        }
        if (!history_edit_message(ui->model, ui->edit_message, content)) {
            ui->feedback = "Não foi possível preparar a edição; aguarde ou tente novamente.";
            return;
        }
        /* Optimistic: the history shows the text at once and the send draft
         * returns; a failed edit keeps the text for the next edit. */
        snprintf(ui->edit_sent, sizeof(ui->edit_sent), "%s", ui->edit_message);
        snprintf(ui->edit_sent_text, sizeof(ui->edit_sent_text), "%s", content);
        ui->edit_sent_failed = false;
        conversation_cancel_edit(ui);
        return;
    }
    if (!retry_id && conversation_profile_command(ui))
        return;
    outbox_submit_result result = retry_id ? send_editor_retry(ui->editor, retry_id, duplicate)
                                           : send_editor_submit(ui->editor, duplicate);
    /* Your own message is shown where it lands: at the newest messages, even
     * after scrolling up to read. Messages from others never move the view. */
    if (result == OUTBOX_ACCEPTED)
        conversation_scroll_latest(ui, false);
    if (result == OUTBOX_CONFIRM_DUPLICATION) {
        ui->duplicate_modal = true;
        ui->reenviar = false;
        ui->retry_id = retry_id;
    }
}

void conversation_confirm_remove(view *ui, size_t index)
{
    if (index >= ui->connections->count)
        return;
    conversation_interactions_close(ui);
    mention_popup_close(ui->mentions);
    ui->duplicate_modal = ui->remove_server = true;
    ui->remove_index = index;
    ui->reenviar = false; /* Cancelar keeps the default focus. */
}

void conversation_close_confirmation(view *ui)
{
    ui->duplicate_modal = ui->remove_server = false;
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
        send_editor_refuse(ui->editor, false);
        ui->editor_scrolling = ui->dragging = false;
        ui->feedback =
            send_editor_changed(ui->editor) ? NULL : "Não foi possível salvar o rascunho.";
        if (before.length != editor->length ||
            memcmp(before.data, editor->data, editor->length * sizeof(editor->data[0])))
            history_typing_activity(ui->model);
    }
    return handled;
}

bool conversation_handle_confirmation(view *ui, int type, uint32_t key, bool enter)
{
    if (type == OK && key == 27) {
        conversation_close_confirmation(ui);
        return false;
    }
    bool removing = ui->remove_server;
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
        if (retry && removing)
            return true;
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
                    if (retry && removing)
                        return true;
                    if (retry)
                        conversation_submit(ui, id, true);
                }
            }
        }
    }
    return false;
}

int conversation_focus_order(view *ui, int order[24])
{
    /* The order follows the screen. The voice buttons come last, so the
     * existing areas keep their relative order. */
    int count = 0;
    order[count++] = SERVER_TABS;
    order[count++] = LIST_BUTTONS;
    order[count++] = CHANNEL_LIST;
    order[count++] = SETTINGS_BUTTON;
    order[count++] = HISTORY_PANEL;
    if (conversation_input(ui)) {
        const GPtrArray *files = conversation_attachments(ui);
        if (files && files->len)
            order[count++] = ATTACHMENT_ROW;
        order[count++] = MESSAGE_INPUT;
        if (!*ui->edit_message)
            order[count++] = ATTACH_BUTTON;
        order[count++] = EMOJI_BUTTON;
    }
    order[count++] = SEARCH_BUTTON;
    order[count++] = PINS_BUTTON;
    order[count++] = MEMBERS_BUTTON;
    if (ui->members_open)
        order[count++] = MEMBERS_PANEL;
    if (voice_available()) {
        order[count++] = VOICE_OUTPUT_BUTTON;
        order[count++] = VOICE_MICROPHONE_BUTTON;
        if (voice_active()) {
            order[count++] = VOICE_LEAVE_BUTTON;
            if (ui->connections->config && ui->connections->config->audio.push_to_talk)
                order[count++] = VOICE_PTT_BUTTON;
        }
    }
    return count;
}

typedef struct {
    int y, x, height, width;
} focus_rect;

static bool window_row(WINDOW *w, int row, click_range hit, focus_rect *r)
{
    if (!w || !hit.width)
        return false;
    *r = (focus_rect){getbegy(w) + row, getbegx(w) + hit.x, 1, hit.width};
    return true;
}

/* Where a component is drawn, in screen cells. On narrow screens only the
 * channel side or the conversation side exists; the hidden side is placed
 * just beyond the matching edge so that ← and → still reach it. */
static bool component_rect(view *ui, int focus, focus_rect *r)
{
    WINDOW *list = ui->list, *body = ui->body;
    int rows = LINES - CONTENT_Y - 3;
    switch (focus) {
    case SERVER_TABS:
        /* The tab bar spans the screen: every area below reaches it. */
        return ui->tabs && window_row(ui->tabs, 0, (click_range){0, getmaxx(ui->tabs)}, r);
    case LIST_BUTTONS:
        return list && window_row(list, 0, (click_range){0, getmaxx(list)}, r);
    case CHANNEL_LIST:
        if (!list) {
            *r = (focus_rect){CONTENT_Y, -COLS, rows, COLS};
            return true;
        }
        *r = (focus_rect){getbegy(list) + 1, getbegx(list), MAX(1, getmaxy(list) - 5),
                          getmaxx(list)};
        return true;
    case SETTINGS_BUTTON:
        return list && window_row(list, getmaxy(list) - 2,
                                  (click_range){getmaxx(list) - 6, 5}, r);
    case VOICE_OUTPUT_BUTTON:
        return list && window_row(list, getmaxy(list) - 2, ui->voice_output_hit, r);
    case VOICE_MICROPHONE_BUTTON:
        return list && window_row(list, getmaxy(list) - 2, ui->voice_microphone_hit, r);
    case VOICE_LEAVE_BUTTON:
        return list && window_row(list, getmaxy(list) - 5, ui->voice_leave_hit, r);
    case VOICE_PTT_BUTTON:
        return list && window_row(list, getmaxy(list) - 6, ui->voice_ptt_hit, r);
    case HISTORY_PANEL:
        if (!body) {
            *r = (focus_rect){CONTENT_Y, COLS, rows, COLS};
            return true;
        }
        *r = (focus_rect){getbegy(body) + ui->history_top, getbegx(body), MAX(1, ui->visible),
                          getmaxx(body)};
        return true;
    case SEARCH_BUTTON:
        return window_row(body, 0, ui->search_hit, r);
    case PINS_BUTTON:
        return window_row(body, 0, ui->pins_hit, r);
    case MEMBERS_BUTTON:
        return window_row(body, 0, ui->members_hit, r);
    case ATTACHMENT_ROW:
        if (!body || !ui->attachment_rows)
            return false;
        *r = (focus_rect){getbegy(body) + ui->attachment_top, getbegx(body),
                          ui->attachment_rows, getmaxx(body)};
        return true;
    case MESSAGE_INPUT:
        if (!body)
            return false;
        /* The composer, padding included, as its buttons fill it. */
        *r = (focus_rect){getbegy(body) + conversation_input_top(ui) - ui->editor_padding,
                          getbegx(body),
                          MAX(1, conversation_input_height(ui)) + 2 * ui->editor_padding,
                          MAX(1, getmaxx(body) - 16)};
        return true;
    case ATTACH_BUTTON:
        return window_row(body, conversation_input_top(ui), ui->attach_button_hit, r);
    case EMOJI_BUTTON:
        return window_row(body, conversation_input_top(ui), ui->emoji_button_hit, r);
    case MEMBERS_PANEL: {
        WINDOW *drawer = conversation_members_window(ui);
        if (!drawer)
            return false;
        *r = (focus_rect){getbegy(drawer), getbegx(drawer), getmaxy(drawer), getmaxx(drawer)};
        return true;
    }
    default:
        return false;
    }
}

/* Gap between [a0, a1) and [b0, b1); 0 when they overlap. */
static int gap(int a0, int a1, int b0, int b1)
{
    return b0 >= a1 ? b0 - a1 + 1 : a0 >= b1 ? a0 - b1 + 1 : 0;
}

int conversation_focus_target(view *ui, int dx, int dy)
{
    int order[24], count = conversation_focus_order(ui, order);
    focus_rect from;
    if (!component_rect(ui, ui->focus, &from))
        return -1;
    int best = -1;
    long best_score = 0;
    for (int i = 0; i < count; ++i) {
        focus_rect to;
        if (order[i] == ui->focus || !component_rect(ui, order[i], &to))
            continue;
        /* Only components entirely past the current one in that direction. */
        int along, across;
        if (dx > 0 && to.x >= from.x + from.width)
            along = to.x - (from.x + from.width);
        else if (dx < 0 && to.x + to.width <= from.x)
            along = from.x - (to.x + to.width);
        else if (dy > 0 && to.y >= from.y + from.height)
            along = to.y - (from.y + from.height);
        else if (dy < 0 && to.y + to.height <= from.y)
            along = from.y - (to.y + to.height);
        else
            continue;
        across = dx ? gap(from.y, from.y + from.height, to.y, to.y + to.height)
                    : gap(from.x, from.x + from.width, to.x, to.x + to.width);
        /* Only what lines up with the current component, so an arrow never
         * jumps across the screen; then the nearest, then the one whose start
         * is closest. */
        if (across)
            continue;
        long start = dx ? labs((long)to.y - from.y) : labs((long)to.x - from.x);
        long score = (long)along * 1000 + start;
        if (best < 0 || score < best_score) {
            best = order[i];
            best_score = score;
        }
    }
    return best;
}

bool conversation_focus_toward(view *ui, int dx, int dy)
{
    int target = conversation_focus_target(ui, dx, dy);
    if (target < 0)
        return false;
    conversation_set_focus(ui, target);
    return true;
}
