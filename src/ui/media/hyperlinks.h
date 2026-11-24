#ifndef PAPO_HYPERLINKS_H
#define PAPO_HYPERLINKS_H
#include <curses.h>
/* Terminal hyperlinks (OSC 8) over link text that curses has drawn. A
 * terminal finds addresses on its own only within one screen row, so a link
 * that wraps opens cut short; with a hyperlink, every row of it opens the
 * whole address. Curses has no such attribute: after it updates the screen,
 * the cells of each link are written again, as they are, inside the
 * hyperlink. Terminals without hyperlinks ignore the sequence.
 *
 * One frame: begin when the links are drawn again, offer each row of a link
 * right after drawing it, prepare before doupdate and flush after it. Links
 * offered in an earlier frame stay while their cells are not drawn again. */
void hyperlinks_frame_begin(void);
/* columns cells of window at (y, x) are link text that opens url, an http://
 * or https:// address from untrusted text; anything else is ignored. The
 * cells are read now, so offer them before drawing anything over them. */
void hyperlinks_offer(WINDOW *window, int y, int x, int columns, const char *url);
void hyperlinks_frame_prepare(void);
/* Writes the links whose cells curses wrote in this update, and only where
 * the screen still shows them: cells under a menu or a selection are left. */
void hyperlinks_frame_flush(void);
#endif
