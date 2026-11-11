#ifndef PAPO_QUICK_SWITCHER_H
#define PAPO_QUICK_SWITCHER_H
#include "ui/input/text.h"
#include <stddef.h>

/* One conversation already loaded by a connected server. Text is copied, so
 * the list never borrows model data; names are remote and only drawn through
 * text_label. */
typedef struct {
    size_t instance; /* Index of the server tab. */
    char channel[37];
    char name[256], server[129];
    char last[65]; /* created_at of its latest message, or empty. */
    bool dm, voice, unread, muted;
    size_t mentions;
} switcher_entry;

/* Ctrl+K: filters conversations of every server by name and opens one. Owns
 * its window, its search text and the entries handed to open. A
 * zero-initialized instance is closed; destroy before curses shutdown. */
typedef struct {
    WINDOW *window;
    text_buffer search;
    int search_first;
    switcher_entry *entries;
    size_t count;
    size_t *visible, visible_count, selected, first;
    int rows; /* List rows of the last draw. */
    bool open, chosen;
} quick_switcher;

/* Takes ownership of entries (malloc'd array of count), sorted here: mentions,
 * then unread conversations, then the most recent activity. False on
 * allocation failure, with entries freed. */
bool quick_switcher_open(quick_switcher *switcher, switcher_entry *entries, size_t count);
void quick_switcher_close(quick_switcher *switcher);
void quick_switcher_destroy(quick_switcher *switcher);
/* Consumes every key while open. Enter or a click marks the selection chosen
 * and closes; read it with quick_switcher_take before the next open. */
bool quick_switcher_key(quick_switcher *switcher, int type, uint32_t key);
void quick_switcher_paste(quick_switcher *switcher, text_paste *paste);
/* The chosen entry, once; false when none was chosen. */
bool quick_switcher_take(quick_switcher *switcher, switcher_entry *entry);
/* Returns the window drawn, or NULL when closed or the terminal is too small. */
WINDOW *quick_switcher_draw(quick_switcher *switcher);
#endif
