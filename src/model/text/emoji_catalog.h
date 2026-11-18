#ifndef PAPO_EMOJI_CATALOG_H
#define PAPO_EMOJI_CATALOG_H
#include <stddef.h>
#include <stdint.h>
typedef struct {
    /* name is the emoji's Discord name, written between colons as :thumbsup:;
     * aliases holds its other names separated by spaces ("+1 thumbup") and is
     * empty when it has none. portuguese and english are the CLDR names. */
    const char *text, *name, *aliases, *portuguese, *english, *keywords;
    const uint32_t *points;
    size_t length;
} unicode_emoji;
size_t emoji_catalog_count(void);
size_t emoji_catalog_index(const unicode_emoji *emoji);
size_t emoji_catalog_order(size_t position);
const unicode_emoji *emoji_catalog_get(size_t index);
/* Exact Unicode sequence matching, longest first; never consumes plain digits. */
const unicode_emoji *emoji_catalog_match(const uint32_t *text, size_t length, size_t *consumed);
/* The emoji that a name or alias without its colons belongs to: "+1" and
 * "thumbsup" both give 👍. The comparison is exact; NULL when nothing has it. */
const unicode_emoji *emoji_catalog_named(const char *name);
#endif
