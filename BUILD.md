# Building Papo TUI

This guide builds the Linux binary or native Windows executable from source.
For downloaded packages, use the [installation guide](docs/installation.md).
Run all project commands from the repository root. A source archive can be
extracted instead of cloning; Git is needed only for checkout and release tools.

Start with [Linux](#linux) or [Windows](#windows-11), then choose a format under
[Packaging and releases](#packaging-and-releases).

## Requirements

Use a **C17** compiler, **CMake 3.21+**, Ninja, pkg-config/pkgconf and
**Python 3.10+**. The commands below build locked dependencies into
`build/release-deps`, leaving system libraries in place. Sources and hashes are
recorded in [dist/dependencies.json](dist/dependencies.json); Meson is installed
in a virtual environment. Internet access is needed for the initial downloads.

CMake checks these library requirements, even in Light or Ultralight mode:

| Library | Minimum |
| --- | --- |
| ncursesw and formw | 6.6 |
| libcurl | 7.86, with WS/WSS, TLS and asynchronous DNS |
| Jansson | 2.14 |
| libunibreak | 6.1 |
| GLib, GModule and GIO | 2.66; GIO 2.88 on Windows |
| libsixel | 1.8 |
| Chafa | 1.16 |
| PangoCairo | 1.50 |
| Fontconfig | 2.13 |
| libmpv | API 2.0 |
| FFmpeg | avformat/avcodec 58, avutil 56, swscale 5 |
| libsecret, optional on Linux | 0.20 |
| Noto Color Emoji | System font or verified font resource |

The private ncurses 6.6 build is required for consistent mouse event ordering;
on Windows it also applies the project's Unicode input patch. A system libcurl
can meet the version requirement while lacking WebSocket support. The locked
recipe enables the required features. Do not skip these dependencies based only
on the versions provided by the distribution.

## Linux

Install the host packages for your distribution, then follow
[Build and run on Linux](#build-and-run-on-linux). Packages below provide the
compiler, build tools and system libraries used by the dependency recipes.
The recipes build ncurses, curl, Jansson, libunibreak, libsixel, Chafa, FFmpeg,
libplacebo, libass, libmpv and libsecret in the private prefix.

### Debian and Ubuntu

The package targets are **Ubuntu 24.04** and **Debian 13**.

```sh
sudo apt update
sudo apt install build-essential cmake ninja-build pkg-config autoconf automake libtool nasm \
    python3 python3-venv python3-dev curl git ca-certificates libssl-dev zlib1g-dev \
    libncurses-dev ncurses-term libglib2.0-dev libpango1.0-dev libcairo2-dev \
    libfontconfig-dev libfreetype-dev libfribidi-dev libharfbuzz-dev libpulse-dev \
    libasound2-dev libgcrypt20-dev libsecret-1-dev fonts-noto-color-emoji
```

### Fedora

The release RPM is built on **Fedora 44**.

```sh
sudo dnf install gcc gcc-c++ make cmake ninja-build pkgconf-pkg-config autoconf automake \
    libtool nasm python3 python3-pip python3-devel curl git ca-certificates \
    openssl-devel zlib-devel ncurses-devel glib2-devel pango-devel cairo-devel \
    fontconfig-devel freetype-devel fribidi-devel harfbuzz-devel pulseaudio-libs-devel \
    alsa-lib-devel libgcrypt-devel libsecret-devel google-noto-color-emoji-fonts
```

### RHEL 10

These source instructions target **RHEL 10**, whose system Pango satisfies the
minimum above. Enable CodeReady Linux Builder for the development packages:

```sh
sudo subscription-manager repos --enable "codeready-builder-for-rhel-10-$(arch)-rpms"
sudo dnf install gcc gcc-c++ make cmake ninja-build pkgconf-pkg-config autoconf automake \
    libtool nasm python3 python3-pip python3-devel curl git ca-certificates \
    openssl-devel zlib-ng-compat-devel ncurses-devel glib2-devel pango-devel cairo-devel \
    fontconfig-devel freetype-devel fribidi-devel harfbuzz-devel pulseaudio-libs-devel \
    alsa-lib-devel libgcrypt-devel libsecret-devel
```

Package availability follows the [RHEL 10 package manifest](https://docs.redhat.com/en/documentation/red_hat_enterprise_linux/10/html-single/package_manifest/index).
The project has no native RHEL CI target; this recipe uses its host development
packages and the same private dependencies as the other Linux builds. RHEL 8/9
system libraries do not satisfy all of the minimums, and the bootstrap does not
build Pango/GLib. Use the [Flatpak installation](docs/installation.md#rhel) there.

### Arch Linux and Arch Linux ARM

```sh
sudo pacman -Syu --needed base-devel cmake ninja pkgconf autoconf automake libtool nasm \
    python python-pip curl git ca-certificates openssl zlib ncurses glib2 pango cairo \
    fontconfig freetype2 fribidi harfbuzz libpulse alsa-lib libgcrypt libsecret noto-fonts-emoji
```

Arch development headers are included in the library packages. On Arch Linux
ARM, build natively on AArch64 with its repositories; the commands do not set up
a cross compiler.

### Build and run on Linux

After installing the host packages, obtain the source and prepare the private
dependencies. Skip the first two commands if the source is already checked out:

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

The binary is **build/local/papo-tui**. Keep the private dependency prefix and
the accompanying **build/local/papo-backends** directory, which holds the media
helper and modules. In a new shell, restore the library path above before
running this local build. This is a development binary with the `-dev` version
suffix; packaging is covered [below](#packaging-and-releases).

On RHEL, if Noto Color Emoji is not installed, the resource command above
downloads the project's verified font. Install it for your user before running:

```sh
install -Dm644 build/resources/NotoColorEmoji.ttf "$HOME/.local/share/fonts/papo-tui/NotoColorEmoji.ttf"
fc-cache -f
```

## Windows 11

Install [MSYS2](https://www.msys2.org/) and native Windows
[CPython 3.13](https://docs.python.org/3.13/using/windows.html), including the
**py launcher**. The project uses native Windows Python for its build scripts
and tests. Open the MSYS2 environment for your architecture:

| Architecture | Shell | Package prefix |
| --- | --- | --- |
| x86_64 | MSYS2 CLANG64 | `mingw-w64-clang-x86_64` |
| ARM64 | MSYS2 CLANGARM64 | `mingw-w64-clang-aarch64` |

See the MSYS2 [environment table](https://www.msys2.org/docs/environments/) and
[ARM64 setup](https://www.msys2.org/docs/arm64/). Use packages from the selected
environment throughout the build.

Update MSYS2 first. If requested, close the terminal, reopen the same environment
and run the update again:

```sh
pacman -Syu
```

Install the tools and system libraries in that CLANG64/CLANGARM64 shell. The
shell supplies `MINGW_PACKAGE_PREFIX` for the selected architecture:

```sh
pacman -S --needed base-devel git autoconf automake libtool nasm \
    "${MINGW_PACKAGE_PREFIX}-toolchain" "${MINGW_PACKAGE_PREFIX}-cmake" \
    "${MINGW_PACKAGE_PREFIX}-ninja" "${MINGW_PACKAGE_PREFIX}-pkgconf" \
    "${MINGW_PACKAGE_PREFIX}-ncurses" "${MINGW_PACKAGE_PREFIX}-glib2" \
    "${MINGW_PACKAGE_PREFIX}-pango" "${MINGW_PACKAGE_PREFIX}-fontconfig" \
    "${MINGW_PACKAGE_PREFIX}-libass" "${MINGW_PACKAGE_PREFIX}-libplacebo" \
    "${MINGW_PACKAGE_PREFIX}-ffmpeg" "${MINGW_PACKAGE_PREFIX}-libgcrypt" \
    "${MINGW_PACKAGE_PREFIX}-vulkan-headers"
```

Vulkan headers are needed by libplacebo's stubs even with Vulkan disabled.
Clone/extract the project and build from the same shell. This route runs the
same configure, build and tests as the native Windows CI:

```sh
git clone https://github.com/prodesert22/papo-tui.git
cd papo-tui
py -3.13 -m venv build/test-env
build/test-env/Scripts/python.exe -m pip install --require-hashes -r requirements/ci-windows-py313.txt
export PATH="$PWD/build/test-env/Scripts:$PATH"
export PAPO_SYSTEM_BIN="$(cygpath -w "$MINGW_PREFIX/bin")"
python dist/build.py --build build/release --prefix build/release-deps
export PATH="$PWD/build/release-deps/bin:$PATH"
export TERMINFO="$(cygpath -m "$MINGW_PREFIX/share/terminfo")"
./build/release/papo-tui.exe
```

Skip clone and cd if already inside the source folder. The executable is
**build/release/papo-tui.exe**; keep **build/release/papo-backends**, the MSYS2
libraries and the private prefix available when running it locally.

For a folder that includes the required DLLs, font configuration, terminal data
and media helper, install the completed build:

```sh
cmake --install build/release --prefix "$PWD/build/windows-portable"
./build/windows-portable/papo-tui.exe
```

Keep the whole installed folder when copying it to another Windows machine of
the same architecture. MSYS2 and Python are needed to build, not to run that
installed folder. The ZIP/installer recipes use this same installation layout.

## Tests and build options

For Linux tests, install an **ffmpeg command-line executable** in addition to
the private FFmpeg libraries: `sudo apt install ffmpeg` on Debian/Ubuntu,
`sudo dnf install ffmpeg-free` on Fedora, or `sudo pacman -S --needed ffmpeg` on
Arch. On RHEL, the executable must come from a repository that supplies it for
your RHEL version; the native binary build above does not require it.

Use **CPython 3.13** and the matching committed lock for the same Python
packages as CI. On Linux, from the dependency build shell:

```sh
python3.13 -m venv build/test-env
build/test-env/bin/python -m pip install --require-hashes -r requirements/ci-linux-py313.txt
build/test-env/bin/python dist/build.py --build build/ci --prefix build/release-deps --variants release
```

On Windows, use native CPython 3.13 in the selected MSYS2 shell and install
`requirements/ci-windows-py313.txt` into `build/test-env`. Run the same
`dist/build.py` command with `build/test-env/Scripts/python.exe`. The four
native OS/architecture combinations run on every PR. Sanitizers and coverage
use separate Linux x86_64 jobs; CI never multiplies build and test concurrency
by starting three variants on one runner.

### Select or reproduce a test

Every unittest method has a stable CTest name. Graphics scenarios additionally
identify their protocol and, for panel composition, their panel. List configured
cases without downloading dependencies, compiling or executing them:

```sh
build/test-env/bin/python dist/build.py --build build/ci/release --list-tests
build/test-env/bin/python dist/build.py --build build/ci/release --test-only --suite terminal --test-jobs 2
build/test-env/bin/python dist/build.py --build build/ci/release --test-only --case 'emojis-terminal/EmojiTerminal.test_grouping_survives_prepend_scroll_and_resize'
```

Categories are `unit`, `integration`, `terminal`, `media`, `contract` and
`lifecycle`; `all` runs the profile's default selection. Release runs all
applicable cases, sanitizers run unit/integration/lifecycle cases, and coverage
runs unit/integration cases. Add `--full-instrumentation` to execute the entire
suite with sanitizers or coverage; the scheduled CI does this automatically.
`--suite all` with that option also reproduces the full instrumented suite.
`--jobs` limits compilation and `--test-jobs` limits concurrent test processes.
Multiple local `--variants release,sanitizers,coverage` execute sequentially.
For separate compilation and test stages, run the build command with
`--build-only`, then repeat it with `--test-only`. CI saves verified dependencies
between these stages, so test failures preserve the compiled dependency cache.

CTest remains available directly:

```sh
ctest --test-dir build/ci/release --output-on-failure --parallel 2 --output-junit results.xml
ctest --test-dir build/ci/release --output-on-failure --repeat until-fail:10 -R 'test_grouping_survives_prepend_scroll_and_resize'
python tools/quality.py
```

A test's `send()` delivers input; it does not establish that rendering or a
network operation has finished. Shared terminal helpers belong in
`tests/support`, with explicit waits for visible state or mock events. Test
modules must be importable without consuming arguments or starting services.
New Python scenarios must appear exactly once in the CTest inventory. Platform
exclusions require a reason in `tests/platforms.json`; missing dependencies in
a required CI profile are failures. `tools/check_test_inventory.py --build`
checks discovery against actual registrations before the shared runner tests.

Static analysis uses **Clang 18** explicitly, independent of the system's default
Clang. Install `clang-tidy-18` and run `python tools/analyze.py --build
build/ci/release`. To use an equivalent executable elsewhere, pass
`--clang-tidy /absolute/path/to/clang-tidy` and record its version. Analyzer
upgrades require their own reviewed validation.

### Reports and timing

Each build's `ci-logs` contains stage timings, environment information, inventory,
JUnit results, a timing summary and all scenarios above **30 seconds**. Failed
Python cases also save safely escaped terminal output, the screen and HTTP
methods/paths under `Testing/Temporary/cases`. Credentials and cookies are
redacted. GitHub uploads these reports even when a job fails.
`preparation.json` measures dependency/resource preparation; `build-stages.json`
and `stages.json` distinguish compilation from test execution in split stages.

Synthetic FFmpeg media is prepared once per tool identity, verified by hashes,
and copied into each case's private directory. Test fonts are downloaded from
verified sources; raster tests use their own Fontconfig configuration. Initial
resource downloads need network access; mock HTTP tests use loopback servers.
Linux CI generates `pt_BR.UTF-8` for the decimal-locale scenario. For an existing
Linux test host, install its locale data and run `sudo localedef -i pt_BR -f UTF-8
pt_BR.UTF-8` if none of `pt_BR.UTF-8`, `de_DE.UTF-8` or `fr_FR.UTF-8` is available.
The FFmpeg CLI needs the `libwebp_anim` encoder to generate animated fixtures;
the application's own libraries perform the decoding checks.
PTY and graphics protocol results do not establish visual correctness in a real
emulator. Keep real-emulator visual evidence separate under `build/`.

To compare completed timing reports, use:

```sh
python tools/test_report.py --compare build/run-1/ci-logs/timings.json build/run-2/ci-logs/timings.json build/run-3/ci-logs/timings.json
```

To record the whole GitHub matrix after an attempt finishes, authenticate `gh`
and run `python tools/ci_timings.py RUN_ID --attempt ATTEMPT`. The report includes
elapsed workflow time, summed job time, each job result and the exact commit;
reports remain under `build/ci-reports`.

The periodic permission-reconciliation scenario deliberately exercises a real
30-second interval. Its reason is recorded in `tests/time_budgets.json`; other
cases over 30 seconds remain visible for investigation and subdivision.

Record cache state, CPU budget and profile alongside comparisons. Report the
whole matrix's elapsed time and runner minutes as well as individual test times;
a shorter job must not conceal omitted scenarios or failed attempts.
CI sets `PAPO_TEST_REQUIRED=1`: an ignored case fails unless its exact ID and
reason are declared for that platform in `tests/platforms.json`. Ad hoc local
runs still report capability skips explicitly.

### Update test dependencies

`requirements-dev.txt` is the input; the native CI locks include transitive
versions and hashes. Use the maintained uv resolver for Linux/Windows markers
without depending on the host's default Python encoding or package environment:

```sh
python -m pip install -r requirements/lock-tools.txt
python tools/lock_requirements.py
```

Review both generated locks and validate them on all four native targets. The
lock generator is a development tool; it adds no runtime dependency to Papo TUI.
Keep the C dependency recipes and resource manifests synchronized with their
hashes. A cached compiled dependency is revalidated against its source, recipe,
compiler, architecture and installation prefix before reuse.

| CMake option | Effect |
| --- | --- |
| `BUILD_TESTING=OFF` | Compile the application without test executables |
| `PAPO_RELEASE_BUILD=ON` | Display the version without `-dev` |
| `PAPO_NATIVE_FILE_DIALOGS=OFF` | Always use the terminal file picker |
| `PAPO_DESKTOP_SESSIONS=OFF` | Disable credential-store session recovery |
| `PAPO_DESKTOP_NOTIFICATIONS=OFF` | Disable desktop popups |
| `PAPO_SANITIZERS=ON` | Enable ASan/UBSan on supported Linux compilers |
| `PAPO_COVERAGE=ON` | Enable coverage instrumentation with GCC on Linux |
| `PAPO_BUNDLE_RUNTIME=ON` | Copy runtime libraries during installation |

The `debug`, `release`, `sanitizers` and `coverage` CMake presets are available.
Pass the same `CMAKE_PREFIX_PATH` and `Python3_EXECUTABLE` when configuring one.
The release preset enables `PAPO_RELEASE_BUILD`; sanitizer/coverage presets are
for Linux development. Installed Linux runtime bundles also require **patchelf**
on the build host.

## Packaging and releases

All examples below use the current version **0.4.0** and **x86_64**. For ARM64,
run on an ARM64 build host and replace the architecture argument with
**aarch64**. The `--arch` option checks the built binaries; it does not set up
cross-compilation. Use a separate native build for each OS/architecture.

| Output | Build host | Additional packaging tools |
| --- | --- | --- |
| Linux binary `papo-tui` | Linux | None beyond the source-build requirements |
| [`.deb`](#debian-and-ubuntu-deb) | Ubuntu 24.04 or Debian 13 | CPack, dpkg-dev, patchelf |
| [`.rpm`](#fedora-rpm) | Fedora 44 | CPack, rpm-build, DNF source download plugin, patchelf |
| [`.pkg.tar.zst`](#arch-linux-package) | Ubuntu 24.04 release recipe | makepkg, pacman-package-manager, zstd |
| [`.AppImage`](#appimage) | Ubuntu 24.04 release recipe | patchelf; verified appimagetool downloaded automatically |
| [`.flatpak`](#flatpak-bundle) | Linux | flatpak, flatpak-builder, Freedesktop 26.08 SDK/runtime |
| Windows executable `papo-tui.exe` | Windows 11 | None beyond the MSYS2 source-build requirements |
| [Portable `.zip`](#windows-zip-and-installer) | Windows 11 | Python's standard ZIP support |
| [Installer `-setup.exe`](#windows-zip-and-installer) | Windows 11 | Verified Inno Setup compiler |

Native package recipes include runtime libraries, their licenses and source
provenance. Keep the dependency prefix and `build/source-cache` created by the
bootstrap. A binary built with system media libraries or without runtime
bundling is not a valid input for these release recipes.

### Prepare a Linux packaging build

On Ubuntu 24.04, Debian 13 or Fedora 44, prepare the full packaging environment:

```sh
sh dist/linux-deps.sh
python3 -m venv build/package-env
build/package-env/bin/python -m pip install -r requirements-dev.txt
export PATH="$PWD/build/package-env/bin:$PATH"
python dist/build.py --build build/package --prefix build/release-deps
./build/package/papo-tui --version
```

The setup script installs development/packaging tools and enables Debian source
repositories for source collection. Fedora uses `dnf5-plugins` for source RPM
downloads. The build script prepares locked dependencies and font resources,
enables `PAPO_RELEASE_BUILD` and `PAPO_BUNDLE_RUNTIME`, compiles and runs CTest.
The version command should print **papo-tui 0.4.0**.

Use the same Python environment for the following commands. Run packaging as
your regular user; the Arch recipe's makepkg refuses root. Each command writes
to **build/artifacts** and also creates the application source archive and a
dependency-source archive. `python dist/package.py --help` lists all options.

### Debian and Ubuntu DEB

After the packaging build on **Ubuntu 24.04**:

```sh
python dist/package.py --build build/package --output build/artifacts \
    --formats deb --arch x86_64 --flavor ubuntu --dependency-prefix build/release-deps
sudo apt install ./build/artifacts/papo-tui-0.4.0-ubuntu-x86_64.deb
papo-tui --version
```

On **Debian 13**, build in Debian and use:

```sh
python dist/package.py --build build/package --output build/artifacts \
    --formats deb --arch x86_64 --flavor debian --dependency-prefix build/release-deps
sudo apt install ./build/artifacts/papo-tui-0.4.0-debian-x86_64.deb
papo-tui --version
```

### Fedora RPM

After the packaging build on **Fedora 44**:

```sh
python dist/package.py --build build/package --output build/artifacts \
    --formats rpm --arch x86_64 --flavor fedora --dependency-prefix build/release-deps
sudo dnf install ./build/artifacts/papo-tui-0.4.0-fedora-x86_64.rpm
papo-tui --version
```

The packaged glibc baseline follows the build host. This creates a Fedora RPM,
not an RPM targeted at RHEL. Use Flatpak for the documented RHEL installation.

### Arch Linux package

The repository's Arch release recipe packages the runtime bundle built on
**Ubuntu 24.04**, then verifies installation on Arch. Its source-provenance
collector supports Debian/Fedora packaging hosts and Windows; it does not
collect native Arch library sources. A local binary can still be compiled
directly on Arch using the earlier instructions.

On the Ubuntu packaging host, after the common build:

```sh
sudo apt install makepkg pacman-package-manager zstd
python dist/package.py --build build/package --output build/artifacts \
    --formats arch --arch x86_64 --flavor ubuntu --dependency-prefix build/release-deps
```

Copy **build/artifacts/papo-tui-0.4.0-arch-x86_64.pkg.tar.zst** to the matching
Arch system and install it with:

```sh
sudo pacman -U ./papo-tui-0.4.0-arch-x86_64.pkg.tar.zst
papo-tui --version
```

For Arch Linux ARM, build the Ubuntu AArch64 bundle natively and use
`--arch aarch64`.

### AppImage

After the common packaging build on **Ubuntu 24.04**:

```sh
python dist/package.py --build build/package --output build/artifacts \
    --formats appimage --arch x86_64 --flavor ubuntu --dependency-prefix build/release-deps
chmod +x build/artifacts/PapoTUI-0.4.0-linux-x86_64.AppImage
APPIMAGE_EXTRACT_AND_RUN=1 ./build/artifacts/PapoTUI-0.4.0-linux-x86_64.AppImage --version
```

The tool downloads and verifies the appimagetool pinned in `dist/tools.json`.
The version check above runs without FUSE. This Ubuntu-built AppImage requires
**glibc 2.39+** on its destination system. To generate the Ubuntu DEB, AppImage
and Arch package together, use `--formats deb,appimage,arch` after installing
the Arch packaging tools.

### Flatpak bundle

Install **flatpak**, **flatpak-builder** and a D-Bus session runner on Linux.
For example, on Ubuntu/Debian:

```sh
sudo apt install flatpak flatpak-builder dbus-daemon python3-venv
python3 -m venv build/package-env
build/package-env/bin/python -m pip install -r requirements-dev.txt
export PATH="$PWD/build/package-env/bin:$PATH"
flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
flatpak install --user flathub org.freedesktop.Platform//26.08 org.freedesktop.Sdk//26.08
export GITHUB_REPOSITORY=prodesert22/papo-tui
export PYTHON="$PWD/build/package-env/bin/python"
dbus-run-session -- sh dist/flatpak/build.sh x86_64 0.4.0
```

On Fedora install the host tools with
`sudo dnf install flatpak flatpak-builder dbus-daemon python3-pip`; on Arch use
`sudo pacman -S --needed flatpak flatpak-builder dbus python python-pip`.
Flatpak builds its own dependencies inside the SDK and does not need the native
packaging build. The recipe installs the generated application for smoke tests
and uninstalls it afterward; use a separate build account if the application
is already installed in your account.

Outputs are **build/artifacts/papo-tui-0.4.0-flatpak-x86_64.flatpak** and the
corresponding **-sources.tar.gz**, plus the application source archive. To use
the resulting bundle after the tests:

```sh
flatpak install --user ./build/artifacts/papo-tui-0.4.0-flatpak-x86_64.flatpak
flatpak run io.github.prodesert22.PapoTUI
```

### Windows ZIP and installer

First complete the **Windows 11** source build above in CLANG64 or CLANGARM64.
It produces **build/release/papo-tui.exe** and a release tree ready for packaging.
In the same MSYS2 shell, create a portable ZIP:

```sh
python dist/package.py --build build/release --output build/artifacts \
    --formats zip --arch x86_64 --flavor windows --dependency-prefix build/release-deps
```

The result is **build/artifacts/papo-tui-0.4.0-windows-x86_64.zip**. It contains
the executable, DLLs, terminal/font data and `papo-backends`; extract the whole
ZIP before running `papo-tui.exe` from PowerShell.

To produce the installer executable as well, download and install the
checksum-verified Inno Setup into `build/inno`, then package:

```sh
export ISCC="$(python dist/windows/tools.py | tail -1)"
python dist/package.py --build build/release --output build/artifacts \
    --formats zip,installer --arch x86_64 --flavor windows --dependency-prefix build/release-deps
python dist/windows/verify.py --artifacts build/artifacts --version 0.4.0 --arch x86_64
```

The installer is **build/artifacts/papo-tui-0.4.0-windows-x86_64-setup.exe**.
The verification command extracts/tests the ZIP and checks installer
installation, upgrade and removal in a temporary directory. Use
`--formats installer` to generate only setup; the verification command requires
both ZIP and installer. For ARM64, use the CLANGARM64 build and `aarch64` in
both commands. The package tool also writes the application and dependency
source archives.

### Complete Linux release matrix and checksums

On a native Ubuntu runner, install **Docker Engine**, enable access to its
daemon for the build user, and install Flatpak/Builder. The pipeline clones the
current **committed HEAD**, so commit the source to be packaged first.

```sh
sudo apt install flatpak flatpak-builder dbus-daemon python3-venv libarchive-tools
python3 -m venv build/package-env
build/package-env/bin/python -m pip install -r requirements-dev.txt
export PATH="$PWD/build/package-env/bin:$PATH"
export GITHUB_REPOSITORY=prodesert22/papo-tui
export SOURCE_DATE_EPOCH="$(git show -s --format=%ct HEAD)"
docker info
python dist/ci_linux.py --prepare
python dist/ci_linux.py --arch x86_64 --version 0.4.0 --jobs 4 --workers 2
```

Install Docker using its [Ubuntu instructions](https://docs.docker.com/engine/install/ubuntu/).
The pipeline uses Ubuntu 24.04, Debian 13 and Fedora 44 containers, plus the
Flatpak SDK. It builds/tests the packages, verifies package installation and
assembles them into **build/artifacts**. Logs are under
**build/release-tasks/logs**. Run separately on AArch64 for the ARM64 artifacts;
the pipeline does not cross-compile them.

After assembling **all** required Linux and Windows artifacts for both
architectures into one directory, validate the complete release and create
**SHA256SUMS**:

```sh
python dist/release.py v0.4.0 --assets build/artifacts
```

This final check requires the complete asset set, including source archives;
it rejects missing or extra artifacts. It does not publish a release. For a
single local package, use its per-format commands above without this final
all-platform check.

Prepare versions on `dev` and open a pull request to `main`. Release-preparation
commit titles use English and start with `Release MAJOR.MINOR.PATCH: ...`.
Update CMakeLists.txt, CHANGELOG.md and the complete versioned release notes,
consolidating Unreleased and leaving it empty in the same commit. Release
automation requires a push to main that increases the CMake version; published
releases and existing tags are never replaced or moved.
