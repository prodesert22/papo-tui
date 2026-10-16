# Installation

Choose **x86_64** for 64-bit Intel/AMD computers or **ARM64** for AArch64
computers. Release packages are available under **Releases**.
Check the `SHA256SUMS` file before installing.

## Linux

Download the appropriate package and install it from the download folder:

```sh
sudo apt install ./papo-tui*.deb          # Debian or Ubuntu; choose your distribution's build
sudo dnf install ./papo-tui*.rpm          # Fedora
sudo pacman -U ./papo-tui*.pkg.tar.zst    # Arch or Arch Linux ARM
```

These commands are alternatives: run only the one for your system.
Then start `papo-tui` in a terminal.

### AppImage

```sh
chmod +x PapoTUI-*.AppImage
./PapoTUI-*.AppImage
```

Without FUSE, extract the file with `--appimage-extract` and run
`./squashfs-root/AppRun`. Administrator installation is not required.

### Flatpak

Install Flatpak and add Flathub to obtain the runtime:

```sh
flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
flatpak install --user ./papo-tui-*-flatpak-*.flatpak
flatpak run io.github.prodesert22.PapoTUI
```

The downloaded file is the application package. Its data is stored in the
application's isolated directory; it does not automatically share profiles
with other builds.

## Windows

Use Windows 11 and a terminal with UTF-8 support, such as Windows Terminal.
Choose the installer for your architecture and follow its instructions.
Installation for the current user does not need to modify other users' directories.

To use the portable ZIP, extract **all** its contents and run `papo-tui.exe`
from the terminal. Do not move only the executable: it uses the included DLLs,
resources and the media helper in `papo-backends`. MSYS2 is not required to run
the package.

## Build from source

The project uses **C17**, **CMake 3.21+** and **pkg-config/pkgconf**. To build it,
install the development packages for the libraries below.

| Library | Minimum | Purpose |
| --- | --- | --- |
| ncursesw and formw | 6.1 | Terminal interface |
| libcurl | 7.86 | HTTP and WebSocket; requires WS/WSS, TLS and asynchronous DNS |
| Jansson | 2.14 | JSON |
| libunibreak | 6.1 | Unicode graphemes and words |
| GLib and GModule | 2.66 | Data structures, tasks and modules |
| GIO | 2.66 on Linux; 2.88 on Windows | Media helper, credential stores and notifications |
| libsixel | 1.8 | Sixel output |
| Chafa | 1.16 | Character rendering and Kitty/iTerm2 graphics output |
| PangoCairo | 1.50 | Text and color emojis |
| Fontconfig | 2.13 | Font discovery |
| libmpv | API 2.0 | Video and audio |
| FFmpeg | avformat/avcodec 58, avutil 56, swscale 5 | Images and frames |
| Noto Color Emoji | — | Emoji font |
| libsecret, optional on Linux | 0.20 | Secret Service/GNOME Keyring |

On Arch Linux:

```sh
sudo pacman -S --needed base-devel cmake pkgconf ncurses curl jansson libunibreak libsixel chafa glib2 pango cairo noto-fonts-emoji mpv ffmpeg libsecret
```

On Debian/Ubuntu and Fedora, development package names and versions vary.
The build recipes provide missing dependencies without replacing system
libraries; see [builds](builds.md). An installed libcurl may lack WebSocket
support even when it meets the minimum version: CMake checks this.

```sh
cmake -S . -B build -DBUILD_TESTING=OFF
cmake --build build
cmake --install build --prefix "$HOME/.local"
```

Add `~/.local/bin` to PATH or run `~/.local/bin/papo-tui`.
To use dependencies installed under another prefix, pass
`-DCMAKE_PREFIX_PATH=/path/to/dependencies`.

On Windows, build in the MSYS2 **CLANG64** or **CLANGARM64** environment with
CMake, Ninja and libraries for the same architecture. See the build recipes.

## Data and removal

On Linux, profiles are stored in `$XDG_CONFIG_HOME/papo-tui` or `~/.config/papo-tui`;
public state is stored in `$XDG_STATE_HOME/papo-tui` or `~/.local/state/papo-tui`.
On Windows, they are stored in `%LOCALAPPDATA%\papo-tui`, with state in the `state`
subfolder. Persistent cookies are stored exclusively in the system credential store.

Remove the package with the manager used to install it. For ZIP/AppImage, remove
the downloaded folder/file; for Flatpak, run `flatpak uninstall io.github.prodesert22.PapoTUI`.
Uninstalling preserves personal data. To disable session recovery, sign out
through the application before uninstalling.
