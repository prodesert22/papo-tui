#include "ui/screens/conversation/private.h"

/* All JSON below is borrowed from the active history for this event/frame. */
typedef struct {
    const json_t *emoji[20];
    json_int_t total[20];
    bool total_known[20];
    int groups, active, count;
    json_t *users[HISTORY_MESSAGE_LIMIT];
} reactor_page;

static int column_split(int width)
{
    return CLAMP(width / 3, 14, 26);
}
static int page_rows(view *ui)
{
    return MAX(1, (ui->context_window ? getmaxy(ui->context_window) : MIN(LINES - 2, 20)) - 6);
}
static void keep_visible(int cursor, int *first, int count, int rows)
{
    *first = CLAMP(*first, 0, MAX(0, count - rows));
    if (cursor < *first)
        *first = cursor;
    if (cursor >= *first + rows)
        *first = cursor - rows + 1;
}
static void select_emoji(view *ui, const json_t *emoji)
{
    const char *id = conversation_str(emoji, "emoji_id"),
               *unicode = conversation_str(emoji, "unicode");
    if (!strcmp(ui->reactor_emoji, id) && !strcmp(ui->reactor_unicode, unicode))
        return;
    snprintf(ui->reactor_emoji, sizeof(ui->reactor_emoji), "%s", id);
    snprintf(ui->reactor_unicode, sizeof(ui->reactor_unicode), "%s", unicode);
    ui->reactor_first[1] = ui->reactor_user = 0;
}
static int find_group(reactor_page *page, const json_t *emoji)
{
    for (int i = 0; i < page->groups; ++i)
        if (history_reaction_equal(page->emoji[i], emoji))
            return i;
    return -1;
}
static void build_page(view *ui, reactor_page *page)
{
    memset(page, 0, sizeof(*page));
    json_t *message = history_find_message(ui->model, ui->selected_message);
    size_t i;
    json_t *emoji, *user;
    json_array_foreach(json_object_get(message, "reactions"), i, emoji)
    {
        json_t *total = json_object_get(emoji, "count");
        if (page->groups == 20 || !history_reaction_valid(emoji) || !json_is_integer(total) ||
            json_integer_value(total) <= 0 || find_group(page, emoji) >= 0)
            continue;
        page->emoji[page->groups] = emoji;
        page->total_known[page->groups] = true;
        page->total[page->groups++] = json_integer_value(total);
    }
    int loaded[20] = {0};
    json_array_foreach(history_reactors(ui->model), i, user)
    {
        int group = find_group(page, user);
        if (group < 0 && page->groups < 20) {
            group = page->groups++;
            page->emoji[group] = user;
        }
        if (group >= 0)
            ++loaded[group];
    }
    page->active = -1;
    for (int group = 0; group < page->groups; ++group) {
        if (!page->total[group])
            page->total[group] = loaded[group];
        if (!strcmp(ui->reactor_emoji, conversation_str(page->emoji[group], "emoji_id")) &&
            !strcmp(ui->reactor_unicode, conversation_str(page->emoji[group], "unicode")))
            page->active = group;
    }
    if (page->active < 0 && page->groups) {
        page->active = 0;
        select_emoji(ui, page->emoji[0]);
    }
    if (page->active >= 0)
        json_array_foreach(history_reactors(ui->model), i,
                           user) if (history_reaction_equal(page->emoji[page->active], user) &&
                                     page->count < HISTORY_MESSAGE_LIMIT)
            page->users[page->count++] = user;
    ui->reactor_cursor = CLAMP(ui->reactor_cursor, 0, MAX(0, page->groups - 1));
    ui->reactor_user = CLAMP(ui->reactor_user, 0, MAX(0, page->count - 1));
    keep_visible(ui->reactor_cursor, &ui->reactor_first[0], page->groups, page_rows(ui));
    keep_visible(ui->reactor_user, &ui->reactor_first[1], page->count, page_rows(ui));
}
void conversation_reactors_open(view *ui)
{
    history_reactors_open(ui->model, ui->selected_message);
    ui->reactors_open = true;
    ui->reactor_users_focus = false;
    ui->reactor_cursor = ui->reactor_user = 0;
    ui->reactor_first[0] = ui->reactor_first[1] = 0;
    ui->reactor_emoji[0] = ui->reactor_unicode[0] = 0;
}
static void activate(view *ui, const reactor_page *page)
{
    if (page->groups)
        select_emoji(ui, page->emoji[ui->reactor_cursor]);
}
static bool missing_users(const reactor_page *page)
{
    return page->active >= 0 &&
           (!page->total_known[page->active] || page->count < page->total[page->active]);
}
static void move_cursor(view *ui, const reactor_page *page, int delta, bool wheel)
{
    bool right = ui->reactor_users_focus;
    int count = right ? page->count : page->groups;
    int *cursor = right ? &ui->reactor_user : &ui->reactor_cursor;
    int *first = &ui->reactor_first[right ? 1 : 0];
    int rows = page_rows(ui);
    if (wheel) {
        *first = CLAMP(*first + delta, 0, MAX(0, count - rows));
        *cursor = CLAMP(*cursor, *first, MAX(*first, MIN(count - 1, *first + rows - 1)));
    } else
        *cursor = CLAMP(*cursor + delta, 0, MAX(0, count - 1));
    keep_visible(*cursor, first, count, rows);
    if (right && delta > 0 && *cursor + rows >= count && missing_users(page))
        history_reactors_more(ui->model);
}
void conversation_reactors_key(view *ui, int type, uint32_t key)
{
    reactor_page page;
    build_page(ui, &page);
    if (type == OK && key == 18)
        history_interactions_retry(ui->model);
    if (type == KEY_CODE_YES && key == KEY_LEFT)
        ui->reactor_users_focus = false;
    if ((type == KEY_CODE_YES && (key == KEY_RIGHT || key == KEY_ENTER)) ||
        (type == OK && (key == '\r' || key == '\n'))) {
        if (!ui->reactor_users_focus)
            activate(ui, &page);
        ui->reactor_users_focus = true;
    }
    if (type == KEY_CODE_YES &&
        (key == KEY_UP || key == KEY_DOWN || key == KEY_PPAGE || key == KEY_NPAGE))
        move_cursor(ui, &page,
                    key == KEY_UP      ? -1
                    : key == KEY_DOWN  ? 1
                    : key == KEY_PPAGE ? -page_rows(ui)
                                       : page_rows(ui),
                    false);
}
void conversation_reactors_mouse(view *ui, const MEVENT *event)
{
    WINDOW *w = ui->context_window;
    if (!w)
        return;
    int x = event->x - getbegx(w), y = event->y - getbegy(w);
    int split = column_split(getmaxx(w));
    reactor_page page;
    build_page(ui, &page);
    if (event->bstate & (BUTTON4_PRESSED | BUTTON5_PRESSED)) {
        ui->reactor_users_focus = x > split;
        move_cursor(ui, &page, event->bstate & BUTTON4_PRESSED ? -3 : 3, true);
    } else if ((event->bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED)) && y >= 3 &&
               y < getmaxy(w) - 3) {
        bool right = x > split;
        int index = ui->reactor_first[right ? 1 : 0] + y - 3;
        if (index < (right ? page.count : page.groups)) {
            ui->reactor_users_focus = right;
            if (right)
                ui->reactor_user = index;
            else {
                ui->reactor_cursor = index;
                activate(ui, &page);
            }
        }
    }
}
static void scrollbar(WINDOW *w, int x, int rows, int first, int count)
{
    if (rows <= 0 || count <= rows)
        return;
    int thumb = MAX(1, rows * rows / count);
    int top = first * (rows - thumb) / (count - rows);
    wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
    mvwvline(w, 3, x, ACS_VLINE, rows);
    wattrset(w, COLOR_PAIR(THEME_PANEL_ACCENT));
    mvwvline(w, 3 + top, x, ACS_CKBOARD, thumb);
}
static void position(WINDOW *w, int x, int width, int first, int count, int rows)
{
    char label[64];
    snprintf(label, sizeof(label), "%d–%d/%d", count ? first + 1 : 0, MIN(count, first + rows),
             count);
    wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
    text_label(w, getmaxy(w) - 3, x, width, label);
}
void conversation_reactors_draw(view *ui, WINDOW *w)
{
    reactor_page page;
    build_page(ui, &page);
    int width = getmaxx(w), height = getmaxy(w), split = column_split(width), rows = height - 6;
    wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
    text_label(w, 0, 2, width - 4, " Quem reagiu ");
    mvwvline(w, 1, split, ACS_VLINE, height - 3);
    mvwhline(w, 2, 1, ACS_HLINE, width - 2);
    mvwaddch(w, 2, split, ACS_PLUS);
    wattrset(w,
             COLOR_PAIR(ui->reactor_users_focus ? THEME_PANEL_MUTED : THEME_PANEL_ACCENT) | A_BOLD);
    text_label(w, 1, 2, split - 4, "Reações");
    wattrset(w,
             COLOR_PAIR(ui->reactor_users_focus ? THEME_PANEL_ACCENT : THEME_PANEL_MUTED) | A_BOLD);
    text_label(w, 1, split + 2, width - split - 4, "Pessoas");
    icons_scope(ui->icons, "reaction-users", 0);
    for (int row = 0; row < rows; ++row) {
        int index = ui->reactor_first[0] + row;
        if (index >= page.groups)
            break;
        int pair = !ui->reactor_users_focus && index == ui->reactor_cursor ? THEME_PANEL_ACTIVE
                                                                           : THEME_PANEL;
        wattrset(w, COLOR_PAIR(pair));
        mvwhline(w, row + 3, 1, ' ', split - 2);
        mvwaddch(w, row + 3, 1, index == page.active ? '>' : ' ');
        char glyph[160], label[224];
        message_reaction_label(history_assets(ui->model), page.emoji[index], glyph, sizeof(glyph));
        snprintf(label, sizeof(label), "%s %lld", glyph, (long long)page.total[index]);
        text_buffer text;
        text_layout measured;
        if (!text_decode(&text, label))
            continue;
        text_icons(&text, icons_resolve, icons_paint, ui->icons);
        text_measure(&text, split - 4, &measured);
        icons_scope(ui->icons, "reaction-users", (size_t)index * (TEXT_LIMIT + 1));
        text_draw(w, &text, &measured, 0, 1, row + 3, 3);
    }
    for (int row = 0; row < rows; ++row) {
        int index = ui->reactor_first[1] + row;
        if (index >= page.count)
            break;
        int pair =
            ui->reactor_users_focus && index == ui->reactor_user ? THEME_PANEL_ACTIVE : THEME_PANEL;
        wattrset(
            w, COLOR_PAIR(theme_member_pair(
                   history_member_color(ui->model, conversation_str(page.users[index], "user_id")),
                   pair)));
        text_label(w, row + 3, split + 2, width - split - 4,
                   history_author(ui->model, conversation_str(page.users[index], "user_id")));
    }
    wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
    if (!page.groups)
        text_label(w, 3, 2, split - 4, "Sem reações");
    if (!page.count)
        text_label(w, 3, split + 2, width - split - 4, "Nenhuma pessoa carregada");
    scrollbar(w, split - 1, rows, ui->reactor_first[0], page.groups);
    scrollbar(w, width - 2, rows, ui->reactor_first[1], page.count);
    position(w, 2, split - 4, ui->reactor_first[0], page.groups, rows);
    position(w, split + 2, width - split - 4, ui->reactor_first[1], page.count, rows);
    wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
    text_label(w, height - 2, 1, width - 2, history_reactors_notice(ui->model));
    if (missing_users(&page) && (page.count < rows || ui->reactor_user + rows >= page.count))
        history_reactors_more(ui->model);
}
