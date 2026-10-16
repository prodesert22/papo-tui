#!/bin/sh
# System development packages for the supported Linux CI/packaging hosts.
set -eu
if [ "$(id -u)" = 0 ]; then elevate=''; else elevate='sudo'; fi
if command -v apt-get >/dev/null 2>&1; then
    $elevate apt-get update
    $elevate apt-get install -y build-essential cmake ninja-build pkg-config autoconf automake libtool \
        nasm python3 python3-venv python3-dev curl git libssl-dev zlib1g-dev libncurses-dev \
        libglib2.0-dev libpango1.0-dev libcairo2-dev libfontconfig-dev libpulse-dev libasound2-dev \
        libgcrypt20-dev libass-dev libplacebo-dev ffmpeg openssl dbus-daemon libsecret-1-dev \
        fonts-noto-color-emoji ncurses-term kitty-terminfo patchelf dpkg-dev rpm libarchive-tools desktop-file-utils appstream
    for sources in /etc/apt/sources.list.d/ubuntu.sources /etc/apt/sources.list.d/debian.sources; do
        if [ -f "$sources" ]; then $elevate sed -i 's/^Types: deb$/Types: deb deb-src/' "$sources"; fi
    done
    $elevate apt-get update
elif command -v dnf >/dev/null 2>&1; then
    $elevate dnf install -y gcc gcc-c++ make cmake ninja-build pkgconf-pkg-config autoconf automake libtool \
        nasm python3 python3-pip python3-devel curl git openssl-devel zlib-devel ncurses-devel \
        glib2-devel pango-devel cairo-devel fontconfig-devel pulseaudio-libs-devel alsa-lib-devel \
        libgcrypt-devel libass-devel libplacebo-devel ffmpeg-free openssl dbus-daemon libsecret-devel \
        google-noto-color-emoji-fonts kitty-terminfo patchelf rpm-build libarchive desktop-file-utils appstream dnf5-plugins
else
    echo 'Use a supported Debian/Ubuntu or Fedora packaging host.' >&2
    exit 1
fi
