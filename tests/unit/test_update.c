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
    update_asset asset;
    update_parse result = update_parse_releases(json, strlen(json), prereleases, INSTALL_UNKNOWN,
                                                "x86_64", &latest, &asset);
    if (*asset.name) /* No installation: no file is ever chosen. */
        return UPDATE_PARSE_INVALID;
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
    update_asset asset;
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
    CHECK(update_parse_releases(NULL, 0, true, INSTALL_ARCH, "x86_64", &latest, &asset) ==
          UPDATE_PARSE_INVALID);
    GString *many = g_string_new("[");
    for (int i = 0; i < 101; ++i)
        g_string_append_printf(many, "%s" RELEASE("v1.0.%d", "false", "false"), i ? "," : "", i);
    g_string_append_c(many, ']');
    CHECK(parse(many->str, true, text) == UPDATE_PARSE_INVALID);
    g_string_free(many, TRUE);

    /* The file of the chosen release for each kind of installation, with the
     * size and digest GitHub publishes (real answer). */
    char *observed = NULL;
    CHECK(g_file_get_contents(argv[1], &observed, NULL, NULL));
    CHECK(update_parse_releases(observed, strlen(observed), true, INSTALL_APPIMAGE, "x86_64",
                                &latest, &asset) == UPDATE_PARSE_FOUND);
    CHECK(!strcmp(asset.name, "PapoTUI-1.0.0-beta.2-linux-x86_64.AppImage"));
    CHECK(
        !strcmp(asset.sha256, "6ed4a373fb1eb1c529b0e87d7caefbbf66f0ba13f31e7235f3fee87fa5ab1182"));
    CHECK(asset.size == 58468856 && update_asset_valid(&asset));
    CHECK(update_parse_releases(observed, strlen(observed), false, INSTALL_TARBALL, "x86_64",
                                &latest, &asset) == UPDATE_PARSE_FOUND);
    CHECK(!strcmp(asset.name, "papo-tui-0.5.0-linux-x86_64.tar.gz") && asset.size == 39274283);
    /* That release has no Windows package for ARM64: the version is still found. */
    CHECK(update_parse_releases(observed, strlen(observed), true, INSTALL_WINDOWS_INSTALLER,
                                "aarch64", &latest, &asset) == UPDATE_PARSE_FOUND);
    CHECK(!*asset.name && !update_asset_valid(&asset));
    CHECK(update_parse_releases(observed, strlen(observed), true, INSTALL_UNKNOWN, "x86_64",
                                &latest, &asset) == UPDATE_PARSE_FOUND &&
          !*asset.name);
    g_free(observed);

#define SUM "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
#define WITH_ASSET(digest, size)                                                                   \
    "{\"tag_name\":\"v3.0.0\",\"draft\":false,\"prerelease\":false,\"assets\":[{\"name\":"         \
    "\"other.txt\",\"size\":1,\"digest\":\"sha256:" SUM "\"},{\"name\":"                           \
    "\"papo-tui-3.0.0-arch-x86_64.pkg.tar.zst\"" digest size "}]}"
#define PARSE_ASSET(json)                                                                          \
    update_parse_releases(json, strlen(json), false, INSTALL_ARCH, "x86_64", &latest, &asset)
    CHECK(PARSE_ASSET(WITH_ASSET(",\"digest\":\"sha256:" SUM "\"", ",\"size\":4096")) ==
          UPDATE_PARSE_FOUND);
    CHECK(!strcmp(asset.name, "papo-tui-3.0.0-arch-x86_64.pkg.tar.zst"));
    CHECK(!strcmp(asset.sha256, SUM) && asset.size == 4096);
    /* A file that cannot be checked, or is beyond the limit, is not offered. */
    static const char *const unusable[] = {
        WITH_ASSET("", ",\"size\":4096"),
        WITH_ASSET(",\"digest\":null", ",\"size\":4096"),
        WITH_ASSET(",\"digest\":\"md5:" SUM "\"", ",\"size\":4096"),
        WITH_ASSET(",\"digest\":\"sha256:0123\"", ",\"size\":4096"),
        WITH_ASSET(",\"digest\":\"sha256:" SUM "0\"", ",\"size\":4096"),
        WITH_ASSET(",\"digest\":\"sha256:0123456789ABCDEF0123456789abcdef0123456789abcdef"
                   "0123456789abcdef\"",
                   ",\"size\":4096"),
        WITH_ASSET(",\"digest\":\"sha256:" SUM "\"", ""),
        WITH_ASSET(",\"digest\":\"sha256:" SUM "\"", ",\"size\":0"),
        WITH_ASSET(",\"digest\":\"sha256:" SUM "\"", ",\"size\":-5"),
        WITH_ASSET(",\"digest\":\"sha256:" SUM "\"", ",\"size\":\"4096\""),
        WITH_ASSET(",\"digest\":\"sha256:" SUM "\"", ",\"size\":268435457"),
        "{\"tag_name\":\"v3.0.0\",\"draft\":false,\"prerelease\":false}",
        "{\"tag_name\":\"v3.0.0\",\"draft\":false,\"prerelease\":false,\"assets\":{}}",
        "{\"tag_name\":\"v3.0.0\",\"draft\":false,\"prerelease\":false,\"assets\":[1,null]}",
    };
    for (size_t i = 0; i < sizeof(unusable) / sizeof(unusable[0]); ++i) {
        if (PARSE_ASSET(unusable[i]) != UPDATE_PARSE_FOUND || *asset.name) {
            fprintf(stderr, "offered an unusable file %zu\n", i);
            return 1;
        }
    }
    CHECK(PARSE_ASSET(WITH_ASSET(",\"digest\":\"sha256:" SUM "\"", ",\"size\":268435456")) ==
              UPDATE_PARSE_FOUND &&
          asset.size == UPDATE_ASSET_LIMIT);

    /* The download address is built here, from the version and the name. */
    CHECK(app_version_parse("3.0.0", &final));
    g_unsetenv("PAPO_UPDATE_API_URL");
    CHECK(update_asset_url(&final, &asset, url, sizeof(url)));
    CHECK(!strcmp(url, "https://github.com/prodesert22/papo-tui/releases/download/v3.0.0/"
                       "papo-tui-3.0.0-arch-x86_64.pkg.tar.zst"));
    CHECK(!update_asset_url(&final, &asset, url, 40));
    update_asset broken = asset;
    snprintf(broken.name, sizeof(broken.name), "../escape.deb");
    CHECK(!update_asset_valid(&broken) && !update_asset_url(&final, &broken, url, sizeof(url)));
    snprintf(broken.name, sizeof(broken.name), "a b.deb");
    CHECK(!update_asset_valid(&broken));
    snprintf(broken.name, sizeof(broken.name), ".hidden");
    CHECK(!update_asset_valid(&broken));
    broken = asset;
    memset(broken.name, 'a', sizeof(broken.name));
    CHECK(!update_asset_valid(&broken));
    broken = asset;
    broken.size = (uint64_t)UPDATE_ASSET_LIMIT + 1;
    CHECK(!update_asset_valid(&broken));
    CHECK(!update_asset_valid(NULL));

    /* Redirects stay on GitHub's own hosts, over https and its default port. */
    CHECK(update_redirect_allowed("https://github.com/a/b", NULL));
    CHECK(update_redirect_allowed("https://release-assets.githubusercontent.com/x?y=1", NULL));
    CHECK(update_redirect_allowed("https://objects.githubusercontent.com:443/x", NULL));
    static const char *const refused[] = {
        "http://github.com/a/b",
        "https://github.com.example.org/a",
        "https://example.org/github.com",
        "https://githubusercontent.com.example.org/x",
        "https://evilgithubusercontent.com/x",
        "https://user@github.com/a",
        "https://github.com:8443/a",
        "https://127.0.0.1/a",
        "ftp://github.com/a",
        "https://",
        "",
    };
    for (size_t i = 0; i < sizeof(refused) / sizeof(refused[0]); ++i) {
        if (update_redirect_allowed(refused[i], NULL)) {
            fprintf(stderr, "followed %s\n", refused[i]);
            return 1;
        }
    }
    CHECK(!update_redirect_allowed(NULL, NULL));
    /* With a test endpoint, only its own origin. */
    g_setenv("PAPO_UPDATE_API_URL", "https://127.0.0.1:9443/papo/github", TRUE);
    CHECK(update_asset_url(&final, &asset, url, sizeof(url)));
    CHECK(!strcmp(url, "https://127.0.0.1:9443/papo/github/download/v3.0.0/"
                       "papo-tui-3.0.0-arch-x86_64.pkg.tar.zst"));
    CHECK(update_redirect_allowed("https://127.0.0.1:9443/elsewhere", NULL));
    CHECK(!update_redirect_allowed("https://127.0.0.1:94430/elsewhere", NULL));
    CHECK(!update_redirect_allowed("https://127.0.0.1:9444/elsewhere", NULL));
    CHECK(!update_redirect_allowed("https://github.com/a/b", NULL));
    CHECK(!update_redirect_allowed("http://127.0.0.1:9443/elsewhere", NULL));

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
