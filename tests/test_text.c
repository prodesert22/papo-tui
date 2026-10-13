#define _POSIX_C_SOURCE 200809L
#include "ui/text.h"

#include <locale.h>
#include <stdio.h>
#include <string.h>

#define CHECK(value) do { if (!(value)) { fprintf(stderr, "check failed at line %d\n", __LINE__); return 1; } } while (0)

int main(void)
{
    CHECK(setlocale(LC_ALL, "C.UTF-8"));
    init_graphemebreak();
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

    puts("Unicode roundtrip, graphemes, cells, wrapping, cursor, controls and limits passed");
    return 0;
}
