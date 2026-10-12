#include "net/rate_limit.h"

#include <stdio.h>

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
    puts("global burst, refill, idle cap and cooldown passed");
    return 0;
}
