#include "ui/screens/conversation/private.h"

void conversation_destroy_layout(view *ui)
{
    if (ui->confirm)
        delwin(ui->confirm);
    if (ui->list)
        delwin(ui->list);
    if (ui->attachment_bar)
        delwin(ui->attachment_bar);
    ui->attachment_bar = NULL;
    if (ui->body)
        delwin(ui->body);
    if (ui->footer)
        delwin(ui->footer);
    if (ui->tabs)
        delwin(ui->tabs);
    if (ui->frame)
        delwin(ui->frame);
    ui->confirm = ui->list = ui->body = ui->footer = ui->tabs = ui->frame = NULL;
}

bool conversation_layout(view *ui)
{
    graphics_frame_invalidate();
    conversation_destroy_layout(ui);
    /* Consume stdscr's resize damage without flushing a blank screen. The
     * replacement windows are composed before the next single doupdate(). */
    erase();
    wnoutrefresh(stdscr);
    ui->width = COLS;
    ui->height = LINES;
    if (COLS < 44 || LINES < 14)
        return true;
    bool wide = COLS >= 80;
    int rows = LINES - CONTENT_Y - 3;
    int body_width = wide ? COLS - 27 : COLS - 2;
    ui->members_reserved = ui->members_open && body_width >= 74 ? 30 : 0;
    ui->side = wide ? 24 : COLS - 2;
    ui->history_top = rows >= 8 ? 3 : rows >= 7 ? 2 : 1;
    /* Reserve a visible history row, both input borders and the typing row. */
    ui->editor_padding = rows - ui->history_top >= 7 ? 1 : 0;
    ui->editor_rows = 1;
    ui->editor_border = rows - 4 - 2 * ui->editor_padding;
    ui->visible = ui->editor_border - ui->history_top;
    media_view_viewport(ui->media, ui->visible);
    ui->frame = newwin(LINES, COLS, 0, 0);
    if (!ui->frame)
        return false;
    wbkgd(ui->frame, COLOR_PAIR(THEME_NORMAL));
    ui->tabs = derwin(ui->frame, 2, COLS - 2, 1, 1);
    ui->footer = derwin(ui->frame, 2, COLS - 2, LINES - 3, 1);
    if (wide || ui->show_channels)
        ui->list = derwin(ui->frame, rows, ui->side, CONTENT_Y, 1);
    if (wide || !ui->show_channels)
        ui->body =
            derwin(ui->frame, rows, (wide ? COLS - 3 - ui->side : COLS - 2) - ui->members_reserved,
                   CONTENT_Y, wide ? ui->side + 2 : 1);
    bool ok = ui->tabs && ui->footer && (wide ? ui->list && ui->body : ui->list || ui->body);
    ui->layout_failed = !ok;
    return ok;
}

void conversation_release_layout(view *ui, cached_layout *c)
{
    ui->layout_bytes -= c->bytes;
    c->bytes = 0;
    message_blocks_free(&c->blocks);
}

cached_layout *conversation_find_layout(view *ui, size_t index, int width)
{
    json_t *m = history_message(ui->model, index);
    const char *id = conversation_str(m, "id");
    cached_layout *c = g_hash_table_lookup(ui->layout_index, id);
    if (!c) {
        cached_layout *oldest = &ui->layouts[0];
        for (size_t i = 0; i < HISTORY_MESSAGE_LIMIT; ++i) {
            cached_layout *entry = &ui->layouts[i];
            if (!*entry->id) {
                oldest = entry;
                break;
            }
            if (entry->used < oldest->used)
                oldest = entry;
        }
        c = oldest;
        if (*c->id)
            g_hash_table_remove(ui->layout_index, c->id);
        conversation_release_layout(ui, c);
        memset(c, 0, sizeof(*c));
        snprintf(c->id, sizeof(c->id), "%s", id);
        g_hash_table_insert(ui->layout_index, c->id, c);
    }
    uint64_t revision = history_message_revision(ui->model, index),
             names = history_users_revision(ui->model);
    bool has_media = json_array_size(json_object_get(m, "attachments")) ||
                     json_array_size(json_object_get(m, "embeds"));
    uint64_t dimensions = has_media ? media_view_dimensions_revision(ui->media) : 0;
    uint64_t reply_revision =
        *conversation_str(m, "reply_to") ? history_reference_revision(ui->model) : 0;
    graphics_terminal terminal = graphics_terminal_get();
    bool graphics = terminal.supported && ui->media;
    bool continuation = index && message_same_author(history_message(ui->model, index - 1), m);
    uint64_t assets = chat_assets_emoji_revision(history_assets(ui->model));
    if (c->continuation != continuation || c->assets != assets ||
        c->calendar_day != ui->calendar_day || c->cell_width != terminal.cell_width ||
        c->cell_height != terminal.cell_height || c->revision != revision || c->names != names ||
        c->dimensions != dimensions || c->width != width || c->graphics != graphics ||
        c->reply_revision != reply_revision) {
        conversation_release_layout(ui, c);
        c->height = 0;
        c->continuation = continuation;
        c->assets = assets;
        c->calendar_day = ui->calendar_day;
        c->cell_width = terminal.cell_width;
        c->cell_height = terminal.cell_height;
        c->revision = revision;
        c->names = names;
        c->dimensions = dimensions;
        c->width = width;
        c->graphics = graphics;
        c->reply_revision = reply_revision;
    }
    c->used = ++ui->layout_sequence;
    return c;
}

void conversation_account_layout(view *ui, cached_layout *c)
{
    message_blocks *b = &c->blocks;
    c->height = b->height;
    c->tail_gap = b->count && !b->rows[b->count - 1].count && !b->rows[b->count - 1].media_count &&
                  !b->rows[b->count - 1].card;
    c->bytes = b->capacity * sizeof(*b->rows) + b->media_count * sizeof(*b->media);
    for (size_t i = 0; i < b->count; ++i)
        c->bytes += b->rows[i].icon_count * sizeof(message_icon_region);
    for (size_t i = 0; i < b->count; ++i)
        for (int k = 0; k < b->rows[i].count; ++k) {
            message_segment *s = &b->rows[i].segments[k];
            c->bytes += (s->text ? strlen(s->text) + 1 : 0) + s->highlight_length + 1;
        }
    ui->layout_bytes += c->bytes;
    while (ui->layout_bytes > LAYOUT_BYTES) {
        cached_layout *oldest = NULL;
        for (size_t i = 0; i < HISTORY_MESSAGE_LIMIT; ++i) {
            cached_layout *entry = &ui->layouts[i];
            if (entry != c && entry->bytes && (!oldest || entry->used < oldest->used))
                oldest = entry;
        }
        if (!oldest)
            break;
        conversation_release_layout(ui, oldest);
    }
}

int conversation_walk(view *ui, int first, bool paint, bool save, int *anchor)
{
    uint64_t now = 0;
    net_now_ms(&now);
    bool observed = text_terminal_focused() ||
                    (text_terminal_focus_unknown() && now < ui->view_requested_until);
    int total = 0, width = ui->body ? getmaxx(ui->body) - 4 : ui->width - 4;
    bool saved = false;
    if (paint)
        ui->hit_count = 0;
    graphics_terminal terminal = graphics_terminal_get();
    int avatar_rows =
        terminal.supported && terminal.cell_height > 0
            ? (MESSAGE_AVATAR_PIXELS + terminal.cell_height - 1) / terminal.cell_height
            : 1;
    int group_start = 0;
    *anchor = 0;
    for (size_t i = 0; i < history_message_count(ui->model); ++i) {
        json_t *m = history_message(ui->model, i);
        bool selected = (ui->navigating || ui->context_open || ui->reactors_open) &&
                        !strcmp(ui->selected_message, conversation_str(m, "id"));
        bool editing = !strcmp(ui->edit_message, conversation_str(m, "id"));
        bool replying =
            !*ui->edit_message && !strcmp(conversation_reply_target(ui), conversation_str(m, "id"));
        bool bordered = selected || replying || editing;
        int border_pair = selected ? THEME_PANEL_ACCENT : THEME_FOCUS_BORDER;
        int border_start = total;
        if (bordered) {
            if (selected)
                ui->selected_start = total;
            ++total;
        }
        cached_layout *cached = conversation_find_layout(ui, i, width);
        bool anchored = !strcmp(ui->anchor, conversation_str(m, "id"));
        bool continuation = cached->continuation;
        if (!continuation)
            group_start = total;
        bool followed = message_same_author(m, history_message(ui->model, i + 1));
        bool compact_end = ui->visible == 1 && i + 1 == history_message_count(ui->model);
        int measured_rows = cached->height;
        if (!bordered && (followed || compact_end) && measured_rows && cached->tail_gap)
            --measured_rows;
        if (measured_rows && !followed && !compact_end &&
            total + measured_rows < group_start + avatar_rows)
            measured_rows = group_start + avatar_rows - total;
        bool avatar_visible =
            !continuation && total < first + ui->visible && total + avatar_rows > first;
        if (cached->height && !anchored &&
            (!paint || (!avatar_visible &&
                        (total + measured_rows <= first || total >= first + ui->visible))) &&
            (!save || first < border_start || first >= total + measured_rows)) {
            total += measured_rows;
            if (selected)
                ui->selected_end = total;
            continue;
        }
        char reply[TEXT_UTF8_BYTES];
        conversation_reply_label(ui, conversation_str(m, "reply_to"), width - 12, reply,
                                 sizeof(reply));
        message_blocks *blocks = &cached->blocks;
        if (!blocks->rows) {
            json_t *display = json_deep_copy(m);
            if (!display)
                continue;
            size_t reaction_index;
            json_t *reaction;
            json_array_foreach(json_object_get(display, "reactions"), reaction_index, reaction)
            {
                const server_emoji *emoji = chat_assets_emoji_id(
                    history_assets(ui->model), conversation_str(reaction, "emoji_id"));
                if (*conversation_str(reaction, "emoji_id"))
                    chat_assets_emojis_request(history_assets(ui->model), false);
                if (emoji)
                    json_object_set_new(reaction, "_display_name", json_string(emoji->name));
            }
            bool ok = message_blocks_build_visual(
                blocks, display, history_message_author(ui->model, m), reply, width,
                graphics_terminal_get().supported && ui->media, media_view_measure, ui->media,
                history_mention_name, ui->model,
                auth_user_id(ui->connections->items[ui->active_tab].auth), icons_resolve,
                icons_paint, ui->icons, continuation);
            json_decref(display);
            if (!ok)
                continue;
        }
        if (!cached->bytes)
            conversation_account_layout(ui, cached);
        if (anchored)
            *anchor = bordered && !strcmp(ui->block, "selection-border") ? border_start : total;
        if (save && bordered && first == border_start) {
            snprintf(ui->anchor, sizeof(ui->anchor), "%s", conversation_str(m, "id"));
            snprintf(ui->block, sizeof(ui->block), "selection-border");
            ui->offset = 0;
            saved = true;
        }
        bool *offered =
            paint && ui->media ? calloc(blocks->media_count + 1, sizeof(*offered)) : NULL;
        size_t rows = blocks->count;
        int height = blocks->height;
        /* Continuations share the avatar gutter; pad only the end of a group. */
        if (!bordered && (followed || compact_end) && rows && !blocks->rows[rows - 1].count &&
            !blocks->rows[rows - 1].media_count && !blocks->rows[rows - 1].card) {
            --rows;
            --height;
        }
        if (!followed && !compact_end && total + height < group_start + avatar_rows)
            height = group_start + avatar_rows - total;
        if (paint && avatar_visible && !ui->settings.open && !media_view_modal(ui->media)) {
            int top = first > total ? first - total : 0;
            int bottom = total + avatar_rows > first + ui->visible
                             ? total + avatar_rows - first - ui->visible
                             : 0;
            if (top + bottom < avatar_rows)
                icons_message_avatar(ui->icons, m,
                                     getbegy(ui->body) + ui->history_top + total + top - first,
                                     getbegx(ui->body) + 2, top, bottom,
                                     ui->focus == HISTORY_PANEL ? THEME_HISTORY_AREA : THEME_PANEL);
        }
        if (selected)
            ui->selected_end = total + height;
        if (paint && ui->body && total + height > first && border_start < first + ui->visible) {
            int top = MAX(border_start, first), bottom = MIN(total + height, first + ui->visible);
            conversation_message_hit(ui, conversation_str(m, "id"), MESSAGE_HIT,
                                     getbegy(ui->body) + ui->history_top + top - first,
                                     getbegx(ui->body), bottom - top, getmaxx(ui->body), 0);
            if (bordered) {
                if (selected && ui->selected_media >= blocks->media_count)
                    ui->selected_media = 0;
                wattrset(ui->body, COLOR_PAIR(border_pair) | A_BOLD);
                for (int line = top; line < bottom; ++line) {
                    int y = ui->history_top + line - first;
                    if (line == border_start || line == total + height - 1) {
                        mvwhline(ui->body, y, 1, ACS_HLINE, getmaxx(ui->body) - 2);
                        mvwaddch(ui->body, y, 1,
                                 line == border_start ? ACS_ULCORNER : ACS_LLCORNER);
                        mvwaddch(ui->body, y, getmaxx(ui->body) - 2,
                                 line == border_start ? ACS_URCORNER : ACS_LRCORNER);
                    } else {
                        mvwaddch(ui->body, y, 1, ACS_VLINE);
                        mvwaddch(ui->body, y, getmaxx(ui->body) - 2, ACS_VLINE);
                    }
                }
                if (border_start >= first && border_start < first + ui->visible &&
                    (editing || replying || (selected && blocks->media_count))) {
                    char label[96];
                    const char *action = editing    ? " ✎ Editando "
                                         : replying ? " ↰ Respondendo "
                                                    : " ";
                    if (selected && blocks->media_count)
                        snprintf(label, sizeof(label), "%s%sMídia %zu/%zu ", action,
                                 editing || replying ? "· " : "", ui->selected_media + 1,
                                 blocks->media_count);
                    else
                        snprintf(label, sizeof(label), "%s", action);
                    text_label(ui->body, ui->history_top + border_start - first, 3, width - 2,
                               label);
                }
            }
        }
        for (size_t row = 0; row < rows; ++row) {
            const message_row *r = &blocks->rows[row];
            char key[128];
            size_t offset = 0;
            if (r->count) {
                snprintf(key, sizeof(key), "%s", r->segments[0].key);
                offset = r->segments[0].offset;
            } else if (r->media_count) {
                const message_media_region *region = &r->media[0];
                const message_media *item = &blocks->media[region->index];
                snprintf(key, sizeof(key), "media:%s:%s:%u", item->id, item->role, item->position);
                offset = (size_t)(region->row < 0 ? 0 : region->row);
            } else
                snprintf(key, sizeof(key), "gap:%zu", row);
            if (anchored && !strcmp(key, ui->block) && offset <= ui->offset)
                *anchor = total + r->y;
            if (save && !saved && first == total + r->y) {
                snprintf(ui->anchor, sizeof(ui->anchor), "%s", conversation_str(m, "id"));
                snprintf(ui->block, sizeof(ui->block), "%s", key);
                ui->offset = offset;
                saved = true;
            }
            if (paint && total + r->y >= first && total + r->y < first + ui->visible) {
                int y = ui->history_top + total + r->y - first;
                int pair = ui->focus == HISTORY_PANEL ? THEME_HISTORY_AREA : THEME_PANEL;
                int accent = ui->focus == HISTORY_PANEL ? THEME_HISTORY_ACCENT : THEME_PANEL_ACCENT;
                if (r->count && !strcmp(r->segments[0].key, "header"))
                    pair = accent;
                unsigned author_color =
                    !r->header || history_message_webhook(m)
                        ? 0x1ffffff
                        : history_member_color(ui->model, conversation_str(m, "author_id"));
                message_row_draw(ui->body, r, y, 2 + r->x, width - r->x, pair, accent,
                                 author_color);
                if (r->count && !strcmp(r->segments[0].key, "reply"))
                    conversation_message_hit(
                        ui, conversation_str(m, "id"), REPLY_HIT, getbegy(ui->body) + y,
                        getbegx(ui->body) + 2 + r->indent, 1, width - r->indent, 0);
                for (size_t k = 0; k < blocks->reaction_count; ++k) {
                    const message_reaction_region *reaction = &blocks->reactions[k];
                    if (reaction->row != (int)row)
                        continue;
                    int x = 2 + r->indent + r->x;
                    if (reaction->mine)
                        mvwchgat(ui->body, y, x, reaction->width, A_BOLD, THEME_PANEL_ACTIVE, NULL);
                    conversation_message_hit(ui, conversation_str(m, "id"), REACTION_HIT,
                                             getbegy(ui->body) + y, getbegx(ui->body) + x, 1,
                                             reaction->width, reaction->index);
                }
                if (!ui->settings.open && !media_view_modal(ui->media)) {
                    for (size_t k = 0; k < r->icon_count; ++k) {
                        const message_icon_region *icon = &r->icons[k];
                        char scope[180];
                        snprintf(scope, sizeof(scope), "%s:%s", conversation_str(m, "id"),
                                 icon->key);
                        icons_scope(ui->icons, scope, 0);
                        int icon_pair = r->card ? THEME_EMBED_AREA : pair;
                        for (size_t n = 0; n < blocks->reaction_count; ++n)
                            if (blocks->reactions[n].row == (int)row && blocks->reactions[n].mine)
                                icon_pair = THEME_PANEL_ACTIVE;
                        int x = 2 + r->x + r->indent + icon->x;
                        if (icon->pixels) {
                            if (icon->row && total + r->y != first)
                                continue;
                            int cell_height = graphics_terminal_get().cell_height;
                            int start = total + r->y - icon->row;
                            int top = icon->row * cell_height;
                            int bottom =
                                MAX(0, icon->pixels - (first + ui->visible - start) * cell_height);
                            icons_paint_clipped(ui->icons, ui->body, icon->id, y, x, icon_pair,
                                                icon->offset, icon->pixels, top, bottom);
                        } else
                            icons_paint(ui->icons, ui->body, icon->id, y, x, icon_pair, false,
                                        icon->offset);
                    }
                }
                if (observed && !emoji_picker_open(ui->emojis) &&
                    !(r8_modal_is_open(ui->r8) || ui->settings.open) &&
                    !media_view_modal(ui->media) && !ui->context_open && !ui->reactors_open &&
                    !conversation_members_modal(ui) &&
                    !(ui->members_open && !ui->members_reserved)) {
                    if ((r->count && strcmp(r->segments[0].key, "header")) || r->media_count)
                        inbox_view_message(history_inbox(ui->model), conversation_str(m, "id"));
                    if (ui->at_end && (r->count || r->media_count))
                        history_read_visible(ui->model, conversation_str(m, "id"));
                }
                if (blocks->self_mention && r->y + 1 < blocks->height) {
                    wattrset(ui->body, COLOR_PAIR(accent) | A_BOLD);
                    mvwaddch(ui->body, y, 0, ACS_VLINE);
                }
                for (int k = 0;
                     k < r->media_count && (size_t)k < sizeof(r->media) / sizeof(r->media[0]) &&
                     offered && ui->media;
                     ++k) {
                    const message_media_region *region = &r->media[k];
                    if (offered[region->index])
                        continue;
                    offered[region->index] = true;
                    int start = total + r->y - region->row;
                    int top = first > start ? first - start : 0;
                    int bottom = start + region->height > first + ui->visible
                                     ? start + region->height - first - ui->visible
                                     : 0;
                    if (bottom > region->height)
                        bottom = region->height;
                    int graphics_y =
                        getbegy(ui->body) + ui->history_top + (start > first ? start - first : 0);
                    if (!region->height)
                        graphics_y = getbegy(ui->body) + y;
                    media_view_offer(ui->media, &blocks->media[region->index], graphics_y,
                                     getbegx(ui->body) + 2 + r->x + r->indent + region->x,
                                     region->width, region->height, top, bottom);
                }
            }
        }
        if (paint && bordered && total + height - 1 >= first &&
            total + height - 1 < first + ui->visible) {
            int y = ui->history_top + total + height - 1 - first;
            wattrset(ui->body, COLOR_PAIR(border_pair) | A_BOLD);
            mvwhline(ui->body, y, 1, ACS_HLINE, getmaxx(ui->body) - 2);
            mvwaddch(ui->body, y, 1, ACS_LLCORNER);
            mvwaddch(ui->body, y, getmaxx(ui->body) - 2, ACS_LRCORNER);
        }
        total += height;
        free(offered);
        if (ui->layout_bytes > LAYOUT_BYTES)
            conversation_release_layout(ui, cached);
    }
    return total;
}

static bool centered_selection(view *ui)
{
    return ui->navigating && *ui->centered_message &&
           !strcmp(ui->centered_message, ui->selected_message);
}
static int centered_position(view *ui, int last)
{
    return CLAMP((ui->selected_start + ui->selected_end - ui->visible) / 2, 0, last);
}
int conversation_position(view *ui)
{
    int anchor;
    int total = conversation_walk(ui, 0, false, false, &anchor);
    int last = total > ui->visible ? total - ui->visible : 0;
    if (centered_selection(ui)) {
        int first = centered_position(ui, last);
        /* Keep the UUID centered as late media, reply excerpts, names or pages
         * change row heights. Save a normal anchor for leaving this mode. */
        conversation_walk(ui, first, false, true, &anchor);
        return first;
    }
    return ui->at_end ? last : anchor > last ? last : anchor;
}

void conversation_scroll_history(view *ui, int delta, bool paginate)
{
    if (!ui->body)
        return;
    if (!history_message_count(ui->model) && delta < 0) {
        ui->centered_message[0] = 0;
        ui->at_end = false;
        ui->anchor[0] = 0;
        ui->waiting_page = 0;
        return;
    }
    int anchor, total = conversation_walk(ui, 0, false, false, &anchor);
    int last = total > ui->visible ? total - ui->visible : 0;
    int first = (centered_selection(ui) ? centered_position(ui, last)
                 : ui->at_end           ? last
                                        : anchor) +
                delta;
    ui->centered_message[0] = 0;
    if (first < 0)
        first = 0;
    if (first > last)
        first = last;
    ui->at_end = first == last && !history_has_newer(ui->model);
    if (ui->at_end)
        ui->unseen = 0;
    ui->waiting_page = delta < 0 && first == 0 ? -1 : delta > 0 && first == last ? 1 : 0;
    conversation_walk(ui, first, false, true, &anchor);
    /* Load near the edge on actual scroll input. Drawing/polling never chains
     * reads, and Home remains a jump within the currently loaded window. */
    if (paginate && delta < 0 && first <= 2 * ui->visible && history_has_more(ui->model)) {
        ui->at_end = false;
        history_older(ui->model);
    } else if (paginate && delta > 0 && first >= last - 2 * ui->visible &&
               history_has_newer(ui->model))
        history_newer(ui->model);
}

bool conversation_editor_geometry(view *ui)
{
    if (!ui->body)
        return false;
    int rows = 1;
    const GPtrArray *files = conversation_attachments(ui);
    int available = getmaxy(ui->body) - ui->history_top;
    int attachment_rows = files && files->len ? (available >= 10 ? 3 : 1) : 0;
    int padding = available >= (attachment_rows ? 10 : 7) ? 1 : 0;
    text_buffer *buffer = conversation_input(ui);
    if (buffer) {
        text_layout *measured = &ui->measured;
        text_measure(buffer, conversation_input_width(ui), measured);
        rows = measured->rows;
    }
    /* Keep the attachment block outside the editor and preserve history even
     * at minimum height, where the typing row and block borders collapse. */
    int bottom_rows = attachment_rows && available <= 5 ? 2 : 3;
    int maximum = available - 2 * padding - bottom_rows - 1 - attachment_rows;
    if (maximum > 5)
        maximum = 5;
    if (maximum < 1)
        maximum = 1;
    if (rows > maximum)
        rows = maximum;
    int border = getmaxy(ui->body) - rows - 2 * padding - bottom_rows;
    int attachment_top = border - attachment_rows;
    bool changed = rows != ui->editor_rows || border != ui->editor_border ||
                   padding != ui->editor_padding || attachment_rows != ui->attachment_rows ||
                   attachment_top != ui->attachment_top;
    if (changed && ui->attachment_bar) {
        delwin(ui->attachment_bar);
        ui->attachment_bar = NULL;
    }
    ui->editor_padding = padding;
    ui->editor_rows = rows;
    ui->editor_border = border;
    ui->attachment_rows = attachment_rows;
    ui->attachment_top = attachment_top;
    if (attachment_rows && !ui->attachment_bar)
        ui->attachment_bar =
            derwin(ui->body, attachment_rows, getmaxx(ui->body), attachment_top, 0);
    ui->visible = attachment_top - ui->history_top;
    media_view_viewport(ui->media, ui->visible);
    return changed;
}
