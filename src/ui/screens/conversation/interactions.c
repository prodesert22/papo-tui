#include "ui/screens/conversation/private.h"

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
    ui->context_open = ui->context_keyboard = ui->reactors_open = false;
    history_reactors_close(ui->model);
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
        ui->navigating = ui->reaction_picker = false;
        ui->selection_pending = 0;
        ui->selected_message[0] = 0;
        ui->hit_count = 0;
        snprintf(ui->action_channel, sizeof(ui->action_channel), "%s", channel);
        ui->action_generation = generation;
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
        snprintf(ui->selected_message, sizeof(ui->selected_message), "%s", target);
        ui->navigating = true;
        ui->return_focus = HISTORY_PANEL;
        conversation_set_focus(ui, HISTORY_PANEL);
        snprintf(ui->anchor, sizeof(ui->anchor), "%s", target);
        ui->block[0] = 0;
        ui->offset = 0;
        ui->at_end = false;
    }
}
static void open_context(view *ui, const MEVENT *event)
{
    conversation_interactions_close(ui);
    if (selected_index(ui) == SIZE_MAX)
        return;
    ui->context_open = true;
    ui->menu_selected = 0;
    ui->context_keyboard = !event;
    if (event) {
        ui->menu_y = event->y;
        ui->menu_x = event->x;
    }
    mention_popup_close(ui->mentions);
    emoji_picker_dismiss(ui->emojis);
    ui->dragging = false;
}
static int menu_count(view *ui)
{
    return *conversation_str(history_find_message(ui->model, ui->selected_message), "reply_to") ? 4
                                                                                                : 3;
}
static void message_action(view *ui, int action)
{
    json_t *m = history_find_message(ui->model, ui->selected_message);
    char original[37];
    snprintf(original, sizeof(original), "%s", conversation_str(m, "reply_to"));
    conversation_interactions_close(ui);
    if (action == 0)
        conversation_reactors_open(ui);
    else if (action == 1) {
        snprintf(ui->reaction_target, sizeof(ui->reaction_target), "%s", ui->selected_message);
        ui->reaction_picker = true;
        ui->emoji_return_focus = ui->focus;
        emoji_picker_reaction(ui->emojis, ui->action_channel, ui->action_generation);
    } else if (action == 2) {
        if (outbox_set_reply(send_editor_outbox(ui->editor), ui->editor_channel,
                             ui->selected_message)) {
            ui->navigating = false;
            conversation_set_focus(ui, MESSAGE_INPUT);
        }
    } else if (*original)
        history_jump(ui->model, original);
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
        if (type == OK && (key == 27 || (key == 'm' && ui->context_open))) {
            conversation_interactions_close(ui);
            return true;
        }
        if (ui->context_open) {
            if (type == KEY_CODE_YES && key == KEY_UP && ui->menu_selected)
                --ui->menu_selected;
            if (type == KEY_CODE_YES && key == KEY_DOWN && ui->menu_selected + 1 < menu_count(ui))
                ++ui->menu_selected;
            if (enter)
                message_action(ui, ui->menu_selected);
        } else
            conversation_reactors_key(ui, type, key);
        return true;
    }
    if (type == KEY_CODE_YES && key == KEY_F(7)) {
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
                message_action(ui, ui->menu_selected);
            } else if ((left || right) && !inside)
                conversation_interactions_close(ui);
        }
        return true;
    }
    if (!left && !right)
        return false;
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
    int width = MIN(COLS - 2, ui->reactors_open ? 76 : 30);
    int height = ui->reactors_open ? MIN(LINES - 2, 20) : menu_count(ui) + 2;
    if (width < 8 || height < 4)
        return;
    int y = ui->reactors_open ? (LINES - height) / 2 : CLAMP(ui->menu_y, 0, LINES - height);
    int x = ui->reactors_open ? (COLS - width) / 2 : CLAMP(ui->menu_x, 0, COLS - width);
    if (ui->context_keyboard && ui->body) {
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
    if (ui->context_open) {
        const char *labels[] = {"Ver quem reagiu", "Adicionar reação", "Responder",
                                "Ir para mensagem original"};
        for (int i = 0; i < menu_count(ui); ++i) {
            wattrset(w, COLOR_PAIR(i == ui->menu_selected ? THEME_PANEL_ACTIVE : THEME_PANEL));
            text_label(w, i + 1, 1, width - 2, labels[i]);
        }
    } else
        conversation_reactors_draw(ui, w);
    wnoutrefresh(w);
}
