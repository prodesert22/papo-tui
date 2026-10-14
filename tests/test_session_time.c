#include "net/session_time.h"
#include "net/retry.h"
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

#define CHECK(value) do { if (!(value)) { fprintf(stderr, "Check failed at line %d\n", __LINE__); return 1; } } while (0)

int main(void)
{
    uint64_t a, b, deadline;
    CHECK(session_time_parse("2024-02-29T12:00:00Z", &a));
    CHECK(session_time_parse("2024-02-29T15:30:00+03:30", &b) && a == b);
    CHECK(session_time_parse("2024-02-29t08:00:00-04:00", &b) && a == b);
    CHECK(session_time_parse("2024-02-29T12:00:00.123456789Z", &b) && b == a + 123);
    CHECK(session_time_parse("2024-02-29T12:00:00.1z", &b) && b == a + 100);
    const char *invalid[] = {"", "2023-02-29T12:00:00Z", "2024-02-30T12:00:00Z", "2024-13-01T12:00:00Z",
        "2024-02-29T24:00:00Z", "2024-02-29T12:00:60Z", "2024-02-29T12:00:00.Z", "2024-02-29T12:00:00+24:00",
        "2024-02-29T12:00:00+00:60", "2024-02-29T12:00:00Zjunk", "2024-02-29T12:00:00", "1969-12-31T23:59:59Z"};
    for (unsigned i = 0; i < sizeof(invalid)/sizeof(*invalid); ++i) CHECK(!session_time_parse(invalid[i], &b));
    CHECK(!session_time_parse(NULL, &b));
    CHECK(session_time_deadline(121000, 1000, 500, &deadline) && deadline == 60500);
    CHECK(session_time_deadline(61000, 1000, 500, &deadline) && deadline == 30500);
    CHECK(session_time_deadline(62000, 1000, 500, &deadline) && deadline == 31000);
    CHECK(session_time_deadline(2000, 1000, 500, &deadline) && deadline == 1000);
    CHECK(!session_time_deadline(1000, 1000, 500, &deadline));
    CHECK(!session_time_deadline(1999, 1000, 500, &deadline));
    CHECK(!session_time_deadline(61000, 1000, UINT64_MAX - 10, &deadline));
    const unsigned lower[] = {800, 1600, 3200, 6400, 12800, 24000};
    const unsigned upper[] = {1200, 2400, 4800, 9600, 19200, 30000};
    for (unsigned n = 0; n < 12; ++n)
        for (unsigned seed = 0; seed < 100000; seed += 997) {
            uint64_t delay = net_retry_ms(n, seed);
            CHECK(delay >= lower[n < 5 ? n : 5] && delay <= upper[n < 5 ? n : 5]);
        }
    CHECK(!session_time_expiration("2000-01-01T00:00:00Z", &deadline));
    return 0;
}
