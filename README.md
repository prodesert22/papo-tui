# Papo TUI

[![CI](https://github.com/prodesert22/papo-tui/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/prodesert22/papo-tui/actions/workflows/ci.yml?query=branch%3Amain)
[![Latest release](https://img.shields.io/github/v/release/prodesert22/papo-tui?label=release&sort=semver)](https://github.com/prodesert22/papo-tui/releases/latest)
[![Pre-release](https://img.shields.io/github/v/release/prodesert22/papo-tui?include_prereleases&label=pre-release&sort=semver)](https://github.com/prodesert22/papo-tui/releases)
[![Downloads](https://img.shields.io/github/downloads/prodesert22/papo-tui/total)](https://github.com/prodesert22/papo-tui/releases)
[![License: MIT](https://img.shields.io/github/license/prodesert22/papo-tui)](LICENSE)
![C17](https://img.shields.io/badge/language-C17-00599C?logo=c&logoColor=white)
![Platforms](https://img.shields.io/badge/platforms-Linux%20%7C%20Windows%20%7C%20Android%20(Termux)-informational)
![Architectures](https://img.shields.io/badge/arch-x86__64%20%7C%20ARM64-informational)

Terminal chat client for Papo, written in C. It connects to independent Papo
instances, keeps each one in its own tab, and shows images, video and emoji in
terminals that support graphics.

- [About](#about)
- [Supported platforms](#supported-platforms)
- [Download and install](#download-and-install)
- [Dependencies](#dependencies)
- [Build from source](#build-from-source)
- [Documentation](#documentation)
- [Releases and license](#releases-and-license)

## About

Papo TUI offers:

- channels and direct messages on several servers at once, with editing,
  deletion, replies, reactions, pinned messages, Markdown formatting and server
  message search;
- mentions, Unicode and server emoji, and GIF search on Wikimedia Commons;
- a notification center, desktop notifications, sounds, unread indicators and
  per-channel muting;
- file attachments, clipboard paste and attachment downloads;
- images, animated GIFs, video and audio playback with Kitty, Sixel or iTerm2
  graphics, a Chafa character fallback and a text-only mode;
- voice calls over GStreamer WebRTC and Opus;
- profile and password management, and server administration: roles, channels,
  emojis, webhooks, members and the audit log;
- a Portuguese and English interface, accessibility options and customizable
  shortcuts;
- update notices, with downloads accepted only from releases signed with the
  project's key.

The [user guide](docs/usage.md) explains how to use each feature, with one page
per topic in [docs/usage/](docs/usage/). The service contract is in
[openapi.yml](openapi.yml).

## Supported platforms

Official packages are built for **x86_64** (64-bit Intel/AMD) and **ARM64**
(AArch64). All packages of a release use the same code and report the same
version with `papo-tui --version`.

| System | Target | Formats | x86_64 | ARM64 |
| --- | --- | --- | :---: | :---: |
| Ubuntu | 24.04 | DEB | ✅ | ✅ |
| Debian | 13 | DEB | ✅ | ✅ |
| Fedora | 44 | RPM | ✅ | ✅ |
| Arch Linux / Arch Linux ARM | rolling | pacman package | ✅ | ✅ |
| Any Linux | glibc 2.39+ | AppImage, portable `.tar.gz` | ✅ | ✅ |
| Any Linux with Flatpak (including RHEL) | Freedesktop 26.08 runtime | Flatpak bundle | ✅ | ✅ |
| Windows | 11 | Installer, portable ZIP | ✅ | ✅ ¹ |
| Android / Termux | Android 9+, standard `com.termux` | Termux DEB | — | ✅ ² |

¹ Windows on ARM64 is published with 0.5.0 but is not part of the
1.0.0-beta.2 pre-release.
² The Termux package has no voice calls, because the WebRTC stack is not
published for Android, and no desktop credential storage or notification popups.

There are no macOS builds. RHEL 10 can be built from source (see
[BUILD.md](BUILD.md#rhel-10)); on RHEL 8 and 9 use the Flatpak bundle.

**Terminal requirements:** a UTF-8 terminal with at least 44 columns and 22 rows.
Images in Normal mode need Kitty, Sixel or iTerm2 graphics; other terminals,
including the Linux console (TTY), run in Light mode. On Windows, use
[Windows Terminal](https://learn.microsoft.com/en-us/windows/terminal/install).

## Download and install

Download packages from the [Releases page](https://github.com/prodesert22/papo-tui/releases).
[**Latest stable**](https://github.com/prodesert22/papo-tui/releases/latest) is
the recommended version; pre-releases (beta) are marked as such and may not have
been verified on every system.

Release files are named after the version, system and architecture
(`x86_64` or `aarch64`):

| Format | File |
| --- | --- |
| Ubuntu / Debian | `papo-tui-<version>-ubuntu-<arch>.deb`, `papo-tui-<version>-debian-<arch>.deb` |
| Fedora | `papo-tui-<version>-fedora-<arch>.rpm` |
| Arch Linux | `papo-tui-<version>-arch-<arch>.pkg.tar.zst` |
| AppImage | `PapoTUI-<version>-linux-<arch>.AppImage` |
| Linux portable folder | `papo-tui-<version>-linux-<arch>.tar.gz` |
| Flatpak | `papo-tui-<version>-flatpak-<arch>.flatpak` |
| Windows | `papo-tui-<version>-windows-<arch>-setup.exe`, `papo-tui-<version>-windows-<arch>.zip` |
| Termux | `papo-tui-<version>-termux-aarch64.deb` |

### Quick install

Run the commands from the folder where you downloaded the file. The
[installation guide](docs/installation.md) lists the runtime packages to
install first on each system, optional clipboard helpers, AppImage without
FUSE, Termux repository problems, updating and removal. The
[build comparison](docs/builds.md) summarizes every package.

#### Ubuntu and Debian

```sh
sudo apt install ./papo-tui-*-ubuntu-*.deb   # Ubuntu 24.04
sudo apt install ./papo-tui-*-debian-*.deb   # Debian 13
papo-tui
```

#### Fedora

```sh
sudo dnf install ./papo-tui-*-fedora-*.rpm
papo-tui
```

#### Arch Linux and Arch Linux ARM

```sh
sudo pacman -U ./papo-tui-*-arch-*.pkg.tar.zst
papo-tui
```

#### AppImage (any Linux)

No installation needed:

```sh
chmod +x PapoTUI-*.AppImage
./PapoTUI-*.AppImage
```

#### Portable folder (any Linux)

```sh
tar -xzf papo-tui-*-linux-*.tar.gz
./papo-tui-*/bin/papo-tui
```

#### Flatpak (any Linux, including RHEL)

```sh
flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
flatpak install --user ./papo-tui-*-flatpak-*.flatpak
flatpak run io.github.prodesert22.PapoTUI
```

#### Windows 11

Run the **installer** (`-setup.exe`), which installs for the current user and
adds a Start menu shortcut. For the **portable ZIP**, extract all of it and run
the program from Windows Terminal in the extracted folder:

```powershell
.\papo-tui.exe
```

#### Android (Termux)

```sh
pkg update
apt install ./papo-tui-*-termux-aarch64.deb
papo-tui
```

`papo-tui --help` describes the command-line options, environment variables and
where the application keeps its files. To start using the program, see
[Getting started](docs/usage/getting-started.md).

### Verify the download

Every release file is signed with the project's OpenPGP key
**AA09 D90E 1547 7426 DAFD F098 30F8 5714 E332 3D6B**, published with each
release as `papo-tui-signing-key.asc`. Download it together with `SHA256SUMS`
and `SHA256SUMS.asc`, then:

```sh
gpg --import papo-tui-signing-key.asc
gpg --fingerprint 30F85714E3323D6B        # must match the fingerprint above
gpg --verify SHA256SUMS.asc SHA256SUMS
sha256sum --check --ignore-missing SHA256SUMS
```

Each file also has its own signature: `gpg --verify FILE.asc FILE`. The release
notes describe the RPM and Arch package signatures. The Windows installer and
ZIP have no Authenticode signature, so Windows may warn that the publisher is
unknown; verify them with their `.asc` signatures.

## Dependencies

Papo TUI is written in C17 and builds on these libraries:

| Area | Libraries |
| --- | --- |
| Terminal interface | ncursesw and formw, libunibreak (Unicode text segmentation), md4c (Markdown) |
| Network and data | libcurl (HTTPS and WebSocket), Jansson (JSON) |
| Images and text drawing | Chafa, libsixel, PangoCairo, librsvg (SVG), Fontconfig, zlib, the Noto Color Emoji font |
| Video and audio | FFmpeg, libmpv |
| Voice calls | GStreamer WebRTC, libnice, Opus, libsrtp, WebRTC Audio Processing |
| System integration | GLib, GModule and GIO; libsecret and libxcb, optional on Linux |

**Running a release package** needs no separate setup of these libraries: the
DEB, RPM and Arch packages carry the program's private libraries and let the
package manager install their declared system dependencies, the AppImage and
the Windows ZIP/installer include their libraries beside the program, Flatpak
uses the Freedesktop 26.08 runtime and the Termux package uses Termux
libraries. Some features use optional programs on Linux:

- `wl-clipboard` (Wayland) or `xclip` (X11) to copy and paste files and images
  through the system clipboard;
- `xdg-open` from xdg-utils to open links in the browser;
- a Secret Service or KWallet credential store to remember sessions.

**Building from source** needs a C17 compiler (with C++17 for the media and
voice dependencies), **CMake 3.21+**, Ninja, pkg-config and **Python 3.10+**.
Locked versions of the libraries above are built into a private prefix
(`build/release-deps`), leaving system libraries untouched; the first build
needs internet access to download them. Minimum versions are in
[BUILD.md](BUILD.md#requirements), and the locked sources and hashes in
[dist/dependencies.json](dist/dependencies.json).

## Build from source

On Linux, install the host packages for your distribution from
[BUILD.md](BUILD.md#linux) (Debian/Ubuntu, Fedora, RHEL 10, Arch Linux), then:

```sh
git clone https://github.com/prodesert22/papo-tui.git
cd papo-tui
python3 -m venv build/build-env
build/build-env/bin/python -m pip install meson==1.12.1
export PATH="$PWD/build/build-env/bin:$PWD/build/release-deps/bin:$PATH"
python dist/bootstrap.py --prefix build/release-deps
python dist/resources.py
export PKG_CONFIG_PATH="$PWD/build/release-deps/lib/pkgconfig:$PWD/build/release-deps/lib64/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export LD_LIBRARY_PATH="$PWD/build/release-deps/lib:$PWD/build/release-deps/lib64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
cmake -S . -B build/local -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$PWD/build/release-deps" -DBUILD_TESTING=OFF
cmake --build build/local --parallel 2
./build/local/papo-tui
```

On Windows 11, the build runs in an MSYS2 CLANG64 (x86_64) or CLANGARM64 (ARM64)
shell with native CPython 3.13; see [BUILD.md](BUILD.md#windows-11).

Configure with `-DPAPO_VOICE=OFF` to build without voice calls and the
GStreamer WebRTC stack.

[BUILD.md](BUILD.md) has the complete instructions:

- [Library requirements](BUILD.md#requirements) and host packages per system
- [Tests and build options](BUILD.md#tests-and-build-options)
- [Reproduce CI locally](BUILD.md#reproduce-ci-locally) with
  `python3 tools/ci.py run`, in the same toolchain images as GitHub Actions
- [Packaging and releases](BUILD.md#packaging-and-releases): DEB, RPM, Arch,
  AppImage, Flatpak, Windows ZIP/installer and Termux

## Documentation

| Document | Contents |
| --- | --- |
| [docs/installation.md](docs/installation.md) | Installing, updating and removing each package |
| [docs/builds.md](docs/builds.md) | Targets of each official package |
| [docs/usage.md](docs/usage.md) | User guide: an index of the pages in [docs/usage/](docs/usage/), one per feature, with troubleshooting |
| [BUILD.md](BUILD.md) | Building, testing and packaging |
| [CHANGELOG.md](CHANGELOG.md) | Summary of each release |
| [openapi.yml](openapi.yml) | Contract of the Papo service |

Report problems in the [issue tracker](https://github.com/prodesert22/papo-tui/issues).

## Releases and license

[Changelog](CHANGELOG.md) · [Full release notes](releases/v0.5.0.md) ·
[All releases](https://github.com/prodesert22/papo-tui/releases)

Original code is licensed under [MIT](LICENSE). Bundled libraries and data
retain their [upstream licenses](THIRD_PARTY_NOTICES.md).
