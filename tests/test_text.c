#define _POSIX_C_SOURCE 200809L
#include "ui/text.h"

#include <locale.h>
#include <stdio.h>
#include <string.h>
#include <wordbreak.h>

#define CHECK(value) do { if (!(value)) { fprintf(stderr, "check failed at line %d\n", __LINE__); return 1; } } while (0)

int main(void)
{
    CHECK(setlocale(LC_ALL, "C.UTF-8"));
    init_graphemebreak();
    init_wordbreak();
    text_buffer buffer;
    text_layout layout;
    char output[TEXT_UTF8_BYTES];

    CHECK(text_decode(&buffer, "ação界語\ncafe\u0301"));
    CHECK(text_encode(&buffer, output));
    CHECK(strcmp(output, "ação界語\ncafe\u0301") == 0);
    text_horizontal(&buffer, false);
    CHECK(buffer.cursor == buffer.length - 2);
    text_horizontal(&buffer, true);
    text_delete(&buffer, true);
    CHECK(text_encode(&buffer, output));
    CHECK(strcmp(output, "ação界語\ncaf") == 0);

    CHECK(text_decode(&buffer, "a👨‍👩‍👧‍👦🇧🇷"));
    CHECK(buffer.length == 10);
    text_horizontal(&buffer, false);
    CHECK(buffer.cursor == 8);
    text_horizontal(&buffer, false);
    CHECK(buffer.cursor == 1);
    text_delete(&buffer, false);
    CHECK(text_encode(&buffer, output));
    CHECK(strcmp(output, "a🇧🇷") == 0);
    text_delete(&buffer, false);
    CHECK(buffer.length == 1 && buffer.cursor == 1);

    CHECK(text_decode(&buffer, "界\na\u0301b"));
    text_measure(&buffer, 10, &layout);
    CHECK(layout.rows == 2 && layout.cursor_row == 1 && layout.cursor_column == 2);
    text_vertical(&buffer, 10, -1);
    CHECK(buffer.cursor == 1);
    text_vertical(&buffer, 10, 1);
    CHECK(buffer.cursor == buffer.length);
    text_line_edge(&buffer, 10, false);
    CHECK(buffer.cursor == 2);
    text_line_edge(&buffer, 10, true);
    CHECK(buffer.cursor == buffer.length);
    text_click(&buffer, 10, 0, 1);
    CHECK(buffer.cursor == 0); /* Never select the second cell of a wide glyph. */

    CHECK(text_decode(&buffer, "ab界c\ndefgh"));
    text_measure(&buffer, 5, &layout);
    CHECK(layout.rows == 4);
    for (size_t i = 0; i < layout.count; ++i) {
        CHECK(layout.cells[i].column >= 0 && layout.cells[i].column < 5);
        CHECK(layout.cells[i].column + layout.cells[i].width < 5);
    }
    text_keep_cursor(&buffer, &layout, 2);
    CHECK(buffer.first_row == 2);
    text_measure(&buffer, 30, &layout);
    text_keep_cursor(&buffer, &layout, 3);
    CHECK(buffer.first_row == 0);

    CHECK(text_decode(&buffer, "abcdefgh"));
    text_click(&buffer, 5, 0, 1);
    text_line_edge(&buffer, 5, true);
    text_measure(&buffer, 5, &layout);
    CHECK(buffer.cursor == 4 && layout.cursor_row == 0 && layout.cursor_column == 4);
    text_vertical(&buffer, 5, 1);
    CHECK(buffer.cursor == 8);
    text_vertical(&buffer, 5, -1);
    text_measure(&buffer, 5, &layout);
    CHECK(buffer.cursor == 4 && layout.cursor_row == 0);
    text_line_edge(&buffer, 5, false);
    CHECK(buffer.cursor == 0);
    text_click(&buffer, 5, 0, 9);
    text_measure(&buffer, 5, &layout);
    CHECK(buffer.cursor == 4 && layout.cursor_row == 0);
    text_horizontal(&buffer, true);
    text_measure(&buffer, 5, &layout);
    CHECK(buffer.cursor == 4 && layout.cursor_row == 1 && layout.cursor_column == 0);

    CHECK(text_decode(&buffer, "a\u0301"));
    buffer.cursor = 0;
    CHECK(text_insert(&buffer, 'b', true));
    CHECK(buffer.cursor == 1);
    CHECK(text_insert(&buffer, 0x0302, true));
    CHECK(buffer.cursor == 2);
    text_delete(&buffer, true);
    CHECK(text_encode(&buffer, output));
    CHECK(strcmp(output, "a\u0301") == 0);

    CHECK(text_decode(&buffer, "x👨‍👩‍👧‍👦❤\ufe0f"));
    text_measure(&buffer, 40, &layout);
    CHECK(layout.count == 4);
    CHECK(layout.cells[1].glyph[0] == '?' && layout.cells[1].width == 1);
    CHECK(layout.cells[2].glyph[0] == '?' && layout.cells[2].width == 1);
    CHECK(text_encode(&buffer, output));
    CHECK(strcmp(output, "x👨‍👩‍👧‍👦❤\ufe0f") == 0);

    text_clear(&buffer);
    CHECK(!text_insert(&buffer, 27, true));
    CHECK(!text_insert(&buffer, 127, true));
    CHECK(!text_insert(&buffer, 0xd800, true));
    CHECK(!text_insert(&buffer, 0x110000, true));
    CHECK(!text_insert(&buffer, '\n', false));
    CHECK(text_insert(&buffer, '\t', true));
    CHECK(buffer.data[0] == ' ');
    CHECK(text_insert(&buffer, '\r', true));
    CHECK(buffer.data[1] == '\n');
    text_clear(&buffer);
    for (int i = 0; i < TEXT_LIMIT; ++i) CHECK(text_insert(&buffer, 0x754c, true));
    CHECK(!text_insert(&buffer, 'x', true));
    CHECK(buffer.length == TEXT_LIMIT);
    CHECK(text_encode(&buffer, output));
    CHECK(strlen(output) == (size_t)TEXT_LIMIT * 3);
    text_delete(&buffer, true);
    CHECK(buffer.length == TEXT_LIMIT - 1);

    /* Reading can exceed the editor's capacity and must retain a combining
     * cluster straddling that boundary, without emitting terminal controls. */
    char remote[TEXT_LIMIT + 32];
    memset(remote, 'a', TEXT_LIMIT - 1);
    strcpy(remote + TEXT_LIMIT - 1, "e\u0301fim\033]52;c;ignored\a");
    const char *part = remote;
    CHECK(text_decode_chunk(&buffer, &part));
    CHECK(buffer.length == TEXT_LIMIT - 1);
    CHECK(*part == 'e');
    CHECK(text_decode_chunk(&buffer, &part));
    CHECK(!*part);
    CHECK(text_encode(&buffer, output));
    CHECK(strcmp(output, "e\u0301fim]52;c;ignored") == 0);

    CHECK(text_decode(&buffer, "ação duas palavras"));
    text_edit(&buffer, KEY_CODE_YES, TEXT_CTRL_HOME, 12, true);
    text_edit(&buffer, KEY_CODE_YES, TEXT_CTRL_RIGHT, 12, true);
    CHECK(buffer.cursor == 4);
    text_edit(&buffer, KEY_CODE_YES, TEXT_CTRL_SHIFT_RIGHT, 12, true);
    size_t lo, hi;
    CHECK(text_selection(&buffer, &lo, &hi) && lo == 4 && hi == 9);
    CHECK(text_insert(&buffer, 'X', true));
    CHECK(text_encode(&buffer, output) && !strcmp(output, "açãoX palavras"));
    text_edit(&buffer, KEY_CODE_YES, TEXT_SHIFT_LEFT, 12, true);
    text_edit(&buffer, KEY_CODE_YES, KEY_LEFT, 12, true);
    CHECK(buffer.cursor == 4 && !text_selection(&buffer, NULL, NULL));
    text_edit(&buffer, KEY_CODE_YES, TEXT_CTRL_RIGHT, 12, true);
    text_edit(&buffer, KEY_CODE_YES, TEXT_CTRL_LEFT, 12, true);
    CHECK(buffer.cursor == 0);

    CHECK(text_decode(&buffer, "ação  duas\npalavras"));
    const size_t ends[] = {4, 10, 19, 19};
    buffer.cursor = 0;
    for (size_t i = 0; i < sizeof(ends) / sizeof(ends[0]); ++i) {
        CHECK(text_edit(&buffer, KEY_CODE_YES, TEXT_CTRL_RIGHT, 12, true));
        CHECK(buffer.cursor == ends[i]);
    }
    const size_t starts[] = {11, 6, 0, 0};
    for (size_t i = 0; i < sizeof(starts) / sizeof(starts[0]); ++i) {
        CHECK(text_edit(&buffer, KEY_CODE_YES, TEXT_CTRL_LEFT, 12, true));
        CHECK(buffer.cursor == starts[i]);
    }
    buffer.cursor = 2;
    CHECK(text_edit(&buffer, KEY_CODE_YES, TEXT_CTRL_RIGHT, 12, true) && buffer.cursor == 4);
    buffer.cursor = 8;
    CHECK(text_edit(&buffer, KEY_CODE_YES, TEXT_CTRL_LEFT, 12, true) && buffer.cursor == 6);
    CHECK(text_decode(&buffer, "a\u0301bc  "));
    buffer.cursor = 0;
    CHECK(text_edit(&buffer, KEY_CODE_YES, TEXT_CTRL_RIGHT, 12, true) && buffer.cursor == 4);
    CHECK(text_edit(&buffer, KEY_CODE_YES, TEXT_CTRL_RIGHT, 12, true) && buffer.cursor == 6);
    CHECK(text_edit(&buffer, KEY_CODE_YES, TEXT_CTRL_LEFT, 12, true) && buffer.cursor == 0);

    CHECK(text_decode(&buffer, "a\u0301界fim"));
    text_edit(&buffer, KEY_CODE_YES, TEXT_CTRL_HOME, 20, true);
    text_edit(&buffer, KEY_CODE_YES, TEXT_SHIFT_RIGHT, 20, true);
    CHECK(text_selection(&buffer, &lo, &hi) && lo == 0 && hi == 2);
    text_delete(&buffer, false);
    CHECK(text_encode(&buffer, output) && !strcmp(output, "界fim"));
    text_click(&buffer, 20, 0, 0); text_drag(&buffer, 20, 0, 2);
    CHECK(text_selection(&buffer, &lo, &hi) && hi == 1);
    text_buffer insertion; CHECK(text_decode(&insertion, "texto\ncolado"));
    CHECK(text_insert_buffer(&buffer, &insertion));
    CHECK(text_encode(&buffer, output) && !strcmp(output, "texto\ncoladofim"));
    text_edit(&buffer, OK, 3, 20, true); CHECK(buffer.length == 0);
    CHECK(text_decode(&buffer, "texto intacto"));
    memset(&insertion, 0, sizeof(insertion)); insertion.length = TEXT_LIMIT;
    CHECK(!text_insert_buffer(&buffer, &insertion));
    CHECK(text_encode(&buffer, output) && !strcmp(output, "texto intacto"));
    puts("Unicode, selection, words, atomic insertion, wrapping, controls and limits passed");
    return 0;
}
