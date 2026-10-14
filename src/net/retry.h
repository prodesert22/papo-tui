#ifndef PAPO_RETRY_H
#define PAPO_RETRY_H
#include <stdint.h>
/* Caller supplies entropy (not a security primitive). First failure is 1s;
 * jitter is bounded to +/-20%, with an absolute 30s ceiling. */
static inline uint64_t net_retry_ms(unsigned failures, unsigned entropy)
{
    unsigned base = failures >= 5 ? 30000U : 1000U << failures;
    unsigned minimum = base * 4U / 5U;
    unsigned maximum = base * 6U / 5U;
    if (maximum > 30000U) maximum = 30000U;
    return minimum + entropy % (maximum - minimum + 1U);
}
#endif
