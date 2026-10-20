#include "ui/components/status_bar.h"
#include "ui/components/theme.h"
#include "ui/input/text.h"
#include <stdio.h>
#include <string.h>

static void copy_text(char output[1025], const char *input)
{
    text_buffer buffer;
    if (!input || !text_decode_chunk(&buffer, &input)) {
        output[0] = 0;
        return;
    }
    if (buffer.length > 256) {
        char breaks[TEXT_LIMIT];
        set_graphemebreaks_utf32(buffer.data, buffer.length, NULL, breaks);
        size_t cut = 256;
        while (cut && breaks[cut - 1] != GRAPHEMEBREAK_BREAK)
            --cut;
        buffer.length = cut;
    }
    for (size_t i = 0; i < buffer.length; ++i)
        if (buffer.data[i] == '\n')
            buffer.data[i] = ' ';
    char encoded[TEXT_UTF8_BYTES];
    if (!text_encode(&buffer, encoded)) {
        output[0] = 0;
        return;
    }
    memcpy(output, encoded, strlen(encoded) + 1);
}
bool status_bar_set(status_bar *bar, status_context context, const char *text)
{
    return status_bar_set_compact(bar, context, text, text);
}
bool status_bar_set_compact(status_bar *bar, status_context context, const char *text,
                            const char *compact)
{
    if (!bar || context < 0 || context >= STATUS_CONTEXT_COUNT)
        return false;
    char copy[1025], short_copy[1025];
    copy_text(copy, text);
    copy_text(short_copy, compact ? compact : text);
    status_notice *notice = &bar->notices[context];
    if (!strcmp(copy, notice->text) && !strcmp(short_copy, notice->compact))
        return false;
    memcpy(notice->text, copy, strlen(copy) + 1);
    memcpy(notice->compact, short_copy, strlen(short_copy) + 1);
    notice->order = *copy ? ++bar->sequence : 0;
    return true;
}
bool status_bar_touch(status_bar *bar, status_context context)
{
    if (!bar || context < 0 || context >= STATUS_CONTEXT_COUNT || !*bar->notices[context].text)
        return false;
    bar->notices[context].order = ++bar->sequence;
    return true;
}
static int cells(const char *text)
{
    return text_label(NULL, 0, 0, 1024, text);
}
static void label(WINDOW *window, int row, int x, int width, const char *text)
{
    if (width <= 0)
        return;
    if (cells(text) <= width)
        text_label(window, row, x, width, text);
    else {
        int used = text_label(window, row, x, width - 1, text);
        text_label(window, row, x + used, 1, "…");
    }
}
void status_bar_draw(status_bar *bar, WINDOW *window, int row, const char *full,
                     const char *shorter, const char *minimal)
{
    if (!bar || !window || row < 0 || row >= getmaxy(window))
        return;
    int available = getmaxx(window) - 2;
    if (available <= 0)
        return;
    status_context ordered[STATUS_CONTEXT_COUNT];
    size_t count = 0;
    int need = 0;
    for (int i = 0; i < STATUS_CONTEXT_COUNT; ++i) {
        status_notice *notice = &bar->notices[i];
        notice->x = notice->width = 0;
        if (!*notice->text)
            continue;
        size_t at = count;
        while (at && bar->notices[ordered[at - 1]].order > notice->order) {
            ordered[at] = ordered[at - 1];
            --at;
        }
        ordered[at] = (status_context)i;
        ++count;
        need += cells(notice->text);
    }
    if (count)
        need += 3 * ((int)count - 1);
    if (bar->connection_critical) {
        for (size_t i = 0; i < count; ++i)
            if (ordered[i] == STATUS_CONNECTION) {
                for (size_t j = i; j + 1 < count; ++j)
                    ordered[j] = ordered[j + 1];
                ordered[count - 1] = STATUS_CONNECTION;
                break;
            }
    }
    const char *hints = full;
    if (count && cells(hints) + 3 + need > available)
        hints = shorter;
    if (count && cells(hints) + 3 + need > available)
        hints = minimal;
    int hint_width = cells(hints);
    if (hint_width > available)
        hint_width = available;
    wattrset(window, COLOR_PAIR(THEME_PANEL_MUTED));
    label(window, row, 1, hint_width, hints);
    if (!count)
        return;
    int budget = available - hint_width - 3;
    if (budget < 1)
        return;
    size_t first = 0;
    int overflow_width = 0;
    char overflow[32] = "";
    /* Reserve useful compact labels before showing additional contexts. Older
     * notices remain stored and reappear when more space is available. */
    for (;;) {
        int minimum = overflow_width + (overflow_width ? 3 : 0);
        for (size_t i = first; i < count; ++i) {
            status_notice *notice = &bar->notices[ordered[i]];
            int n = cells(*notice->compact ? notice->compact : notice->text);
            minimum += n < 24 ? n : 24;
            if (i > first)
                minimum += 3;
        }
        if (minimum <= budget || first + 1 == count)
            break;
        ++first;
        snprintf(overflow, sizeof(overflow), "+%zu", first);
        overflow_width = cells(overflow);
    }
    if (overflow_width && overflow_width + 3 >= budget)
        overflow_width = 0;
    if (overflow_width && bar->connection_critical && first + 1 == count &&
        ordered[first] == STATUS_CONNECTION &&
        overflow_width + 3 + cells(bar->notices[STATUS_CONNECTION].compact) > budget)
        overflow_width = 0; /* Preserve the critical label before an older-notice count. */
    int widths[STATUS_CONTEXT_COUNT] = {0};
    const char *labels[STATUS_CONTEXT_COUNT] = {0};
    int used = overflow_width + (overflow_width ? 3 : 0);
    for (size_t i = first; i < count; ++i) {
        labels[i] = bar->notices[ordered[i]].text;
        widths[i] = cells(labels[i]);
        used += widths[i] + (i > first ? 3 : 0);
    }
    while (used > budget) {
        size_t largest = count;
        int saving = 0;
        for (size_t i = first; i < count; ++i) {
            status_notice *notice = &bar->notices[ordered[i]];
            int n = widths[i] - cells(notice->compact);
            if (labels[i] == notice->text && *notice->compact && n > saving) {
                saving = n;
                largest = i;
            }
        }
        if (largest == count)
            break;
        labels[largest] = bar->notices[ordered[largest]].compact;
        widths[largest] -= saving;
        used -= saving;
    }
    while (used > budget) {
        size_t largest = first;
        for (size_t i = first + 1; i < count; ++i)
            if (widths[i] > widths[largest])
                largest = i;
        if (!widths[largest])
            break;
        --widths[largest];
        --used;
    }
    int x = 1 + available - used;
    if (overflow_width) {
        label(window, row, x, overflow_width, overflow);
        x += overflow_width;
        text_label(window, row, x, 3, " | ");
        x += 3;
    }
    for (size_t i = first; i < count; ++i) {
        status_notice *notice = &bar->notices[ordered[i]];
        if (i > first) {
            text_label(window, row, x, 3, " | ");
            x += 3;
        }
        notice->x = x;
        notice->width = widths[i];
        wattrset(window, COLOR_PAIR(ordered[i] == STATUS_EVENT || ordered[i] == STATUS_EDITOR
                                        ? THEME_PANEL_ACCENT
                                        : THEME_PANEL));
        label(window, row, x, widths[i], labels[i]);
        x += widths[i];
        wattrset(window, COLOR_PAIR(THEME_PANEL_MUTED));
    }
}
bool status_bar_hit(const status_bar *bar, status_context context, int x)
{
    if (!bar || context < 0 || context >= STATUS_CONTEXT_COUNT)
        return false;
    const status_notice *notice = &bar->notices[context];
    return notice->width > 0 && x >= notice->x && x < notice->x + notice->width;
}
