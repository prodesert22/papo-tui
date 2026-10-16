#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "net/rate_limit.h"
#include <glib.h>

#include <curl/curl.h>
#include <math.h>
#include <string.h>
#include <time.h>

static request_budget global_budget;

bool net_rate_parameters_valid(double requests_per_second, unsigned burst)
{
    return isfinite(requests_per_second) && requests_per_second > 0 && burst > 0;
}

static void defaults(request_budget *budget)
{
    if (!net_rate_parameters_valid(budget->requests_per_second, budget->burst)) {
        budget->requests_per_second = PAPO_REQUESTS_PER_SECOND;
        budget->burst = PAPO_REQUEST_BURST;
    }
}

static void refill(request_budget *budget, uint64_t now)
{
    if (now < budget->updated || now < budget->blocked_until)
        return;
    long double added =
        (long double)(now - budget->updated) * budget->requests_per_second / 1000000000.0L;
    long double room = budget->burst - budget->credit;
    budget->credit += added < room ? added : room;
    budget->updated = now;
}

bool request_budget_configure(request_budget *budget, uint64_t now, double requests_per_second,
                              unsigned burst)
{
    if (!net_rate_parameters_valid(requests_per_second, burst))
        return false;
    defaults(budget);
    if (budget->initialized)
        refill(budget, now);
    budget->requests_per_second = requests_per_second;
    budget->burst = burst;
    if (budget->credit > burst)
        budget->credit = burst;
    return true;
}

bool request_budget_take(request_budget *budget, uint64_t now)
{
    defaults(budget);
    if (!budget->initialized) {
        budget->initialized = true;
        budget->updated = now;
        budget->credit = budget->burst;
    }
    if (now < budget->blocked_until || now < budget->updated)
        return false;
    refill(budget, now);
    if (budget->credit < 1)
        return false;
    budget->credit -= 1;
    return true;
}

void request_budget_defer(request_budget *budget, uint64_t now, unsigned seconds)
{
    defaults(budget);
    uint64_t delay = (uint64_t)seconds * UINT64_C(1000000000);
    uint64_t until = delay > UINT64_MAX - now ? UINT64_MAX : now + delay;
    if (until < budget->blocked_until)
        until = budget->blocked_until;
    /* Start empty at the end of cooldown, avoiding another immediate burst. */
    budget->initialized = true;
    budget->blocked_until = until;
    budget->updated = until;
    budget->credit = 0;
}

static bool monotonic_now(uint64_t *now)
{
    gint64 value = g_get_monotonic_time();
    if (value < 0 || (uint64_t)value > UINT64_MAX / 1000)
        return false;
    *now = (uint64_t)value * 1000;
    return true;
}

bool net_rate_configure(double requests_per_second, unsigned burst)
{
    uint64_t now;
    return net_rate_parameters_valid(requests_per_second, burst) && monotonic_now(&now) &&
           request_budget_configure(&global_budget, now, requests_per_second, burst);
}

int net_rate_try_acquire(void)
{
    uint64_t now;
    if (!monotonic_now(&now))
        return -1;
    return request_budget_take(&global_budget, now) ? 1 : 0;
}

void net_rate_defer(unsigned seconds)
{
    uint64_t now;
    if (monotonic_now(&now))
        request_budget_defer(&global_budget, now, seconds);
}

unsigned net_rate_retry_after(const char *value, size_t length)
{
    if (!value || !length || length >= 128)
        return PAPO_RATE_DEFAULT_COOLDOWN_SECONDS;
    char copy[128];
    memcpy(copy, value, length);
    copy[length] = '\0';
    char *start = copy;
    while (*start == ' ' || *start == '\t')
        ++start;
    char *end = start + strlen(start);
    while (end > start && (end[-1] == '\r' || end[-1] == '\n' || end[-1] == ' ' || end[-1] == '\t'))
        --end;
    *end = '\0';
    bool digits = *start != '\0';
    for (char *p = start; *p; ++p)
        digits = digits && *p >= '0' && *p <= '9';
    if (digits) {
        unsigned seconds = 0;
        for (char *p = start; *p && seconds < 86400; ++p)
            seconds = seconds * 10 + (unsigned)(*p - '0');
        return seconds < 86400 ? seconds : 86400;
    }
    time_t date = curl_getdate(start, NULL), now = time(NULL);
    if (date < 0 || now < 0)
        return PAPO_RATE_DEFAULT_COOLDOWN_SECONDS;
    double delay = difftime(date, now);
    return delay <= 0 ? 0U : delay >= 86400 ? 86400U : (unsigned)delay;
}
