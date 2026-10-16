#define _POSIX_C_SOURCE 200809L
#include "ui/components/status_bar.h"
#include "ui/input/text.h"
#include <assert.h>
#include <locale.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
#ifdef _WIN32
    assert(setlocale(LC_ALL, ".UTF8"));
#else
    assert(setlocale(LC_ALL, "C.UTF-8"));
#endif
    init_graphemebreak();
    status_bar bar = {0};
    assert(status_bar_set(&bar, STATUS_CONNECTION, "Connected"));
    assert(status_bar_set(&bar, STATUS_SEND, "Confirmed"));
    uint64_t send_order = bar.notices[STATUS_SEND].order;
    assert(status_bar_set(&bar, STATUS_EVENT, "Ignored event"));
    assert(!status_bar_set(&bar, STATUS_SEND, "Confirmed"));
    assert(bar.notices[STATUS_SEND].order == send_order);
    assert(status_bar_set(&bar, STATUS_EVENT, "Invalid event"));
    uint64_t event_order = bar.notices[STATUS_EVENT].order;
    assert(status_bar_touch(&bar, STATUS_EVENT));
    assert(bar.notices[STATUS_EVENT].order > event_order);
    assert(!strcmp(bar.notices[STATUS_CONNECTION].text, "Connected"));
    assert(!strcmp(bar.notices[STATUS_SEND].text, "Confirmed"));
    assert(status_bar_set(&bar, STATUS_EDITOR, "Unicode 界界\n\033]52;c;unsafe\a"));
    assert(!strchr(bar.notices[STATUS_EDITOR].text, '\033'));
    assert(!strchr(bar.notices[STATUS_EDITOR].text, '\a'));
    assert(!strchr(bar.notices[STATUS_EDITOR].text, '\n'));
    assert(!status_bar_set(&bar, (status_context)-1, "bad"));
    assert(!status_bar_set(&bar, STATUS_CONTEXT_COUNT, "bad"));
#ifdef _WIN32
    /* The Windows ncurses backend cannot create a screen from tmpfile streams.
     * Cover state here; rendering is exercised through ConPTY instead. */
    assert(status_bar_set_compact(&bar, STATUS_CONNECTION, "Desconectado · Reconectando…",
                                  "Desconectado"));
    bar.connection_critical = true;
    assert(status_bar_set(&bar, STATUS_EDITOR, "Aviso mais recente do editor"));
    return 0;
#endif
    FILE *input = tmpfile(), *output = tmpfile();
    assert(input && output);
    SCREEN *screen = newterm("xterm-256color", output, input);
    assert(screen);
    for (int width = 8; width <= 180; ++width) {
        WINDOW *window = newwin(1, width, 0, 0);
        assert(window);
        status_bar_draw(&bar, window, 0, "Full navigation", "Short", "h");
        int last = 0;
        for (int i = 0; i < STATUS_CONTEXT_COUNT; ++i) {
            status_notice *notice = &bar.notices[i];
            if (!notice->width)
                continue;
            assert(notice->x >= 5 && notice->x + notice->width <= width - 1);
            assert(status_bar_hit(&bar, (status_context)i, notice->x));
            assert(!status_bar_hit(&bar, (status_context)i, notice->x + notice->width));
            if (notice->x + notice->width > last)
                last = notice->x + notice->width;
        }
        assert(last == width - 1); /* Newest notice stays flush right. */
        delwin(window);
    }
    WINDOW *wide = newwin(1, 180, 0, 0);
    assert(wide);
    status_bar_draw(&bar, wide, 0, "Full navigation", "Short", "h");
    assert(bar.notices[STATUS_CONNECTION].x < bar.notices[STATUS_SEND].x);
    assert(bar.notices[STATUS_SEND].x < bar.notices[STATUS_EVENT].x);
    assert(bar.notices[STATUS_EVENT].x < bar.notices[STATUS_EDITOR].x);
    assert(status_bar_set(&bar, STATUS_CONNECTION, "Reconnected"));
    werase(wide);
    status_bar_draw(&bar, wide, 0, "Full navigation", "Short", "h");
    assert(bar.notices[STATUS_CONNECTION].x > bar.notices[STATUS_EDITOR].x);
    assert(status_bar_set(&bar, STATUS_SEND, NULL));
    assert(*bar.notices[STATUS_EVENT].text && *bar.notices[STATUS_CONNECTION].text);
    assert(status_bar_set_compact(&bar, STATUS_CONNECTION, "Desconectado · Reconectando…",
                                  "Desconectado"));
    bar.connection_critical = true;
    assert(status_bar_set(&bar, STATUS_EDITOR, "Aviso mais recente do editor"));
    WINDOW *narrow = newwin(1, 44, 0, 0);
    assert(narrow);
    status_bar_draw(&bar, narrow, 0, "Dicas completas de navegação", "Dicas", "h");
    char line[256];
    mvwinnstr(narrow, 0, 0, line, sizeof(line) - 1);
    assert(strstr(line, "Desconectado"));
    assert(bar.notices[STATUS_CONNECTION].width > 0);
    delwin(narrow);
    delwin(wide);
    endwin();
    delscreen(screen);
    fclose(input);
    fclose(output);
    return 0;
}
