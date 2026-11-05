#!/usr/bin/env bash
# Makes TuxAPE's package for Ubuntu and Debian (.deb) from its Linux
# archive (tools/linux-release.sh): installed with a double click (the App
# Center) or `sudo apt install ./tuxape_<version>_amd64.deb`, it is in the
# list of programs with its icon, and goes with `sudo apt remove tuxape`.
#
#   tools/linux-deb.sh [ARCHIVE]
#
# ARCHIVE: the newest tuxape-*-linux-x86_64.tar.gz of release/ by default;
# the package goes beside it.
#
# What is in it:
#   /opt/tuxape/                       the archive's folder, as it is
#   /usr/bin/tuxape                    a link to it, for a terminal
#   /usr/share/applications/           tuxape.desktop, the menu's entry
#   /usr/share/icons/hicolor/*/apps/   the icon, at each size
#   /usr/share/doc/tuxape/             the licence and the read-me files
# Installed there, where only the system writes, TuxAPE keeps the user's
# files in ~/TuxAPE (see src/app/programfolder.h).
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
archive=${1:-$(ls -t "$root"/release/tuxape-*-linux-x86_64.tar.gz 2>/dev/null | head -1 || true)}
[ -n "$archive" ] && [ -f "$archive" ] || { echo "no archive: tools/linux-release.sh makes one" >&2; exit 1; }
archive=$(realpath "$archive")
name=$(basename "$archive" .tar.gz)                 # tuxape-1.1-linux-x86_64
version=${name#tuxape-}; version=${version%-linux-x86_64}
out=$(dirname "$archive")/tuxape_${version}_amd64.deb

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
pkg=$work/tuxape_${version}_amd64

# The program and what it comes with.
mkdir -p "$pkg/opt"
tar -xzf "$archive" -C "$pkg/opt"
mv "$pkg/opt/$name" "$pkg/opt/tuxape"
mkdir -p "$pkg/usr/bin"
ln -s /opt/tuxape/tuxape "$pkg/usr/bin/tuxape"

# Its entry in the list of programs, and its icon.
install -D -m 644 "$root/tools/linux-desktop/tuxape.desktop" "$pkg/usr/share/applications/tuxape.desktop"
for size in 16 32 48 64 128 256 512; do
    install -D -m 644 "$root/src/app/resources/icons/tuxape-$size.png" \
        "$pkg/usr/share/icons/hicolor/${size}x${size}/apps/tuxape.png"
done
mkdir -p "$pkg/usr/share/doc/tuxape"
for text in LICENCE.txt LISEZMOI.txt README.txt; do
    [ -f "$pkg/opt/tuxape/$text" ] && cp "$pkg/opt/tuxape/$text" "$pkg/usr/share/doc/tuxape/"
done
cp "$pkg/opt/tuxape/LICENCE.txt" "$pkg/usr/share/doc/tuxape/copyright"

# Files readable by all, programs and folders runnable.
find "$pkg" -type d -exec chmod 755 {} +
find "$pkg" -type f -exec chmod 644 {} +
find "$pkg/opt/tuxape" -type f -exec sh -c 'head -c 4 "$1" | grep -q "ELF" && chmod 755 "$1"' _ {} \;

# What the system is to have: Qt is in the package, so only what any
# desktop of Ubuntu 22.04 and later, or Debian 12 and later, has. Ubuntu
# 24.04 renamed some of them (t64): either name will do.
mkdir -p "$pkg/DEBIAN"
size=$(du -sk --exclude=DEBIAN "$pkg" | cut -f1)
cat >"$pkg/DEBIAN/control" <<EOF
Package: tuxape
Version: $version
Architecture: amd64
Maintainer: Thomas Bressel <contact.thomasbressel@gmail.com>
Installed-Size: $size
Section: games
Priority: optional
Depends: libc6 (>= 2.35), libstdc++6, libgcc-s1, libgl1, libegl1, libfontconfig1, libfreetype6,
 libdbus-1-3, libglib2.0-0t64 | libglib2.0-0, libx11-6, libx11-xcb1, libxcb1, libxcb-glx0,
 libxcb-randr0, libxcb-render0, libxcb-shm0, libxcb-sync1, libxcb-xfixes0, libxkbcommon0,
 libwayland-client0, libwayland-cursor0, libwayland-egl1, libpng16-16t64 | libpng16-16, zlib1g,
 libgssapi-krb5-2
Recommends: libgl1-mesa-dri, libpulse0 | libasound2t64 | libasound2, libgtk-3-0t64 | libgtk-3-0
Description: Amstrad CPC, Plus and GX4000 emulator
 TuxAPE emulates the Amstrad CPC 464, 664 and 6128, the 464 Plus, 6128 Plus
 and GX4000, modelled on WinAPE: discs, tapes, cartridges, a debugger and an
 assembler, a CTM644 monitor shader, expansion cards and plug-ins.
 .
 The user's discs, quick saves, profiles and Library are kept in ~/TuxAPE.
EOF

dpkg-deb --build --root-owner-group -Zxz "$pkg" "$out" >/dev/null
echo "$out: $(du -h "$out" | cut -f1)"
