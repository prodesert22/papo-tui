#ifndef PAPO_MENTIONS_H
#define PAPO_MENTIONS_H
#include <jansson.h>
#include <stdbool.h>
#include <stddef.h>
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
/* @everyone as the backend matches it: any case, as a whole word. */
bool mention_everyone(const char *text);
/* The message would notify id in the notification center: a direct mention,
 * a reply to a message by id, or @everyone from a person. reply_author is the
 * author of the replied message, or NULL when unknown. Own messages never do. */
bool mention_notifies(const json_t *message, const char *id, const char *reply_author);
void mention_cleanup(void);
typedef const char *(*mention_resolver)(void *context, const char *id);
#endif
