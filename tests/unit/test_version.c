#include "model/version.h"

#include <stdio.h>
#include <string.h>

#define CHECK(value)                                                                               \
    do {                                                                                           \
        if (!(value)) {                                                                            \
            fprintf(stderr, "check failed at line %d\n", __LINE__);                                \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static int order(const char *left, const char *right)
{
    app_version a, b;
    if (!app_version_parse(left, &a) || !app_version_parse(right, &b))
        return 99;
    return app_version_compare(&a, &b);
}

int main(void)
{
    app_version version;
    char text[APP_VERSION_TEXT];
    bool development = true;

    CHECK(app_version_parse("1.0.0-beta.2", &version));
    CHECK(version.major == 1 && version.minor == 0 && version.patch == 0);
    CHECK(version.stage == VERSION_BETA && version.number == 2);
    CHECK(app_version_format(&version, text, sizeof(text)) && !strcmp(text, "1.0.0-beta.2"));
    CHECK(app_version_parse("0.5.0", &version));
    CHECK(version.stage == VERSION_FINAL && version.number == 0);
    CHECK(app_version_format(&version, text, sizeof(text)) && !strcmp(text, "0.5.0"));
    CHECK(app_version_parse("12.345.999999-rc.10", &version));
    CHECK(version.patch == 999999 && version.stage == VERSION_RC && version.number == 10);
    CHECK(!app_version_format(&version, text, 8));

    /* The release tag grammar: nothing else is a version. */
    static const char *const rejected[] = {
        "",
        "1",
        "1.0",
        "1.0.0.0",
        "v1.0.0",
        "1.0.0 ",
        " 1.0.0",
        "01.0.0",
        "1.00.0",
        "1.0.00",
        "1.0.0-beta",
        "1.0.0-beta.",
        "1.0.0-beta.0",
        "1.0.0-beta.01",
        "1.0.0-preview.1",
        "1.0.0-Beta.1",
        "1.0.0-beta.1.2",
        "1.0.0-dev",
        "1.0.0-beta.2-dev",
        "1.0.0+build",
        "1.0.0-",
        "-1.0.0",
        "1.-0.0",
        "1..0",
        "1.0.0\n",
        "1234567.0.0",
        "1.0.0-rc.1234567",
        "1.0.x",
        "١.0.0",
    };
    for (size_t i = 0; i < sizeof(rejected) / sizeof(rejected[0]); ++i) {
        if (app_version_parse(rejected[i], &version)) {
            fprintf(stderr, "accepted invalid version: %s\n", rejected[i]);
            return 1;
        }
    }
    CHECK(!app_version_parse(NULL, &version));

    /* SemVer precedence, as dist/release.py orders the tags. */
    CHECK(order("1.0.0", "1.0.0") == 0);
    CHECK(order("1.0.0-beta.2", "1.0.0-beta.2") == 0);
    CHECK(order("0.5.0", "1.0.0-alpha.1") < 0);
    CHECK(order("1.0.0-alpha.9", "1.0.0-beta.1") < 0);
    CHECK(order("1.0.0-beta.1", "1.0.0-beta.2") < 0);
    CHECK(order("1.0.0-beta.9", "1.0.0-beta.10") < 0);
    CHECK(order("1.0.0-beta.10", "1.0.0-rc.1") < 0);
    CHECK(order("1.0.0-rc.3", "1.0.0") < 0);
    CHECK(order("1.0.0", "1.0.1-alpha.1") < 0);
    CHECK(order("1.0.9", "1.1.0") < 0);
    CHECK(order("1.9.9", "2.0.0") < 0);
    CHECK(order("1.0.0", "1.0.0-rc.3") > 0);
    CHECK(order("2.0.0", "1.99.99") > 0);
    CHECK(order("0.10.0", "0.9.0") > 0);

    /* Development builds carry the version they follow. */
    CHECK(app_version_parse_build("1.0.0-beta.2-dev", &version, &development) && development);
    CHECK(version.stage == VERSION_BETA && version.number == 2);
    CHECK(app_version_parse_build("1.0.0-dev", &version, &development) && development);
    CHECK(version.stage == VERSION_FINAL);
    CHECK(app_version_parse_build("1.0.0-beta.2", &version, &development) && !development);
    CHECK(app_version_parse_build("1.0.0", &version, &development) && !development);
    CHECK(!app_version_parse_build("1.0.0-dev-dev", &version, &development));
    CHECK(!app_version_parse_build("1.0.0-devel", &version, &development));
    CHECK(!app_version_parse_build("1.0.0-beta.2-", &version, &development));
    CHECK(!app_version_parse_build("dev", &version, &development));
    return 0;
}
