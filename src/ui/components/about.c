#include "ui/components/about.h"
#include "model/text/translation.h"
#include "platform/clipboard.h"
#include "platform/runtime.h"
#include "ui/components/theme.h"
#include "ui/input/text.h"
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The same holder and license as the LICENSE file shipped with the program. */
#define ABOUT_LICENSE "MIT · Copyright (c) 2026 Papo TUI contributors"
#define ABOUT_AUTHOR "Papo TUI contributors"
/* Rows that fit the description and its spacer besides the facts. */
#define ABOUT_ROOMY_ROWS 8

/* Packages install the notices under the prefix that holds the executable's
 * directory; a build tree has none. */
static void resolve_licenses(about_view *view)
{
    view->resolved = true;
    view->licenses[0] = 0;
    char *executable = papo_executable_path();
    if (!executable)
        return;
    char *directory = g_path_get_dirname(executable);
#ifdef _WIN32
    char *prefix = g_strdup(directory);
#else
    char *name = g_path_get_basename(directory);
    char *prefix = !strcmp(name, "bin") ? g_path_get_dirname(directory) : g_strdup(directory);
    g_free(name);
#endif
    char *licenses = g_build_filename(prefix, "share", "licenses", "papo-tui", NULL);
    if (g_file_test(licenses, G_FILE_TEST_IS_DIR) && strlen(licenses) < sizeof(view->licenses))
        snprintf(view->licenses, sizeof(view->licenses), "%s", licenses);
    g_free(licenses);
    g_free(prefix);
    g_free(directory);
    free(executable);
}

void about_draw(about_view *view, WINDOW *window, int top, int left, int width, int rows)
{
    view->link_row = -1;
    if (rows < 1 || width < 1)
        return;
    if (!view->resolved)
        resolve_licenses(view);
    int row = top, end = top + rows;
    char line[1280];
    wattrset(window, COLOR_PAIR(THEME_PANEL_ACCENT) | A_BOLD);
    snprintf(line, sizeof(line), "Papo TUI %s", PAPO_VERSION);
    text_label(window, row++, left, width, line);
    if (rows >= ABOUT_ROOMY_ROWS) {
        wattrset(window, COLOR_PAIR(THEME_PANEL_MUTED));
        text_label(window, row, left, width, _("Cliente de conversas para terminal"));
        row += 2;
    }
    wattrset(window, COLOR_PAIR(THEME_PANEL));
    if (row < end) {
        snprintf(line, sizeof(line), _("Licença: %s"), ABOUT_LICENSE);
        text_label(window, row++, left, width, line);
    }
    if (row < end) {
        snprintf(line, sizeof(line), _("Autor: %s"), ABOUT_AUTHOR);
        text_label(window, row++, left, width, line);
    }
    if (row < end) {
        snprintf(line, sizeof(line), _("Código-fonte: %s"), ABOUT_REPOSITORY);
        view->link_row = row;
        text_label(window, row++, left, width, line);
    }
    if (!*view->licenses) {
        if (row < end)
            text_label(window, row, left, width,
                       _("Licenças de terceiros: THIRD_PARTY_NOTICES.md no repositório"));
        return;
    }
    if (row + 1 < end) {
        text_label(window, row++, left, width, _("Licenças de terceiros:"));
        wattrset(window, COLOR_PAIR(THEME_PANEL_MUTED));
        text_label(window, row, left + 2, width - 2, view->licenses);
    }
}

bool about_copy_link(void)
{
    return clipboard_copy_text(ABOUT_REPOSITORY);
}
