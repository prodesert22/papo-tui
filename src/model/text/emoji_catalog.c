#include "model/text/emoji_catalog.h"
#include "model/text/emoji_data.inc"
#include <string.h>
size_t emoji_catalog_count(void)
{
    return sizeof(catalogue) / sizeof(*catalogue);
}
size_t emoji_catalog_index(const unicode_emoji *emoji)
{
    return (size_t)(emoji - catalogue);
}
size_t emoji_catalog_order(size_t position)
{
    return position < emoji_catalog_count() ? emoji_order[position] : emoji_catalog_count();
}
const unicode_emoji *emoji_catalog_get(size_t index)
{
    return index < emoji_catalog_count() ? &catalogue[index] : NULL;
}
const unicode_emoji *emoji_catalog_match(const uint32_t *text, size_t length, size_t *consumed)
{
    *consumed = 0;
    if (!length || text[0] < 0x23)
        return NULL;
    size_t low = 0, high = emoji_catalog_count();
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        if (catalogue[mid].points[0] < text[0])
            low = mid + 1;
        else
            high = mid;
    }
    const unicode_emoji *best = NULL;
    for (size_t i = low; i < emoji_catalog_count() && catalogue[i].points[0] == text[0]; ++i) {
        const unicode_emoji *e = &catalogue[i];
        if (e->length <= length && e->length > *consumed &&
            !memcmp(text, e->points, e->length * sizeof(*text))) {
            best = e;
            *consumed = e->length;
        }
    }
    /* Aliases include qualified variants of the same composed grapheme. */
    low = 0;
    high = sizeof(emoji_aliases) / sizeof(*emoji_aliases);
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        if (emoji_aliases[mid].points[0] < text[0])
            low = mid + 1;
        else
            high = mid;
    }
    for (size_t i = low; i < sizeof(emoji_aliases) / sizeof(*emoji_aliases) &&
                         emoji_aliases[i].points[0] == text[0];
         ++i) {
        const emoji_alias *a = &emoji_aliases[i];
        if (a->length <= length && a->length > *consumed &&
            !memcmp(text, a->points, a->length * sizeof(*text))) {
            best = &catalogue[a->index];
            *consumed = a->length;
        }
    }
    /* VS15 explicitly requests text presentation. */
    if (*consumed < length && text[*consumed] == 0xfe0e) {
        *consumed = 0;
        return NULL;
    }
    return best;
}
