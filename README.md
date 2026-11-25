# Papo TUI

[![CI](https://github.com/prodesert22/papo-tui/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/prodesert22/papo-tui/actions/workflows/ci.yml?query=branch%3Amain)
[![Latest release](https://img.shields.io/github/v/release/prodesert22/papo-tui?label=release&sort=semver)](https://github.com/prodesert22/papo-tui/releases/latest)
[![Pre-release](https://img.shields.io/github/v/release/prodesert22/papo-tui?include_prereleases&label=pre-release&sort=semver)](https://github.com/prodesert22/papo-tui/releases)
[![Downloads](https://img.shields.io/github/downloads/prodesert22/papo-tui/total)](https://github.com/prodesert22/papo-tui/releases)
[![License: MIT](https://img.shields.io/github/license/prodesert22/papo-tui)](LICENSE)
![C17](https://img.shields.io/badge/language-C17-00599C?logo=c&logoColor=white)
![Platforms](https://img.shields.io/badge/platforms-Linux%20%7C%20Windows%20%7C%20Android%20(Termux)-informational)
![Architectures](https://img.shields.io/badge/arch-x86__64%20%7C%20ARM64-informational)

Terminal chat client written in C. Connect to independent Papo instances and
chat in channels or direct messages, with message editing, deletion, replies,
reactions, pinned messages, server message search, notifications, mentions,
emojis, GIF search, voice calls and media in supported terminals.

- [Features](#features)
- [Supported platforms](#supported-platforms)
- [Download and install](#download-and-install)
- [Run](#run)
- [Build from source](#build-from-source)
- [Documentation](#documentation)
- [Releases and license](#releases-and-license)

## Features

**Chat**

- Several servers at once, each in its own tab; **Ctrl+K** jumps to any channel
  or DM of the connected servers by name, and unsent drafts come back the next
  time you open the application.
- Available channels and message actions follow your account permissions.
  Member names follow their role color, with white as the default.
- Open the member drawer with **👤** or **Ctrl+U** to search people, view
  profiles, start direct messages and access available member actions.
- Use the arrows to select DMs, and **m** or a right-click to close one from its
  menu. Press **m/M** in the channel list, or right-click a channel, to mute it
  or choose its notification preference.
- Attach files with **Ctrl+O** or **📎**, or paste copied files and images with
  **Ctrl+V** when the local clipboard is available. Files attached to messages
  appear as cards with a **↓** button; click it, or choose **Baixar anexo** in
  the message's context menu, to save the file where you choose.
- Links in messages are hyperlinks for the terminal, so its own Ctrl+click opens
  the whole address even when a long link wraps. A message that is only a link
  also offers **Abrir no navegador** (Open in browser) in its context menu.
- The terminal window title shows the current conversation and server, a **●**
  when a connected server has news outside muted channels, and the total known
  unread mentions across connected instances when there are any.

**Account and server management**

- Change your own nickname, status, description, avatar and banner on each
  server in **Ctrl+S → Perfil**, or with `/nick`, `/status`, `/bio`, `/avatar`
  and `/banner` in the message box.
- Change your password in **Ctrl+S → Conta**, or set a new one from a reset link
  with **[Redefinir senha]** on the sign-in screen.
- Open **Configurações do servidor** from the server tab's menu to manage the
  server, roles, channels, emojis, members and the audit log; only the tabs your
  permissions allow appear.

**Media and voice**

- Press **Ctrl+S**, or click the gear below your name at the bottom of the
  channel list, to choose an application mode:

  | Mode | Behavior |
  | --- | --- |
  | Normal | Kitty, Sixel or iTerm2 graphics, with a Chafa fallback. Images, animated GIFs, video and audio. |
  | Light | Images, animated GIFs and video drawn with Chafa characters. Plays audio. |
  | Ultralight | Text only; releases the media libraries. |

  Audio attachments can keep playing in the background. Video decoding can use
  compatible GPU drivers, with automatic CPU fallback; animated media also
  benefit from parallel Chafa processing in text terminals.
- Join a voice channel with Enter or a click. The buttons **🔊 🎤** beside the
  gear toggle call audio and microphone independently; text terminals use
  **♪ ●**. Configure devices, volumes, microphone testing and push-to-talk in
  **Ctrl+S → Áudio**. Voice uses GStreamer WebRTC and Opus in every application
  mode.

**Interface and accessibility**

- Portuguese and English: the interface follows the system language, or the
  choice in **Ctrl+S → Aparência → Idioma**.
- Your account's theme (dark, light or following the terminal), history display
  and notification settings are saved on each server in **Ctrl+S → Aparência**
  and **Ctrl+S → Notificações**, and the client plays short sounds of its own
  for notifications, mentions and voice calls.
- Press **Ctrl+G** for a table of shortcuts and mouse controls for the focused
  area, or **Ctrl+/** for the global reference, which includes a search field
  for keys, actions and areas. F1 also opens contextual help. The footer keeps
  only the help hint alongside operational notices.
- **Ctrl+S → Atalhos** lists every key and changes the keys of commands on this
  device.
- **Ctrl+S → Acessibilidade** offers high contrast, no colors, color-blind safe
  colors, focus cues beyond color, readable role colors, a screen reader mode,
  text symbols, emoji names, ASCII borders, visual alerts, longer notices,
  keyboard-only use without the mouse and a second key before quitting.

**Updates**

- **Ctrl+S → Sobre** shows the version, the license and the address of the
  source code.
- Once a day the client asks GitHub whether a newer release exists and tells you
  on the sign-in screen and in the footer. **Baixar atualização** in
  **Ctrl+S → Atualizações** then downloads the package for your installation,
  only from a release signed with the project's key, checks its SHA-256 and
  shows the command that installs it.
- An AppImage or a folder unpacked from the portable archive is replaced by the
  program itself, with **Baixar e instalar** and then **Instalar e sair**.
- Turn the check off in **Atualizações**, or start with `PAPO_UPDATE_CHECK=0`;
  **Aceitar versões beta** there adds pre-releases.

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

Quick install, from the folder where you downloaded the file:

```sh
# Debian / Ubuntu
sudo apt install ./papo-tui-*-ubuntu-*.deb   # or the -debian- package

# Fedora
sudo dnf install ./papo-tui-*-fedora-*.rpm

# Arch Linux / Arch Linux ARM
sudo pacman -U ./papo-tui-*-arch-*.pkg.tar.zst

# AppImage (no installation needed)
chmod +x PapoTUI-*.AppImage && ./PapoTUI-*.AppImage

# Flatpak
flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
flatpak install --user ./papo-tui-*-flatpak-*.flatpak

# Android / Termux
pkg update && apt install ./papo-tui-*-termux-aarch64.deb
```

On Windows, run the **installer** (current user, Start menu shortcut) or extract
**all** of the portable ZIP and run `papo-tui.exe` from Windows Terminal.

The [installation guide](docs/installation.md) lists the runtime packages to
install first on each system, optional clipboard helpers, AppImage without FUSE,
Termux repository problems, updating and removal. The
[build comparison](docs/builds.md) summarizes every package.

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

## Run

```sh
papo-tui                                   # Linux packages and Termux
flatpak run io.github.prodesert22.PapoTUI  # Flatpak
```

```powershell
.\papo-tui.exe                             # Windows, from the extracted ZIP folder
```

Enter the instance URL and your credentials. Use **Tab** to move focus,
**Enter** to confirm and **Ctrl+Q** to quit. `papo-tui --help` describes the
command-line options, environment variables and where the application keeps its
files.

The [usage guide](docs/usage.md) covers controls, features and common problems.

## Build from source

The build uses a **C17** compiler, **CMake 3.21+**, Ninja, pkg-config and
**Python 3.10+**. Locked versions of ncurses, curl, Jansson, Chafa, FFmpeg,
libmpv, GStreamer and the other media and voice libraries are built into a
private prefix (`build/release-deps`), leaving system libraries untouched; the
first build needs internet access to download them.

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

The service contract is in [openapi.yml](openapi.yml).

## Documentation

| Document | Contents |
| --- | --- |
| [docs/installation.md](docs/installation.md) | Installing, updating and removing each package |
| [docs/builds.md](docs/builds.md) | Targets of each official package |
| [docs/usage.md](docs/usage.md) | Controls, features and troubleshooting |
| [BUILD.md](BUILD.md) | Building, testing and packaging |
| [CHANGELOG.md](CHANGELOG.md) | Summary of each release |

Report problems in the [issue tracker](https://github.com/prodesert22/papo-tui/issues).

## Releases and license

[Changelog](CHANGELOG.md) · [Full release notes](releases/v0.5.0.md) ·
[All releases](https://github.com/prodesert22/papo-tui/releases)

Original code is licensed under [MIT](LICENSE). Bundled libraries and data
retain their [upstream licenses](THIRD_PARTY_NOTICES.md).
