#include "ui/screens/conversation/private.h"

enum member_modal { MEMBER_NO_MODAL, MEMBER_MENU, MEMBER_CARD, MEMBER_CONFIRM, MEMBER_LINK };
typedef struct {
    json_t *user; /* Owned reference; stable across asynchronous directory updates. */
    char *group, *sort;
    char group_id[40];
    unsigned status;
} member_row;
struct member_drawer {
    GPtrArray *rows;
    WINDOW *window, *modal;
    text_buffer query;
    char selected[37], restore[37], target[37], notice[256];
    uint64_t users, presence, permissions, generation;
    bool dirty, by_status, accept, opening_dm;
    int focus, return_focus, cursor, first, menu, menu_y, scroll, field_first;
    int hit_first[256], hit_last[256], hit_index[256], hit_count;
    enum member_modal panel;
    member_action confirmation;
};
static const char *s(const json_t *o, const char *key)
{
    return conversation_str(o, key);
}
static const char *display_name(const json_t *u)
{
    return *s(u, "nickname") ? s(u, "nickname") : s(u, "username");
}
static void draw_initial(WINDOW *w, int y, int x, int width, int rows, const json_t *user)
{
    text_buffer name;
    if (!text_decode(&name, display_name(user)))
        text_decode(&name, "?");
    size_t first = 0;
    while (first < name.length && name.data[first] <= ' ')
        ++first;
    name.data[0] = first < name.length ? name.data[first] : '?';
    name.length = 1;
    name.data[1] = 0;
    char initial[TEXT_UTF8_BYTES];
    if (text_encode(&name, initial)) {
        int columns = text_label(NULL, 0, 0, width, initial);
        text_label(w, y + (rows - 1) / 2, x + MAX(0, width - columns) / 2, columns, initial);
    }
}
static const char *const statuses[] = {"Online", "Ausente", "Ocupado", "Offline", "Desconhecido"};
static const unsigned status_colors[] = {0x12ecc71, 0x1f1c40f, 0x1e74c3c, 0x195a5a6, 0x195a5a6};
static unsigned status_index(view *ui, const char *id)
{
    const char *state = inbox_presence(history_inbox(ui->model), id);
    return !strcmp(state, "online")    ? 0
           : !strcmp(state, "away")    ? 1
           : !strcmp(state, "busy")    ? 2
           : !strcmp(state, "offline") ? 3
                                       : 4;
}
static void row_free(gpointer pointer)
{
    member_row *r = pointer;
    json_decref(r->user);
    g_free(r->group);
    g_free(r->sort);
    free(r);
}
static struct member_drawer *drawer(view *ui)
{
    if (!ui->members) {
        ui->members = calloc(1, sizeof(*ui->members));
        if (!ui->members)
            return NULL;
        ui->members->rows = g_ptr_array_new_with_free_func(row_free);
        ui->members->by_status = ui->connections->config->members_by_status;
        text_decode(&ui->members->query, history_member_view_query(ui->model));
        snprintf(ui->members->selected, sizeof(ui->members->selected), "%s",
                 history_member_view_selected(ui->model));
        ui->members->dirty = true;
    }
    return ui->members;
}
static gint compare_rows(gconstpointer a, gconstpointer b)
{
    const member_row *left = *(member_row *const *)a, *right = *(member_row *const *)b;
    int result = strcmp(left->group_id, right->group_id);
    if (result) {
        if (left->group_id[0] == 's' && right->group_id[0] == 's')
            return result;
        if (!*left->group_id || !*right->group_id)
            return !*left->group_id ? 1 : -1;
        result = g_utf8_collate(left->group, right->group);
        return result ? result : strcmp(left->group_id, right->group_id);
    }
    result = g_utf8_collate(left->sort, right->sort);
    if (!result)
        result = strcmp(s(left->user, "username"), s(right->user, "username"));
    return result ? result : strcmp(s(left->user, "id"), s(right->user, "id"));
}
static bool refresh_rows(view *ui)
{
    struct member_drawer *d = ui->members;
    uint64_t users = history_users_revision(ui->model),
             presence = inbox_revision(history_inbox(ui->model)),
             permission = permissions_revision(
                 auth_permissions(ui->connections->items[ui->active_tab].auth));
    if (!d->dirty && users == d->users && presence == d->presence && permission == d->permissions)
        return false;
    d->dirty = false;
    d->users = users;
    d->presence = presence;
    d->permissions = permission;
    char query[TEXT_UTF8_BYTES];
    text_encode(&d->query, query);
    char *needle = history_search_normalize(query);
    g_ptr_array_set_size(d->rows, 0);
    for (size_t i = 0; i < history_user_count(ui->model) && i < 10000; ++i) {
        json_t *u = history_user(ui->model, i);
        if (!history_uuid(s(u, "id")))
            continue;
        char *name = history_search_normalize(display_name(u)),
             *username = history_search_normalize(s(u, "username"));
        bool matches = needle && name && username &&
                       (!*needle || strstr(name, needle) || strstr(username, needle));
        g_free(name);
        g_free(username);
        if (!matches)
            continue;
        member_row *r = calloc(1, sizeof(*r));
        if (!r)
            break;
        r->user = json_incref(u);
        r->sort = g_utf8_casefold(display_name(u), -1);
        r->status = status_index(ui, s(u, "id"));
        if (d->by_status) {
            snprintf(r->group_id, sizeof(r->group_id), "s%u", r->status);
            r->group = g_strdup(statuses[r->status]);
        } else {
            json_t *role = json_array_get(json_object_get(u, "roles"), 0);
            const json_t *info = permissions_role_info(
                auth_permissions(ui->connections->items[ui->active_tab].auth), s(role, "id"));
            snprintf(r->group_id, sizeof(r->group_id), "%s", s(role, "id"));
            r->group = g_strdup(*s(info, "name")   ? s(info, "name")
                                : *s(role, "name") ? s(role, "name")
                                : *r->group_id     ? "Cargo indisponível"
                                                   : "Sem cargo");
        }
        if (!r->sort || !r->group) {
            row_free(r);
            break;
        }
        g_ptr_array_add(d->rows, r);
    }
    g_free(needle);
    g_ptr_array_sort(d->rows, compare_rows);
    const char *goal = *d->restore ? d->restore : d->selected;
    int found = -1;
    for (guint i = 0; i < d->rows->len; ++i)
        if (!strcmp(s(((member_row *)g_ptr_array_index(d->rows, i))->user, "id"), goal))
            found = (int)i;
    if (found >= 0 || history_users_complete(ui->model))
        d->restore[0] = 0;
    d->cursor = found >= 0 ? found : CLAMP(d->cursor, 0, MAX(0, (int)d->rows->len - 1));
    if (d->rows->len)
        snprintf(d->selected, sizeof(d->selected), "%s",
                 s(((member_row *)g_ptr_array_index(d->rows, d->cursor))->user, "id"));
    d->first = CLAMP(d->first, 0, MAX(0, (int)d->rows->len - 1));
    return true;
}
void conversation_members_toggle(view *ui)
{
    struct member_drawer *d = drawer(ui);
    if (!d)
        return;
    ui->members_open = !ui->members_open;
    history_members_active(ui->model, ui->members_open);
    if (ui->members_open) {
        d->return_focus = ui->focus;
        d->focus = 2;
        d->dirty = true;
        d->generation = auth_context_generation(ui->connections->items[ui->active_tab].auth);
        mention_popup_close(ui->mentions);
        emoji_picker_close(ui->emojis);
        conversation_interactions_close(ui);
        r8_modal_close(ui->r8);
        snprintf(d->restore, sizeof(d->restore), "%s", d->selected);
        history_users_refresh(ui->model);
        history_member_prepare(ui->model);
        history_users_active(ui->model, false);
        conversation_set_focus(ui, MEMBERS_PANEL);
    } else {
        d->panel = MEMBER_NO_MODAL;
        d->opening_dm = false;
        history_member_close(ui->model);
        conversation_set_focus(ui, d->return_focus);
    }
    conversation_layout(ui);
}
void conversation_members_destroy(view *ui)
{
    struct member_drawer *d = ui->members;
    if (!d)
        return;
    char query[TEXT_UTF8_BYTES];
    if (d->generation == auth_context_generation(ui->connections->items[ui->active_tab].auth) &&
        text_encode(&d->query, query))
        history_member_view_save(ui->model, query, d->selected);
    history_members_active(ui->model, false);
    history_member_close(ui->model);
    if (d->window)
        delwin(d->window);
    if (d->modal)
        delwin(d->modal);
    g_ptr_array_free(d->rows, true);
    memset(d, 0, sizeof(*d));
    free(d);
    ui->members = NULL;
}
bool conversation_members_modal(view *ui)
{
    return ui->members_open && ui->members && ui->members->panel != MEMBER_NO_MODAL;
}
bool conversation_members_poll(view *ui)
{
    if (!ui->members_open)
        return false;
    struct member_drawer *d = ui->members;
    if (d->generation != auth_context_generation(ui->connections->items[ui->active_tab].auth)) {
        conversation_members_toggle(ui);
        return true;
    }
    bool changed = refresh_rows(ui);
    if (*history_member_reset_url(ui->model) && d->panel != MEMBER_LINK) {
        d->panel = MEMBER_LINK;
        changed = true;
    }
    if (d->opening_dm && !inbox_opening(history_inbox(ui->model))) {
        const char *opened = inbox_opened(history_inbox(ui->model));
        if (*opened) {
            d->panel = MEMBER_NO_MODAL;
            ui->focus = MESSAGE_INPUT;
        }
        d->opening_dm = false;
        snprintf(d->notice, sizeof(d->notice), "%s",
                 *opened ? "" : inbox_notice(history_inbox(ui->model)));
        changed = true;
    }
    return changed;
}
static void select_row(struct member_drawer *d, int index)
{
    d->restore[0] = 0;
    d->cursor = CLAMP(index, 0, MAX(0, (int)d->rows->len - 1));
    if (d->rows->len)
        snprintf(d->selected, sizeof(d->selected), "%s",
                 s(((member_row *)g_ptr_array_index(d->rows, d->cursor))->user, "id"));
    d->focus = 2;
}
static void open_menu(view *ui, int y)
{
    struct member_drawer *d = ui->members;
    if (!d->rows->len || !*d->selected || !history_member_menu(ui->model, d->selected))
        return;
    d->restore[0] = 0;
    conversation_interactions_close(ui);
    snprintf(d->target, sizeof(d->target), "%s", d->selected);
    d->panel = MEMBER_MENU;
    d->menu = 0;
    if (y < 0) {
        y = CONTENT_Y + 6;
        for (int i = 0; i < d->hit_count; ++i)
            if (d->hit_index[i] == d->cursor) {
                y = getbegy(d->window) + d->hit_first[i];
                break;
            }
    }
    d->menu_y = y;
    d->notice[0] = 0;
}
static void open_profile(view *ui)
{
    struct member_drawer *d = ui->members;
    conversation_interactions_close(ui);
    if (history_member_profile_open(ui->model, d->target)) {
        d->panel = MEMBER_CARD;
        d->scroll = 0;
        d->notice[0] = 0;
    }
}
static int menu_items(view *ui, int items[5], const char *labels[5])
{
    struct member_drawer *d = ui->members;
    int count = 0;
    auth_client *auth = ui->connections->items[ui->active_tab].auth;
    if (strcmp(d->target, auth_user_id(auth)) && !history_member_ready(ui->model) &&
        history_member_busy(ui->model)) {
        d->menu = 0;
        return 0;
    }
    items[count] = 0;
    labels[count++] = "Ver perfil";
    if (!strcmp(d->target, auth_user_id(auth)))
        return count;
    items[count] = 1;
    labels[count++] = "Mensagem";
    if (history_member_ready(ui->model)) {
        items[count] = 2;
        labels[count++] = history_member_blocked(ui->model) ? "Desbloquear" : "Bloquear";
        if (history_member_can_ban(ui->model)) {
            items[count] = 3;
            labels[count++] = history_member_banned(ui->model) ? "Desbanir" : "Banir";
        }
        if (history_member_admin(ui->model)) {
            items[count] = 4;
            labels[count++] = "Gerar link de reset de senha";
        }
    }
    d->menu = CLAMP(d->menu, 0, count - 1);
    return count;
}
static void activate_menu(view *ui)
{
    struct member_drawer *d = ui->members;
    int items[5];
    const char *labels[5];
    int count = menu_items(ui, items, labels);
    if (d->menu < 0 || d->menu >= count || (items[d->menu] >= 2 && history_member_busy(ui->model)))
        return;
    switch (items[d->menu]) {
    case 0:
        open_profile(ui);
        break;
    case 1:
        if (inbox_open_dm(history_inbox(ui->model), d->target)) {
            d->opening_dm = true;
            snprintf(d->notice, sizeof(d->notice), "Abrindo conversa…");
        }
        break;
    case 2:
        history_member_change(ui->model,
                              history_member_blocked(ui->model) ? MEMBER_UNBLOCK : MEMBER_BLOCK);
        break;
    case 3:
        d->confirmation = history_member_banned(ui->model) ? MEMBER_UNBAN : MEMBER_BAN;
        d->accept = false;
        d->panel = MEMBER_CONFIRM;
        break;
    case 4:
        d->confirmation = MEMBER_RESET;
        d->accept = false;
        d->panel = MEMBER_CONFIRM;
        break;
    }
}
static void change_group(view *ui)
{
    struct member_drawer *d = ui->members;
    bool next = !d->by_status;
    if (profiles_set_member_grouping(ui->connections->config, next)) {
        d->by_status = next;
        d->dirty = true;
        d->first = 0;
        d->notice[0] = 0;
    } else
        snprintf(d->notice, sizeof(d->notice), "%s", ui->connections->config->message);
}
static void close_modal(view *ui)
{
    struct member_drawer *d = ui->members;
    d->panel = MEMBER_NO_MODAL;
    d->notice[0] = 0;
    history_member_close(ui->model);
}
static void retry_profile_media(view *ui)
{
    if (app_mode_get() == APP_MODE_ULTRALIGHT)
        return;
    media_cache *cache = media_view_cache(ui->media);
    json_t *profile = history_member_profile(ui->model);
    media_resource *banner =
        media_cache_acquire(cache, AUTH_MEDIA_CONTENT, s(profile, "banner_media"), 0, false);
    media_resource_retry(banner);
    media_resource_release(banner);
    const author_avatar *avatar =
        chat_assets_avatar(history_assets(ui->model), ui->members->target);
    if (avatar && avatar->path) {
        media_resource *image = media_cache_acquire_file(cache, avatar->path, avatar->revision);
        media_resource_retry(image);
        media_resource_release(image);
    }
}
bool conversation_members_key(view *ui, int type, uint32_t key)
{
    if (type == KEY_CODE_YES && key == KEY_F(10)) {
        conversation_members_toggle(ui);
        return true;
    }
    if (r8_modal_is_open(ui->r8))
        return false;
    if (ui->focus == MEMBERS_BUTTON && ((type == OK && (key == '\n' || key == '\r')) ||
                                        (type == KEY_CODE_YES && key == KEY_ENTER))) {
        conversation_members_toggle(ui);
        return true;
    }
    if (type == KEY_CODE_YES && !conversation_members_modal(ui) &&
        (key == KEY_F(1) || key == KEY_F(3) || key == KEY_F(8) || key == KEY_F(9)))
        return false;
    if (!ui->members_open || (ui->focus != MEMBERS_PANEL && !conversation_members_modal(ui)) ||
        (type == KEY_CODE_YES && key == KEY_MOUSE))
        return false;
    struct member_drawer *d = ui->members;
    bool enter =
        (type == OK && (key == '\r' || key == '\n')) || (type == KEY_CODE_YES && key == KEY_ENTER);
    if (type == OK && key == 27) {
        if (d->panel != MEMBER_NO_MODAL)
            close_modal(ui);
        else
            conversation_members_toggle(ui);
        return true;
    }
    if (type == OK && key == 18) {
        auth_permissions_refresh(ui->connections->items[ui->active_tab].auth);
        if (d->panel == MEMBER_CARD) {
            retry_profile_media(ui);
            open_profile(ui);
        } else if (d->panel != MEMBER_NO_MODAL) {
            history_member_refresh(ui->model);
            history_member_menu(ui->model, d->target);
        } else {
            history_member_prepare(ui->model);
            snprintf(d->restore, sizeof(d->restore), "%s", d->selected);
            history_users_refresh(ui->model);
            history_users_active(ui->model, false);
            history_members_active(ui->model, true);
            chat_assets_profiles_retry(history_assets(ui->model));
        }
        return true;
    }
    if (d->panel == MEMBER_MENU) {
        int items[5];
        const char *labels[5];
        int count = menu_items(ui, items, labels);
        if (type == KEY_CODE_YES && key == KEY_UP)
            d->menu = MAX(0, d->menu - 1);
        else if (type == KEY_CODE_YES && key == KEY_DOWN)
            d->menu = MIN(MAX(0, count - 1), d->menu + 1);
        else if (enter)
            activate_menu(ui);
        else if (type == OK && key == 'm')
            close_modal(ui);
        return true;
    }
    if (d->panel == MEMBER_CONFIRM) {
        if ((type == OK && key == '\t') || (type == KEY_CODE_YES && key == KEY_BTAB))
            d->accept = !d->accept;
        else if (type == KEY_CODE_YES && (key == KEY_LEFT || key == KEY_RIGHT))
            d->accept = key == KEY_RIGHT;
        else if (enter) {
            if (d->accept)
                history_member_change(ui->model, d->confirmation);
            d->panel = MEMBER_MENU;
        }
        return true;
    }
    if (d->panel == MEMBER_LINK) {
        if (enter || (type == OK && key == 'c')) {
            text_buffer link;
            bool copied = false;
            if (text_decode(&link, history_member_reset_url(ui->model))) {
                link.anchor = 0;
                link.cursor = link.length;
                link.selecting = true;
                copied = clipboard_copy(&link);
            }
            memset(&link, 0, sizeof(link));
            snprintf(d->notice, sizeof(d->notice),
                     copied ? "Cópia solicitada." : "Não foi possível copiar o link.");
        }
        return true;
    }
    if (d->panel == MEMBER_CARD) {
        if (type == KEY_CODE_YES && (key == KEY_DOWN || key == KEY_NPAGE))
            d->scroll += key == KEY_DOWN ? 1 : 8;
        if (type == KEY_CODE_YES && (key == KEY_UP || key == KEY_PPAGE))
            d->scroll = MAX(0, d->scroll - (key == KEY_UP ? 1 : 8));
        return true;
    }
    if ((type == OK && key == '\t') || (type == KEY_CODE_YES && key == KEY_BTAB)) {
        if (type == OK && d->focus == 2)
            return false; /* Continue through the conversation's normal Tab order. */
        if (type == KEY_CODE_YES && d->focus == 0) {
            conversation_set_focus(ui, MEMBERS_BUTTON);
            return true;
        }
        d->focus = (d->focus + (type == OK ? 1 : 2)) % 3;
        return true;
    }
    if (type == KEY_CODE_YES && (key == KEY_UP || key == KEY_DOWN)) {
        if (d->focus == 2 && key == KEY_UP && d->cursor == 0)
            d->focus = 1;
        else if (d->focus < 2)
            d->focus = CLAMP(d->focus + (key == KEY_DOWN ? 1 : -1), 0, 2);
        else
            select_row(d, d->cursor + (key == KEY_DOWN ? 1 : -1));
        return true;
    }
    if (type == OK && key == '/' && d->focus != 0) {
        d->focus = 0;
        return true;
    }
    if (type == KEY_CODE_YES && key == KEY_LEFT && d->focus == 2) {
        if (!ui->members_reserved)
            conversation_members_toggle(ui);
        conversation_set_focus(ui, MEMBERS_BUTTON);
        return true;
    }
    if (d->focus == 0) {
        if (enter)
            d->focus = 2;
        else if (text_edit(&d->query, type, key, 24, false)) {
            d->restore[0] = 0;
            d->dirty = true;
            d->first = 0;
        }
        return true;
    }
    if (d->focus == 1) {
        if (enter || (type == KEY_CODE_YES && (key == KEY_LEFT || key == KEY_RIGHT)))
            change_group(ui);
        return true;
    }
    if (type == OK && key == '/')
        d->focus = 0;
    else if (type == OK && key == 'm')
        open_menu(ui, -1);
    else if (enter && d->rows->len && *d->selected) {
        d->restore[0] = 0;
        snprintf(d->target, sizeof(d->target), "%s", d->selected);
        open_profile(ui);
    } else if (type == KEY_CODE_YES && key == KEY_HOME)
        select_row(d, 0);
    else if (type == KEY_CODE_YES && key == KEY_END)
        select_row(d, (int)d->rows->len - 1);
    else if (type == KEY_CODE_YES && (key == KEY_NPAGE || key == KEY_PPAGE))
        select_row(d, d->cursor + (key == KEY_NPAGE ? 5 : -5));
    return true;
}
static bool inside(WINDOW *w, int y, int x)
{
    return w && y >= getbegy(w) && y < getbegy(w) + getmaxy(w) && x >= getbegx(w) &&
           x < getbegx(w) + getmaxx(w);
}
bool conversation_members_mouse(view *ui, const MEVENT *event)
{
    bool left = event->bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED);
    bool right = event->bstate & (BUTTON3_PRESSED | BUTTON3_CLICKED);
    if (ui->body && event->y == getbegy(ui->body) && left &&
        conversation_hit(ui->members_hit, event->x - getbegx(ui->body))) {
        conversation_members_toggle(ui);
        return true;
    }
    if (!ui->members_open)
        return false;
    struct member_drawer *d = ui->members;
    if (d->panel != MEMBER_NO_MODAL) {
        if (!inside(d->modal, event->y, event->x)) {
            if (left || right)
                close_modal(ui);
            return true;
        }
        int y = event->y - getbegy(d->modal), x = event->x - getbegx(d->modal);
        if (d->panel == MEMBER_MENU && left && y >= 3) {
            int items[5];
            const char *labels[5];
            int count = menu_items(ui, items, labels);
            if (y - 3 < count) {
                d->menu = y - 3;
                activate_menu(ui);
            }
        } else if (d->panel == MEMBER_CONFIRM && left && y == getmaxy(d->modal) - 2) {
            d->accept = x >= getmaxx(d->modal) / 2;
            conversation_members_key(ui, OK, '\n');
        } else if (d->panel == MEMBER_LINK && left && y == getmaxy(d->modal) - 2)
            conversation_members_key(ui, OK, '\n');
        else if (d->panel == MEMBER_CARD && (event->bstate & (BUTTON4_PRESSED | BUTTON5_PRESSED)))
            conversation_members_key(ui, KEY_CODE_YES,
                                     event->bstate & BUTTON4_PRESSED ? KEY_UP : KEY_DOWN);
        return true;
    }
    if (!inside(d->window, event->y, event->x))
        return false;
    ui->focus = MEMBERS_PANEL;
    int y = event->y - getbegy(d->window);
    if (event->bstate & (BUTTON4_PRESSED | BUTTON5_PRESSED))
        select_row(d, d->cursor + (event->bstate & BUTTON4_PRESSED ? -3 : 3));
    else if (left && y == 2)
        d->focus = 0;
    else if (left && y == 4) {
        d->focus = 1;
        change_group(ui);
    } else if (left || right) {
        for (int i = 0; i < d->hit_count; ++i)
            if (y >= d->hit_first[i] && y < d->hit_last[i]) {
                select_row(d, d->hit_index[i]);
                if (right)
                    open_menu(ui, event->y);
                break;
            }
    }
    return true;
}
static WINDOW *panel_window(WINDOW **slot, int height, int width, int y, int x)
{
    if (*slot)
        delwin(*slot);
    *slot = newwin(height, width, y, x);
    if (*slot) {
        wbkgd(*slot, COLOR_PAIR(THEME_PANEL));
        werase(*slot);
        wattrset(*slot, COLOR_PAIR(THEME_BORDER));
        box(*slot, 0, 0);
    }
    return *slot;
}
static void member_overlay(view *ui, WINDOW *w)
{
    icons_overlay(ui->icons, getbegy(w), getbegx(w), getmaxy(w), getmaxx(w));
}
static void status_label(view *ui, WINDOW *w, const char *id, int y, int x, int width)
{
    unsigned status = status_index(ui, id);
    wattrset(w, COLOR_PAIR(theme_member_pair(status_colors[status], THEME_PANEL)));
    text_label(w, y, x, width, "●");
    wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
    text_label(w, y, x + 2, width - 2, statuses[status]);
}
static int wrapped(view *ui, WINDOW *w, const char *text, int y, int x, int width, int first,
                   int limit)
{
    text_buffer buffer;
    if (!text_decode(&buffer, text))
        return 0;
    text_layout *layout = &ui->measured;
    text_measure(&buffer, width, layout);
    int visible = MIN(limit, layout->rows - first);
    if (visible > 0)
        text_draw(w, &buffer, layout, first, visible, y, x);
    return layout->rows;
}
static unsigned role_color(const json_t *role)
{
    const char *color = s(role, "color");
    if (strlen(color) != 7 || *color != '#')
        return 0x1ffffff;
    unsigned rgb = 0x1000000;
    for (int i = 1; i < 7; ++i) {
        int digit = g_ascii_xdigit_value(color[i]);
        if (digit < 0)
            return 0x1ffffff;
        rgb |= (unsigned)digit << ((6 - i) * 4);
    }
    return rgb;
}
static void draw_card(view *ui, WINDOW *w)
{
    struct member_drawer *d = ui->members;
    int width = getmaxx(w), height = getmaxy(w);
    json_t *profile = history_member_profile(ui->model);
    if (!profile) {
        text_label(w, 3, 2, width - 4, history_member_notice(ui->model));
        return;
    }
    bool pictures = app_mode_get() != APP_MODE_ULTRALIGHT;
    int banner_rows = pictures ? MIN(4, MAX(2, height / 6)) : 0;
    if (banner_rows) {
        for (int y = 2; y < 2 + banner_rows; ++y)
            mvwhline(w, y, 2, ' ', width - 4);
        const char *banner = s(profile, "banner_media");
        if (*banner) {
            icons_member_picture(ui->icons, banner, true, "member:banner", getbegy(w) + 2,
                                 getbegx(w) + 2, width - 4, banner_rows, THEME_PANEL);
            media_resource *resource = media_cache_acquire(media_view_cache(ui->media),
                                                           AUTH_MEDIA_CONTENT, banner, 0, false);
            if (media_resource_error(resource, false)) {
                wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
                text_label(w, 2, 2, width - 4, "Banner indisponível · Ctrl+R: tentar novamente");
            }
            media_resource_release(resource);
        }
    }
    int y = 2 + banner_rows, avatar_rows = pictures ? 3 : 0, name_x = pictures ? 12 : 2;
    if (pictures) {
        draw_initial(w, y, 2, 8, avatar_rows, profile);
        icons_member_picture(ui->icons, d->target, false, "member:profile-avatar", getbegy(w) + y,
                             getbegx(w) + 2, 8, avatar_rows, THEME_PANEL);
    }
    wattrset(
        w, COLOR_PAIR(theme_member_pair(history_member_color(ui->model, d->target), THEME_PANEL)) |
               A_BOLD);
    text_label(w, y, name_x, width - name_x - 2, display_name(profile));
    char username[1100];
    snprintf(username, sizeof(username), "@%s", s(profile, "username"));
    wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
    text_label(w, y + 1, name_x, width - name_x - 2, username);
    status_label(ui, w, d->target, y + 2, name_x, width - name_x - 2);
    y += MAX(3, avatar_rows) + 1;
    int available = height - y - 3;
    GString *body = g_string_new(NULL);
    const json_t *presence = inbox_presence_info(history_inbox(ui->model), d->target);
    const char *phrase = json_object_get(presence, "status_message") ? s(presence, "status_message")
                                                                     : s(profile, "status_message");
    if (*phrase)
        g_string_append_printf(body, "Status: %s\n\n", phrase);
    if (*s(profile, "description"))
        g_string_append_printf(body, "%s\n\n", s(profile, "description"));
    g_string_append(body, "Cargos\n");
    size_t i;
    json_t *role;
    json_t *user_roles = json_object_get(profile, "roles");
    /* Live directory role membership supersedes the profile read when available. */
    for (size_t k = 0; k < history_user_count(ui->model); ++k) {
        json_t *u = history_user(ui->model, k);
        if (!strcmp(s(u, "id"), d->target)) {
            user_roles = json_object_get(u, "roles");
            break;
        }
    }
    int total = wrapped(ui, w, body->str, y, 2, width - 4, 0, 0);
    int role_count = (int)json_array_size(user_roles);
    d->scroll = CLAMP(d->scroll, 0, MAX(0, total + MAX(1, role_count) - available));
    wattrset(w, COLOR_PAIR(THEME_PANEL));
    wrapped(ui, w, body->str, y, 2, width - 4, d->scroll, available);
    int row_y = y + total - d->scroll;
    if (!role_count && row_y >= y && row_y < y + available)
        text_label(w, row_y, 2, width - 4, "Sem cargo");
    json_array_foreach(user_roles, i, role)
    {
        int line = row_y + (int)i;
        if (line < y || line >= y + available)
            continue;
        const json_t *info = permissions_role_info(
            auth_permissions(ui->connections->items[ui->active_tab].auth), s(role, "id"));
        if (!info)
            info = role;
        wattrset(w, COLOR_PAIR(theme_member_pair(role_color(info), THEME_PANEL)));
        char label[1100];
        snprintf(label, sizeof(label), "● %s",
                 *s(info, "name") ? s(info, "name") : "Cargo indisponível");
        text_label(w, line, 2, width - 4, label);
    }
    g_string_free(body, true);
    wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
    text_label(w, height - 2, 2, width - 4, "↑↓ / roda: rolar · Ctrl+R: atualizar · Esc: voltar");
}
void conversation_members_draw(view *ui)
{
    if (!ui->members_open || ui->settings.open || media_view_modal(ui->media) ||
        ui->duplicate_modal || COLS < 44 || LINES < 14)
        return;
    struct member_drawer *d = ui->members;
    refresh_rows(ui);
    int width = 30, height = LINES - CONTENT_Y - 3;
    WINDOW *w = panel_window(&d->window, height, width, CONTENT_Y, COLS - width - 1);
    if (!w)
        return;
    member_overlay(ui, w);
    wattrset(w, COLOR_PAIR(THEME_PANEL_ACCENT) | A_BOLD);
    char title[80];
    snprintf(title, sizeof(title), "Membros (%u) · F10", d->rows->len);
    text_label(w, 0, 2, width - 4, title);
    wattrset(w,
             COLOR_PAIR(d->focus == 0 && ui->focus == MEMBERS_PANEL ? THEME_FOCUSED : THEME_INPUT));
    text_label(w, 1, 2, width - 4, "Buscar nickname / @username");
    text_draw_field(w, &d->query, &d->field_first, 2, 2, width - 4, false);
    button_draw(w, 4, 2, width - 4, d->by_status ? "Agrupar: Status ↔" : "Agrupar: Cargo ↔",
                d->focus == 1 && ui->focus == MEMBERS_PANEL ? BUTTON_FOCUSED : BUTTON_NORMAL,
                BUTTON_PANEL);
    graphics_terminal terminal = graphics_terminal_get();
    int image_rows =
        terminal.supported && terminal.cell_height > 0 && app_mode_get() != APP_MODE_ULTRALIGHT
            ? MAX(1, (32 + terminal.cell_height - 1) / terminal.cell_height)
            : 1;
    int image_columns =
        terminal.cell_width > 0 ? MAX(2, (32 + terminal.cell_width - 1) / terminal.cell_width) : 4;
    image_columns = MIN(image_columns, 6);
    int member_height = MAX(2, image_rows), end = height - 3;
    d->first = MIN(d->first, d->cursor);
    /* Keep the selected member visible, including intervening group headings. */
    for (;;) {
        int needed = 0;
        const char *group = NULL;
        for (int i = d->first; i <= d->cursor && i < (int)d->rows->len; ++i) {
            member_row *r = g_ptr_array_index(d->rows, i);
            if (!group || strcmp(group, r->group_id))
                ++needed;
            needed += member_height;
            group = r->group_id;
        }
        if (needed <= end - 6 || d->first >= d->cursor)
            break;
        ++d->first;
    }
    int y = 6;
    d->hit_count = 0;
    const char *group = NULL;
    for (int i = d->first; i < (int)d->rows->len && y < end; ++i) {
        member_row *r = g_ptr_array_index(d->rows, i);
        if (!group || strcmp(group, r->group_id)) {
            wattrset(w, COLOR_PAIR(THEME_PANEL_ACCENT) | A_BOLD);
            text_label(w, y++, 2, width - 4, r->group);
            group = r->group_id;
        }
        if (y + member_height > end)
            break;
        int pair = i == d->cursor && ui->focus == MEMBERS_PANEL && d->focus == 2 ? THEME_FOCUS_AREA
                                                                                 : THEME_PANEL;
        wattrset(w, COLOR_PAIR(pair));
        for (int k = 0; k < member_height; ++k)
            mvwhline(w, y + k, 1, ' ', width - 2);
        draw_initial(w, y, 2, image_columns, member_height, r->user);
        char scope[64];
        snprintf(scope, sizeof(scope), "member:%s", s(r->user, "id"));
        icons_member_picture(ui->icons, s(r->user, "id"), false, scope, getbegy(w) + y,
                             getbegx(w) + 2, image_columns, image_rows, pair);
        wattrset(w, COLOR_PAIR(theme_member_pair(history_member_color(ui->model, s(r->user, "id")),
                                                 pair)));
        text_label(w, y, image_columns + 3, width - image_columns - 7, display_name(r->user));
        wattrset(w, COLOR_PAIR(theme_member_pair(status_colors[r->status], pair)));
        text_label(w, y, width - 3, 2, "●");
        wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
        text_label(w, y + 1, image_columns + 3, width - image_columns - 5,
                   json_is_true(json_object_get(r->user, "banned")) ? "Banido"
                                                                    : statuses[r->status]);
        if (d->hit_count < 256) {
            int n = d->hit_count++;
            d->hit_first[n] = y;
            d->hit_last[n] = y + member_height;
            d->hit_index[n] = i;
        }
        y += member_height;
    }
    if (!d->rows->len) {
        wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
        text_label(w, 6, 2, width - 4, "Nenhum membro encontrado.");
    }
    wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
    const char *users_notice = history_users_notice(ui->model);
    text_label(w, height - 3, 2, width - 4, !strncmp(users_notice, "Tab /", 5) ? "" : users_notice);
    text_label(w, height - 2, 2, width - 4, "m / botão direito: opções");
    wnoutrefresh(w);
    if (d->panel == MEMBER_NO_MODAL)
        return;
    int items[5];
    const char *labels[5];
    int count = d->panel == MEMBER_MENU ? menu_items(ui, items, labels) : 0;
    const char *notice = *d->notice ? d->notice : history_member_notice(ui->model);
    int modal_width = d->panel == MEMBER_MENU ? MIN(COLS - 2, 42) : MIN(COLS - 2, 64);
    int modal_height = d->panel == MEMBER_CARD   ? MIN(LINES - 2, 28)
                       : d->panel == MEMBER_MENU ? MAX(1, count) + 4 + (*notice ? 1 : 0)
                                                 : 12;
    int modal_y = d->panel == MEMBER_MENU ? CLAMP(d->menu_y, 0, LINES - modal_height)
                                          : (LINES - modal_height) / 2;
    int modal_x = d->panel == MEMBER_MENU
                      ? CLAMP(getbegx(d->window) - modal_width - 1, 0, COLS - modal_width)
                      : (COLS - modal_width) / 2;
    if (d->panel == MEMBER_MENU) {
        int top = d->menu_y, bottom = top + 1;
        for (int i = 0; i < d->hit_count; ++i) {
            member_row *row = g_ptr_array_index(d->rows, d->hit_index[i]);
            if (!strcmp(s(row->user, "id"), d->target)) {
                top = getbegy(d->window) + d->hit_first[i];
                bottom = getbegy(d->window) + d->hit_last[i];
                break;
            }
        }
        if (modal_x + modal_width >= getbegx(d->window))
            modal_y = bottom + modal_height <= LINES ? bottom : MAX(0, top - modal_height);
        else
            modal_y = CLAMP(top, 0, LINES - modal_height);
        modal_y = CLAMP(modal_y, 0, LINES - modal_height);
    }
    w = panel_window(&d->modal, modal_height, modal_width, modal_y, modal_x);
    if (!w)
        return;
    member_overlay(ui, w);
    wattrset(w, COLOR_PAIR(THEME_PANEL_ACCENT) | A_BOLD);
    const char *target_name = history_author(ui->model, d->target);
    text_label(w, 1, 2, modal_width - 4,
               d->panel == MEMBER_CARD ? "Perfil do membro" : target_name);
    if (d->panel == MEMBER_CARD)
        draw_card(ui, w);
    else if (d->panel == MEMBER_MENU) {
        if (!count) {
            wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
            text_label(w, 3, 2, modal_width - 4, "Consultando opções…");
        }
        for (int i = 0; i < count; ++i) {
            wattrset(w, COLOR_PAIR(i == d->menu ? THEME_FOCUSED : THEME_PANEL));
            text_label(w, 3 + i, 2, modal_width - 4, labels[i]);
        }
        wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
        if (*notice)
            text_label(w, modal_height - 2, 2, modal_width - 4, notice);
    } else if (d->panel == MEMBER_CONFIRM) {
        wattrset(w, COLOR_PAIR(THEME_PANEL));
        const char *question =
            d->confirmation == MEMBER_BAN ? "Banir este membro de toda a instância?"
            : d->confirmation == MEMBER_UNBAN
                ? "Desbanir este membro?"
                : "Gerar link de reset de senha? O novo link invalida o anterior.";
        wrapped(ui, w, question, 3, 2, modal_width - 4, 0, 5);
        button_draw(w, modal_height - 2, 2, 14, "Cancelar",
                    !d->accept ? BUTTON_FOCUSED : BUTTON_NORMAL, BUTTON_PANEL);
        button_draw(w, modal_height - 2, modal_width - 17, 15, "Confirmar",
                    d->accept ? BUTTON_FOCUSED : BUTTON_NORMAL, BUTTON_PANEL);
    } else if (d->panel == MEMBER_LINK) {
        wattrset(w, COLOR_PAIR(THEME_PANEL));
        text_label(w, 2, 2, modal_width - 4, "Link de reset de senha (uso único)");
        wrapped(ui, w, history_member_reset_url(ui->model), 4, 2, modal_width - 4, 0, 3);
        text_label(w, 8, 2, modal_width - 4, history_member_reset_expires(ui->model));
        text_label(w, 9, 2, modal_width - 4, d->notice);
        button_draw(w, modal_height - 2, 2, 18, "Copiar · Enter", BUTTON_FOCUSED, BUTTON_PANEL);
    }
    wnoutrefresh(w);
}

bool conversation_members_cursor(view *ui)
{
    struct member_drawer *d = ui->members;
    if (!ui->members_open || !d || !d->window || ui->focus != MEMBERS_PANEL || d->focus != 0 ||
        d->panel != MEMBER_NO_MODAL || r8_modal_is_open(ui->r8) || ui->settings.open ||
        media_view_modal(ui->media) || emoji_picker_open(ui->emojis) ||
        file_picker_open(ui->files) || ui->remove_confirm || ui->duplicate_modal ||
        ui->context_open || ui->reactors_open)
        return false;
    wattrset(d->window, COLOR_PAIR(THEME_INPUT));
    text_draw_field(d->window, &d->query, &d->field_first, 2, 2, getmaxx(d->window) - 4, false);
    int y, x;
    getyx(d->window, y, x);
    wnoutrefresh(d->window);
    setsyx(getbegy(d->window) + y, getbegx(d->window) + x);
    return true;
}
