#ifndef PAPO_INSTALL_H
#define PAPO_INSTALL_H

#include <stdbool.h>
#include <stddef.h>

/* The package this copy of the program came from, which decides the release
 * asset that replaces it. */
typedef enum {
    INSTALL_UNKNOWN, /* A build tree, a manual install or a package not recognized. */
    INSTALL_DEB_UBUNTU,
    INSTALL_DEB_DEBIAN,
    INSTALL_RPM_FEDORA,
    INSTALL_ARCH,
    INSTALL_APPIMAGE,
    INSTALL_TARBALL,
    INSTALL_FLATPAK,
    INSTALL_TERMUX,
    INSTALL_WINDOWS_INSTALLER,
    INSTALL_WINDOWS_ZIP,
    INSTALL_CHANNELS
} install_channel;

/* Written by the packaging scripts under the installation prefix: one word,
 * the token of the package's channel. */
#define INSTALL_MARKER_DIRECTORY "share/papo-tui"
#define INSTALL_MARKER_FILE "install-channel"
#define INSTALL_ASSET_BYTES 160U

/* The directory that holds bin/ on Linux, or the program's own folder on
 * Windows; owned by the caller (g_free), NULL when the executable is unknown. */
char *install_prefix(void);
/* The marker when the package carries one; otherwise what the system shows
 * (sandbox variables, the package manager's distribution, an uninstaller).
 * Looked up once per run. */
install_channel install_detect(void);
/* The same answer for a given prefix, not remembered. */
install_channel install_detect_at(const char *prefix);
/* The marker word of a channel ("" for unknown) and its inverse. */
const char *install_channel_token(install_channel channel);
install_channel install_channel_parse(const char *token);
/* The architecture this program was built for, as the asset names spell it;
 * "" when no package is published for it. */
const char *install_architecture(void);
/* The release asset of a channel for an architecture and version, as the
 * releases name them; false when the channel has none or it does not fit. */
bool install_asset_name(install_channel channel, const char *architecture, const char *version,
                        char *name, size_t size);

#endif
