#ifndef PAPO_MENTIONS_H
#define PAPO_MENTIONS_H
#include <stdbool.h>
#include <stddef.h>
typedef struct {
    size_t start, end;
    char id[37];
} mention_range;
/* Byte offsets into borrowed UTF-8; POSIX regex cache lives for the process. */
bool mention_next(const char *text, size_t from, mention_range *range);
bool mention_contains(const char *text, const char *id);
void mention_cleanup(void);
typedef const char *(*mention_resolver)(void *context, const char *id);
#endif
