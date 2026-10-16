#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "ui/media/message_time.h"
#include "net/auth/session_time.h"
#include "platform/runtime.h"
#include <stdint.h>
#include <stdio.h>
bool message_time_format(const char *timestamp, time_t now, char *output, size_t capacity)
{
    uint64_t milliseconds;
    struct tm today, date;
    if (!session_time_parse(timestamp, &milliseconds))
        goto invalid;
    time_t when = (time_t)(milliseconds / 1000);
    if (when < 0 || (uint64_t)when != milliseconds / 1000 || !papo_localtime(&when, &date) ||
        !papo_localtime(&now, &today))
        goto invalid;
    bool same = date.tm_year == today.tm_year && date.tm_yday == today.tm_yday;
    if (strftime(output, capacity, same ? "%X" : "%x %X", &date))
        return true;
invalid:
    if (capacity)
        snprintf(output, capacity, "hora indisponível");
    return false;
}
