# Installation

Choose **x86_64** for 64-bit Intel/AMD computers or **ARM64** for AArch64
computers. Download the package for your system from
[Releases](https://github.com/prodesert22/papo-tui/releases), together with
`SHA256SUMS` to verify the download. The [build comparison](builds.md) lists
the distribution versions targeted by each package.

To compile the program instead, see [BUILD.md](../BUILD.md).

## Linux

Use a UTF-8 terminal. Release packages include the application's private
libraries; the package manager installs their declared system dependencies.
The commands below install runtime requirements, not compiler toolchains.

### Debian and Ubuntu

Choose the matching Debian or Ubuntu DEB. In its download folder, run:

```sh
sudo apt update
sudo apt install ca-certificates ncurses-base fontconfig fonts-noto-color-emoji
sudo apt install ./papo-tui-*.deb
papo-tui
```

Keep only the chosen application DEB in that folder when using the wildcard.

### Fedora

The release RPM targets **Fedora 44**. Install it with DNF so its runtime
requirements are resolved:

```sh
sudo dnf install ca-certificates ncurses-base fontconfig alsa-lib google-noto-color-emoji-fonts
sudo dnf install ./papo-tui-*-fedora-*.rpm
papo-tui
```

### RHEL

On RHEL, install Flatpak and use the downloaded **Flatpak** package with the
[commands below](#flatpak):

```sh
sudo dnf install flatpak
```

The Fedora RPM targets a different distribution and glibc baseline; it is not
a RHEL package. For a native build on RHEL 10, see [BUILD.md](../BUILD.md#rhel-10).
The current AppImage requires **glibc 2.39+**, so it cannot run on RHEL 8 or 9.
See Red Hat's [Flatpak installation instructions](https://docs.redhat.com/en/documentation/red_hat_enterprise_linux/9/html/administering_the_system_using_the_gnome_desktop_environment/assembly_installing-applications-using-flatpak_administering-the-system-using-the-gnome-desktop-environment).

### Arch Linux and Arch Linux ARM

Update the system, then install the downloaded package for its architecture:

```sh
sudo pacman -Syu --needed ca-certificates ncurses fontconfig alsa-lib noto-fonts-emoji
sudo pacman -U ./papo-tui-*-arch-*.pkg.tar.zst
papo-tui
```

The release package is downloaded from Releases; these commands do not require
an AUR helper. Keep only the matching application package in the folder.

### Optional clipboard and session support

For copying or pasting files/images through the local Linux clipboard, install
**wl-clipboard** on Wayland or **xclip** on X11. Text paste through the terminal
and F5 file selection remain available without these helpers.

| System | Wayland | X11 |
| --- | --- | --- |
| Debian/Ubuntu | `sudo apt install wl-clipboard` | `sudo apt install xclip` |
| Fedora | `sudo dnf install wl-clipboard` | `sudo dnf install xclip` |
| Arch | `sudo pacman -S --needed wl-clipboard` | `sudo pacman -S --needed xclip` |

To remember sessions, unlock your desktop's Secret Service or KWallet credential
store. Without an available store, the application asks you to sign in again.

### AppImage

```sh
chmod +x PapoTUI-*.AppImage
./PapoTUI-*.AppImage
```

Without FUSE, extract the file with `--appimage-extract` and run
`./squashfs-root/AppRun`. Administrator installation is not required.

### Flatpak

Install **flatpak** with your system's package manager: `sudo apt install flatpak`
on Debian/Ubuntu, `sudo dnf install flatpak` on Fedora/RHEL, or
`sudo pacman -S --needed flatpak` on Arch. Add Flathub to obtain the required
Freedesktop runtime, then install the downloaded application bundle:

```sh
flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
flatpak install --user ./papo-tui-*-flatpak-*.flatpak
flatpak run io.github.prodesert22.PapoTUI
```

The downloaded file is the application package. Its data is stored in the
application's isolated directory; it does not automatically share profiles
with other builds.

## Windows

Use **Windows 11** and a terminal with UTF-8 support. Install
[Windows Terminal](https://learn.microsoft.com/en-us/windows/terminal/install)
from Microsoft Store if it is not already present, or run in PowerShell:

```powershell
winget install --id Microsoft.WindowsTerminal --exact
```

Download the **installer** or **portable ZIP** for x86_64 or ARM64. The installer
supports the current user and creates a Start menu shortcut. With the default
installation folder, you can also run it from PowerShell:

```powershell
& "$env:LOCALAPPDATA\Programs\Papo TUI\papo-tui.exe"
```

Use your chosen folder if you changed it during installation. The installer
does not add the executable to PATH.

To use the portable ZIP, extract **all** its contents and run `papo-tui.exe`
from the terminal. Do not move only the executable: it uses the included DLLs,
resources and the media helper in `papo-backends`. MSYS2 is not required to run
the package. The package also contains its font and terminal data; you do not
need CMake, Python or a separate development environment to use it.

From PowerShell in the extracted ZIP folder, run:

```powershell
.\papo-tui.exe
```

## Build from source

[BUILD.md](../BUILD.md) lists development packages and complete build/run commands
for Debian/Ubuntu, Fedora, RHEL 10, Arch Linux and Windows, including native
x86_64 and ARM64 environments. It also covers optional tests and packaging.

## Data and removal

On Linux, profiles are stored in `$XDG_CONFIG_HOME/papo-tui` or `~/.config/papo-tui`;
public state is stored in `$XDG_STATE_HOME/papo-tui` or `~/.local/state/papo-tui`.
On Windows, they are stored in `%LOCALAPPDATA%\papo-tui`, with state in the `state`
subfolder. Persistent cookies are stored exclusively in the system credential store.

Remove the package with the manager used to install it. For ZIP/AppImage, remove
the downloaded folder/file; for Flatpak, run `flatpak uninstall io.github.prodesert22.PapoTUI`.
Uninstalling preserves personal data. To disable session recovery, sign out
through the application before uninstalling.
