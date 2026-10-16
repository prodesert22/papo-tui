#!/bin/sh
set -eu
architecture="$1"
version="$2"
python="${PYTHON:-python3}"
mkdir -p build/artifacts build/flatpak
"$python" -c 'import sys; sys.path.insert(0,"dist"); from package import source_archive; from pathlib import Path; source_archive(Path("build/artifacts"),sys.argv[1])' "$version"
source_archive="$(pwd)/build/artifacts/papo-tui-$version-source.tar.gz"
"$python" dist/flatpak/manifest.py --source "$source_archive" --output build/flatpak/manifest.json
flatpak-builder --user --force-clean --disable-rofiles-fuse --bundle-sources --repo=build/flatpak/repo --jobs="${PAPO_BUILD_JOBS:-2}" \
    --default-branch=stable --arch="$architecture" build/flatpak/app build/flatpak/manifest.json
flatpak build-bundle --arch="$architecture" --runtime-repo=https://flathub.org/repo/flathub.flatpakrepo \
    build/flatpak/repo "build/artifacts/papo-tui-$version-flatpak-$architecture.flatpak" io.github.prodesert22.PapoTUI stable
flatpak install --user --noninteractive --reinstall "build/artifacts/papo-tui-$version-flatpak-$architecture.flatpak"
flatpak run io.github.prodesert22.PapoTUI --version | grep -Fx "papo-tui $version"
flatpak run io.github.prodesert22.PapoTUI --help >/dev/null
export PAPO_FLATPAK_TEST_BUS="$DBUS_SESSION_BUS_ADDRESS"
export PYTHONPATH="$(pwd)/tests/support:$(pwd)/tests/terminal:$(pwd)/tests/integration:$(pwd)"
"$python" tests/terminal/test_installed.py "$(pwd)/dist/flatpak/smoke.sh"
# Ship all downloaded source archives and the manifest alongside the bundle.
# Runtime sources remain available through the Freedesktop SDK source extensions.
"$python" dist/flatpak/manifest.py --source "$source_archive" --output build/flatpak/rebuild.json \
    --url "https://github.com/$GITHUB_REPOSITORY/releases/download/v$version/papo-tui-$version-source.tar.gz"
tar -czf "build/artifacts/papo-tui-$version-flatpak-$architecture-sources.tar.gz" \
    build/flatpak/manifest.json build/flatpak/rebuild.json .flatpak-builder/downloads
flatpak uninstall --user --noninteractive io.github.prodesert22.PapoTUI
