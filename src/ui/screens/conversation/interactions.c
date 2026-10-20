#include "ui/screens/conversation/private.h"

enum { SHOW_REACTORS, ADD_REACTION, REPLY, ORIGINAL, EDIT_MESSAGE, DELETE_MESSAGE, PIN_MESSAGE };
static bool action_allowed(view *ui, int action, const json_t *message);
static int menu_actions(view *ui, int items[7]);

const char *conversation_reply_target(view *ui)
{
    return *ui->editor_channel ? outbox_reply(send_editor_outbox(ui->editor), ui->editor_channel)
                               : "";
}
void conversation_reply_label(view *ui, const char *id, int width, char *label, size_t capacity)
{
    history_reference_request(ui->model, id);
    json_t *m = history_find_message(ui->model, id);
    char raw[35000];
    if (json_is_object(m)) {
        const char *content = conversation_str(m, "content");
        if (!*content) {
            json_t *attachment = json_array_get(json_object_get(m, "attachments"), 0);
            content = attachment ? conversation_str(attachment, "original_file_name") : "[embed]";
        }
        snprintf(raw, sizeof(raw), "%s: %s", history_message_author(ui->model, m), content);
    } else
        snprintf(raw, sizeof(raw), "%s",
                 json_is_false(m) ? "mensagem não disponível"
                                  : history_reference_notice(ui->model));
    text_buffer text, projected;
    const char *p = raw;
    if (!text_decode_chunk(&text, &p)) {
        snprintf(label, capacity, "mensagem não disponível");
        return;
    }
    for (size_t i = 0; i < text.length; ++i)
        if (text.data[i] == '\n' || text.data[i] == '\r' || text.data[i] == '\t')
            text.data[i] = ' ';
    text_mentions(&text, history_mention_name, ui->model);
    text_project(&text, &projected);
    text_layout measured;
    text_measure(&projected, width > 0 ? width : 1, &measured);
    for (size_t i = 0; i + 1 < measured.count; ++i)
        if (measured.cells[i].row) {
            size_t end = measured.cells[i].start;
            projected.length = end;
            if (end)
                projected.data[end - 1] = 0x2026;
            projected.data[end] = 0;
            break;
        }
    char encoded[TEXT_UTF8_BYTES];
    if (!text_encode(&projected, encoded))
        encoded[0] = 0;
    snprintf(label, capacity, "%s", encoded);
}
void conversation_message_hit(view *ui, const char *id, int kind, int y, int x, int height,
                              int width, size_t index)
{
    if (ui->hit_count == sizeof(ui->hits) / sizeof(ui->hits[0]) || height < 1 || width < 1)
        return;
    message_hit *hit = &ui->hits[ui->hit_count++];
    *hit = (message_hit){
        .kind = kind, .y = y, .x = x, .height = height, .width = width, .index = index};
    snprintf(hit->id, sizeof(hit->id), "%s", id);
}
void conversation_interactions_close(view *ui)
{
    if (ui->context_window) {
        delwin(ui->context_window);
        ui->context_window = NULL;
    }
    if (ui->context_open && !*ui->dm_context_id && !*ui->channel_context_id)
        history_discovery_close(ui->model);
    ui->context_open = ui->context_keyboard = ui->reactors_open = ui->delete_confirm = false;
    ui->dm_context_id[0] = ui->channel_context_id[0] = 0;
    ui->channel_menu = 0;
    history_reactors_close(ui->model);
}
static json_t *dm_channel(view *ui, const char *id)
{
    for (size_t i = 0; i < history_channel_count(ui->model); ++i) {
        json_t *channel = history_channel(ui->model, i);
        if (json_is_true(json_object_get(channel, "_dm")) &&
            !strcmp(conversation_str(channel, "id"), id))
            return channel;
    }
    return NULL;
}
void conversation_close_dm(view *ui, const char *id)
{
    if (!dm_channel(ui, id))
        return;
    outbox_operation operation;
    bool busy = outbox_latest(send_editor_outbox(ui->editor), id, &operation) &&
                (operation.state == OUTBOX_QUEUED || operation.state == OUTBOX_SENDING);
    if (busy)
        ui->feedback = "Aguarde o envio antes de fechar a DM.";
    else if (!inbox_close_dm(history_inbox(ui->model), id))
        ui->feedback = "Não foi possível fechar a DM; aguarde ou tente novamente.";
}
static void open_dm_context(view *ui, const MEVENT *event)
{
    json_t *channel = conversation_list_channel(ui, ui->highlighted);
    if (!ui->list_dms || !channel)
        return;
    conversation_interactions_close(ui);
    snprintf(ui->dm_context_id, sizeof(ui->dm_context_id), "%s", conversation_str(channel, "id"));
    ui->context_open = true;
    ui->context_keyboard = !event;
    ui->menu_selected = 0;
    if (event) {
        ui->menu_y = event->y;
        ui->menu_x = event->x;
    }
    ui->selected_message[0] = 0;
    ui->navigating = ui->dragging = false;
    mention_popup_close(ui->mentions);
    emoji_picker_dismiss(ui->emojis);
    conversation_set_focus(ui, CHANNEL_LIST);
}
static json_t *context_channel(view *ui)
{
    for (size_t i = 0; i < history_channel_count(ui->model); ++i) {
        json_t *channel = history_channel(ui->model, i);
        if (history_channel_visible(ui->model, i) &&
            !strcmp(conversation_str(channel, "id"), ui->channel_context_id) &&
            !json_is_true(json_object_get(channel, "_dm")) &&
            strcmp(conversation_str(channel, "type"), "category"))
            return channel;
    }
    return NULL;
}
static void open_channel_context(view *ui, const MEVENT *event)
{
    json_t *channel = conversation_list_channel(ui, ui->highlighted);
    if (ui->list_dms || !channel || !strcmp(conversation_str(channel, "type"), "category"))
        return;
    conversation_interactions_close(ui);
    snprintf(ui->channel_context_id, sizeof(ui->channel_context_id), "%s",
             conversation_str(channel, "id"));
    ui->context_open = true;
    ui->context_keyboard = !event;
    ui->menu_selected = ui->channel_menu = 0;
    ui->selected_message[0] = 0;
    ui->navigating = ui->dragging = false;
    mention_popup_close(ui->mentions);
    emoji_picker_dismiss(ui->emojis);
    conversation_set_focus(ui, CHANNEL_LIST);
}
static const char *const notification_modes[] = {"all", "only_mentions", "off"};
static void channel_menu_activate(view *ui)
{
    inbox *box = history_inbox(ui->model);
    if (!context_channel(ui)) {
        conversation_interactions_close(ui);
        return;
    }
    if (!ui->channel_menu) {
        if (!ui->menu_selected && inbox_conversation_muted(box, ui->channel_context_id)) {
            if (!inbox_mute_conversation(box, ui->channel_context_id, 0))
                ui->feedback = "Não foi possível salvar o silenciamento; arquivo preservado.";
            else
                ui->feedback = "Canal reativado.";
            conversation_interactions_close(ui);
            return;
        }
        ui->channel_menu = ui->menu_selected ? 2 : 1;
        ui->menu_selected = 0;
        if (ui->channel_menu == 2) {
            const char *current = history_channel_notifications(ui->model, ui->channel_context_id);
            for (int i = 0; i < 3; ++i)
                if (current && !strcmp(current, notification_modes[i]))
                    ui->menu_selected = i;
        }
        return;
    }
    if (ui->channel_menu == 1) {
        static const int durations[] = {600, 1800, 3600, 14400, 43200, 86400, -1};
        if (!inbox_mute_conversation(box, ui->channel_context_id, durations[ui->menu_selected]))
            ui->feedback = "Não foi possível salvar o silenciamento; arquivo preservado.";
        else
            ui->feedback = "Canal silenciado neste cliente.";
    } else {
        bool ok = history_set_channel_notifications(ui->model, ui->channel_context_id,
                                                    notification_modes[ui->menu_selected]);
        ui->feedback =
            ok ? NULL : "Aguarde a configuração pendente ou atualize os canais e tente novamente.";
    }
    conversation_interactions_close(ui);
}
static size_t selected_index(view *ui)
{
    for (size_t i = 0; i < history_message_count(ui->model); ++i)
        if (!strcmp(ui->selected_message, conversation_str(history_message(ui->model, i), "id")))
            return i;
    return SIZE_MAX;
}
static void select_message(view *ui, size_t index)
{
    ui->centered_message[0] = 0;
    json_t *m = history_message(ui->model, index);
    if (!m)
        return;
    snprintf(ui->selected_message, sizeof(ui->selected_message), "%s", conversation_str(m, "id"));
    ui->selected_media = 0;
}
static void reveal(view *ui)
{
    int ignored;
    conversation_walk(ui, 0, false, false, &ignored);
    int first = conversation_position(ui);
    int goal = first;
    if (ui->selected_start < first)
        goal = ui->selected_start;
    else if (ui->selected_end > first + ui->visible)
        goal = ui->selected_end - ui->selected_start > ui->visible ? ui->selected_start
                                                                   : ui->selected_end - ui->visible;
    if (goal != first)
        conversation_scroll_history(ui, goal - first, false);
}
void conversation_interactions_sync(view *ui)
{
    const char *channel =
        conversation_str(history_channel(ui->model, history_selected(ui->model)), "id");
    uint64_t generation = auth_context_generation(ui->connections->items[ui->active_tab].auth);
    if (strcmp(ui->action_channel, channel) || ui->action_generation != generation) {
        conversation_interactions_close(ui);
        conversation_cancel_edit(ui);
        ui->navigating = ui->reaction_picker = false;
        ui->selection_pending = 0;
        ui->selected_message[0] = 0;
        ui->centered_message[0] = 0;
        ui->hit_count = 0;
        snprintf(ui->action_channel, sizeof(ui->action_channel), "%s", channel);
        ui->action_generation = generation;
    }
    json_t *selected_message = history_find_message(ui->model, ui->selected_message);
    if (ui->delete_confirm && !action_allowed(ui, DELETE_MESSAGE, selected_message)) {
        conversation_interactions_close(ui);
        ui->feedback = "Permissão de exclusão removida; operação cancelada.";
    }
    if (ui->reaction_picker && !action_allowed(ui, ADD_REACTION, selected_message)) {
        emoji_picker_close(ui->emojis);
        ui->reaction_picker = false;
        ui->feedback = "Permissão para reagir indisponível.";
    }
    int available[7];
    if (*ui->dm_context_id && (!ui->list_dms || !dm_channel(ui, ui->dm_context_id)))
        conversation_interactions_close(ui);
    if (*ui->channel_context_id && (ui->list_dms || !context_channel(ui)))
        conversation_interactions_close(ui);
    if (ui->context_open && !*ui->dm_context_id && !*ui->channel_context_id &&
        !ui->delete_confirm && !menu_actions(ui, available)) {
        conversation_interactions_close(ui);
        ui->feedback = "Permissões ainda não confirmadas. Aguarde ou use Ctrl+R.";
    }
    char edited[37];
    bool success;
    if (history_take_edit_result(ui->model, edited, &success) && ui->edit_pending &&
        !strcmp(edited, ui->edit_message)) {
        ui->edit_pending = false;
        ui->feedback = NULL;
        char content[TEXT_UTF8_BYTES];
        if (success && text_encode(&ui->edit_input, content) &&
            !strcmp(content, ui->edit_submitted))
            conversation_cancel_edit(ui);
    }
    if (*ui->edit_message && json_is_false(history_find_message(ui->model, ui->edit_message))) {
        conversation_cancel_edit(ui);
        ui->feedback = "Mensagem não disponível; edição cancelada.";
    }
    if (ui->reactors_open && history_recovering(ui->model))
        conversation_interactions_close(ui);
    if (*ui->selected_message && selected_index(ui) == SIZE_MAX) {
        conversation_interactions_close(ui);
        if (history_message_count(ui->model))
            select_message(ui, history_message_count(ui->model) - 1);
        else
            ui->selected_message[0] = 0;
    }
    size_t selected = selected_index(ui);
    if (ui->selection_pending && selected != SIZE_MAX) {
        if (ui->selection_pending < 0 && selected > 0) {
            select_message(ui, selected - 1);
            ui->selection_pending = 0;
            reveal(ui);
        } else if (ui->selection_pending > 0 && selected + 1 < history_message_count(ui->model)) {
            select_message(ui, selected + 1);
            ui->selection_pending = 0;
            reveal(ui);
        }
    }
    char target[37];
    if (history_take_jump(ui->model, target)) {
        r8_modal_location_complete(ui->r8, target);
        snprintf(ui->selected_message, sizeof(ui->selected_message), "%s", target);
        ui->navigating = true;
        ui->return_focus = HISTORY_PANEL;
        conversation_set_focus(ui, HISTORY_PANEL);
        snprintf(ui->anchor, sizeof(ui->anchor), "%s", target);
        ui->block[0] = 0;
        ui->offset = 0;
        ui->at_end = false;
        ui->selected_media = 0;
        snprintf(ui->centered_message, sizeof(ui->centered_message), "%s", target);
    }
}
static void open_context(view *ui, const MEVENT *event)
{
    conversation_interactions_close(ui);
    if (selected_index(ui) == SIZE_MAX)
        return;
    int available[7];
    if (!menu_actions(ui, available)) {
        ui->feedback = "Permissões ainda não confirmadas. Aguarde ou use Ctrl+R.";
        return;
    }
    ui->context_open = true;
    history_pins_open(ui->model);
    ui->menu_selected = 0;
    ui->menu_cached_count = 0;
    ui->menu_permissions = 0;
    ui->context_keyboard = !event;
    if (event) {
        ui->menu_y = event->y;
        ui->menu_x = event->x;
    }
    mention_popup_close(ui->mentions);
    emoji_picker_dismiss(ui->emojis);
    ui->dragging = false;
}
static bool message_is_own(view *ui, const json_t *m)
{
    return permissions_message(auth_permissions(ui->connections->items[ui->active_tab].auth), m,
                               true) == PERMISSION_ALLOWED;
}
static bool begin_edit(view *ui, json_t *m)
{
    if (!message_is_own(ui, m) || !conversation_input(ui))
        return false;
    text_buffer next;
    if (!text_decode(&next, conversation_str(m, "content"))) {
        ui->feedback = "Mensagem excede o limite do editor; edição não iniciada.";
        return false;
    }
    ui->edit_input = next;
    snprintf(ui->edit_message, sizeof(ui->edit_message), "%s", conversation_str(m, "id"));
    ui->edit_pending = false;
    ui->edit_submitted[0] = 0;
    ui->navigating = ui->dragging = ui->editor_scrolling = false;
    ui->feedback = NULL;
    mention_popup_close(ui->mentions);
    emoji_picker_dismiss(ui->emojis);
    conversation_set_focus(ui, MESSAGE_INPUT);
    snprintf(ui->selected_message, sizeof(ui->selected_message), "%s", ui->edit_message);
    ui->navigating = true;
    reveal(ui);
    ui->navigating = false;
    return true;
}
static bool action_allowed(view *ui, int action, const json_t *message)
{
    if (!json_is_object(message))
        return false;
    permissions *p = auth_permissions(ui->connections->items[ui->active_tab].auth);
    if (action == EDIT_MESSAGE || action == DELETE_MESSAGE)
        return permissions_message(p, message, action == EDIT_MESSAGE) == PERMISSION_ALLOWED;
    if (action == PIN_MESSAGE && permissions_role(p, PERMISSION_PIN_MESSAGE) != PERMISSION_ALLOWED)
        return false;
    return history_permission(ui->model, action == ADD_REACTION || action == REPLY
                                             ? PERMISSION_SEND_MESSAGES
                                             : PERMISSION_READ_CHANNEL) == PERMISSION_ALLOWED;
}
static int menu_actions(view *ui, int items[7])
{
    json_t *m = history_find_message(ui->model, ui->selected_message);
    int count = 0;
    for (int action = SHOW_REACTORS; action <= PIN_MESSAGE; ++action)
        if (action_allowed(ui, action, m) &&
            (action != ORIGINAL || *conversation_str(m, "reply_to")))
            items[count++] = action;
    uint64_t revision =
        permissions_revision(auth_permissions(ui->connections->items[ui->active_tab].auth));
    if (ui->menu_permissions != revision) {
        int selected =
            ui->menu_selected < ui->menu_cached_count ? ui->menu_cached[ui->menu_selected] : -1;
        ui->menu_selected = 0;
        for (int i = 0; i < count; ++i)
            if (items[i] == selected)
                ui->menu_selected = i;
        ui->menu_permissions = revision;
    }
    memcpy(ui->menu_cached, items, (size_t)count * sizeof(*items));
    ui->menu_cached_count = count;
    return count;
}
static int menu_count(view *ui)
{
    int items[7];
    return *ui->channel_context_id ? ui->channel_menu == 1   ? 7
                                     : ui->channel_menu == 2 ? 3
                                                             : 2
           : *ui->dm_context_id    ? 1
           : ui->delete_confirm    ? 2
                                   : menu_actions(ui, items);
}
static void message_action(view *ui, int action)
{
    json_t *m = history_find_message(ui->model, ui->selected_message);
    if (!action_allowed(ui, action, m)) {
        ui->feedback = "Permissão indisponível para esta ação.";
        return;
    }
    char original[37];
    snprintf(original, sizeof(original), "%s", conversation_str(m, "reply_to"));
    int pinned = history_pin_state(ui->model, ui->selected_message);
    if (action == PIN_MESSAGE && pinned < 0)
        return;
    bool keyboard = ui->context_keyboard;
    conversation_interactions_close(ui);
    if (action == PIN_MESSAGE) {
        if (!history_pin_message(ui->model, ui->selected_message, pinned == 1))
            ui->feedback = "Não foi possível preparar a fixação; atualize e tente novamente.";
    } else if (action == DELETE_MESSAGE) {
        ui->context_open = ui->delete_confirm = true;
        ui->context_keyboard = keyboard;
        ui->menu_selected = 0;
    } else if (action == EDIT_MESSAGE)
        begin_edit(ui, m);
    else if (action == SHOW_REACTORS)
        conversation_reactors_open(ui);
    else if (action == ADD_REACTION) {
        snprintf(ui->reaction_target, sizeof(ui->reaction_target), "%s", ui->selected_message);
        ui->reaction_picker = true;
        ui->emoji_return_focus = ui->focus;
        emoji_picker_reaction(ui->emojis, ui->action_channel, ui->action_generation);
    } else if (action == REPLY) {
        conversation_cancel_edit(ui);
        if (outbox_set_reply(send_editor_outbox(ui->editor), ui->editor_channel,
                             ui->selected_message)) {
            ui->navigating = false;
            conversation_set_focus(ui, MESSAGE_INPUT);
        }
    } else if (action == ORIGINAL && *original)
        history_jump(ui->model, original);
}
static void menu_activate(view *ui)
{
    if (*ui->channel_context_id) {
        channel_menu_activate(ui);
        return;
    }
    if (*ui->dm_context_id) {
        char id[37];
        snprintf(id, sizeof(id), "%s", ui->dm_context_id);
        conversation_interactions_close(ui);
        conversation_close_dm(ui, id);
        return;
    }
    if (ui->delete_confirm) {
        bool remove = ui->menu_selected == 1;
        conversation_interactions_close(ui);
        if (remove) {
            if (!history_delete_message(ui->model, ui->selected_message))
                ui->feedback = "Não foi possível preparar a exclusão; aguarde ou tente novamente.";
            else if (!strcmp(ui->edit_message, ui->selected_message))
                conversation_cancel_edit(ui);
        }
        return;
    }
    int items[7];
    int count = menu_actions(ui, items);
    if (ui->menu_selected >= 0 && ui->menu_selected < count)
        message_action(ui, items[ui->menu_selected]);
}
bool conversation_accept_reaction(view *ui)
{
    json_t *choice = emoji_picker_take_reaction(ui->emojis);
    if (!choice)
        return false;
    if (!history_react(ui->model, ui->reaction_target, choice, false))
        ui->feedback = "Não foi possível preparar a reação; aguarde ou tente novamente.";
    json_decref(choice);
    ui->reaction_picker = false;
    conversation_set_focus(ui, ui->emoji_return_focus);
    return true;
}
bool conversation_interactions_key(view *ui, int type, uint32_t key)
{
    bool enter =
        (type == OK && (key == '\n' || key == '\r')) || (type == KEY_CODE_YES && key == KEY_ENTER);
    if (ui->context_open || ui->reactors_open) {
        if (type == KEY_CODE_YES && key == KEY_MOUSE)
            return false;
        if (*ui->channel_context_id && ui->channel_menu &&
            ((type == OK && key == 27) || (type == KEY_CODE_YES && key == KEY_LEFT))) {
            ui->menu_selected = ui->channel_menu == 2 ? 1 : 0;
            ui->channel_menu = 0;
            return true;
        }
        if (type == OK && (key == 27 || ((key == 'm' || key == 'M') && ui->context_open))) {
            conversation_interactions_close(ui);
            return true;
        }
        if (ui->context_open) {
            if (type == OK && key == 18) {
                if (*ui->channel_context_id)
                    history_reload_channels(ui->model);
                else if (*ui->dm_context_id)
                    inbox_refresh(history_inbox(ui->model));
                else
                    history_pins_refresh(ui->model);
            }
            if (type == KEY_CODE_YES && key == KEY_UP && ui->menu_selected)
                --ui->menu_selected;
            if (type == KEY_CODE_YES && key == KEY_DOWN && ui->menu_selected + 1 < menu_count(ui))
                ++ui->menu_selected;
            if (enter || (*ui->channel_context_id && !ui->channel_menu && type == KEY_CODE_YES &&
                          key == KEY_RIGHT))
                menu_activate(ui);
        } else
            conversation_reactors_key(ui, type, key);
        return true;
    }
    if (ui->focus == CHANNEL_LIST && type == OK && (key == 'm' || key == 'M')) {
        if (ui->list_dms)
            open_dm_context(ui, NULL);
        else
            open_channel_context(ui, NULL);
        return true;
    }
    if (type == OK && key == 27 && *ui->edit_message) {
        conversation_cancel_edit(ui);
        ui->navigating = false;
        conversation_set_focus(ui, MESSAGE_INPUT);
        return true;
    }
    text_buffer *input = conversation_input(ui);
    if (!ui->navigating && ui->focus == MESSAGE_INPUT && input && !input->length &&
        !*ui->edit_message && type == KEY_CODE_YES && key == KEY_UP) {
        for (size_t i = history_message_count(ui->model); i > 0; --i) {
            json_t *m = history_message(ui->model, i - 1);
            if (message_is_own(ui, m)) {
                begin_edit(ui, m);
                return true;
            }
        }
        ui->feedback = "Nenhuma mensagem própria no histórico carregado para editar.";
        return true;
    }
    if (type == KEY_CODE_YES && key == KEY_F(7)) {
        ui->centered_message[0] = 0;
        if (!ui->body)
            return true;
        if (ui->navigating) {
            ui->navigating = false;
            conversation_set_focus(ui, ui->return_focus);
        } else {
            ui->return_focus = ui->focus;
            conversation_set_focus(ui, HISTORY_PANEL);
            ui->navigating = true;
            for (size_t i = ui->hit_count; i > 0; --i)
                if (ui->hits[i - 1].kind == MESSAGE_HIT) {
                    snprintf(ui->selected_message, sizeof(ui->selected_message), "%s",
                             ui->hits[i - 1].id);
                    break;
                }
            if (selected_index(ui) == SIZE_MAX && history_message_count(ui->model))
                select_message(ui, history_message_count(ui->model) - 1);
            ui->selected_media = 0;
            reveal(ui);
        }
        return true;
    }
    if (!ui->navigating && type == OK && key == 27 &&
        (ui->focus == MESSAGE_INPUT || ui->focus == EMOJI_BUTTON) &&
        *conversation_reply_target(ui)) {
        outbox_set_reply(send_editor_outbox(ui->editor), ui->editor_channel, NULL);
        conversation_set_focus(ui, MESSAGE_INPUT);
        return true;
    }
    if (!ui->navigating)
        return false;
    if (type == OK && key == 27) {
        ui->centered_message[0] = 0;
        ui->navigating = false;
        conversation_set_focus(ui, ui->return_focus);
        return true;
    }
    if (type == OK && (key == 'r' || key == 'R' || key == 'e' || key == 'E')) {
        if (selected_index(ui) != SIZE_MAX)
            message_action(ui, key == 'r' || key == 'R' ? 2 : key == 'e' ? 1 : 0);
        return true;
    }
    size_t index = selected_index(ui), count = history_message_count(ui->model);
    if (type == KEY_CODE_YES && (key == KEY_UP || key == KEY_DOWN)) {
        if (key == KEY_UP && index && index != SIZE_MAX)
            select_message(ui, index - 1);
        else if (key == KEY_DOWN && index + 1 < count)
            select_message(ui, index + 1);
        else if (key == KEY_UP && history_has_more(ui->model)) {
            ui->selection_pending = -1;
            history_older(ui->model);
        } else if (key == KEY_DOWN && history_has_newer(ui->model)) {
            ui->selection_pending = 1;
            history_newer(ui->model);
        }
        reveal(ui);
        return true;
    }
    if (type == OK && key == 'm') {
        reveal(ui);
        open_context(ui, NULL);
        return true;
    }
    if (index != SIZE_MAX && ui->body &&
        (enter || (type == KEY_CODE_YES && (key == KEY_LEFT || key == KEY_RIGHT)))) {
        int ignored;
        conversation_walk(ui, 0, false, false, &ignored);
        cached_layout *c = conversation_find_layout(ui, index, getmaxx(ui->body) - 4);
        size_t media_count = c->blocks.media_count;
        if (media_count) {
            if (ui->selected_media >= media_count)
                ui->selected_media = 0;
            if (type == KEY_CODE_YES && key == KEY_LEFT)
                ui->selected_media = (ui->selected_media + media_count - 1) % media_count;
            if (type == KEY_CODE_YES && key == KEY_RIGHT)
                ui->selected_media = (ui->selected_media + 1) % media_count;
            if (enter)
                media_view_open(ui->media, &c->blocks.media[ui->selected_media]);
        }
        return true;
    }
    if ((type == OK && key >= 32 && iswprint((wint_t)key)) || (type == OK && key == '\t') ||
        (type == KEY_CODE_YES && key == KEY_BTAB))
        ui->navigating = false;
    return false;
}
bool conversation_interactions_mouse(view *ui, const MEVENT *event)
{
    bool right = (event->bstate & (BUTTON3_PRESSED | BUTTON3_CLICKED)) != 0;
    bool left = (event->bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED)) != 0;
    if (ui->context_open || ui->reactors_open) {
        if (ui->context_window) {
            int y = event->y - getbegy(ui->context_window),
                x = event->x - getbegx(ui->context_window);
            bool inside = y >= 0 && x >= 0 && y < getmaxy(ui->context_window) &&
                          x < getmaxx(ui->context_window);
            if (inside && ui->reactors_open)
                conversation_reactors_mouse(ui, event);
            else if (left && inside && ui->context_open && y >= 1 && y <= menu_count(ui)) {
                ui->menu_selected = y - 1;
                menu_activate(ui);
            } else if ((left || right) && !inside)
                conversation_interactions_close(ui);
        }
        return true;
    }
    if (!left && !right)
        return false;
    if (right && ui->list) {
        int y = event->y, x = event->x;
        if (wmouse_trafo(ui->list, &y, &x, false) && y >= 1 && y < getmaxy(ui->list) - 3) {
            size_t row = (size_t)(ui->first_channel + y - 1);
            if (conversation_list_selectable(ui, row)) {
                ui->highlighted = row;
                if (ui->list_dms)
                    open_dm_context(ui, event);
                else
                    open_channel_context(ui, event);
            }
            return true;
        }
    }
    if (left && ui->body && *ui->edit_message &&
        event->y == getbegy(ui->body) + ui->editor_border) {
        if (event->x >= getbegx(ui->body) + getmaxx(ui->body) - 5)
            conversation_cancel_edit(ui);
        return true;
    }
    if (left && ui->body && *conversation_reply_target(ui) &&
        event->y == getbegy(ui->body) + ui->editor_border) {
        if (event->x >= getbegx(ui->body) + getmaxx(ui->body) - 5)
            outbox_set_reply(send_editor_outbox(ui->editor), ui->editor_channel, NULL);
        else
            history_jump(ui->model, conversation_reply_target(ui));
        return true;
    }
    for (size_t i = ui->hit_count; i > 0; --i) {
        const message_hit *hit = &ui->hits[i - 1];
        if (event->y < hit->y || event->y >= hit->y + hit->height || event->x < hit->x ||
            event->x >= hit->x + hit->width)
            continue;
        if (right) {
            ui->centered_message[0] = 0;
            snprintf(ui->selected_message, sizeof(ui->selected_message), "%s", hit->id);
            ui->selected_media = 0;
            open_context(ui, event);
            return true;
        }
        json_t *m = history_find_message(ui->model, hit->id);
        if (hit->kind == REPLY_HIT) {
            history_jump(ui->model, conversation_str(m, "reply_to"));
            return true;
        }
        if (hit->kind == REACTION_HIT) {
            json_t *emoji = json_array_get(json_object_get(m, "reactions"), hit->index);
            if (!history_react(ui->model, hit->id, emoji, history_reaction_mine(m, emoji)))
                ui->feedback = "Aguarde a reação pendente antes de tentar novamente.";
            return true;
        }
        if (ui->navigating) {
            ui->centered_message[0] = 0;
            snprintf(ui->selected_message, sizeof(ui->selected_message), "%s", hit->id);
            ui->selected_media = 0;
        }
        return false;
    }
    return false;
}
void conversation_interactions_draw(view *ui)
{
    if (!ui->context_open && !ui->reactors_open)
        return;
    int width = MIN(COLS - 2, ui->reactors_open ? 76 : *ui->channel_context_id ? 34 : 30);
    int height = ui->reactors_open ? MIN(LINES - 2, 20) : menu_count(ui) + 2;
    if (width < 8 || height < (ui->reactors_open ? 4 : 3))
        return;
    int y = ui->reactors_open ? (LINES - height) / 2 : CLAMP(ui->menu_y, 0, LINES - height);
    int x = ui->reactors_open ? (COLS - width) / 2 : CLAMP(ui->menu_x, 0, COLS - width);
    if ((*ui->dm_context_id || *ui->channel_context_id) && ui->list) {
        int row = getbegy(ui->list) + 1 + (int)ui->highlighted - ui->first_channel;
        int left = getbegx(ui->list) - width - 1;
        int right = getbegx(ui->list) + getmaxx(ui->list) + 1;
        y = CLAMP(row, 0, LINES - height);
        if (left >= 0)
            x = left;
        else if (right + width <= COLS)
            x = right;
        else {
            x = 0;
            y = row + 1 + height <= LINES ? row + 1 : MAX(0, row - height);
        }
    } else if (ui->context_keyboard && ui->body) {
        int top = getbegy(ui->body) + ui->history_top;
        y = CLAMP(top + ui->selected_start - ui->first + 1, top,
                  MAX(top, top + ui->visible - height));
        x = getbegx(ui->body) + getmaxx(ui->body) - width - 2;
        y = CLAMP(y, 0, LINES - height);
        x = CLAMP(x, 0, COLS - width);
    }
    if (ui->context_window)
        delwin(ui->context_window);
    ui->context_window = newwin(height, width, y, x);
    if (!ui->context_window)
        return;
    WINDOW *w = ui->context_window;
    icons_overlay(ui->icons, y, x, height, width);
    wbkgd(w, COLOR_PAIR(THEME_PANEL));
    box(w, 0, 0);
    if (*ui->channel_context_id) {
        const char *mute_labels[] = {"10 minutos", "30 minutos", "1 hora",         "4 horas",
                                     "12 horas",   "24 horas",   "Até eu reativar"};
        const char *notify_labels[] = {"Todas as mensagens", "Apenas menções", "Nada"};
        const char *current = history_channel_notifications(ui->model, ui->channel_context_id);
        wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
        text_label(w, 0, 2, width - 4,
                   ui->channel_menu == 1   ? "Silenciar canal"
                   : ui->channel_menu == 2 ? "Notificações do canal"
                                           : "Canal");
        for (int i = 0; i < menu_count(ui); ++i) {
            wattrset(w, COLOR_PAIR(i == ui->menu_selected ? THEME_PANEL_ACTIVE : THEME_PANEL));
            mvwhline(w, i + 1, 1, ' ', width - 2);
            char label[96];
            if (ui->channel_menu == 1)
                snprintf(label, sizeof(label), "%s", mute_labels[i]);
            else if (ui->channel_menu == 2)
                snprintf(label, sizeof(label), "[%c] %s",
                         current && !strcmp(current, notification_modes[i]) ? 'x' : ' ',
                         notify_labels[i]);
            else
                snprintf(
                    label, sizeof(label), "%s",
                    i ? "Notificações…"
                    : inbox_conversation_muted(history_inbox(ui->model), ui->channel_context_id)
                        ? "Reativar canal"
                        : "Silenciar canal…");
            text_label(w, i + 1, 1, width - 2, label);
        }
    } else if (*ui->dm_context_id) {
        wattrset(w, COLOR_PAIR(THEME_PANEL_ACTIVE));
        text_label(w, 1, 1, width - 2, "Fechar DM");
    } else if (ui->context_open) {
        const char *labels[] = {
            "Ver quem reagiu",
            "Adicionar reação",
            "Responder",
            "Ir para mensagem original",
            "Editar mensagem",
            "Excluir mensagem",
            history_pin_state(ui->model, ui->selected_message) < 0 ? "Fixação indisponível (Ctrl+R)"
            : history_pin_state(ui->model, ui->selected_message)   ? "Desafixar mensagem"
                                                                   : "Fixar mensagem"};
        int items[7];
        menu_actions(ui, items);
        if (ui->delete_confirm)
            text_label(w, 0, 2, width - 4, "Excluir mensagem?");
        for (int i = 0; i < menu_count(ui); ++i) {
            bool destructive = ui->delete_confirm && i == 1;
            bool selected = i == ui->menu_selected;
            int pair = destructive && selected ? THEME_DESTRUCTIVE
                       : selected              ? THEME_PANEL_ACTIVE
                                               : THEME_PANEL;
            wattrset(w, COLOR_PAIR(pair) | (destructive && selected ? A_BOLD : 0));
            if (destructive)
                mvwhline(w, i + 1, 1, ' ', width - 2);
            text_label(w, i + 1, 1, width - 2,
                       ui->delete_confirm ? i ? "Excluir mensagem" : "Cancelar" : labels[items[i]]);
        }
    } else
        conversation_reactors_draw(ui, w);
    wnoutrefresh(w);
}
