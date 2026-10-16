#define _POSIX_C_SOURCE 200809L
#include "model/text/emoji_usage.h"
#include "ui/components/emoji_picker.h"
#include <assert.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>

static chat_assets *assets(void)
{
    chat_assets *a = chat_assets_create();
    assert(a);
    chat_assets_emojis_request(a, false);
    json_t *page =
        json_pack("{s:[{s:s,s:s,s:s,s:s,s:s}],s:b}", "emojis", "id",
                  "00000000-0000-4000-8000-000000000001", "name", "gato_fixture", "format", "PNG",
                  "created_at", "2026-10-14T10:00:00Z", "image_blob", "AQID", "has_more", 0);
    assert(page && chat_assets_emojis_response(a, page, 200));
    json_decref(page);
    return a;
}
static void insert(emoji_picker *p, text_buffer *b, const char *query, const char *expected)
{
    assert(text_decode(b, query));
    emoji_picker_suggest(p, b, true);
    assert(emoji_picker_suggesting(p));
    bool inserted;
    assert(emoji_picker_key(p, b, OK, '\t', &inserted) && inserted);
    assert(!emoji_picker_suggesting(p));
    char value[TEXT_UTF8_BYTES];
    assert(text_encode(b, value));
    if (strcmp(value, expected))
        fprintf(stderr, "Completion for %s: expected %s, got %s\n", query, expected, value);
    assert(!strcmp(value, expected));
}
int main(void)
{
    char *directory = g_dir_make_tmp("papo-completion-XXXXXX", NULL);
    assert(directory && g_setenv("XDG_CONFIG_HOME", directory, true));
    chat_assets *a = assets();
    emoji_picker *p = emoji_picker_create(a, NULL, "https://one.example", "account");
    assert(p);
    text_buffer b;
    insert(p, &b, ":rocket", "🚀");
    insert(p, &b, ":gato_fixture", ":gato_fixture:");
    insert(p, &b, ":gato_FIXTURE", ":gato_fixture:");
    insert(p, &b, ":", ":gato_fixture:");

    assert(text_decode(&b, "antes :gato_fixture: depois"));
    b.cursor = 10; /* Replace the complete token while editing its middle. */
    emoji_picker_suggest(p, &b, true);
    bool inserted;
    assert(emoji_picker_key(p, &b, OK, '\n', &inserted) && inserted);
    char value[TEXT_UTF8_BYTES];
    assert(text_encode(&b, value) && !strcmp(value, "antes :gato_fixture: depois"));
    const char *literal[] = {"https:", "12:30", "\\:gato", "`:gato", ":gato_fixture:", "::"};
    for (size_t i = 0; i < G_N_ELEMENTS(literal); ++i) {
        assert(text_decode(&b, literal[i]));
        emoji_picker_suggest(p, &b, true);
        assert(!emoji_picker_suggesting(p));
    }
    assert(text_decode(&b, ":unknown_fixture_zzzz"));
    emoji_picker_suggest(p, &b, true);
    text_buffer before = b;
    assert(emoji_picker_key(p, &b, OK, '\n', &inserted) && !inserted);
    assert(!memcmp(b.data, before.data, sizeof(b.data)));
    emoji_picker_key(p, &b, OK, 27, &inserted);
    emoji_picker_suggest(p, &b, true);
    assert(!emoji_picker_suggesting(p));
    text_delete(&b, true);
    emoji_picker_suggest(p, &b, true);
    assert(emoji_picker_suggesting(p));

    text_clear(&b);
    b.length = b.cursor = TEXT_LIMIT;
    for (size_t i = 0; i < TEXT_LIMIT - 3; ++i)
        b.data[i] = 'x';
    b.data[TEXT_LIMIT - 3] = ' ';
    b.data[TEXT_LIMIT - 2] = ':';
    b.data[TEXT_LIMIT - 1] = 'g';
    emoji_picker_suggest(p, &b, true);
    before = b;
    assert(emoji_picker_key(p, &b, OK, '\t', &inserted) && !inserted);
    assert(b.length == before.length && b.cursor == before.cursor &&
           !memcmp(b.data, before.data, sizeof(b.data)));
    emoji_picker_destroy(p);

    p = emoji_picker_create(a, NULL, "https://one.example", "account");
    insert(p, &b, ":", ":gato_fixture:");
    emoji_picker_destroy(p);
    p = emoji_picker_create(a, NULL, "https://two.example", "account");
    insert(p, &b, ":", "😀");
    emoji_picker_destroy(p);
    p = emoji_picker_create(a, NULL, "https://one.example", "other-account");
    insert(p, &b, ":", "😀");
    emoji_picker_destroy(p);

    /* Storage errors must preserve the draft and keep ranking in memory. */
    char *context = g_strdup_printf("%s\n%s", "https://broken.example", "account");
    char *digest = g_compute_checksum_for_string(G_CHECKSUM_SHA256, context, -1);
    char *name = g_strconcat(digest, ".json", NULL);
    char *folder = g_build_filename(directory, "papo-tui", "emoji-usage", NULL);
    char *path = g_build_filename(folder, name, NULL);
    assert(g_file_set_contents(path, "broken", -1, NULL));
    p = emoji_picker_create(a, NULL, "https://broken.example", "account");
    insert(p, &b, ":gato_fixture", ":gato_fixture:");
    insert(p, &b, ":", ":gato_fixture:");
    emoji_picker_destroy(p);
    char *data;
    assert(g_file_get_contents(path, &data, NULL, NULL) && !strcmp(data, "broken"));
    g_free(data);
    g_free(context);
    g_free(digest);
    g_free(name);
    g_free(path);
    GDir *dir = g_dir_open(folder, 0, NULL);
    assert(dir);
    while ((name = (char *)g_dir_read_name(dir))) {
        path = g_build_filename(folder, name, NULL);
        assert(!g_remove(path));
        g_free(path);
    }
    g_dir_close(dir);
    assert(!g_rmdir(folder));
    g_free(folder);
    folder = g_build_filename(directory, "papo-tui", NULL);
    assert(!g_rmdir(folder) && !g_rmdir(directory));
    g_free(folder);
    g_free(directory);
    chat_assets_destroy(a);
    return 0;
}
