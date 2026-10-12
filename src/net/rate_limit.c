#define _POSIX_C_SOURCE 200809L
#include "net/rate_limit.h"

#include <time.h>
#include <curl/curl.h>
#include <string.h>

#define CAPACITY (PAPO_RATE_INTERVAL_NS * PAPO_REQUEST_BURST)

static request_budget global_budget;

bool request_budget_take(request_budget *budget, uint64_t now)
{
    if (!budget->initialized) {
        budget->initialized = true;
        budget->updated = now;
        budget->credit = CAPACITY;
    }
    if (now < budget->blocked_until || now < budget->updated) return false;
    uint64_t elapsed = now - budget->updated;
    uint64_t remaining = CAPACITY - budget->credit;
    budget->credit += elapsed < remaining ? elapsed : remaining;
    budget->updated = now;
    if (budget->credit < PAPO_RATE_INTERVAL_NS) return false;
    budget->credit -= PAPO_RATE_INTERVAL_NS;
    return true;
}

void request_budget_defer(request_budget *budget, uint64_t now, unsigned seconds)
{
    uint64_t delay = (uint64_t)seconds * UINT64_C(1000000000);
    uint64_t until = delay > UINT64_MAX - now ? UINT64_MAX : now + delay;
    if (until < budget->blocked_until) until = budget->blocked_until;
    /* Start empty at the end of cooldown, avoiding another immediate burst. */
    budget->initialized = true;
    budget->blocked_until = until;
    budget->updated = until;
    budget->credit = 0;
}

static bool monotonic_now(uint64_t *now)
{
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0 || value.tv_sec < 0) return false;
    *now = (uint64_t)value.tv_sec * UINT64_C(1000000000) + (uint64_t)value.tv_nsec;
    return true;
}

int net_rate_try_acquire(void)
{
    uint64_t now;
    if (!monotonic_now(&now)) return -1;
    return request_budget_take(&global_budget, now) ? 1 : 0;
}

void net_rate_defer(unsigned seconds)
{
    uint64_t now;
    if (monotonic_now(&now)) request_budget_defer(&global_budget, now, seconds);
}

unsigned net_rate_retry_after(const char *value, size_t length)
{
    if (!value || !length || length >= 128) return PAPO_RATE_DEFAULT_COOLDOWN_SECONDS;
    char copy[128];
    memcpy(copy, value, length);
    copy[length] = '\0';
    char *start = copy;
    while (*start == ' ' || *start == '\t') ++start;
    char *end = start + strlen(start);
    while (end > start && (end[-1] == '\r' || end[-1] == '\n' || end[-1] == ' ' || end[-1] == '\t')) --end;
    *end = '\0';
    bool digits = *start != '\0';
    for (char *p = start; *p; ++p) digits = digits && *p >= '0' && *p <= '9';
    if (digits) {
        unsigned seconds = 0;
        for (char *p = start; *p && seconds < 86400; ++p)
            seconds = seconds * 10 + (unsigned)(*p - '0');
        return seconds < 86400 ? seconds : 86400;
    }
    time_t date = curl_getdate(start, NULL), now = time(NULL);
    if (date < 0 || now < 0) return PAPO_RATE_DEFAULT_COOLDOWN_SECONDS;
    double delay = difftime(date, now);
    return delay <= 0 ? 0U : delay >= 86400 ? 86400U : (unsigned)delay;
}
