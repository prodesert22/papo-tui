#include "ui/button.h"
#include "ui/text.h"
#include "ui/theme.h"

int button_draw(WINDOW *window, int row, int column, int width, const char *label,
                button_state state, button_surface surface)
{
    int color = state == BUTTON_FOCUSED ? PROFILE_BUTTON_FOCUS :
                state == BUTTON_SELECTED ? PROFILE_BUTTON_SELECTED :
                surface == BUTTON_PANEL ? PROFILE_PANEL : PROFILE_TEXT;
    attr_t previous = getattrs(window);
    attr_t attributes = COLOR_PAIR(color) | A_BOLD;
    if (!has_colors() && state == BUTTON_FOCUSED) attributes |= A_REVERSE;
    wattrset(window, attributes);
    int painted = text_label(window, row, column, width, label);
    wattrset(window, previous);
    return painted;
}
