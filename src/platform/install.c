#include "platform/install.h"
#include "platform/runtime.h"
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Asset names are <prefix><version><middle><architecture><suffix>. */
static const struct {
    const char *token, *prefix, *middle, *suffix;
} channels[INSTALL_CHANNELS] = {
    [INSTALL_UNKNOWN] = {"", NULL, NULL, NULL},
    [INSTALL_DEB_UBUNTU] = {"deb-ubuntu", "papo-tui-", "-ubuntu-", ".deb"},
    [INSTALL_DEB_DEBIAN] = {"deb-debian", "papo-tui-", "-debian-", ".deb"},
    [INSTALL_RPM_FEDORA] = {"rpm-fedora", "papo-tui-", "-fedora-", ".rpm"},
    [INSTALL_ARCH] = {"arch", "papo-tui-", "-arch-", ".pkg.tar.zst"},
    [INSTALL_APPIMAGE] = {"appimage", "PapoTUI-", "-linux-", ".AppImage"},
    [INSTALL_TARBALL] = {"tarball", "papo-tui-", "-linux-", ".tar.gz"},
    [INSTALL_FLATPAK] = {"flatpak", "papo-tui-", "-flatpak-", ".flatpak"},
    [INSTALL_TERMUX] = {"termux", "papo-tui-", "-termux-", ".deb"},
    [INSTALL_WINDOWS_INSTALLER] = {"windows-installer", "papo-tui-", "-windows-", "-setup.exe"},
    [INSTALL_WINDOWS_ZIP] = {"windows-zip", "papo-tui-", "-windows-", ".zip"},
};

const char *install_channel_token(install_channel channel)
{
    return channel > INSTALL_UNKNOWN && channel < INSTALL_CHANNELS ? channels[channel].token : "";
}
install_channel install_channel_parse(const char *token)
{
    if (token && *token)
        for (int i = INSTALL_UNKNOWN + 1; i < INSTALL_CHANNELS; ++i)
            if (!strcmp(token, channels[i].token))
                return (install_channel)i;
    return INSTALL_UNKNOWN;
}
const char *install_architecture(void)
{
#if defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "aarch64";
#else
    return "";
#endif
}
bool install_asset_name(install_channel channel, const char *architecture, const char *version,
                        char *name, size_t size)
{
    if (channel <= INSTALL_UNKNOWN || channel >= INSTALL_CHANNELS || !architecture ||
        !*architecture || !version || !*version)
        return false;
    /* Termux is published for one architecture only. */
    if (channel == INSTALL_TERMUX && strcmp(architecture, "aarch64"))
        return false;
    int written = snprintf(name, size, "%s%s%s%s%s", channels[channel].prefix, version,
                           channels[channel].middle, architecture, channels[channel].suffix);
    return written > 0 && (size_t)written < size;
}
char *install_prefix(void)
{
    char *executable = papo_executable_path();
    if (!executable)
        return NULL;
    char *directory = g_path_get_dirname(executable);
    /* The media helper lives one folder below the program. */
    char *base = g_path_get_basename(directory);
    if (!strcmp(base, "papo-backends")) {
        char *parent = g_path_get_dirname(directory);
        g_free(directory);
        directory = parent;
    }
    g_free(base);
#ifdef _WIN32
    char *prefix = directory;
#else
    char *name = g_path_get_basename(directory);
    char *prefix = !strcmp(name, "bin") ? g_path_get_dirname(directory) : g_strdup(directory);
    g_free(name);
    g_free(directory);
#endif
    free(executable);
    return prefix;
}
static install_channel marked(const char *prefix)
{
    char *path = g_build_filename(prefix, INSTALL_MARKER_DIRECTORY, INSTALL_MARKER_FILE, NULL);
    char *text = NULL;
    gsize length = 0;
    install_channel channel = INSTALL_UNKNOWN;
    if (g_file_get_contents(path, &text, &length, NULL) && length < 64 && strlen(text) == length)
        channel = install_channel_parse(g_strstrip(text));
    g_free(text);
    g_free(path);
    return channel;
}
#ifndef _WIN32
/* One KEY=value of /etc/os-release, without quotes; owned by the caller. */
static char *release_field(const char *text, const char *key)
{
    size_t length = strlen(key);
    for (const char *line = text; line && *line; line = strchr(line, '\n')) {
        line += *line == '\n';
        if (strncmp(line, key, length) || line[length] != '=')
            continue;
        const char *value = line + length + 1;
        char *field = g_strndup(value, strcspn(value, "\n"));
        g_strstrip(field);
        size_t size = strlen(field);
        if (size >= 2 && (field[0] == '"' || field[0] == '\'') && field[size - 1] == field[0]) {
            memmove(field, field + 1, size - 2);
            field[size - 2] = 0;
        }
        return field;
    }
    return g_strdup("");
}
static bool listed(const char *id, const char *like, const char *name)
{
    if (!strcmp(id, name))
        return true;
    char **names = g_strsplit(like, " ", -1);
    bool found = g_strv_contains((const char *const *)names, name);
    g_strfreev(names);
    return found;
}
/* The distribution whose package manager owns /usr/bin. */
static install_channel distribution(void)
{
    char *text = NULL;
    if (!g_file_get_contents("/etc/os-release", &text, NULL, NULL))
        return INSTALL_UNKNOWN;
    char *id = release_field(text, "ID"), *like = release_field(text, "ID_LIKE");
    /* Ubuntu lists Debian as its family, so it is asked first. */
    install_channel channel = listed(id, like, "ubuntu")   ? INSTALL_DEB_UBUNTU
                              : listed(id, like, "debian") ? INSTALL_DEB_DEBIAN
                              : listed(id, like, "fedora") ? INSTALL_RPM_FEDORA
                              : listed(id, like, "arch")   ? INSTALL_ARCH
                                                           : INSTALL_UNKNOWN;
    g_free(id);
    g_free(like);
    g_free(text);
    return channel;
}
#endif
/* Packages published before the marker existed are told apart by what only
 * each of them shows. */
static install_channel unmarked(const char *prefix)
{
#ifdef _WIN32
    char *uninstaller = g_build_filename(prefix, "unins000.exe", NULL);
    char *backends = g_build_filename(prefix, "papo-backends", NULL);
    install_channel channel = g_file_test(uninstaller, G_FILE_TEST_IS_REGULAR)
                                  ? INSTALL_WINDOWS_INSTALLER
                              : g_file_test(backends, G_FILE_TEST_IS_DIR) ? INSTALL_WINDOWS_ZIP
                                                                          : INSTALL_UNKNOWN;
    g_free(uninstaller);
    g_free(backends);
    return channel;
#else
    const char *flatpak = getenv("FLATPAK_ID"), *termux = getenv("TERMUX_VERSION");
    const char *image = getenv("APPIMAGE"), *mount = getenv("APPDIR");
    if ((flatpak && *flatpak) || g_file_test("/.flatpak-info", G_FILE_TEST_IS_REGULAR))
        return INSTALL_FLATPAK;
    if (termux && *termux)
        return INSTALL_TERMUX;
    /* A terminal that is itself an AppImage passes these to every program it
     * starts: they count only when this program runs from that mount. */
    if (image && *image && mount && *mount && g_str_has_prefix(prefix, mount) &&
        (prefix[strlen(mount)] == '/' || !prefix[strlen(mount)]))
        return INSTALL_APPIMAGE;
    return !strcmp(prefix, "/usr") ? distribution() : INSTALL_UNKNOWN;
#endif
}
install_channel install_detect_at(const char *prefix)
{
    install_channel channel = marked(prefix);
    return channel == INSTALL_UNKNOWN ? unmarked(prefix) : channel;
}
install_channel install_detect(void)
{
    static bool looked;
    static install_channel channel;
    if (looked)
        return channel;
    looked = true;
    char *prefix = install_prefix();
    if (prefix)
        channel = install_detect_at(prefix);
    g_free(prefix);
    return channel;
}
