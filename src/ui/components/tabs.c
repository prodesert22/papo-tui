#include "ui/components/tabs.h"
#include "ui/components/theme.h"
#include "ui/input/text.h"
#include <stdio.h>

/* Share of the background mixed into each of the last cells of a cut name,
 * from the first faded cell to the last, which is nearly gone. */
static const int fade_percent[] = {20, 44, 68, 92};

bool tab_blocks(void)
{
    /* Without colors, as with Contraste: Sem cores, the brackets mark the tab. */
    return theme_colors() && COLORS >= 256;
}
int tab_pair(tab_kind kind, bool cursor, bool strip_focused)
{
    if (cursor)
        return THEME_PROFILE_BUTTON_FOCUS;
    switch (kind) {
    case TAB_SELECTED:
        return THEME_PANEL_ACTIVE;
    case TAB_ADD:
        return strip_focused ? THEME_PANEL : THEME_FOCUS_AREA;
    default:
        return strip_focused ? THEME_FOCUS_MUTED : THEME_PANEL_MUTED;
    }
}
int tab_name_width(const char *name)
{
    return text_label(NULL, 0, 0, TAB_NAME_MAX, name);
}
int tab_name_draw(WINDOW *window, int row, int x, int width, const char *name, int pair)
{
    int pairs[sizeof(fade_percent) / sizeof(fade_percent[0])];
    for (size_t i = 0; i < sizeof(pairs) / sizeof(pairs[0]); ++i)
        pairs[i] = theme_faded_pair(pair, fade_percent[i]);
    return text_label_fade(window, row, x, width > TAB_NAME_MAX ? TAB_NAME_MAX : width, name, pairs,
                           (int)(sizeof(pairs) / sizeof(pairs[0])));
}

static int label_of(const char *const *labels, int index, int active, char *label, size_t size)
{
    if (index == active)
        snprintf(label, size, "< %s >", labels[index]);
    else
        snprintf(label, size, "%s", labels[index]);
    return text_label(NULL, 0, 0, (int)size, label);
}
static void visible_window(const char *const *labels, int count, int active, int available,
                           int *first, int *last)
{
    int best_first = active, best_last = active, best_count = 0, best_balance = count;
    char label[64];
    for (int from = 0; from <= active; ++from) {
        for (int to = active; to < count; ++to) {
            int used = from > 0 ? 2 : 0;
            for (int i = from; i <= to; ++i) {
                used += label_of(labels, i, active, label, sizeof(label));
                if (i > from)
                    used += 2;
            }
            if (to < count - 1)
                used += 2;
            if (used > available)
                continue;
            int shown = to - from + 1;
            int balance = (active - from) - (to - active);
            if (balance < 0)
                balance = -balance;
            if (shown > best_count || (shown == best_count && balance < best_balance)) {
                best_first = from;
                best_last = to;
                best_count = shown;
                best_balance = balance;
            }
        }
    }
    *first = best_first;
    *last = best_last;
}
void tab_strip_reset(tab_strip *strip)
{
    for (int i = 0; i < TAB_STRIP_MAX; ++i)
        strip->x[i] = strip->w[i] = 0;
    strip->prev = strip->next = -1;
}
void tab_strip_draw(tab_strip *strip, WINDOW *w, int row, int width, const char *const *labels,
                    int count, int active, bool focused)
{
    tab_strip_reset(strip);
    if (count <= 0 || count > TAB_STRIP_MAX || active < 0 || active >= count)
        return;
    int first, last, x = 2;
    char label[64];
    visible_window(labels, count, active, width - 4, &first, &last);
    if (first > 0) {
        wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
        text_label(w, row, x, 1, "‹");
        strip->prev = x;
        x += 2;
    }
    for (int i = first; i <= last; ++i) {
        if (i > first)
            x += 2;
        int label_width = label_of(labels, i, active, label, sizeof(label));
        strip->x[i] = x;
        strip->w[i] = label_width;
        wattrset(w, COLOR_PAIR(i == active
                                   ? (focused ? THEME_PROFILE_BUTTON_FOCUS : THEME_PANEL_ACCENT)
                                   : THEME_PANEL) |
                        (i == active ? A_BOLD : 0) |
                        (i == active && focused ? theme_focus_attributes() : 0));
        text_label(w, row, x, label_width, label);
        x += label_width;
    }
    if (last < count - 1) {
        wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
        text_label(w, row, x + 1, 1, "›");
        strip->next = x + 1;
    }
}
int tab_strip_hit(const tab_strip *strip, int count, int active, int x, int width)
{
    if (count <= 0)
        return -1;
    for (int i = 0; i < count && i < TAB_STRIP_MAX; ++i)
        if (strip->w[i] > 0 && x >= strip->x[i] && x < strip->x[i] + strip->w[i])
            return i;
    if (strip->prev >= 0 && x <= strip->prev + 1)
        return (active + count - 1) % count;
    if (strip->next >= 0 && x >= strip->next - 1)
        return (active + 1) % count;
    return (active + (x < width / 2 ? count - 1 : 1)) % count;
}
