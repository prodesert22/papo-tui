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

## Build a configuration

After preparing the dependencies from the [installation guide](instalacao.md):

```sh
cmake --preset release
cmake --build --preset release
```

The `debug`, `sanitizers` and `coverage` presets are also available.
The last two are for Linux development. If you use a Python virtual environment
or external dependencies, pass `-DPython3_EXECUTABLE=...` and
`-DCMAKE_PREFIX_PATH=...` when configuring the preset.

Useful options:

| CMake option | Effect |
| --- | --- |
| `BUILD_TESTING=OFF` | Build only the application |
| `PAPO_RELEASE_BUILD=ON` | Version without the `-dev` suffix |
| `PAPO_DESKTOP_SESSIONS=OFF` | Disable recovery through the credential store |
| `PAPO_DESKTOP_NOTIFICATIONS=OFF` | Disable popups; keep the notification center |
| `PAPO_BUNDLE_RUNTIME=ON` | Include libraries in the installation |

See the [usage guide](uso.md) for instructions and troubleshooting.
