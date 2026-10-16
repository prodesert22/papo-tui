#!/bin/sh
# One isolated distribution build; called by ci_linux.py on a native runner.
set -eu
flavor="$1"
architecture="$2"
version="$3"
jobs="$4"
trap 'rm -rf build/dependency-*; chown -R "$PAPO_RUNNER_UID:$PAPO_RUNNER_GID" /workspace' EXIT
sh dist/linux-deps.sh
python3 -m venv build/test-env
export PATH="$PWD/build/test-env/bin:$PATH"
python -m pip install -r requirements-dev.txt
variants=release
if [ "$flavor:$architecture" = ubuntu:x86_64 ]; then
    variants=release,sanitizers,coverage
    apt-get install -y clang-tidy
fi
status=0
python dist/build.py --build build --prefix build/release-deps --variants "$variants" --jobs "$jobs" || status=$?
if [ "$variants" != release ]; then
    if [ -f build/coverage/CMakeCache.txt ]; then
        gcovr --root . --filter src/ --exclude 'src/model/text/emoji_data.inc' \
            --html-details build/coverage/index.html --xml build/coverage/coverage.xml --print-summary || status=1
    fi
    python tools/analyze.py --build build/release --jobs "$jobs" || status=1
fi
[ "$status" = 0 ] || exit "$status"
case "$flavor" in
    ubuntu) formats=deb,appimage,arch ;;
    debian) formats=deb ;;
    fedora) formats=rpm ;;
    *) exit 2 ;;
esac
if [ "$flavor" = ubuntu ]; then
    apt-get install -y makepkg pacman-package-manager zstd
    useradd -m builder
    chown -R builder:builder /workspace
    runuser -u builder -- build/test-env/bin/python dist/package.py --build build/release --output build/artifacts \
        --formats "$formats" --arch "$architecture" --flavor "$flavor" --dependency-prefix build/release-deps
else
    python dist/package.py --build build/release --output build/artifacts \
        --formats "$formats" --arch "$architecture" --flavor "$flavor" --dependency-prefix build/release-deps
fi
if [ "$flavor" = fedora ]; then
    dnf install -y "build/artifacts/papo-tui-$version-fedora-$architecture.rpm"
else
    apt-get install -y "./build/artifacts/papo-tui-$version-$flavor-$architecture.deb"
fi
papo-tui --version
PYTHONPATH="$PWD/tests/support:$PWD/tests/terminal:$PWD/tests/integration:$PWD" \
    python tests/terminal/test_installed.py /usr/bin/papo-tui
if [ "$flavor" = fedora ]; then dnf remove -y papo-tui; else apt-get remove -y papo-tui; fi
