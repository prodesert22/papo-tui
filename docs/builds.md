# Available builds

All official builds of a release use the same code and display the same version
with `--version`. Graphics features depend on the terminal's capabilities.

| Build | Target | How to run |
| --- | --- | --- |
| DEB Ubuntu | Ubuntu 24.04, x86_64/ARM64 | `papo-tui` |
| DEB Debian | Debian 13, x86_64/ARM64 | `papo-tui` |
| RPM | Fedora 44, x86_64/ARM64 | `papo-tui` |
| Arch | Arch Linux x86_64 or Arch Linux ARM AArch64 | `papo-tui` |
| AppImage | Linux with glibc 2.39+, x86_64/ARM64 | `.AppImage` file |
| Flatpak | Linux with Flatpak, x86_64/ARM64 | `flatpak run io.github.prodesert22.PapoTUI` |
| ZIP/installer | Windows 11, x86_64/ARM64 | `papo-tui.exe` |

DEB/RPM/Arch packages are installed through the package manager. AppImage and
ZIP include their libraries alongside the program. Flatpak uses Freedesktop
26.08 and its own data directory. ARM64 packages are native builds.

The Windows installer supports installation and removal for the current user.
The ZIP runs without installation; its data is still stored in the user profile.

See the [installation guide](installation.md) to install a downloaded package
and the [usage guide](usage.md) for controls and troubleshooting.
To compile a binary yourself, follow [BUILD.md](../BUILD.md).
