#define _GNU_SOURCE
#include "net/session_time.h"
#include "net/clock.h"

#include <ctype.h>
#include <string.h>
#include <time.h>

bool session_time_parse(const char *value, uint64_t *epoch_ms)
{
    if (!value || !epoch_ms || strlen(value) < 20 || strlen(value) > 64) return false;
    for (int i = 0; i < 19; ++i) {
        if (i == 4 || i == 7) { if (value[i] != '-') return false; }
        else if (i == 10) { if (value[i] != 'T' && value[i] != 't') return false; }
        else if (i == 13 || i == 16) { if (value[i] != ':') return false; }
        else if (!isdigit((unsigned char)value[i])) return false;
    }
    struct tm date = {0};
    char *end = strptime(value, value[10] == 'T' ? "%Y-%m-%dT%H:%M:%S" : "%Y-%m-%dt%H:%M:%S", &date);
    if (end != value + 19 || date.tm_sec > 59) return false;
    unsigned milliseconds = 0, digits = 0;
    if (*end == '.') {
        ++end;
        if (!isdigit((unsigned char)*end)) return false;
        while (isdigit((unsigned char)*end)) {
            if (digits < 3) milliseconds = 10 * milliseconds + (unsigned)(*end - '0');
            ++digits; ++end;
        }
        while (digits < 3) { milliseconds *= 10; ++digits; }
    }
    long offset = 0;
    if (*end == 'Z' || *end == 'z') { if (end[1]) return false; }
    else {
        if ((*end != '+' && *end != '-') || strlen(end) != 6 || end[3] != ':' ||
            !isdigit((unsigned char)end[1]) || !isdigit((unsigned char)end[2]) ||
            !isdigit((unsigned char)end[4]) || !isdigit((unsigned char)end[5]) ||
            (end[1] - '0') * 10 + end[2] - '0' > 23 || (end[4] - '0') * 10 + end[5] - '0' > 59) return false;
        struct tm zone = {0};
        char *zone_end = strptime(end, "%z", &zone);
        if (!zone_end || *zone_end) return false;
        offset = zone.tm_gmtoff;
    }
    struct tm original = date;
    time_t calendar = timegm(&date);
    if (calendar < 0 || original.tm_year != date.tm_year || original.tm_mon != date.tm_mon ||
        original.tm_mday != date.tm_mday || original.tm_hour != date.tm_hour ||
        original.tm_min != date.tm_min || original.tm_sec != date.tm_sec) return false;
    int64_t seconds = (int64_t)calendar - offset;
    if (seconds < 0 || (uint64_t)seconds > (UINT64_MAX - milliseconds) / 1000) return false;
    *epoch_ms = (uint64_t)seconds * 1000 + milliseconds;
    return true;
}

bool session_time_compare(const char *left, const char *right, int *ordering)
{
    uint64_t a,b;
    if (!ordering || !session_time_parse(left,&a) || !session_time_parse(right,&b)) return false;
    if (a/1000!=b/1000) { *ordering=a>b ? 1 : -1; return true; }
    const char *af=left+19,*bf=right+19;
    if (*af=='.') ++af;
    if (*bf=='.') ++bf;
    while (isdigit((unsigned char)*af) || isdigit((unsigned char)*bf)) {
        char ac=isdigit((unsigned char)*af) ? *af++ : '0',bc=isdigit((unsigned char)*bf) ? *bf++ : '0';
        if (ac!=bc) { *ordering=ac>bc ? 1 : -1; return true; }
    }
    *ordering=0; return true;
}

bool session_time_deadline(uint64_t expires_ms, uint64_t wall_ms, uint64_t monotonic_ms,
                           uint64_t *deadline_ms)
{
    if (!deadline_ms || expires_ms <= wall_ms || expires_ms - wall_ms < 1000) return false;
    uint64_t lifetime = expires_ms - wall_ms;
    uint64_t interval = lifetime > 120000 ? lifetime - 60000 : lifetime / 2;
    if (interval < 1000) interval = lifetime / 2 < 1000 ? lifetime / 2 : 1000;
    if (interval > UINT64_MAX - monotonic_ms) return false;
    *deadline_ms = monotonic_ms + interval;
    return true;
}

bool session_time_schedule(const char *expires_at, uint64_t *deadline_ms)
{
    uint64_t expires, now;
    struct timespec wall;
    if (!session_time_parse(expires_at, &expires) || !net_now_ms(&now) ||
        clock_gettime(CLOCK_REALTIME, &wall) != 0 || wall.tv_sec < 0 ||
        (uint64_t)wall.tv_sec > (UINT64_MAX - 999) / 1000 || wall.tv_nsec < 0 || wall.tv_nsec >= 1000000000L) return false;
    uint64_t wall_ms = (uint64_t)wall.tv_sec * 1000 + (uint64_t)wall.tv_nsec / 1000000;
    return session_time_deadline(expires, wall_ms, now, deadline_ms);
}

bool session_time_expiration(const char *expires_at, uint64_t *deadline_ms)
{
    uint64_t expires, now;
    struct timespec wall;
    if (!session_time_parse(expires_at, &expires) || !net_now_ms(&now) ||
        clock_gettime(CLOCK_REALTIME, &wall) != 0 || wall.tv_sec < 0 ||
        (uint64_t)wall.tv_sec > (UINT64_MAX - 999) / 1000) return false;
    uint64_t wall_ms = (uint64_t)wall.tv_sec * 1000 + (uint64_t)wall.tv_nsec / 1000000;
    if (expires <= wall_ms || expires - wall_ms > UINT64_MAX - now) return false;
    *deadline_ms = now + expires - wall_ms;
    return true;
}
