#ifndef PAPO_MESSAGE_TIME_H
#define PAPO_MESSAGE_TIME_H
#include <stdbool.h>
#include <stddef.h>
#include <time.h>
bool message_time_format(const char *timestamp, time_t now, char *output, size_t capacity);
#endif
