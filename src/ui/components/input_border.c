#include "ui/components/input_border.h"

void input_border_draw(WINDOW *window, int row, int column, int width, int pair, bool top,
                       bool bottom)
{
    if (!window)
        return;
    int rows, columns;
    getmaxyx(window, rows, columns);
    int left = column - 1, right = column + width;
    if (width < 1 || left < 0 || right >= columns || row < 0 || row >= rows)
        return;
    wattrset(window, COLOR_PAIR(pair) | A_BOLD);
    mvwaddch(window, row, left, ACS_VLINE);
    mvwaddch(window, row, right, ACS_VLINE);
    if (top && row > 0) {
        mvwhline(window, row - 1, column, ACS_HLINE, width);
        mvwaddch(window, row - 1, left, ACS_ULCORNER);
        mvwaddch(window, row - 1, right, ACS_URCORNER);
    }
    if (bottom && row + 1 < rows) {
        mvwhline(window, row + 1, column, ACS_HLINE, width);
        mvwaddch(window, row + 1, left, ACS_LLCORNER);
        mvwaddch(window, row + 1, right, ACS_LRCORNER);
    }
}
