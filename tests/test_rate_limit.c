#include "net/rate_limit.h"

#include <stdio.h>
#include <math.h>

#define CHECK(value) do { if (!(value)) { fprintf(stderr, "check failed at line %d\n", __LINE__); return 1; } } while (0)

int main(void)
{
    request_budget budget = {0};
    const uint64_t start = UINT64_C(10000000000);
    for (unsigned i = 0; i < PAPO_REQUEST_BURST; ++i) CHECK(request_budget_take(&budget, start));
    CHECK(!request_budget_take(&budget, start));
    CHECK(!request_budget_take(&budget, start + PAPO_RATE_INTERVAL_NS - 1));
    CHECK(request_budget_take(&budget, start + PAPO_RATE_INTERVAL_NS));
    CHECK(!request_budget_take(&budget, start + PAPO_RATE_INTERVAL_NS));
    CHECK(!request_budget_take(&budget, start)); /* clock rollback cannot add credit */
    const uint64_t second = start + UINT64_C(1100000000);
    for (unsigned i = 0; i < PAPO_REQUESTS_PER_SECOND; ++i) CHECK(request_budget_take(&budget, second));
    CHECK(!request_budget_take(&budget, second));
    const uint64_t idle = second + UINT64_C(1000000000000);
    for (unsigned i = 0; i < PAPO_REQUEST_BURST; ++i) CHECK(request_budget_take(&budget, idle));
    CHECK(!request_budget_take(&budget, idle)); /* idle never grows the burst */
    request_budget_defer(&budget, idle, 2);
    request_budget_defer(&budget, idle, 1); /* shorter cooldown cannot replace longer */
    CHECK(!request_budget_take(&budget, idle + UINT64_C(1999999999)));
    CHECK(!request_budget_take(&budget, idle + UINT64_C(2000000000)));
    CHECK(request_budget_take(&budget, idle + UINT64_C(2000000000) + PAPO_RATE_INTERVAL_NS));
    CHECK(!request_budget_take(&budget, idle + UINT64_C(2000000000) + PAPO_RATE_INTERVAL_NS));
    for (unsigned i = 0; i < PAPO_REQUEST_BURST; ++i) CHECK(net_rate_try_acquire() == 1);
    CHECK(net_rate_try_acquire() == 0);
    CHECK(net_rate_configure(10, 50));
    CHECK(net_rate_try_acquire() == 0); /* Increasing burst cannot reset global credit. */
    net_rate_defer(1);
    CHECK(net_rate_configure(1000, 25));
    CHECK(net_rate_try_acquire() == 0); /* Saving cannot clear global cooldown. */

    request_budget custom = {0};
    CHECK(request_budget_configure(&custom, start, 2.5, 2));
    CHECK(request_budget_take(&custom, start));
    CHECK(request_budget_take(&custom, start));
    CHECK(!request_budget_take(&custom, start));
    CHECK(request_budget_configure(&custom, start + UINT64_C(200000000), 2, 20));
    CHECK(custom.credit == 0.5L); /* Refill elapsed time at the previous rate. */
    CHECK(!request_budget_take(&custom, start + UINT64_C(200000000)));
    CHECK(request_budget_take(&custom, start + UINT64_C(450000000)));
    CHECK(!request_budget_take(&custom, start + UINT64_C(450000000)));
    request_budget_defer(&custom, start + UINT64_C(450000000), 2);
    const uint64_t blocked = custom.blocked_until;
    CHECK(request_budget_configure(&custom, start + UINT64_C(500000000), 4, 1));
    CHECK(custom.blocked_until == blocked && custom.updated == blocked && custom.credit == 0);
    CHECK(!request_budget_take(&custom, blocked));
    CHECK(request_budget_take(&custom, blocked + UINT64_C(250000000)));
    CHECK(!request_budget_take(&custom, blocked + UINT64_C(250000000)));
    CHECK(request_budget_configure(&custom, blocked + UINT64_C(10000000000), 0.5, 1));
    CHECK(custom.credit == 1); /* Idle credit is clamped when lowering capacity. */
    CHECK(request_budget_take(&custom, blocked + UINT64_C(10000000000)));
    CHECK(!request_budget_take(&custom, blocked + UINT64_C(11999999999)));
    CHECK(request_budget_take(&custom, blocked + UINT64_C(12000000000)));
    const uint64_t updated = custom.updated;
    CHECK(request_budget_configure(&custom, start, 5, 25));
    CHECK(custom.updated == updated && custom.credit == 0); /* Clock rollback cannot refill. */
    CHECK(!request_budget_configure(&custom, updated, 0, 25));
    CHECK(!request_budget_configure(&custom, updated, -1, 25));
    CHECK(!request_budget_configure(&custom, updated, NAN, 25));
    CHECK(!request_budget_configure(&custom, updated, INFINITY, 25));
    CHECK(!request_budget_configure(&custom, updated, 10, 0));
    CHECK(custom.requests_per_second == 5 && custom.burst == 25 && custom.credit == 0);
    puts("global and configurable burst, fractional refill, idle cap and cooldown passed");
    return 0;
}
