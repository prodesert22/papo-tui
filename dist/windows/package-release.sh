#!/usr/bin/bash
# The Windows release packages: ZIP, installer and their installation lifecycle. tools/ci.py exec
# runs it in the toolchain's shell, which sets PAPO_BUILD and PAPO_PREFIX; CI sets RELEASE_REF and
# BUILD_ARCH.
set -eu
ISCC="$(python dist/windows/tools.py | tail -1)"
export ISCC
version="$(python dist/release.py "$RELEASE_REF" --require-head)"
python dist/package.py --build "$PAPO_BUILD/release" --output build/artifacts --formats zip,installer \
    --arch "$BUILD_ARCH" --flavor windows --dependency-prefix "$PAPO_PREFIX"
python dist/windows/verify.py --artifacts build/artifacts --version "$version" --arch "$BUILD_ARCH"
