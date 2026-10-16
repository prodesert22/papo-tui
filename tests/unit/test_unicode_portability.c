#include "net/auth/client.h"
#include "platform/unicode.h"
#include "ui/input/text.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "Check failed: %s:%d\n", __FILE__, __LINE__);                          \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
int main(void)
{
    const char *examples[] = {"Olá",  "\xf0\x9f\x98\x80", "a\xcc\x81", "\xf0\x90\x90\x80",
                              "中文", "👩‍💻"};
    for (size_t i = 0; i < sizeof(examples) / sizeof(examples[0]); ++i) {
        text_buffer text;
        char output[TEXT_UTF8_BYTES];
        CHECK(text_decode(&text, examples[i]));
        CHECK(text_encode(&text, output));
        CHECK(!strcmp(output, examples[i]));
    }
    text_buffer text;
    CHECK(text_decode(&text, "\xf0\x9f\x98\x80"));
    CHECK(text.length == 1 && text.data[0] == 0x1f600);
    text_delete(&text, true);
    CHECK(text.length == 0);
    const char *invalid[] = {"\xc0\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xf0\x9f", "\x80"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
        CHECK(!text_decode(&text, invalid[i]));
    CHECK(text_decode(&text, "A\033]52;c;bad\007B"));
    for (size_t i = 0; i < text.length; ++i)
        CHECK(text.data[i] >= 32 && text.data[i] != 127);
    memset(&text, 0, sizeof(text));
    text.length = 1;
    text.data[0] = 0xd800;
    char output[TEXT_UTF8_BYTES];
    CHECK(!text_encode(&text, output));
    text.data[0] = 0x110000;
    CHECK(!text_encode(&text, output));
    auth_input_error field;
    char notice[192];
    CHECK(auth_validate_registration("😀ab", "Ábcdefg!", &field, notice, sizeof(notice)));
    CHECK(!auth_validate_registration("ab", "Ábcdefg!", &field, notice, sizeof(notice)) &&
          field == AUTH_INPUT_USERNAME);
    CHECK(!auth_validate_registration("abc", "abcdefg!", &field, notice, sizeof(notice)) &&
          field == AUTH_INPUT_PASSWORD);
    return 0;
}
