#ifndef PAPO_MESSAGE_TIME_H
#define PAPO_MESSAGE_TIME_H
#include <stdbool.h>
#include <stddef.h>
#include <time.h>
bool message_time_format(const char *timestamp, time_t now, char *output, size_t capacity);
/* Without seconds and in 24 hours, for narrow lists: a locale's "%X" can add an AM/PM mark
 * that leaves no room for the rest of the row. */
bool message_time_format_compact(const char *timestamp, time_t now, char *output, size_t capacity);
#endif
