#include "ui/components/settings.h"
#include "ui/components/button.h"
#include "ui/components/theme.h"
#include "ui/input/mouse.h"
#include "ui/input/shortcuts.h"
#include "ui/input/text.h"
#include <string.h>
static const char *sections[] = {"Geral", "Aparência", "Áudio", "Acessibilidade", "Atalhos"};
void settings_close(app_settings *s)
{
    s->open = false;
    if (s->window) {
        delwin(s->window);
        s->window = NULL;
    }
}
void settings_open(app_settings *s, const profile_config *config)
{
    settings_close(s);
    memset(s, 0, sizeof(*s));
    s->open = true;
    s->draft = config->mode;
    s->selection = config->mode;
}
void settings_draw(app_settings *s)
{
    if (!s->open)
        return;
    int width = COLS > 76 ? 76 : COLS - 2, height = LINES > 20 ? 20 : LINES - 2;
    if (width < 40 || height < 12)
        return;
    if (s->window) {
        delwin(s->window);
        s->window = NULL;
    }
    s->window = newwin(height, width, (LINES - height) / 2, (COLS - width) / 2);
    if (!s->window)
        return;
    WINDOW *w = s->window;
    wbkgd(w, COLOR_PAIR(THEME_PANEL));
    werase(w);
    wattrset(w, COLOR_PAIR(THEME_BORDER));
    box(w, 0, 0);
    wattrset(w, COLOR_PAIR(THEME_PANEL_ACCENT) | A_BOLD);
    text_label(w, 1, 2, width - 4, "Configurações");
    wattrset(w, COLOR_PAIR(s->focus == 0 ? THEME_FOCUSED : THEME_PANEL_ACCENT) |
                    (!has_colors() && s->focus == 0 ? A_REVERSE : 0));
    char label[160];
    snprintf(label, sizeof(label), "< %s >", sections[s->section]);
    text_label(w, 3, 2, width - 4, label);
    if (!s->section) {
        for (int i = 0; i < 3; ++i) {
            int step = height >= 18 ? 3 : 1;
            int row = 5 + i * step;
            wattrset(w,
                     COLOR_PAIR(s->focus == 1 && s->selection == i ? THEME_FOCUSED : THEME_PANEL) |
                         (!has_colors() && s->focus == 1 && s->selection == i ? A_REVERSE : 0));
            snprintf(label, sizeof(label), "(%c) %s", s->draft == (app_mode)i ? '*' : ' ',
                     i == 0   ? "Normal"
                     : i == 1 ? "Light"
                              : "Ultralight");
            text_label(w, row, 2, width - 4, label);
            wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
            if (height >= 18) {
                text_buffer description;
                text_layout measured;
                if (text_decode(&description, app_mode_description((app_mode)i))) {
                    text_measure(&description, width - 6, &measured);
                    text_draw(w, &description, &measured, 0, 2, row + 1, 3);
                }
            }
        }
        if (height < 18) {
            text_buffer description;
            text_layout measured;
            if (text_decode(&description, app_mode_description((app_mode)s->selection))) {
                text_measure(&description, width - 6, &measured);
                text_draw(w, &description, &measured, 0, height - 10, 8, 3);
            }
        }
    } else {
        wattrset(w, COLOR_PAIR(THEME_PANEL_MUTED));
        text_label(w, 6, 3, width - 6, "Em breve. Esta seção ainda não altera configurações.");
    }
    wattrset(w, COLOR_PAIR(THEME_ERROR));
    if (*s->error)
        text_label(w, height - 3, 2, width - 4, s->error);
    button_draw(w, height - 2, 2, 18, "Salvar e aplicar",
                s->focus == 2 ? BUTTON_FOCUSED : BUTTON_NORMAL, BUTTON_PANEL);
    button_draw(w, height - 2, width - 14, 12, "Cancelar",
                s->focus == 3 ? BUTTON_FOCUSED : BUTTON_NORMAL, BUTTON_PANEL);
    wnoutrefresh(w);
}
static void save(app_settings *s, profile_config *config)
{
    if (profiles_set_mode(config, s->draft)) {
        s->applied = true;
        settings_close(s);
    } else
        snprintf(s->error, sizeof(s->error), "%s", config->message);
}
void settings_key(app_settings *s, profile_config *config, int type, uint32_t key)
{
    if (!s->open)
        return;
    bool enter =
        (type == OK && (key == '\r' || key == '\n')) || (type == KEY_CODE_YES && key == KEY_ENTER);
    if (type == OK && key == 27) {
        settings_close(s);
        return;
    }
    if (shortcut_is(type, key, SHORTCUT_SETTINGS)) {
        settings_close(s);
        return;
    }
    if ((type == OK && key == '\t') || (type == KEY_CODE_YES && key == KEY_BTAB)) {
        s->focus = (s->focus + (type == OK ? 1 : 3)) % 4;
        if (s->section && s->focus == 1)
            s->focus = type == OK ? 2 : 0;
        return;
    }
    if (type == KEY_CODE_YES && key == KEY_MOUSE && s->window) {
        MEVENT e;
        if (ui_mouse_get(&e) != OK || !(e.bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED)))
            return;
        int y = e.y, x = e.x;
        if (!wmouse_trafo(s->window, &y, &x, false))
            return;
        if (y == 3) {
            s->section = (s->section + (x < getmaxx(s->window) / 2 ? 4 : 1)) % 5;
            s->focus = 0;
        } else if (!s->section && y >= 5 && y < 5 + 3 * (getmaxy(s->window) >= 18 ? 3 : 1)) {
            s->selection = (y - 5) / (getmaxy(s->window) >= 18 ? 3 : 1);
            if (s->selection > 2)
                s->selection = 2;
            s->draft = (app_mode)s->selection;
            s->focus = 1;
        } else if (y == getmaxy(s->window) - 2) {
            if (x >= 2 && x < 20)
                save(s, config);
            else if (x >= getmaxx(s->window) - 14)
                settings_close(s);
        }
        return;
    }
    if (s->focus == 0) {
        if (type == KEY_CODE_YES && (key == KEY_LEFT || key == KEY_RIGHT))
            s->section = (s->section + (key == KEY_LEFT ? 4 : 1)) % 5;
        else if (enter || (type == KEY_CODE_YES && key == KEY_DOWN))
            s->focus = s->section ? 2 : 1;
    } else if (s->focus == 1) {
        if (type == KEY_CODE_YES && (key == KEY_UP || key == KEY_DOWN)) {
            if (key == KEY_UP && !s->selection)
                s->focus = 0;
            else {
                s->selection += (key == KEY_UP ? -1 : 1);
                if (s->selection > 2) {
                    s->selection = 2;
                    s->focus = 2;
                }
            }
        } else if (enter || (type == OK && key == ' '))
            s->draft = (app_mode)s->selection;
    } else if (type == KEY_CODE_YES && key == KEY_UP) {
        s->focus = s->section ? 0 : 1;
    } else if (enter) {
        if (s->focus == 2)
            save(s, config);
        else
            settings_close(s);
    } else if (type == KEY_CODE_YES && (key == KEY_LEFT || key == KEY_RIGHT))
        s->focus = key == KEY_LEFT ? 2 : 3;
}
