#ifndef PAPO_CLOCK_H
#define PAPO_CLOCK_H

#include <glib.h>
#include <stdbool.h>
#include <stdint.h>

/* Monotonic deadlines belong to the application's single event-loop thread. */
static inline bool net_now_ms(uint64_t *now)
{
    gint64 value = g_get_monotonic_time();
    if (!now || value < 0)
        return false;
    *now = (uint64_t)value / 1000;
    return true;
}

#endif
