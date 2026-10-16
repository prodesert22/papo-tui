#ifndef PAPO_STATUS_BAR_H
#define PAPO_STATUS_BAR_H

#include <ncurses.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    STATUS_CONNECTION,
    STATUS_HISTORY,
    STATUS_SEND,
    STATUS_MUTATION,
    STATUS_EDITOR,
    STATUS_EVENT,
    STATUS_UNREAD,
    STATUS_CLIPBOARD,
    STATUS_NOTIFICATIONS,
    STATUS_DESKTOP,
    STATUS_CONTEXT_COUNT
} status_context;

typedef struct {
    char text[1025];
    char compact[1025];
    uint64_t order;
    int x, width;
} status_notice;

/* Owned bounded notices, scoped to one view. A new notice replaces only its
 * context. Drawing never discards notices that cannot fit the current width. */
typedef struct {
    status_notice notices[STATUS_CONTEXT_COUNT];
    uint64_t sequence;
    bool connection_critical;
} status_bar;

bool status_bar_set(status_bar *bar, status_context context, const char *text);
bool status_bar_set_compact(status_bar *bar, status_context context, const char *text,
                            const char *compact);
/* Record a new occurrence whose text happens to match the previous notice. */
bool status_bar_touch(status_bar *bar, status_context context);
void status_bar_draw(status_bar *bar, WINDOW *window, int row, const char *full_hints,
                     const char *short_hints, const char *minimal_hints);
bool status_bar_hit(const status_bar *bar, status_context context, int x);

#endif
