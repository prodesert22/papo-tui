#define _POSIX_C_SOURCE 200809L
#include "model/mentions.h"
#include <ctype.h>
#include <regex.h>
#include <string.h>
#include <strings.h>
static regex_t expression;
static bool ready;
bool mention_next(const char *text, size_t from, mention_range *r)
{
    if (!text || from > strlen(text)) return false;
    if (!ready) {
        if (regcomp(&expression,
                    "@mention\\(<@([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12})>\\)",
                    REG_EXTENDED | REG_ICASE))
            return false;
        ready = true;
    }
    regmatch_t match[2];
    if (regexec(&expression, text + from, 2, match, 0)) return false;
    r->start = from + (size_t)match[0].rm_so;
    r->end = from + (size_t)match[0].rm_eo;
    for (int i = 0; i < 36; ++i)
        r->id[i] = (char)tolower((unsigned char)text[from + (size_t)match[1].rm_so + (size_t)i]);
    r->id[36] = 0;
    return true;
}
bool mention_contains(const char *text, const char *id)
{
    if (!id) return false;
    mention_range r;
    size_t from = 0;
    while (mention_next(text, from, &r)) {
        if (!strcasecmp(r.id, id)) return true;
        from = r.end;
    }
    return false;
}
void mention_cleanup(void)
{
    if (ready) {
        regfree(&expression);
        ready = false;
    }
}
