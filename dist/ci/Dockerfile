# The Linux CI toolchain: GitHub and a developer's machine run every job inside this image.
# tools/ci.py names it after the hash of its inputs, so any change here yields a new tag.
ARG UBUNTU=ubuntu:24.04@sha256:73b999393d07f2bf65a40373778a44dc9a6d54ad7718d29247d8ff9413c4d6a8

# Tools the Ubuntu archive does not carry in the versions CI uses; each file is verified.
FROM scratch AS downloads-amd64
ADD --checksum=sha256:5a1133ff103c71eb5120e2cc3de922733e7d8a26a98ae716397e8676adb367bf \
    https://github.com/Kitware/CMake/releases/download/v3.31.6/cmake-3.31.6-linux-x86_64.tar.gz /cmake.tar.gz
ADD --checksum=sha256:5749cbc4e668273514150a80e387a957f933c6ed3f5f11e03fb30955e2bbead6 \
    https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-linux.zip /ninja.zip
ADD --checksum=sha256:8aca8db96f1b94770f1b0d72b6dddcb1ebb8123cb3712530b08cc387b349a3d8 \
    https://github.com/rhysd/actionlint/releases/download/v1.7.12/actionlint_1.7.12_linux_amd64.tar.gz /actionlint.tar.gz
ADD --checksum=sha256:f2d2199eb25880e07863b391cf7124e7ffa1338b0d65600dfcec117c4223a7fb \
    https://github.com/astral-sh/python-build-standalone/releases/download/20261110/cpython-3.13.18+20261110-x86_64-unknown-linux-gnu-install_only_stripped.tar.gz /python.tar.gz

FROM scratch AS downloads-arm64
ADD --checksum=sha256:b4cc788d63112b2749b40627e719eb5d3b8ed8f00c36d77189f4019cfe64bc9e \
    https://github.com/Kitware/CMake/releases/download/v3.31.6/cmake-3.31.6-linux-aarch64.tar.gz /cmake.tar.gz
ADD --checksum=sha256:fd2cacc8050a7f12a16a2e48f9e06fca5c14fc4c2bee2babb67b58be17a607fc \
    https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-linux-aarch64.zip /ninja.zip
ADD --checksum=sha256:325e971b6ba9bfa504672e29be93c24981eeb1c07576d730e9f7c8805afff0c6 \
    https://github.com/rhysd/actionlint/releases/download/v1.7.12/actionlint_1.7.12_linux_arm64.tar.gz /actionlint.tar.gz
ADD --checksum=sha256:55c00325f7c1fddf325dfc621aabdfbbac4dc54adda224af88ec411dc12db239 \
    https://github.com/astral-sh/python-build-standalone/releases/download/20261110/cpython-3.13.18+20261110-aarch64-unknown-linux-gnu-install_only_stripped.tar.gz /python.tar.gz

FROM downloads-${TARGETARCH} AS downloads

FROM ${UBUNTU}
ARG APT_SNAPSHOT=20261118T000000Z
ENV DEBIAN_FRONTEND=noninteractive

# Every package comes from one dated snapshot of the archive. The snapshot is served over HTTPS
# and the base image has no certificates, so they are installed first and then, like every
# other package, set to the snapshot's version by the pin below.
COPY dist/linux-deps.sh /opt/papo/recipe/dist/linux-deps.sh
RUN <<EOF
set -eu
apt-get update
apt-get install -y --no-install-recommends ca-certificates
cat > /etc/apt/apt.conf.d/50papo-snapshot <<CONF
APT::Snapshot "${APT_SNAPSHOT}";
Acquire::Snapshots::URI::Host::ports.ubuntu.com "https://snapshot.ubuntu.com/ubuntu/@SNAPSHOTID@/";
CONF
printf 'Package: *\nPin: release o=Ubuntu\nPin-Priority: 1001\n' > /etc/apt/preferences.d/papo-snapshot
apt-get update
apt-get -y --allow-downgrades dist-upgrade
sh /opt/papo/recipe/dist/linux-deps.sh
# actionlint checks the workflows' shell with shellcheck when it is installed.
apt-get install -y clang-tidy-18 shellcheck
test -z "$(apt-get -s --allow-downgrades dist-upgrade | grep -E '^(Inst|Remv) ')"
# The same account name and home on every host; the entry point gives it the caller's uid.
userdel --remove ubuntu
dpkg-query -W -f '${Package} ${Version}\n' > /opt/papo/apt-packages.txt
rm -rf /var/lib/apt/lists/*
EOF

RUN --mount=type=bind,from=downloads,target=/downloads <<EOF
set -eu
mkdir -p /opt/papo/cmake /opt/papo/python
tar -xzf /downloads/cmake.tar.gz -C /opt/papo/cmake --strip-components=1
ln -s /opt/papo/cmake/bin/cmake /opt/papo/cmake/bin/ctest /opt/papo/cmake/bin/cpack /usr/local/bin/
bsdtar -xf /downloads/ninja.zip -C /usr/local/bin ninja
tar -xzf /downloads/actionlint.tar.gz -C /usr/local/bin actionlint
tar -xzf /downloads/python.tar.gz -C /opt/papo/python --strip-components=1
chmod 755 /usr/local/bin/ninja /usr/local/bin/actionlint
EOF

COPY requirements/ci-linux-py313.txt /opt/papo/recipe/requirements/ci-linux-py313.txt
RUN /opt/papo/python/bin/python3.13 -m venv /opt/papo/venv \
    && /opt/papo/venv/bin/python -m pip install --no-cache-dir --require-hashes \
        -r /opt/papo/recipe/requirements/ci-linux-py313.txt
ENV PATH=/opt/papo/venv/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

# The locked libraries are compiled once, here. Their stamps name this prefix and compiler, so
# dist/build.py finds them complete and neither downloads nor compiles a dependency in a job.
COPY dist/bootstrap.py dist/voice_dependencies.py dist/dependencies.json dist/resources.py \
    dist/resources.json /opt/papo/recipe/dist/
COPY dist/patches /opt/papo/recipe/dist/patches
COPY tests/fixtures/resources.json /opt/papo/recipe/tests/fixtures/resources.json
RUN <<EOF
set -eu
python /opt/papo/recipe/dist/bootstrap.py --prefix /opt/papo/deps \
    --cache /opt/papo/source-cache --jobs "$(nproc)"
python /opt/papo/recipe/dist/resources.py --output /opt/papo/resources --manifest \
    /opt/papo/recipe/dist/resources.json /opt/papo/recipe/tests/fixtures/resources.json
rm -rf /opt/papo/dependency-* /opt/papo/source-cache
# dist/build.py marks the prefix as prepared, as whichever user runs the job.
chmod -R a+rwX /opt/papo/deps
EOF

COPY --chmod=755 dist/ci/entrypoint.sh /opt/papo/entrypoint.sh
ENTRYPOINT ["/opt/papo/entrypoint.sh"]
