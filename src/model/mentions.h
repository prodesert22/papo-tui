#ifndef PAPO_MENTIONS_H
#define PAPO_MENTIONS_H
#include <stdbool.h>
#include <stddef.h>
#include <jansson.h>
typedef struct {
    size_t start, end;
    char id[37];
} mention_range;
/* Byte offsets into borrowed UTF-8; POSIX regex cache lives for the process. */
bool mention_next(const char *text, size_t from, mention_range *range);
bool mention_contains(const char *text, const char *id);
/* Visits canonical mentions in message content and displayed embed text.
 * Input JSON and ranges are borrowed for the duration of the callback. */
typedef void (*mention_visitor)(void *context, const mention_range *range);
void mention_visit_message(const json_t *message, mention_visitor visitor, void *context);
bool mention_message_contains(const json_t *message, const char *id);
void mention_cleanup(void);
typedef const char *(*mention_resolver)(void *context, const char *id);
#endif
