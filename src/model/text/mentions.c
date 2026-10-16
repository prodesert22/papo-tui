#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "model/text/mentions.h"
#include <ctype.h>
#include <glib.h>
#include <string.h>
#include <strings.h>
static GRegex *expression;
bool mention_next(const char *text, size_t from, mention_range *r)
{
    if (!text || from > strlen(text))
        return false;
    if (!expression) {
        expression = g_regex_new(
            "@mention\\(<@([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12})>\\)",
            G_REGEX_CASELESS | G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
        if (!expression)
            return false;
    }
    GMatchInfo *match = NULL;
    if (!g_regex_match(expression, text + from, 0, &match)) {
        g_match_info_free(match);
        return false;
    }
    int start, end, id_start, id_end;
    bool valid = g_match_info_fetch_pos(match, 0, &start, &end) &&
                 g_match_info_fetch_pos(match, 1, &id_start, &id_end) && id_end - id_start == 36;
    g_match_info_free(match);
    if (!valid)
        return false;
    r->start = from + (size_t)start;
    r->end = from + (size_t)end;
    for (int i = 0; i < 36; ++i)
        r->id[i] = g_ascii_tolower(text[from + (size_t)id_start + (size_t)i]);
    r->id[36] = 0;
    return true;
}
bool mention_contains(const char *text, const char *id)
{
    if (!id)
        return false;
    mention_range r;
    size_t from = 0;
    while (mention_next(text, from, &r)) {
        if (!strcasecmp(r.id, id))
            return true;
        from = r.end;
    }
    return false;
}
static void visit_text(const json_t *object, const char *key, mention_visitor visitor,
                       void *context)
{
    const char *value = json_string_value(json_object_get(object, key));
    mention_range range;
    size_t from = 0;
    while (mention_next(value, from, &range)) {
        visitor(context, &range);
        from = range.end;
    }
}
void mention_visit_message(const json_t *message, mention_visitor visitor, void *context)
{
    if (!visitor)
        return;
    visit_text(message, "content", visitor, context);
    const json_t *embeds = json_object_get(message, "embeds");
    for (size_t i = 0; i < json_array_size(embeds) && i < 10; ++i) {
        const json_t *embed = json_array_get(embeds, i);
        const char *keys[] = {"title", "description", "site_name", "provider", "url"};
        for (size_t k = 0; k < sizeof(keys) / sizeof(keys[0]); ++k)
            visit_text(embed, keys[k], visitor, context);
        visit_text(json_object_get(embed, "author"), "name", visitor, context);
        visit_text(json_object_get(embed, "footer"), "text", visitor, context);
        const json_t *fields = json_object_get(embed, "fields");
        for (size_t j = 0; j < json_array_size(fields) && j < 25; ++j) {
            visit_text(json_array_get(fields, j), "name", visitor, context);
            visit_text(json_array_get(fields, j), "value", visitor, context);
        }
    }
}
typedef struct {
    const char *id;
    bool found;
} mention_match;
static void match_user(void *context, const mention_range *range)
{
    mention_match *match = context;
    match->found |= !strcasecmp(range->id, match->id);
}
bool mention_message_contains(const json_t *message, const char *id)
{
    if (!id)
        return false;
    mention_match match = {id, false};
    mention_visit_message(message, match_user, &match);
    return match.found;
}
void mention_cleanup(void)
{
    g_clear_pointer(&expression, g_regex_unref);
}
