#include "model/version.h"
#include <stdio.h>
#include <string.h>

#define NUMBER_DIGITS 6

/* Reads a decimal number without sign or leading zeros and advances *text. */
static bool number(const char **text, unsigned *value)
{
    const char *cursor = *text;
    unsigned result = 0;
    int digits = 0;
    while (*cursor >= '0' && *cursor <= '9') {
        if (++digits > NUMBER_DIGITS || (digits == 2 && **text == '0'))
            return false;
        result = result * 10U + (unsigned)(*cursor++ - '0');
    }
    if (!digits)
        return false;
    *text = cursor;
    *value = result;
    return true;
}
static bool expect(const char **text, const char *literal)
{
    size_t length = strlen(literal);
    if (strncmp(*text, literal, length))
        return false;
    *text += length;
    return true;
}
/* Parses the version at *text and leaves the cursor after it. */
static bool parse(const char **text, app_version *version)
{
    static const char *const stages[] = {"alpha.", "beta.", "rc."};
    app_version parsed = {.stage = VERSION_FINAL};
    const char *cursor = *text;
    if (!number(&cursor, &parsed.major) || !expect(&cursor, ".") ||
        !number(&cursor, &parsed.minor) || !expect(&cursor, ".") || !number(&cursor, &parsed.patch))
        return false;
    if (*cursor == '-') {
        const char *label = cursor + 1;
        for (unsigned stage = 0; stage < 3; ++stage) {
            const char *after = label;
            if (!expect(&after, stages[stage]))
                continue;
            if (!number(&after, &parsed.number) || !parsed.number)
                return false;
            parsed.stage = (version_stage)stage;
            cursor = after;
            break;
        }
    }
    *text = cursor;
    *version = parsed;
    return true;
}
bool app_version_parse(const char *text, app_version *version)
{
    return text && version && parse(&text, version) && !*text;
}
bool app_version_parse_build(const char *text, app_version *version, bool *development)
{
    if (!text || !version || !development || !parse(&text, version))
        return false;
    *development = !strcmp(text, "-dev");
    return *development || !*text;
}
int app_version_compare(const app_version *left, const app_version *right)
{
    const unsigned a[] = {left->major, left->minor, left->patch, left->stage, left->number};
    const unsigned b[] = {right->major, right->minor, right->patch, right->stage, right->number};
    for (size_t i = 0; i < sizeof(a) / sizeof(a[0]); ++i)
        if (a[i] != b[i])
            return a[i] < b[i] ? -1 : 1;
    return 0;
}
bool app_version_format(const app_version *version, char *text, size_t size)
{
    static const char *const stages[] = {"alpha", "beta", "rc"};
    int written =
        version->stage == VERSION_FINAL
            ? snprintf(text, size, "%u.%u.%u", version->major, version->minor, version->patch)
            : snprintf(text, size, "%u.%u.%u-%s.%u", version->major, version->minor, version->patch,
                       stages[version->stage], version->number);
    return written > 0 && (size_t)written < size;
}
