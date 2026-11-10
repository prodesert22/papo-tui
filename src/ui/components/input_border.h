#ifndef PAPO_INPUT_BORDER_H
#define PAPO_INPUT_BORDER_H

#include <ncurses.h>
#include <stdbool.h>

/* Border of an input whose value was refused. The input keeps its own
 * background; only the frame is drawn, in the color pair given, on the cells
 * around it: the columns just left and right of the input's row, and, when the
 * caller has the rows free, the lines above and below with their corners.
 * Borrows the window; draws nothing that would fall outside it. */
void input_border_draw(WINDOW *window, int row, int column, int width, int pair, bool top,
                       bool bottom);

#endif
