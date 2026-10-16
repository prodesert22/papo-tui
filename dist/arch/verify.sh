#!/bin/sh
# Runs on a native ARM64 runner; x86_64 validation uses the official container.
set -eu
package="$(realpath "$1")"
root="$(pwd)/build/arch-root"
mkdir -p "$root"
curl --fail --location --retry 3 https://os.archlinuxarm.org/os/ArchLinuxARM-aarch64-latest.tar.gz -o build/arch-root.tar.gz
sudo bsdtar -xpf build/arch-root.tar.gz -C "$root"
sudo cp /etc/resolv.conf "$root/etc/resolv.conf"
sudo cp "$package" "$root/tmp/papo.pkg.tar.zst"
sudo mount --bind /dev "$root/dev"
sudo mount -t proc proc "$root/proc"
trap 'sudo umount "$root/proc"; sudo umount "$root/dev"' EXIT HUP INT TERM
sudo chroot "$root" pacman-key --init
sudo chroot "$root" pacman-key --populate archlinuxarm
sudo chroot "$root" pacman -Syu --noconfirm
sudo chroot "$root" pacman -U --noconfirm /tmp/papo.pkg.tar.zst
sudo chroot "$root" /usr/bin/papo-tui --version
sudo chroot "$root" /usr/bin/papo-tui --help >/dev/null
sudo chroot "$root" pacman -R --noconfirm papo-tui
