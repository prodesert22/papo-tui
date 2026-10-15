#ifndef PAPO_EMOJI_CATALOG_H
#define PAPO_EMOJI_CATALOG_H
#include <stddef.h>
#include <stdint.h>
typedef struct {
    const char *text, *name, *english, *keywords;
    const uint32_t *points;
    size_t length;
} unicode_emoji;
size_t emoji_catalog_count(void);
size_t emoji_catalog_index(const unicode_emoji *emoji);
size_t emoji_catalog_order(size_t position);
const unicode_emoji *emoji_catalog_get(size_t index);
/* Exact Unicode sequence matching, longest first; never consumes plain digits. */
const unicode_emoji *emoji_catalog_match(const uint32_t *text, size_t length, size_t *consumed);
#endif
