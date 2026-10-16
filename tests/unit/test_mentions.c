#include "model/text/mentions.h"
#include "ui/input/text.h"
#include "ui/media/message_blocks.h"
#include <assert.h>
#include <locale.h>
#include <stdio.h>
#include <string.h>
static const char *name(void *ctx, const char *id)
{
    (void)ctx;
    (void)id;
    return "Álice 界";
}
int main(void)
{
    assert(setlocale(LC_ALL, ""));
    init_graphemebreak();
    const char *id = "00000000-0000-4000-8000-000000000001";
    const char *token = "@mention(<@00000000-0000-4000-8000-000000000001>)";
    mention_range r;
    assert(mention_next(token, 0, &r) && r.start == 0 && r.end == 49 && !strcmp(r.id, id));
    assert(!mention_next("@mention(<@bad>)", 0, &r));
    assert(mention_contains(token, id));
    text_buffer b, d;
    assert(text_decode(&b, token));
    text_mentions(&b, name, NULL);
    text_project(&b, &d);
    char visible[TEXT_UTF8_BYTES];
    assert(text_encode(&d, visible));
    assert(!strcmp(visible, "@Álice 界"));
    text_layout l;
    text_measure(&b, 20, &l);
    assert(l.cursor_column == 9);
    assert(l.cells[0].mention);
    b.cursor = 0;
    text_horizontal(&b, true);
    assert(b.cursor == 49);
    text_delete(&b, true);
    assert(b.length == 0);
    assert(text_decode(&b, token));
    text_mentions(&b, name, NULL);
    b.cursor = 0;
    text_delete(&b, false);
    assert(!b.length);
    assert(text_decode(&b, token));
    text_mentions(&b, name, NULL);
    text_click(&b, 20, 0, 4);
    assert(b.cursor == 0 || b.cursor == 49);
    b.cursor = b.length;
    assert(text_edit(&b, KEY_CODE_YES, TEXT_SHIFT_LEFT, 20, true));
    size_t lo, hi;
    assert(text_selection(&b, &lo, &hi) && lo == 0 && hi == 49);
    assert(text_insert(&b, 'X', true) && b.length == 1 && b.data[0] == 'X');
    char draft[100];
    snprintf(draft, sizeof(draft), "%s próxima", token);
    assert(text_decode(&b, draft));
    text_mentions(&b, name, NULL);
    b.cursor = 0;
    assert(text_edit(&b, KEY_CODE_YES, TEXT_CTRL_RIGHT, 20, true) && b.cursor == 49);
    assert(text_edit(&b, KEY_CODE_YES, TEXT_CTRL_RIGHT, 20, true) && b.cursor == 57);
    assert(text_edit(&b, KEY_CODE_YES, TEXT_CTRL_LEFT, 20, true) && b.cursor == 50);
    assert(text_edit(&b, KEY_CODE_YES, TEXT_CTRL_LEFT, 20, true) && b.cursor == 0);
    json_t *m = json_pack("{s:s,s:s,s:s}", "id", id, "content", token, "created_at",
                          "2026-10-12T12:00:00Z");
    message_blocks blocks;
    assert(message_blocks_build_mentions(&blocks, m, "Autor", "", 20, false, name, NULL, id));
    assert(blocks.self_mention);
    bool colored = false;
    for (size_t i = 0; i < blocks.count; ++i)
        for (int j = 0; j < blocks.rows[i].count; ++j) {
            message_segment *s = &blocks.rows[i].segments[j];
            for (size_t k = 0; k < s->highlight_length; ++k)
                colored |= s->highlight[k] != 0;
        }
    assert(colored);
    message_blocks_free(&blocks);
    char long_text[2200];
    memset(long_text, 'a', 2030);
    strcpy(long_text + 2030, token);
    json_object_set_new(m, "content", json_string(long_text));
    assert(message_blocks_build_mentions(&blocks, m, "Autor", "", 40, false, name, NULL, id));
    colored = false;
    for (size_t i = 0; i < blocks.count; ++i)
        for (int j = 0; j < blocks.rows[i].count; ++j) {
            message_segment *segment = &blocks.rows[i].segments[j];
            assert(strstr(segment->text, "@mention(") == NULL);
            for (size_t k = 0; k < segment->highlight_length; ++k)
                colored |= segment->highlight[k] != 0;
        }
    assert(colored);
    message_blocks_free(&blocks);
    /* All displayed embed text resolves and highlights canonical mentions,
     * including tokens crossing a decoder chunk and inline field columns. */
    json_object_set_new(m, "content", json_null());
    json_t *embed =
        json_pack("{s:s,s:s,s:s,s:{s:s},s:{s:s},s:[{s:s,s:s,s:b},{s:s,s:s,s:b}]}", "title", token,
                  "description", long_text, "site_name", draft, "author", "name", token, "footer",
                  "text", token, "fields", "name", token, "value", token, "inline", 1, "name",
                  token, "value", token, "inline", 1);
    assert(embed);
    json_t *embeds = json_array();
    assert(embeds && !json_array_append_new(embeds, embed));
    assert(!json_object_set_new(m, "embeds", embeds));
    assert(mention_message_contains(m, id));
    assert(!mention_message_contains(m, "00000000-0000-4000-8000-000000000002"));
    for (int width = 18; width <= 76; width += 29) {
        assert(
            message_blocks_build_mentions(&blocks, m, "Autor", "", width, false, name, NULL, id));
        assert(blocks.self_mention);
        size_t marked = 0;
        for (size_t i = 0; i < blocks.count; ++i)
            for (int j = 0; j < blocks.rows[i].count; ++j) {
                message_segment *segment = &blocks.rows[i].segments[j];
                assert(!strstr(segment->text, "@mention("));
                if (!blocks.rows[i].card)
                    continue;
                for (size_t k = 0; k < segment->highlight_length; ++k)
                    marked += segment->highlight[k] != 0;
            }
        assert(marked == 9 * 8); /* Nine @Álice 界 labels, eight Unicode scalars each. */
        message_blocks_free(&blocks);
    }
    json_object_set_new(embed, "description", json_null());
    json_object_set_new(embed, "title", json_string("Plain title"));
    assert(message_blocks_build_mentions(&blocks, m, "Autor", "", 76, false, name, NULL, id));
    bool plain_bold = false;
    for (size_t i = 0; i < blocks.count; ++i)
        for (int j = 0; j < blocks.rows[i].count; ++j) {
            message_segment *segment = &blocks.rows[i].segments[j];
            if (!strcmp(segment->text, "Plain title"))
                plain_bold = segment->bold && !segment->highlight;
        }
    assert(plain_bold);
    message_blocks_free(&blocks);
    json_decref(m);
    mention_cleanup();
    return 0;
}
