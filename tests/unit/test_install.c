#include "platform/install.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <jansson.h>
#include <stdio.h>
#include <string.h>

#define CHECK(value)                                                                               \
    do {                                                                                           \
        if (!(value)) {                                                                            \
            fprintf(stderr, "check failed at line %d\n", __LINE__);                                \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static bool published(const json_t *assets, const char *name)
{
    size_t index;
    json_t *asset;
    json_array_foreach(assets, index, asset)
    {
        const char *listed = json_string_value(json_object_get(asset, "name"));
        if (listed && !strcmp(listed, name))
            return true;
    }
    return false;
}
#ifndef _WIN32
/* Only the POSIX checks below read an installation marker. */
static bool mark(const char *prefix, const char *text)
{
    char *directory = g_build_filename(prefix, INSTALL_MARKER_DIRECTORY, NULL);
    char *path = g_build_filename(directory, INSTALL_MARKER_FILE, NULL);
    bool ok = !g_mkdir_with_parents(directory, 0700) && g_file_set_contents(path, text, -1, NULL);
    g_free(path);
    g_free(directory);
    return ok;
}
#endif

int main(int argc, char **argv)
{
    char name[INSTALL_ASSET_BYTES];
    CHECK(argc == 2);

    /* Every channel's asset, for both architectures, is a file of the real
     * 1.0.0-beta.2 release, except the ones that release did not publish. */
    json_t *releases = json_load_file(argv[1], 0, NULL);
    json_t *assets = NULL;
    size_t index;
    json_t *release;
    json_array_foreach(releases, index, release)
    {
        const char *tag = json_string_value(json_object_get(release, "tag_name"));
        if (tag && !strcmp(tag, "v1.0.0-beta.2"))
            assets = json_object_get(release, "assets");
    }
    CHECK(json_array_size(assets) > 0);
    static const char *const architectures[] = {"x86_64", "aarch64"};
    for (int channel = INSTALL_UNKNOWN + 1; channel < INSTALL_CHANNELS; ++channel)
        for (size_t a = 0; a < 2; ++a) {
            bool named = install_asset_name((install_channel)channel, architectures[a],
                                            "1.0.0-beta.2", name, sizeof(name));
            bool termux = channel == INSTALL_TERMUX;
            bool windows = channel == INSTALL_WINDOWS_INSTALLER || channel == INSTALL_WINDOWS_ZIP;
            if (termux && a == 0) {
                CHECK(!named); /* Termux is ARM64 only. */
                continue;
            }
            CHECK(named);
            /* That release has no Windows ARM64 package: the name is still the
             * one earlier releases used, and is simply not found. */
            if (published(assets, name) != !(windows && a == 1)) {
                fprintf(stderr, "unexpected for %s: %s\n", install_channel_token(channel), name);
                return 1;
            }
        }
    json_decref(releases);

    CHECK(install_asset_name(INSTALL_APPIMAGE, "x86_64", "2.0.0", name, sizeof(name)));
    CHECK(!strcmp(name, "PapoTUI-2.0.0-linux-x86_64.AppImage"));
    CHECK(install_asset_name(INSTALL_WINDOWS_INSTALLER, "aarch64", "0.5.0", name, sizeof(name)));
    CHECK(!strcmp(name, "papo-tui-0.5.0-windows-aarch64-setup.exe"));
    CHECK(!install_asset_name(INSTALL_UNKNOWN, "x86_64", "1.0.0", name, sizeof(name)));
    CHECK(!install_asset_name(INSTALL_CHANNELS, "x86_64", "1.0.0", name, sizeof(name)));
    CHECK(!install_asset_name(INSTALL_ARCH, "", "1.0.0", name, sizeof(name)));
    CHECK(!install_asset_name(INSTALL_ARCH, "x86_64", "", name, sizeof(name)));
    CHECK(!install_asset_name(INSTALL_ARCH, "x86_64", "1.0.0", name, 16));

    /* Tokens are the words the packaging scripts write. */
    for (int channel = INSTALL_UNKNOWN + 1; channel < INSTALL_CHANNELS; ++channel)
        CHECK(install_channel_parse(install_channel_token((install_channel)channel)) ==
              (install_channel)channel);
    CHECK(!*install_channel_token(INSTALL_UNKNOWN) && !*install_channel_token(INSTALL_CHANNELS));
    CHECK(install_channel_parse("") == INSTALL_UNKNOWN);
    CHECK(install_channel_parse(NULL) == INSTALL_UNKNOWN);
    CHECK(install_channel_parse("Tarball") == INSTALL_UNKNOWN);
    CHECK(install_channel_parse("tarball ") == INSTALL_UNKNOWN);

#ifndef _WIN32
    /* A marker decides; without one only what each sandbox shows counts. */
    char *prefix = g_dir_make_tmp("papo-install-XXXXXX", NULL);
    CHECK(prefix);
    g_unsetenv("FLATPAK_ID");
    g_unsetenv("TERMUX_VERSION");
    g_unsetenv("APPIMAGE");
    g_unsetenv("APPDIR");
    bool sandboxed = g_file_test("/.flatpak-info", G_FILE_TEST_IS_REGULAR);
    if (!sandboxed) {
        CHECK(install_detect_at(prefix) == INSTALL_UNKNOWN);
        g_setenv("TERMUX_VERSION", "0.118", TRUE);
        CHECK(install_detect_at(prefix) == INSTALL_TERMUX);
        g_unsetenv("TERMUX_VERSION");
        /* A terminal packed as an AppImage hands its variables to the program. */
        g_setenv("APPIMAGE", "/opt/terminal.AppImage", TRUE);
        g_setenv("APPDIR", "/tmp/.mount_terminal", TRUE);
        CHECK(install_detect_at(prefix) == INSTALL_UNKNOWN);
        char *inside = g_build_filename(prefix, "usr", NULL);
        g_setenv("APPDIR", prefix, TRUE);
        CHECK(install_detect_at(inside) == INSTALL_APPIMAGE);
        char *sibling = g_strconcat(prefix, "-other", NULL);
        CHECK(install_detect_at(sibling) == INSTALL_UNKNOWN);
        g_free(sibling);
        g_free(inside);
        g_unsetenv("APPIMAGE");
        g_unsetenv("APPDIR");
    }
    g_setenv("FLATPAK_ID", "io.github.prodesert22.PapoTUI", TRUE);
    CHECK(install_detect_at(prefix) == INSTALL_FLATPAK);
    CHECK(mark(prefix, "tarball\n"));
    CHECK(install_detect_at(prefix) == INSTALL_TARBALL); /* The marker wins. */
    g_unsetenv("FLATPAK_ID");
    CHECK(mark(prefix, "deb-debian"));
    CHECK(install_detect_at(prefix) == INSTALL_DEB_DEBIAN);
    if (!sandboxed) {
        CHECK(mark(prefix, "unknown-word\n"));
        CHECK(install_detect_at(prefix) == INSTALL_UNKNOWN);
        CHECK(mark(prefix, "tarball\nappimage\n"));
        CHECK(install_detect_at(prefix) == INSTALL_UNKNOWN);
        char filler[200];
        memset(filler, 'a', sizeof(filler) - 1);
        filler[sizeof(filler) - 1] = 0;
        CHECK(mark(prefix, filler));
        CHECK(install_detect_at(prefix) == INSTALL_UNKNOWN);
    }
    char *marker = g_build_filename(prefix, INSTALL_MARKER_DIRECTORY, INSTALL_MARKER_FILE, NULL);
    char *folder = g_build_filename(prefix, INSTALL_MARKER_DIRECTORY, NULL);
    char *share = g_build_filename(prefix, "share", NULL);
    g_unlink(marker);
    g_rmdir(folder);
    g_rmdir(share);
    g_rmdir(prefix);
    g_free(share);
    g_free(folder);
    g_free(marker);
    g_free(prefix);
#endif
    return 0;
}
