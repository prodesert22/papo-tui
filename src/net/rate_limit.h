#ifndef PAPO_RATE_LIMIT_H
#define PAPO_RATE_LIMIT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PAPO_REQUESTS_PER_SECOND 10U
#define PAPO_REQUEST_BURST 25U
#define PAPO_RATE_INTERVAL_NS (UINT64_C(1000000000) / PAPO_REQUESTS_PER_SECOND)
#define PAPO_RATE_DEFAULT_COOLDOWN_SECONDS 3U

typedef struct {
    uint64_t updated;
    long double credit; /* Fractional request tokens. */
    uint64_t blocked_until;
    bool initialized;
    double requests_per_second;
    unsigned burst;
} request_budget;

/* All network dispatch runs on the application's single event-loop thread.
 * Each request costs one token; idle credit is capped at burst.
 * The supplied monotonic clock makes boundary behavior deterministic in tests. */
bool request_budget_take(request_budget *budget, uint64_t now);
void request_budget_defer(request_budget *budget, uint64_t now, unsigned seconds);
bool request_budget_configure(request_budget *budget, uint64_t now, double requests_per_second,
                              unsigned burst);
bool net_rate_parameters_valid(double requests_per_second, unsigned burst);
/* Refill only elapsed time at the previous rate, then clamp existing credit.
 * A change never grants fresh burst tokens or clears Retry-After. */
bool net_rate_configure(double requests_per_second, unsigned burst);

/* Process-wide budget: login, logout, cancellation and client recreation never
 * reset it. Returns 1 when admitted, 0 when queued, -1 on clock failure. */
int net_rate_try_acquire(void);
void net_rate_defer(unsigned seconds);
/* Parses a bounded Retry-After header value, with the shared fallback/cap. */
unsigned net_rate_retry_after(const char *value, size_t length);

#endif
