#include "ui/components/about.h"
#include "model/text/translation.h"
#include "platform/clipboard.h"
#include "platform/install.h"
#include "ui/components/theme.h"
#include "ui/input/text.h"
#include <glib.h>
#include <stdio.h>
#include <string.h>

/* The same holder and license as the LICENSE file shipped with the program. */
#define ABOUT_LICENSE "MIT · Copyright (c) 2026 Papo TUI contributors"
#define ABOUT_AUTHOR "Papo TUI contributors"

/* Every line the section can show, in display order. */
enum {
    LINE_VERSION,
    LINE_SUBTITLE,
    LINE_SPACER,
    LINE_LICENSE,
    LINE_AUTHOR,
    LINE_SOURCE,
    LINE_NOTICES,
    LINE_NOTICES_PATH,
    ABOUT_LINES
};
/* What a short area gives up first; the version always stays. */
static const int drop_order[] = {LINE_NOTICES_PATH, LINE_NOTICES, LINE_SPACER, LINE_SUBTITLE,
                                 LINE_AUTHOR,       LINE_LICENSE, LINE_SOURCE};

/* Packages install the notices under the installation prefix; a build tree
 * has none. */
static void resolve_licenses(about_view *view)
{
    view->resolved = true;
    view->licenses[0] = 0;
    char *prefix = install_prefix();
    if (!prefix)
        return;
    char *licenses = g_build_filename(prefix, "share", "licenses", "papo-tui", NULL);
    if (g_file_test(licenses, G_FILE_TEST_IS_DIR) && strlen(licenses) < sizeof(view->licenses))
        snprintf(view->licenses, sizeof(view->licenses), "%s", licenses);
    g_free(licenses);
    g_free(prefix);
}
void about_draw(about_view *view, WINDOW *window, int top, int left, int width, int rows,
                bool focused)
{
    view->link_row = -1;
    if (rows < 1 || width < 1)
        return;
    if (!view->resolved)
        resolve_licenses(view);
    char line[1280];
    bool shown[ABOUT_LINES];
    int count = ABOUT_LINES;
    for (int i = 0; i < ABOUT_LINES; ++i)
        shown[i] = true;
    if (!*view->licenses) {
        shown[LINE_NOTICES_PATH] = false;
        --count;
    }
    for (size_t i = 0; count > rows && i < sizeof(drop_order) / sizeof(drop_order[0]); ++i)
        if (shown[drop_order[i]]) {
            shown[drop_order[i]] = false;
            --count;
            /* The folder's label says nothing without the folder. */
            if (drop_order[i] == LINE_NOTICES_PATH) {
                shown[LINE_NOTICES] = false;
                --count;
            }
        }
    int row = top;
    for (int i = 0; i < ABOUT_LINES; ++i) {
        if (!shown[i])
            continue;
        switch (i) {
        case LINE_VERSION:
            wattrset(window, COLOR_PAIR(THEME_PANEL_ACCENT) | A_BOLD);
            snprintf(line, sizeof(line), "Papo TUI %s", PAPO_VERSION);
            text_label(window, row, left, width, line);
            break;
        case LINE_SUBTITLE:
            wattrset(window, COLOR_PAIR(THEME_PANEL_MUTED));
            text_label(window, row, left, width, _("Cliente de conversas para terminal"));
            break;
        case LINE_LICENSE:
            wattrset(window, COLOR_PAIR(THEME_PANEL));
            snprintf(line, sizeof(line), _("Licença: %s"), ABOUT_LICENSE);
            text_label(window, row, left, width, line);
            break;
        case LINE_AUTHOR:
            wattrset(window, COLOR_PAIR(THEME_PANEL));
            snprintf(line, sizeof(line), _("Autor: %s"), ABOUT_AUTHOR);
            text_label(window, row, left, width, line);
            break;
        case LINE_SOURCE:
            wattrset(window, COLOR_PAIR(focused ? THEME_FOCUSED : THEME_PANEL) |
                                 (focused && !has_colors() ? A_REVERSE : 0));
            snprintf(line, sizeof(line), _("Código-fonte: %s"), ABOUT_REPOSITORY);
            text_label(window, row, left, width, line);
            view->link_row = row;
            break;
        case LINE_NOTICES:
            wattrset(window, COLOR_PAIR(THEME_PANEL));
            text_label(window, row, left, width,
                       *view->licenses
                           ? _("Licenças de terceiros:")
                           : _("Licenças de terceiros: THIRD_PARTY_NOTICES.md no repositório"));
            break;
        case LINE_NOTICES_PATH:
            wattrset(window, COLOR_PAIR(THEME_PANEL_MUTED));
            text_label(window, row, left + 2, width - 2, view->licenses);
            break;
        default:
            break;
        }
        ++row;
    }
}
about_action about_key(about_view *view, int type, uint32_t key)
{
    bool enter =
        (type == OK && (key == '\r' || key == '\n')) || (type == KEY_CODE_YES && key == KEY_ENTER);
    if (type == KEY_CODE_YES && key == KEY_UP)
        return ABOUT_LEAVE_UP;
    if (type == KEY_CODE_YES && key == KEY_DOWN)
        return ABOUT_LEAVE_DOWN;
    return (enter || (type == OK && key == ' ')) && view->link_row >= 0 ? ABOUT_COPY_SOURCE
                                                                        : ABOUT_NOTHING;
}
about_action about_click(about_view *view, int row)
{
    return view->link_row >= 0 && view->link_row == row ? ABOUT_COPY_SOURCE : ABOUT_NOTHING;
}
bool about_copy_source(void)
{
    return clipboard_copy_text(ABOUT_REPOSITORY);
}
