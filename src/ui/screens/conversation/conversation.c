#include "ui/screens/conversation/private.h"

help_context conversation_help_context(view *ui, const char **area)
{
    if (file_picker_open(ui->files)) {
        *area = "Seleção de arquivos";
        return HELP_FILES;
    }
    if (ui->remove_confirm || ui->duplicate_modal || ui->delete_confirm) {
        *area = "Confirmação";
        return HELP_CONFIRM;
    }
    if (ui->settings.open) {
        *area = "Configurações";
        return HELP_SETTINGS;
    }
    if (emoji_picker_open(ui->emojis)) {
        *area = "Seletor de emojis";
        return HELP_EMOJIS;
    }
    if (ui->context_open) {
        *area = "Menu de contexto";
        return HELP_MENU;
    }
    if (ui->reactors_open) {
        *area = "Reações e pessoas";
        return HELP_REACTIONS;
    }
    if (r8_modal_is_open(ui->r8)) {
        bool notifications = r8_modal_type(ui->r8) == R8_NOTIFICATIONS;
        *area = notifications ? "Central de notificações" : "Busca e mensagens fixadas";
        return notifications ? HELP_NOTIFICATIONS : HELP_SEARCH;
    }
    if (media_view_modal(ui->media)) {
        media_context context = media_view_context(ui->media);
        if (context == MEDIA_CONTEXT_LIST) {
            *area = "Lista de mídias";
            return HELP_MEDIA_LIST;
        }
        *area = context == MEDIA_CONTEXT_VIDEO ? "Player de vídeo" : "Visualizador de mídia";
        return HELP_MEDIA | (context == MEDIA_CONTEXT_VIDEO   ? HELP_PLAYER
                             : context == MEDIA_CONTEXT_IMAGE ? HELP_IMAGE
                             : context == MEDIA_CONTEXT_ERROR ? HELP_MEDIA_ERROR
                                                              : 0);
    }
    if (conversation_members_modal(ui))
        return conversation_members_help(ui, area);
    if (ui->focus == MESSAGE_INPUT) {
        *area = "Editor de mensagens";
        return HELP_EDITOR;
    }
    if (ui->focus == HISTORY_PANEL) {
        *area = ui->navigating ? "Seleção de mensagens" : "Histórico da conversa";
        return ui->navigating ? HELP_SELECTION : HELP_HISTORY;
    }
    if (ui->focus == CHANNEL_LIST) {
        *area = ui->list_dms ? "Lista de DMs" : "Lista de canais";
        return HELP_CHANNELS | (ui->list_dms ? HELP_DM_LIST : 0);
    }
    if (ui->focus == MEMBERS_PANEL) {
        bool editing = conversation_members_input(ui) != NULL;
        *area = editing ? "Busca de membros" : "Lista de membros";
        return editing ? HELP_FIELDS | HELP_MEMBER_SEARCH : HELP_MEMBERS;
    }
    if (ui->focus == SERVER_TABS) {
        *area = "Servidores conectados";
        return HELP_SERVER_TABS;
    }
    if (ui->focus == ATTACH_BUTTON || ui->focus == EMOJI_BUTTON || ui->focus == ATTACHMENT_ROW) {
        *area = "Anexos e botões do editor";
        return HELP_EDITOR_BUTTONS;
    }
    if (ui->focus == SETTINGS_BUTTON) {
        *area = "Botão Configurações";
        return HELP_SETTINGS_BUTTON;
    }
    *area = "Botões de navegação";
    return HELP_HEADER;
}

static void open_settings(view *ui, const profile_config *config)
{
    media_view_suspend(ui->media);
    mention_popup_close(ui->mentions);
    emoji_picker_close(ui->emojis);
    conversation_interactions_close(ui);
    ui->reaction_picker = false;
    r8_modal_close(ui->r8);
    settings_open(&ui->settings, config);
}

bool conversation_poll_editor(view *ui, auth_client *auth)
{
    const char *notice = send_editor_notice(ui->editor);
    text_buffer *before = conversation_input(ui);
    size_t length = before ? before->length : 0;
    send_editor_poll(ui->editor);
    bool changed = notice != send_editor_notice(ui->editor);
    text_buffer *after = conversation_input(ui);
    changed |= length != (after ? after->length : 0) || !!before != !!after;
    outbox_operation operation;
    json_t *message;
    while ((message = outbox_take_confirmed(send_editor_outbox(ui->editor), &operation))) {
        if (operation.generation == auth_context_generation(auth) &&
            !strcmp(operation.channel_id, ui->editor_channel)) {
            if (!history_confirm(ui->model, message, operation.generation))
                ui->feedback = "Envio confirmado; Ctrl+R para consultar o histórico.";
        }
        json_decref(message);
        outbox_reconciled(send_editor_outbox(ui->editor), operation.id);
        changed = true;
    }
    if (!auth_has_session(auth) && ui->duplicate_modal) {
        conversation_close_confirmation(ui);
        changed = true;
    }
    return changed;
}

bool conversation_poll_model(view *ui, auth_client *auth)
{
    char highlighted[37];
    snprintf(highlighted, sizeof(highlighted), "%s",
             conversation_str(conversation_list_channel(ui, ui->highlighted), "id"));
    /* Retain IDs around the reading anchor, never borrowed model pointers. */
    char neighbors[HISTORY_MESSAGE_LIMIT][37];
    size_t count = 0, anchored = 0;
    if (!ui->at_end)
        for (size_t i = 0; i < history_message_count(ui->model); ++i) {
            snprintf(neighbors[count], sizeof(neighbors[count]), "%s",
                     conversation_str(history_message(ui->model, i), "id"));
            if (!strcmp(neighbors[count], ui->anchor))
                anchored = count;
            ++count;
        }
    uint64_t inserted = history_insertions(ui->model);
    ws_state before = auth_ws_state(auth);
    auth_poll(auth);
    bool changed = conversation_poll_editor(ui, auth);
    changed |= history_poll(ui->model) || before != auth_ws_state(auth);
    uint64_t delta = history_insertions(ui->model) - inserted;
    if (!ui->at_end)
        ui->unseen += delta;
    for (size_t i = 0; i < conversation_list_count(ui); ++i)
        if (!strcmp(highlighted, conversation_str(conversation_list_channel(ui, i), "id")))
            ui->highlighted = i;
    conversation_list_normalize(ui);
    if (ui->initial_channel_pending && !ui->list_dms &&
        conversation_list_selectable(ui, ui->highlighted)) {
        ui->initial_channel_pending = false;
        int focus = ui->focus;
        bool show_channels = ui->show_channels;
        conversation_open_channel(ui, true);
        conversation_set_focus(ui, focus);
        if (ui->show_channels != show_channels) {
            ui->show_channels = show_channels;
            if (COLS < 80)
                conversation_layout(ui);
        }
        changed = true;
    }
    if (!ui->at_end && count) {
        bool found = false;
        for (size_t i = 0; i < history_message_count(ui->model); ++i)
            found |= !strcmp(ui->anchor, conversation_str(history_message(ui->model, i), "id"));
        if (!found) {
            for (size_t step = 0; step < count && !found; ++step) {
                size_t index = anchored + step < count ? anchored + step
                                                       : count - 1 - (anchored + step - count);
                for (size_t i = 0; i < history_message_count(ui->model); ++i)
                    if (!strcmp(neighbors[index],
                                conversation_str(history_message(ui->model, i), "id"))) {
                        snprintf(ui->anchor, sizeof(ui->anchor), "%s", neighbors[index]);
                        ui->section = ui->offset = 0;
                        found = true;
                        break;
                    }
            }
        }
    }
    return changed;
}

void conversation_dispose_media(void *v)
{
    media_view_destroy(v);
}

void conversation_update_media(void *v, const json_t *event)
{
    media_view_update(v, event);
}

void conversation_idle_media(void *v)
{
    media_view_idle(v);
}

int history_run(auth_client *auth, instance_connections *connections, size_t *selected,
                bool focus_tabs, volatile sig_atomic_t *interrupted)
{
    text_input_init();
    view *ui = calloc(1, sizeof(*ui));
    if (!ui)
        return HISTORY_ACCOUNT;
    ui->layout_index = g_hash_table_new(g_str_hash, g_str_equal);
    ui->connections = connections;
    ui->active_tab = ui->tab_cursor = instances_index(connections, auth);
    ui->editor = instances_editor(connections, auth);
    ui->files = file_picker_create();
    if (ui->active_tab < connections->count) {
        instance_connection *connection = &connections->items[ui->active_tab];
        if (!connection->media)
            connection->media = media_view_create(auth);
        connection->media_destroy = conversation_dispose_media;
        connection->media_idle = conversation_idle_media;
        connection->media_update = conversation_update_media;
        ui->media = connection->media;
    }
    ui->model = instances_history(connections, auth);
    if (ui->model)
        history_active(ui->model, true);
    ui->r8 = ui->model ? r8_modal_create(ui->model, auth, connections->config) : NULL;
    history_set_media_updates(ui->model, conversation_update_media, ui->media);
    ui->mentions = ui->model ? mention_popup_create(ui->model) : NULL;
    ui->icons =
        ui->model ? icons_create(history_assets(ui->model), media_view_cache(ui->media)) : NULL;
    ui->emojis = ui->icons ? emoji_picker_create(history_assets(ui->model), ui->icons,
                                                 auth_instance_url(auth), auth_user_id(auth))
                           : NULL;
    ui->at_end = ui->show_channels = true;
    ui->initial_channel_pending = true;
    ui->focus = focus_tabs ? SERVER_TABS : CHANNEL_LIST;
    if (!ui->model || !ui->r8 || !ui->mentions || !ui->icons || !ui->emojis || !ui->editor ||
        ui->active_tab == SIZE_MAX || !conversation_layout(ui)) {
        conversation_attachments_close(ui);
        file_picker_destroy(ui->files);
        conversation_interactions_close(ui);
        emoji_picker_destroy(ui->emojis);
        icons_destroy(ui->icons);
        mention_popup_destroy(ui->mentions);
        media_view_suspend(ui->media);
        if (ui->model)
            history_active(ui->model, false);
        help_destroy(&ui->help);
        r8_modal_destroy(ui->r8);
        conversation_destroy_layout(ui);
        g_hash_table_destroy(ui->layout_index);
        free(ui);
        return HISTORY_ACCOUNT;
    }
    mmask_t previous_mouse;
    /* A separately delivered release must reach getmouse(). If it is filtered
     * out, ncurses can wait for another gesture inside text_read_raw(), stalling HTTP
     * and media updates even with timeout(30) and mouseinterval(0). */
    mousemask(BUTTON1_PRESSED | BUTTON1_RELEASED | BUTTON1_CLICKED | BUTTON3_PRESSED |
                  BUTTON3_CLICKED | REPORT_MOUSE_POSITION | BUTTON4_PRESSED | BUTTON5_PRESSED,
              &previous_mouse);
    ui_mouse_drag_tracking(true);
    bool redraw = true;
    draw_scope scope = DRAW_ALL;
    int result = HISTORY_ACCOUNT;
    /* The player volume only reaches the profile once the player closes, so a
     * running player never writes the file for every key press. */
    int saved_volume = player_volume_get();
    while (!*interrupted) {
        if (ui->layout_failed)
            break;
        uint64_t visible_now = 0;
        net_now_ms(&visible_now);
        if (ui->help.open || !ui->body || !ui->at_end || r8_modal_is_open(ui->r8) ||
            ui->settings.open || ui->duplicate_modal || file_picker_open(ui->files) ||
            ui->remove_confirm || mention_popup_open(ui->mentions) ||
            emoji_picker_open(ui->emojis) || media_view_modal(ui->media) || ui->context_open ||
            ui->reactors_open || conversation_members_modal(ui) ||
            (ui->members_open && !ui->members_reserved) ||
            !(text_terminal_focused() ||
              (text_terminal_focus_unknown() && visible_now < ui->view_requested_until)))
            history_read_visible(ui->model, NULL);
        auth_state before = auth_get_state(auth);
        if (session_store_poll(connections->sessions, auth)) {
            redraw = true;
            scope = DRAW_ALL;
        }
        if (conversation_poll_model(ui, auth) || before != auth_get_state(auth)) {
            redraw = true;
            scope = DRAW_ALL;
        }
        if (conversation_members_poll(ui)) {
            redraw = true;
            scope = DRAW_ALL;
        }
        if (conversation_attachments_poll(ui, auth)) {
            redraw = true;
            scope = DRAW_ALL;
        }
        if (instances_poll(connections, auth)) {
            redraw = true;
            scope = DRAW_ALL;
        }
        uint64_t dimensions = media_view_dimensions_revision(ui->media);
        if (media_view_poll(ui->media, ui->model)) {
            bool reflow = dimensions != media_view_dimensions_revision(ui->media);
            if (!redraw)
                scope = reflow ? DRAW_CONVERSATION : DRAW_MEDIA;
            else if (scope == DRAW_NAVIGATION)
                scope = DRAW_ALL;
            else if (scope == DRAW_EDITOR)
                scope = DRAW_CONVERSATION;
            redraw = true;
        }
        if (!media_view_modal(ui->media) && connections->config) {
            int level = player_volume_get();
            if (level != saved_volume) {
                saved_volume = level;
                profiles_set_player_volume(connections->config, level);
            }
        }
        icons_assets(ui->icons, history_assets(ui->model), media_view_cache(ui->media),
                     auth_context_generation(auth));
        if (icons_poll(ui->icons)) {
            if (!redraw)
                scope = DRAW_MEDIA;
            else if (scope == DRAW_NAVIGATION)
                scope = DRAW_ALL;
            else if (scope == DRAW_EDITOR)
                scope = DRAW_CONVERSATION;
            redraw = true;
        }
        time_t clock = time(NULL);
        struct tm date;
        if (papo_localtime(&clock, &date) &&
            ui->calendar_day != date.tm_year * 366 + date.tm_yday) {
            ui->calendar_day = date.tm_year * 366 + date.tm_yday;
            redraw = true;
            scope = DRAW_ALL;
        }
        if (emoji_picker_update(ui->emojis, history_assets(ui->model),
                                conversation_input(ui) ? ui->editor_channel : "",
                                auth_context_generation(auth))) {
            redraw = true;
            scope = DRAW_ALL;
        }
        if (clipboard_poll())
            redraw = true;
        if (conversation_input(ui)) {
            text_mentions(conversation_input(ui), history_mention_name, ui->model);
            text_icons(conversation_input(ui), icons_resolve, icons_paint, ui->icons);
        }
        if (conversation_editor_geometry(ui)) {
            redraw = true;
            scope = DRAW_ALL;
        }
        if (!(r8_modal_is_open(ui->r8) || ui->settings.open) &&
            mention_popup_update(ui->mentions, conversation_input(ui),
                                 ui->focus == MESSAGE_INPUT && !ui->paste && !ui->duplicate_modal &&
                                     !(r8_modal_is_open(ui->r8) || ui->settings.open) &&
                                     !media_view_modal(ui->media) &&
                                     !emoji_picker_open(ui->emojis) && !ui->context_open &&
                                     !file_picker_open(ui->files) && !ui->remove_confirm &&
                                     !ui->reactors_open && ui->focus != MEMBERS_PANEL &&
                                     !conversation_members_modal(ui))) {
            redraw = true;
            scope = DRAW_ALL;
        }
        if (emoji_picker_suggest(ui->emojis, conversation_input(ui),
                                 ui->focus == MESSAGE_INPUT && !ui->paste && !ui->duplicate_modal &&
                                     !(r8_modal_is_open(ui->r8) || ui->settings.open) &&
                                     !media_view_modal(ui->media) &&
                                     !mention_popup_open(ui->mentions) && !ui->context_open &&
                                     !file_picker_open(ui->files) && !ui->remove_confirm &&
                                     !ui->reactors_open && ui->focus != MEMBERS_PANEL &&
                                     !conversation_members_modal(ui))) {
            redraw = true;
            scope = DRAW_ALL;
        }
        inbox *box = history_inbox(ui->model);
        if (*ui->notification_message && !r8_modal_locating(ui->r8)) {
            inbox_cancel_open(box);
            ui->notification_channel[0] = ui->notification_message[0] = ui->notification_time[0] =
                0;
        }
        if (ui->inbox_revision != inbox_revision(box)) {
            ui->inbox_revision = inbox_revision(box);
            redraw = true;
            scope = DRAW_ALL;
        }
        const char *opened = inbox_opened(box);
        if (*r8_modal_destination(ui->r8)) {
            snprintf(ui->notification_channel, sizeof(ui->notification_channel), "%s",
                     r8_modal_destination(ui->r8));
            snprintf(ui->notification_message, sizeof(ui->notification_message), "%s",
                     r8_modal_destination_message(ui->r8));
            snprintf(ui->notification_time, sizeof(ui->notification_time), "%s",
                     r8_modal_destination_time(ui->r8));
        }
        const char *destination = *opened ? opened : r8_modal_destination(ui->r8);
        if (*destination) {
            bool found = false;
            for (size_t i = 0; i < history_channel_count(ui->model); ++i) {
                json_t *c = history_channel(ui->model, i);
                if (strcmp(destination, conversation_str(c, "id")))
                    continue;
                conversation_switch_list(ui, json_is_true(json_object_get(c, "_dm")));
                for (size_t j = 0; j < conversation_list_count(ui); ++j)
                    if (conversation_list_index(ui, j) == i)
                        ui->highlighted = j;
                conversation_open_channel(ui, true);
                if (ui->members_open)
                    conversation_set_focus(ui, MESSAGE_INPUT);
                if (!strcmp(ui->notification_channel, destination) &&
                    !strcmp(destination,
                            conversation_str(
                                history_channel(ui->model, history_selected(ui->model)), "id"))) {
                    if (history_jump_at(ui->model, ui->notification_message, ui->notification_time))
                        r8_modal_location_started(ui->r8);
                    else
                        r8_modal_location_error(ui->r8, "Mensagem não disponível.");
                    ui->notification_channel[0] = ui->notification_message[0] = 0;
                }
                found = true;
                break;
            }
            if (!found)
                inbox_resolve_dm(box, destination);
            inbox_opened_clear(box);
            r8_modal_destination_clear(ui->r8);
            redraw = true;
        }
        if (*ui->notification_message && !inbox_opening(box)) {
            r8_modal_location_error(ui->r8, "Não foi possível abrir a conversa.");
            ui->notification_channel[0] = ui->notification_message[0] = 0;
        }
        conversation_interactions_sync(ui);
        if (r8_modal_poll(ui->r8)) {
            redraw = true;
            scope = DRAW_ALL;
        }
        instances_visible(
            connections, auth,
            ui->body && ui->at_end && !ui->help.open &&
                    !(r8_modal_is_open(ui->r8) || ui->settings.open) &&
                    !emoji_picker_open(ui->emojis) && !media_view_modal(ui->media) &&
                    !ui->context_open && !ui->reactors_open && !file_picker_open(ui->files) &&
                    !ui->remove_confirm
                ? conversation_str(history_channel(ui->model, history_selected(ui->model)), "id")
                : "");
        if (conversation_collect_notices(ui, auth)) {
            if (!redraw)
                scope = DRAW_FOOTER;
            redraw = true;
        }
        if (!auth_has_session(auth) && !auth_busy(auth))
            break;
        if (redraw) {
            conversation_draw(ui, auth, scope);
            redraw = false;
            scope = DRAW_ALL;
        }
        uint32_t key;
        int input_delay = media_view_input_delay(ui->media);
        timeout(input_delay);
        int type = ui->paste ? text_paste_read(&ui->paste_data, &key, true, graphics_terminal_input,
                                               input_delay)
                             : graphics_terminal_input(&key);
        if (type == ERR)
            continue;
        if (type == KEY_CODE_YES && (key == TEXT_FOCUS_IN || key == TEXT_FOCUS_OUT)) {
            ui->view_requested_until = 0;
            redraw = true;
            scope = DRAW_ALL;
            continue;
        }
        if (!(type == KEY_CODE_YES && (key == PASTE_START || key == PASTE_END ||
                                       key == KEY_RESIZE || key == TEXT_IGNORED)) &&
            !ui->paste) {
            uint64_t now;
            if (net_now_ms(&now))
                ui->view_requested_until = now + 2000;
            history_activity(ui->model);
        }
        if (type == KEY_CODE_YES && key == PASTE_START) {
            ui->paste = true;
            text_paste_begin(&ui->paste_data);
            ui->dragging = false;
            if (!ui->help.open && !file_picker_open(ui->files) && !ui->remove_confirm &&
                !ui->duplicate_modal && !(r8_modal_is_open(ui->r8) || ui->settings.open) &&
                !media_view_modal(ui->media) && !emoji_picker_open(ui->emojis) &&
                !ui->context_open && !ui->reactors_open && conversation_input(ui) &&
                ui->focus != SERVER_TABS) {
                ui->navigating = false;
                conversation_set_focus(ui, MESSAGE_INPUT);
                redraw = true;
            }
            continue;
        }
        if (type == KEY_CODE_YES && key == PASTE_END) {
            ui->paste = false;
            if (ui->help.open) {
                help_paste(&ui->help, &ui->paste_data);
                redraw = true;
                scope = DRAW_ALL;
                continue;
            }
            if (!ui->help.open && file_picker_paste(ui->files, &ui->paste_data)) {
                redraw = true;
                scope = DRAW_ALL;
                continue;
            }
            if (!ui->help.open && !file_picker_open(ui->files) && !ui->remove_confirm &&
                !ui->duplicate_modal && !(r8_modal_is_open(ui->r8) || ui->settings.open) &&
                !media_view_modal(ui->media) && !emoji_picker_open(ui->emojis) &&
                !ui->context_open && !ui->reactors_open && ui->focus == MESSAGE_INPUT &&
                conversation_input(ui)) {
                if (!text_paste_commit(&ui->paste_data, conversation_input(ui)))
                    ui->feedback = "Colagem excede o limite; rascunho preservado.";
                else {
                    ui->feedback = send_editor_changed(ui->editor)
                                       ? NULL
                                       : "Não foi possível salvar o rascunho.";
                    history_typing_activity(ui->model);
                    history_activity(ui->model);
                    uint64_t now;
                    if (net_now_ms(&now))
                        ui->view_requested_until = now + 2000;
                }
                ui->editor_scrolling = false;
                redraw = true;
                scope = DRAW_ALL;
            } else
                text_paste_cancel(&ui->paste_data);
            mention_popup_update(ui->mentions, conversation_input(ui), ui->focus == MESSAGE_INPUT);
            mention_popup_close(ui->mentions);
            emoji_picker_suggest(ui->emojis, conversation_input(ui), ui->focus == MESSAGE_INPUT);
            emoji_picker_dismiss(ui->emojis);
            continue;
        }
        if (type == KEY_CODE_YES && key == KEY_RESIZE) {
            conversation_layout(ui);
            redraw = true;
            continue;
        }
        if (ui->paste)
            continue;
        redraw = true;
        if (type == OK && key == 17) {
            result = HISTORY_EXIT;
            break;
        }
        if (help_key(&ui->help, type, key)) {
            scope = DRAW_ALL;
            continue;
        }
        if (shortcut_is(type, key, SHORTCUT_HELP) || shortcut_is(type, key, SHORTCUT_HELP_ALL)) {
            const char *area;
            help_context context = conversation_help_context(ui, &area);
            if (!help_open(&ui->help, context, area, shortcut_is(type, key, SHORTCUT_HELP_ALL)))
                ui->feedback = "Não foi possível abrir a ajuda.";
            scope = DRAW_ALL;
            continue;
        }
        if (file_picker_open(ui->files) || ui->remove_confirm) {
            if (type == KEY_CODE_YES && key == KEY_MOUSE) {
                MEVENT events[64];
                size_t count = ui_mouse_events(events, 64);
                for (size_t i = 0; i < count; ++i)
                    conversation_attachments_mouse(ui, &events[i]);
            } else
                conversation_attachments_key(ui, type, key);
            scope = DRAW_ALL;
            continue;
        }
        if (type == KEY_CODE_YES && key == TEXT_COPY) {
            if (conversation_members_input(ui))
                clipboard_copy(conversation_members_input(ui));
            else if (ui->focus == MESSAGE_INPUT && conversation_input(ui) && !ui->duplicate_modal &&
                     !(r8_modal_is_open(ui->r8) || ui->settings.open) &&
                     !media_view_modal(ui->media))
                clipboard_copy(conversation_input(ui));
            continue;
        }
        if (type == OK && (key == 17 || (key == 3 && !conversation_members_input(ui) &&
                                         !(r8_modal_is_open(ui->r8) || ui->settings.open) &&
                                         (ui->focus != MESSAGE_INPUT || ui->duplicate_modal ||
                                          media_view_modal(ui->media))))) {
            result = HISTORY_EXIT;
            break;
        }
        if (ui->settings.open) {
            settings_key(&ui->settings, connections->config, type, key);
            if (ui->settings.applied) {
                ui->settings.applied = false;
                app_mode_set(connections->config->mode);
                media_service_stop();
                icons_begin(ui->icons);
                for (size_t i = 0; i < connections->count; ++i) {
                    if (connections->items[i].media)
                        media_view_reset(connections->items[i].media);
                    if (app_mode_get() == APP_MODE_ULTRALIGHT)
                        chat_assets_clear_media(history_assets(connections->items[i].model));
                }
                g_hash_table_remove_all(ui->layout_index);
                for (size_t i = 0; i < HISTORY_MESSAGE_LIMIT; ++i) {
                    message_blocks_free(&ui->layouts[i].blocks);
                    memset(&ui->layouts[i], 0, sizeof(ui->layouts[i]));
                }
                ui->layout_bytes = 0;
                ui->layout_sequence = 0;
                if (app_mode_get() == APP_MODE_NORMAL)
                    graphics_terminal_probe();
            }
            scope = DRAW_ALL;
            continue;
        }
        if (shortcut_is(type, key, SHORTCUT_SETTINGS) && !ui->duplicate_modal) {
            open_settings(ui, connections->config);
            scope = DRAW_ALL;
            continue;
        }
        bool accepted = false;
        if (emoji_picker_open(ui->emojis)) {
            if (type == KEY_CODE_YES && key == KEY_MOUSE) {
                MEVENT events[64];
                size_t count = ui_mouse_events(events, 64);
                for (size_t i = 0; i < count && emoji_picker_open(ui->emojis); ++i)
                    emoji_picker_mouse(ui->emojis, conversation_input(ui), &events[i], &accepted);
            } else
                emoji_picker_key(ui->emojis, conversation_input(ui), type, key, &accepted);
            if (accepted && conversation_accept_reaction(ui)) {
                scope = DRAW_ALL;
                continue;
            }
            if (accepted) {
                send_editor_changed(ui->editor);
                history_typing_activity(ui->model);
                ui->editor_scrolling = false;
                conversation_set_focus(ui, MESSAGE_INPUT);
            } else if (!emoji_picker_open(ui->emojis)) {
                conversation_set_focus(ui, ui->emoji_return_focus);
                emoji_picker_suggest(ui->emojis, conversation_input(ui),
                                     ui->focus == MESSAGE_INPUT);
                emoji_picker_dismiss(ui->emojis);
            }
            scope = DRAW_ALL;
            continue;
        }
        if (shortcut_is(type, key, SHORTCUT_EMOJIS) &&
            (ui->focus == HISTORY_PANEL || ui->focus == MESSAGE_INPUT ||
             ui->focus == EMOJI_BUTTON) &&
            !ui->duplicate_modal && !(r8_modal_is_open(ui->r8) || ui->settings.open) &&
            !media_view_modal(ui->media) && !ui->context_open && !ui->reactors_open) {
            conversation_open_emojis(ui);
            scope = DRAW_ALL;
            continue;
        }

        if (emoji_picker_key(ui->emojis, conversation_input(ui), type, key, &accepted) ||
            mention_popup_key(ui->mentions, conversation_input(ui), type, key, &accepted)) {
            if (accepted && conversation_accept_reaction(ui)) {
                scope = DRAW_ALL;
                continue;
            }
            if (accepted) {
                send_editor_changed(ui->editor);
                history_typing_activity(ui->model);
            }
            scope = DRAW_ALL;
            continue;
        }
        bool enter = (type == OK && (key == '\r' || key == '\n')) ||
                     (type == KEY_CODE_YES && key == KEY_ENTER);
        if (ui->duplicate_modal) {
            conversation_handle_confirmation(ui, type, key, enter);
            continue;
        }
        if (shortcut_is(type, key, SHORTCUT_ACCOUNT))
            break;
        if (type == OK && key == 16) {
            result = HISTORY_PROFILES;
            break;
        }
        if (type == OK && key == 12) {
            auth_logout(auth);
            break;
        }
        if (shortcut_is(type, key, SHORTCUT_MENU) &&
            (ui->context_open || ui->focus == CHANNEL_LIST || ui->focus == HISTORY_PANEL)) {
            if (ui->focus == HISTORY_PANEL && !ui->navigating && !ui->context_open)
                conversation_interactions_key(ui, OK, SHORTCUT_MESSAGES);
            key = 'm';
            type = OK;
        }
        if (!media_view_modal(ui->media) && conversation_members_key(ui, type, key)) {
            scope = DRAW_ALL;
            continue;
        }
        if (r8_modal_is_open(ui->r8)) {
            r8_modal_key(ui->r8, type, key);
            scope = DRAW_ALL;
            continue;
        }
        if ((shortcut_is(type, key, SHORTCUT_NOTIFICATIONS) ||
             shortcut_is(type, key, SHORTCUT_SEARCH) || shortcut_is(type, key, SHORTCUT_PINNED)) &&
            !media_view_modal(ui->media)) {
            conversation_interactions_close(ui);
            mention_popup_close(ui->mentions);
            r8_modal_open(ui->r8, shortcut_is(type, key, SHORTCUT_NOTIFICATIONS) ? R8_NOTIFICATIONS
                                  : shortcut_is(type, key, SHORTCUT_SEARCH)      ? R8_SEARCH
                                                                                 : R8_PINNED);
            scope = DRAW_ALL;
            continue;
        }
        if (type == OK && key == 23 && ui->focus == CHANNEL_LIST && ui->list_dms) {
            json_t *dm = conversation_list_channel(ui, ui->highlighted);
            conversation_close_dm(ui, conversation_str(dm, "id"));
            continue;
        }
        if (media_view_modal(ui->media) && type == KEY_CODE_YES && key == KEY_MOUSE) {
            MEVENT e;
            if (ui_mouse_get(&e) == OK) {
                if (e.bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED))
                    media_view_click(ui->media, e.y, e.x);
                else if (e.bstate & (BUTTON4_PRESSED | BUTTON5_PRESSED))
                    media_view_scroll(ui->media, e.y, e.x, e.bstate & BUTTON4_PRESSED ? -3 : 3);
            }
            continue;
        }
        if (media_view_modal(ui->media) && media_view_key(ui->media, type, key))
            continue;
        if (conversation_interactions_key(ui, type, key)) {
            scope = DRAW_ALL;
            continue;
        }
        if (!media_view_modal(ui->media) &&
            ((shortcut_is(type, key, SHORTCUT_ATTACH) && ui->focus != CHANNEL_LIST) ||
             (type == OK && key == 22))) {
            if (*ui->edit_message)
                ui->feedback = "Conclua a edição antes de anexar arquivos.";
            else
                conversation_attachments_open(ui, type == OK && key == 22);
            scope = DRAW_ALL;
            continue;
        }
        if (type == KEY_CODE_YES && (key == KEY_LEFT || key == KEY_RIGHT) &&
            (ui->focus == ATTACH_BUTTON || ui->focus == EMOJI_BUTTON) && !*ui->edit_message) {
            conversation_set_focus(ui, ui->focus == ATTACH_BUTTON ? EMOJI_BUTTON : ATTACH_BUTTON);
            scope = DRAW_ALL;
            continue;
        }
        if (conversation_attachments_key(ui, type, key)) {
            scope = DRAW_ALL;
            continue;
        }
        if (shortcut_is(type, key, SHORTCUT_RETRY_SEND) && conversation_input(ui)) {
            outbox_operation latest;
            if (outbox_latest(send_editor_outbox(ui->editor), ui->editor_channel, &latest) &&
                (latest.state == OUTBOX_FAILED || latest.state == OUTBOX_UNCERTAIN))
                conversation_submit(ui, latest.id, false);
            else
                ui->feedback = "Nenhum envio falhou nesta conversa.";
            continue;
        }
        if (type == OK && key == 18) {
            chat_assets_profiles_retry(history_assets(ui->model));
            if (auth_ws_reconnecting(auth)) {
                auth_connect_ws(auth);
                continue;
            }
            inbox_retry(history_inbox(ui->model));
            if (history_message_count(ui->model)) {
                if (!history_retry(ui->model)) {
                    ui->at_end = true;
                    ui->anchor[0] = 0;
                }
            } else if (history_selected(ui->model) != SIZE_MAX)
                history_recent(ui->model);
            else
                history_reload_channels(ui->model);
            continue;
        }
        if ((type == OK && key == '\t') || (type == KEY_CODE_YES && key == KEY_BTAB)) {
            int order[13] = {SERVER_TABS, LIST_BUTTONS, CHANNEL_LIST, SETTINGS_BUTTON,
                             HISTORY_PANEL};
            int count = 5, at = 0;
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
            while (at < count && order[at] != ui->focus)
                ++at;
            conversation_set_focus(ui, order[(at + (type == OK ? 1 : count - 1)) % count]);
            continue;
        }
        if (type == OK && key == 27) {
            conversation_set_focus(ui, ui->focus == EMOJI_BUTTON    ? MESSAGE_INPUT
                                       : ui->focus == MESSAGE_INPUT ? HISTORY_PANEL
                                       : ui->focus == HISTORY_PANEL ? CHANNEL_LIST
                                                                    : HISTORY_PANEL);
            continue;
        }
        if (type == OK && key == 14 && ui->focus != SERVER_TABS && conversation_input(ui)) {
            conversation_set_focus(ui, MESSAGE_INPUT);
            conversation_edit(ui, type, key);
            continue;
        }
        if (type == KEY_CODE_YES && key == KEY_MOUSE) {
            MEVENT events[64];
            size_t count = ui_mouse_events(events, 64);
            for (size_t event_index = 0; event_index < count; ++event_index) {
                MEVENT e = events[event_index];
                if (ui->footer && (e.bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED)) &&
                    e.y == getbegy(ui->footer) + 1 && e.x >= getbegx(ui->footer) + 1 &&
                    e.x < getbegx(ui->footer) + 1 + text_label(NULL, 0, 0, 128, UI_HELP_HINT)) {
                    const char *area;
                    help_context context = conversation_help_context(ui, &area);
                    help_open(&ui->help, context, area, false);
                    scope = DRAW_ALL;
                    continue;
                }
                if (!ui->frame)
                    continue;
                if (conversation_members_mouse(ui, &e)) {
                    scope = DRAW_ALL;
                    continue;
                }
                if (conversation_attachments_mouse(ui, &e)) {
                    scope = DRAW_ALL;
                    continue;
                }
                if (ui->dragging && conversation_input(ui) && ui->body &&
                    (e.bstate & (BUTTON1_PRESSED | BUTTON1_RELEASED | REPORT_MOUSE_POSITION))) {
                    text_buffer *buffer = conversation_input(ui);
                    int row = e.y - getbegy(ui->body) - conversation_input_top(ui);
                    int column = e.x - getbegx(ui->body) - conversation_input_left(ui);
                    text_layout *measured = &ui->measured;
                    text_measure(buffer, conversation_input_width(ui), measured);
                    if (row < 0 && buffer->first_row > 0)
                        --buffer->first_row;
                    if (row >= conversation_input_height(ui) &&
                        buffer->first_row + conversation_input_height(ui) < measured->rows)
                        ++buffer->first_row;
                    if (row < 0)
                        row = 0;
                    if (row >= conversation_input_height(ui))
                        row = conversation_input_height(ui) - 1;
                    if (column < 0)
                        column = 0;
                    if (column > conversation_input_width(ui) - 1)
                        column = conversation_input_width(ui) - 1;
                    text_drag(buffer, conversation_input_width(ui), buffer->first_row + row,
                              column);
                    send_editor_changed(ui->editor);
                    ui->editor_scrolling = false;
                    if (e.bstate & BUTTON1_RELEASED)
                        ui->dragging = false;
                    continue;
                }
                if (conversation_interactions_mouse(ui, &e)) {
                    scope = DRAW_ALL;
                    continue;
                }
                if (e.bstate & (BUTTON3_PRESSED | BUTTON3_CLICKED)) {
                    if (ui->body && conversation_input(ui) && e.x >= getbegx(ui->body) &&
                        e.x < getbegx(ui->body) + getmaxx(ui->body) &&
                        conversation_input_row(ui, e.y - getbegy(ui->body)))
                        clipboard_copy(conversation_input(ui));
                    continue;
                }
                if (e.bstate & REPORT_MOUSE_POSITION)
                    continue;
                if (emoji_picker_mouse(ui->emojis, conversation_input(ui), &e, &accepted) ||
                    mention_popup_mouse(ui->mentions, conversation_input(ui), &e, &accepted)) {
                    if (accepted) {
                        send_editor_changed(ui->editor);
                        history_typing_activity(ui->model);
                    }
                    scope = DRAW_ALL;
                    continue;
                }
                if (e.bstate & (BUTTON4_PRESSED | BUTTON5_PRESSED)) {
                    scope = DRAW_CONVERSATION;
                    if (ui->body && e.x >= getbegx(ui->body) &&
                        e.x < getbegx(ui->body) + getmaxx(ui->body) &&
                        e.y >= getbegy(ui->body) + ui->history_top &&
                        e.y < getbegy(ui->body) + ui->history_top + ui->visible)
                        conversation_scroll_history(ui, e.bstate & BUTTON4_PRESSED ? -3 : 3, true);
                    else if (ui->body && conversation_input(ui) && e.x >= getbegx(ui->body) &&
                             e.x < getbegx(ui->body) + getmaxx(ui->body) &&
                             conversation_input_row(ui, e.y - getbegy(ui->body))) {
                        text_buffer *buffer = conversation_input(ui);
                        text_layout *measured = &ui->measured;
                        text_measure(buffer, conversation_input_width(ui), measured);
                        int last = measured->rows - conversation_input_height(ui);
                        if (last < 0)
                            last = 0;
                        buffer->first_row += e.bstate & BUTTON4_PRESSED ? -3 : 3;
                        if (buffer->first_row < 0)
                            buffer->first_row = 0;
                        if (buffer->first_row > last)
                            buffer->first_row = last;
                        ui->editor_scrolling = true;
                    }
                    continue;
                }
                if (!(e.bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED)))
                    continue;
                if (conversation_input(ui) && ui->body &&
                    e.y >= getbegy(ui->body) + conversation_input_top(ui) - ui->editor_padding &&
                    e.y <= getbegy(ui->body) + conversation_input_top(ui) + ui->editor_padding &&
                    conversation_hit(ui->emoji_button_hit, e.x - getbegx(ui->body))) {
                    conversation_open_emojis(ui);
                    scope = DRAW_ALL;
                    continue;
                }
                if (ui->body && e.y == getbegy(ui->body) &&
                    (conversation_hit(ui->search_hit, e.x - getbegx(ui->body)) ||
                     conversation_hit(ui->pins_hit, e.x - getbegx(ui->body)))) {
                    conversation_interactions_close(ui);
                    mention_popup_close(ui->mentions);
                    r8_modal_open(ui->r8, conversation_hit(ui->search_hit, e.x - getbegx(ui->body))
                                              ? R8_SEARCH
                                              : R8_PINNED);
                    scope = DRAW_ALL;
                    continue;
                }
                if (media_view_click(ui->media, e.y, e.x))
                    continue;
                if (ui->unseen && e.y == getbegy(ui->footer) + 1 &&
                    status_bar_hit(&ui->status, STATUS_UNREAD, e.x - getbegx(ui->footer))) {
                    if (history_has_newer(ui->model))
                        history_recent(ui->model);
                    ui->at_end = true;
                    ui->unseen = 0;
                    ui->anchor[0] = 0;
                    continue;
                }
                int y = e.y, x = e.x;
                if (wmouse_trafo(ui->tabs, &y, &x, false)) {
                    if (y == 0 && conversation_hit(ui->add_hit, x)) {
                        result = HISTORY_ADD;
                        goto finished;
                    }
                    if (y == 0 && conversation_hit(ui->back_hit, x)) {
                        if (ui->first_tab)
                            --ui->first_tab;
                        ui->tab_cursor = ui->first_tab;
                        ui->focus = SERVER_TABS;
                        continue;
                    }
                    if (y == 0 && conversation_hit(ui->next_hit, x)) {
                        if (ui->first_tab + 1 < connections->count)
                            ++ui->first_tab;
                        ui->tab_cursor = ui->first_tab;
                        ui->focus = SERVER_TABS;
                        continue;
                    }
                    if (y == 0)
                        for (size_t i = 0; i < connections->count; ++i)
                            if (conversation_hit(ui->tab_hits[i], x)) {
                                int action = conversation_activate_tab(ui, i, selected);
                                if (action >= 0) {
                                    result = action;
                                    goto finished;
                                }
                                break;
                            }
                    if (y == 1 && conversation_hit(ui->channels_hit, x))
                        conversation_set_focus(ui, CHANNEL_LIST);
                    if (y == 1 && conversation_hit(ui->conversation_hit, x))
                        conversation_set_focus(ui, HISTORY_PANEL);
                    continue;
                }
                y = e.y;
                x = e.x;
                if (ui->list && wmouse_trafo(ui->list, &y, &x, false)) {
                    if (y >= getmaxy(ui->list) - 3) {
                        if (y == getmaxy(ui->list) - 2 && x >= getmaxx(ui->list) - 6) {
                            open_settings(ui, connections->config);
                            scope = DRAW_ALL;
                        }
                        continue;
                    }
                    size_t index = y >= 1 ? (size_t)(ui->first_channel + y - 1) : SIZE_MAX;
                    if (y == 0 && x >= 1 && x < 19) {
                        conversation_switch_list(ui, x >= 11);
                        ui->navigating = false;
                        conversation_set_focus(ui, ui->list_dms ? CHANNEL_LIST : LIST_BUTTONS);
                        scope = DRAW_ALL;
                    } else if (y < getmaxy(ui->list) - 3 && index < conversation_list_count(ui)) {
                        if (conversation_list_selectable(ui, index)) {
                            ui->highlighted = index;
                            conversation_open_channel(ui, false);
                            if (ui->list_dms && COLS >= 80) {
                                ui->navigating = false;
                                conversation_set_focus(ui, CHANNEL_LIST);
                            }
                        }
                    } else {
                        if (ui->focus == CHANNEL_LIST || ui->focus == SERVER_TABS)
                            scope = DRAW_NAVIGATION;
                        conversation_set_focus(ui, CHANNEL_LIST);
                    }
                    continue;
                }
                y = e.y;
                x = e.x;
                if (ui->body && wmouse_trafo(ui->body, &y, &x, false)) {
                    if (y < ui->history_top + ui->visible)
                        conversation_set_focus(ui, HISTORY_PANEL);
                    else if (conversation_input(ui) && conversation_input_row(ui, y)) {
                        text_buffer *editor = conversation_input(ui);
                        text_click(editor, conversation_input_width(ui),
                                   y - conversation_input_top(ui) + editor->first_row,
                                   x - conversation_input_left(ui));
                        send_editor_changed(ui->editor);
                        conversation_set_focus(ui, MESSAGE_INPUT);
                        ui->editor_scrolling = false;
                        ui->dragging = (e.bstate & BUTTON1_PRESSED) != 0;
                    }
                }
            }
            continue;
        }
        if (ui->focus == SETTINGS_BUTTON) {
            if (enter || (type == OK && key == ' '))
                open_settings(ui, connections->config);
            else if (type == KEY_CODE_YES && key == KEY_UP)
                conversation_set_focus(ui, CHANNEL_LIST);
            else if (type == KEY_CODE_YES && key == KEY_DOWN)
                conversation_set_focus(ui, HISTORY_PANEL);
            continue;
        }
        if (ui->focus == EMOJI_BUTTON) {
            if (enter)
                conversation_open_emojis(ui);
            else if (type == KEY_CODE_YES && (key == KEY_LEFT || key == KEY_UP))
                conversation_set_focus(ui, MESSAGE_INPUT);
            continue;
        }
        if (ui->focus == SEARCH_BUTTON || ui->focus == PINS_BUTTON || ui->focus == MEMBERS_BUTTON) {
            if (enter || (type == OK && key == ' ')) {
                if (ui->focus == MEMBERS_BUTTON)
                    conversation_members_toggle(ui);
                else {
                    conversation_interactions_close(ui);
                    r8_modal_open(ui->r8, ui->focus == SEARCH_BUTTON ? R8_SEARCH : R8_PINNED);
                }
            } else if (type == KEY_CODE_YES && key == KEY_LEFT)
                conversation_set_focus(ui, ui->focus == MEMBERS_BUTTON ? PINS_BUTTON
                                           : ui->focus == PINS_BUTTON  ? SEARCH_BUTTON
                                                                       : CHANNEL_LIST);
            else if (type == KEY_CODE_YES && key == KEY_RIGHT) {
                if (ui->focus == SEARCH_BUTTON)
                    conversation_set_focus(ui, PINS_BUTTON);
                else if (ui->focus == PINS_BUTTON)
                    conversation_set_focus(ui, MEMBERS_BUTTON);
                else if (ui->members_open)
                    conversation_set_focus(ui, MEMBERS_PANEL);
            } else if (type == KEY_CODE_YES && key == KEY_UP)
                conversation_set_focus(ui, SERVER_TABS);
            else if (type == KEY_CODE_YES && key == KEY_DOWN)
                conversation_set_focus(ui, ui->focus == MEMBERS_BUTTON && ui->members_open
                                               ? MEMBERS_PANEL
                                               : HISTORY_PANEL);
            else if (type == OK && key == 27)
                conversation_set_focus(ui, HISTORY_PANEL);
            scope = DRAW_ALL;
            continue;
        }
        if (ui->focus == LIST_BUTTONS) {
            if (type == KEY_CODE_YES && (key == KEY_LEFT || key == KEY_RIGHT)) {
                if (key == KEY_RIGHT && ui->list_button)
                    conversation_set_focus(ui, SEARCH_BUTTON);
                else
                    ui->list_button = key == KEY_RIGHT;
            } else if (enter || (type == OK && key == ' '))
                conversation_switch_list(ui, ui->list_button != 0);
            else if (type == KEY_CODE_YES && key == KEY_DOWN) {
                conversation_switch_list(ui, ui->list_button != 0);
                conversation_set_focus(ui, CHANNEL_LIST);
            } else if (type == KEY_CODE_YES && key == KEY_UP)
                conversation_set_focus(ui, SERVER_TABS);
            scope = DRAW_ALL;
            continue;
        }
        if (ui->focus != SERVER_TABS && conversation_input(ui) && type == OK && iswprint(key)) {
            if (ui->focus != MESSAGE_INPUT)
                conversation_set_focus(ui, MESSAGE_INPUT);
            else if (!mention_popup_open(ui->mentions))
                scope = DRAW_EDITOR;
            conversation_edit(ui, type, key);
            continue;
        }
        if (ui->focus == MESSAGE_INPUT) {
            if (enter)
                conversation_submit(ui, 0, false);
            else if (type == KEY_CODE_YES && (key == KEY_PPAGE || key == KEY_NPAGE)) {
                /* Keep typing while browsing: one line per press. */
                scope = DRAW_CONVERSATION;
                conversation_scroll_history(ui, key == KEY_PPAGE ? -1 : 1, true);
            } else if (type == KEY_CODE_YES &&
                       (key == TEXT_SHIFT_PAGE_UP || key == TEXT_SHIFT_PAGE_DOWN)) {
                scope = DRAW_CONVERSATION;
                conversation_scroll_history(
                    ui, key == TEXT_SHIFT_PAGE_UP ? -ui->visible : ui->visible, true);
            } else {
                if (!mention_popup_open(ui->mentions))
                    scope = DRAW_EDITOR;
                conversation_edit(ui, type, key);
            }
            continue;
        }
        if (ui->focus == SERVER_TABS) {
            if (type == KEY_CODE_YES && key == KEY_DOWN)
                conversation_set_focus(ui, CHANNEL_LIST);
            else if (type == KEY_CODE_YES && (key == KEY_LEFT || key == KEY_RIGHT)) {
                size_t index = ui->tab_cursor;
                if (key == KEY_LEFT && index)
                    --index;
                if (key == KEY_RIGHT && index < connections->count)
                    ++index;
                ui->tab_cursor = index;
                if (index < connections->count) {
                    int action = conversation_activate_tab(ui, index, selected);
                    if (action >= 0) {
                        result = action;
                        break;
                    }
                }
            } else if (enter) {
                int action = conversation_activate_tab(ui, ui->tab_cursor, selected);
                if (action >= 0) {
                    result = action;
                    break;
                }
            }
        } else if (ui->focus == CHANNEL_LIST) {
            scope = DRAW_NAVIGATION;
            size_t count = conversation_list_count(ui);
            size_t row = SIZE_MAX;
            bool voice_history = (type == KEY_CODE_YES && key == TEXT_SHIFT_ENTER) ||
                                 shortcut_is(type, key, SHORTCUT_ATTACH);
            if (type == KEY_CODE_YES && key == KEY_UP) {
                row = ui->highlighted ? conversation_list_find(ui, ui->highlighted - 1, -1)
                                      : SIZE_MAX;
                if (row == SIZE_MAX)
                    conversation_set_focus(ui, LIST_BUTTONS);
            } else if (type == KEY_CODE_YES && key == KEY_DOWN)
                row = conversation_list_find(ui, ui->highlighted + 1, 1);
            else if (type == KEY_CODE_YES && key == KEY_HOME)
                row = conversation_list_find(ui, 0, 1);
            else if (type == KEY_CODE_YES && key == KEY_END && count)
                row = conversation_list_find(ui, count - 1, -1);
            else if (type == KEY_CODE_YES && (key == KEY_PPAGE || key == KEY_NPAGE) && count) {
                size_t page = LINES > CONTENT_Y + 5 ? (size_t)(LINES - CONTENT_Y - 5) : 1;
                int direction = key == KEY_PPAGE ? -1 : 1;
                size_t target =
                    direction < 0
                        ? (ui->highlighted > page ? ui->highlighted - page : 0)
                        : (ui->highlighted + page < count ? ui->highlighted + page : count - 1);
                row = conversation_list_find(ui, target, direction);
                if (row == SIZE_MAX)
                    row = conversation_list_find(ui, target, -direction);
            } else if (enter || voice_history || (type == KEY_CODE_YES && key == KEY_RIGHT)) {
                scope = DRAW_ALL;
                conversation_open_channel(ui, voice_history);
            }
            if (row != SIZE_MAX)
                ui->highlighted = row;
        } else {
            if (type == KEY_CODE_YES && key == KEY_LEFT)
                conversation_set_focus(ui, CHANNEL_LIST);
            else if (type == KEY_CODE_YES && key == KEY_UP && conversation_position(ui) == 0) {
                conversation_set_focus(ui, SEARCH_BUTTON);
                scope = DRAW_ALL;
            } else if (type == KEY_CODE_YES &&
                       (key == KEY_PPAGE || key == KEY_UP || key == KEY_HOME)) {
                scope = DRAW_CONVERSATION;
                conversation_scroll_history(ui,
                                            key == KEY_HOME ? -2147483647
                                            : key == KEY_UP ? -1
                                                            : -ui->visible,
                                            key != KEY_HOME);
            } else if (type == KEY_CODE_YES && (key == KEY_DOWN || key == KEY_NPAGE)) {
                scope = DRAW_CONVERSATION;
                conversation_scroll_history(ui, key == KEY_DOWN ? 1 : ui->visible, true);
            } else if (type == KEY_CODE_YES && key == KEY_END) {
                ui->centered_message[0] = 0;
                scope = DRAW_CONVERSATION;
                if (!ui->unseen || history_evicted(ui->model))
                    history_recent(ui->model);
                ui->at_end = true;
                ui->unseen = 0;
                ui->anchor[0] = 0;
            } else if (enter && conversation_input(ui))
                conversation_set_focus(ui, MESSAGE_INPUT);
            else if (enter && !history_retry(ui->model)) {
                ui->at_end = true;
                ui->anchor[0] = 0;
            }
        }
    }
finished:
    terminal_title_reset();
    /* A session that ends with the player open still keeps the chosen level. */
    if (connections->config) {
        int level = player_volume_get();
        if (level != saved_volume)
            profiles_set_player_volume(connections->config, level);
    }
    conversation_members_destroy(ui);
    conversation_attachments_close(ui);
    file_picker_destroy(ui->files);
    instances_visible(connections, NULL, NULL);
    text_paste_cancel(&ui->paste_data);
    mousemask(previous_mouse, NULL);
    ui_mouse_drag_tracking((previous_mouse & REPORT_MOUSE_POSITION) != 0);
    conversation_interactions_close(ui);
    emoji_picker_destroy(ui->emojis);
    icons_destroy(ui->icons);
    mention_popup_destroy(ui->mentions);
    text_buffer *draft = send_editor_input(ui->editor);
    if (draft) {
        text_mentions(draft, NULL, NULL);
        text_icons(draft, NULL, NULL, NULL);
    }
    for (size_t i = 0; i < HISTORY_MESSAGE_LIMIT; ++i)
        conversation_release_layout(ui, &ui->layouts[i]);
    settings_close(&ui->settings);
    media_view_suspend(ui->media);
    conversation_destroy_layout(ui);
    if (ui->model)
        history_active(ui->model, false);
    help_destroy(&ui->help);
    r8_modal_destroy(ui->r8);
    g_hash_table_destroy(ui->layout_index);
    free(ui);
    touchwin(stdscr);
    return result;
}
