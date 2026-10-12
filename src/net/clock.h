#ifndef PAPO_CLOCK_H
#define PAPO_CLOCK_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/* Monotonic deadlines belong to the application's single event-loop thread. */
static inline bool net_now_ms(uint64_t *now)
{
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0 || value.tv_sec < 0 ||
        (uint64_t)value.tv_sec > UINT64_MAX / 1000) return false;
    *now = (uint64_t)value.tv_sec * 1000 + (uint64_t)value.tv_nsec / 1000000;
    return true;
}

#endif
