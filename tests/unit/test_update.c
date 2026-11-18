#include "net/update.h"

#include <glib.h>
#include <stdio.h>
#include <string.h>

#define CHECK(value)                                                                               \
    do {                                                                                           \
        if (!(value)) {                                                                            \
            fprintf(stderr, "check failed at line %d\n", __LINE__);                                \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static update_parse parse(const char *json, bool prereleases, char text[APP_VERSION_TEXT])
{
    app_version latest;
    update_parse result = update_parse_releases(json, strlen(json), prereleases, &latest);
    text[0] = 0;
    if (result == UPDATE_PARSE_FOUND)
        app_version_format(&latest, text, APP_VERSION_TEXT);
    return result;
}
static update_parse parse_file(const char *path, bool prereleases, char text[APP_VERSION_TEXT])
{
    char *json = NULL;
    if (!g_file_get_contents(path, &json, NULL, NULL))
        return UPDATE_PARSE_INVALID;
    update_parse result = parse(json, prereleases, text);
    g_free(json);
    return result;
}
#define RELEASE(tag, draft, prerelease)                                                            \
    "{\"tag_name\":\"" tag "\",\"draft\":" draft ",\"prerelease\":" prerelease "}"

int main(int argc, char **argv)
{
    char text[APP_VERSION_TEXT], url[UPDATE_URL_BYTES];
    app_version latest, beta, final;
    bool list = false;
    CHECK(argc == 3);

    /* Real answers: the newest release is a pre-release, the latest final is older. */
    CHECK(parse_file(argv[1], true, text) == UPDATE_PARSE_FOUND && !strcmp(text, "1.0.0-beta.2"));
    CHECK(parse_file(argv[1], false, text) == UPDATE_PARSE_FOUND && !strcmp(text, "0.5.0"));
    CHECK(parse_file(argv[2], false, text) == UPDATE_PARSE_FOUND && !strcmp(text, "0.5.0"));
    CHECK(parse_file(argv[2], true, text) == UPDATE_PARSE_FOUND && !strcmp(text, "0.5.0"));

    /* The highest version wins whatever the order of the list. */
    CHECK(parse("[" RELEASE("v1.0.0-beta.9", "false", "true") "," RELEASE(
                    "v1.0.0-beta.10", "false", "true") "," RELEASE("v0.9.9", "false", "false") "]",
                true, text) == UPDATE_PARSE_FOUND &&
          !strcmp(text, "1.0.0-beta.10"));
    CHECK(parse("[" RELEASE("v1.0.0-rc.1", "false", "true") "," RELEASE("v1.0.0", "false",
                                                                        "false") "]",
                true, text) == UPDATE_PARSE_FOUND &&
          !strcmp(text, "1.0.0"));
    /* Drafts, foreign tags and unpublished kinds are not offered. */
    CHECK(
        parse(
            "[" RELEASE("v9.0.0", "true", "false") "," RELEASE("nightly", "false", "false") "," RELEASE(
                "V8.0.0", "false",
                "false") "," RELEASE("v7.0", "false",
                                     "false") "," RELEASE("8.0.0", "false",
                                                          "false") "," RELEASE("v1.2.3", "false",
                                                                               "false") "]",
            true, text) == UPDATE_PARSE_FOUND &&
        !strcmp(text, "1.2.3"));
    CHECK(parse(RELEASE("v2.0.0-beta.1", "false", "true"), false, text) == UPDATE_PARSE_NONE);
    CHECK(parse(RELEASE("v2.0.0", "false", "true"), false, text) == UPDATE_PARSE_NONE);
    CHECK(parse(RELEASE("v2.0.0", "false", "true"), true, text) == UPDATE_PARSE_FOUND);
    CHECK(parse(RELEASE("v2.0.0", "true", "false"), true, text) == UPDATE_PARSE_NONE);
    CHECK(parse("[]", true, text) == UPDATE_PARSE_NONE);

    /* Anything that is not the documented shape is refused whole. */
    static const char *const invalid[] = {
        "",
        "null",
        "\"v1.0.0\"",
        "{}",
        "[1]",
        "[null]",
        "{\"tag_name\":\"v1.0.0\"}",
        "{\"tag_name\":1,\"draft\":false,\"prerelease\":false}",
        "{\"tag_name\":\"v1.0.0\",\"draft\":0,\"prerelease\":false}",
        "{\"tag_name\":\"v1.0.0\",\"draft\":false,\"prerelease\":\"no\"}",
        "{\"tag_name\":\"v1.0.0\\u0000x\",\"draft\":false,\"prerelease\":false}",
        "{\"tag_name\":\"v1.0.0\",\"tag_name\":\"v9.0.0\",\"draft\":false,\"prerelease\":false}",
        "[" RELEASE("v1.0.0", "false", "false") ",{}]",
        "[" RELEASE("v1.0.0", "false", "false"),
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        if (parse(invalid[i], true, text) != UPDATE_PARSE_INVALID) {
            fprintf(stderr, "accepted invalid answer %zu\n", i);
            return 1;
        }
    }
    CHECK(update_parse_releases(NULL, 0, true, &latest) == UPDATE_PARSE_INVALID);
    GString *many = g_string_new("[");
    for (int i = 0; i < 101; ++i)
        g_string_append_printf(many, "%s" RELEASE("v1.0.%d", "false", "false"), i ? "," : "", i);
    g_string_append_c(many, ']');
    CHECK(parse(many->str, true, text) == UPDATE_PARSE_INVALID);
    g_string_free(many, TRUE);

    /* A final build asks for the latest final release; a pre-release lists them. */
    CHECK(app_version_parse("1.0.0-beta.2", &beta) && app_version_parse("1.0.0", &final));
    g_unsetenv("PAPO_UPDATE_API_URL");
    CHECK(update_request_url(&beta, url, sizeof(url), &list) && list);
    CHECK(!strcmp(url, "https://api.github.com/repos/prodesert22/papo-tui/releases?per_page=5"));
    CHECK(update_request_url(&final, url, sizeof(url), &list) && !list);
    CHECK(!strcmp(url, "https://api.github.com/repos/prodesert22/papo-tui/releases/latest"));
    g_setenv("PAPO_UPDATE_API_URL", "https://127.0.0.1:9/fake", TRUE);
    CHECK(update_request_url(&beta, url, sizeof(url), &list) && list);
    CHECK(!strcmp(url, "https://127.0.0.1:9/fake/releases?per_page=5"));
    CHECK(!update_request_url(&final, url, 16, &list));
    g_setenv("PAPO_UPDATE_API_URL", "", TRUE);
    CHECK(update_request_url(&final, url, sizeof(url), &list));
    CHECK(!strcmp(url, "https://api.github.com/repos/prodesert22/papo-tui/releases/latest"));
    return 0;
}
