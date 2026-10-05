#!/usr/bin/env bash
# Installs the Qt 6 development files into .deps/qt6 without root access.
#
# The normal way to get them is:  sudo apt install qt6-base-dev
# Use this script only when you cannot (or do not want to) install system
# packages. It downloads the -dev packages matching the Qt runtime already
# installed, unpacks them locally, and links them to the system libraries.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
prefix="$root/.deps/qt6"
debs="$root/.deps/debs"
arch_dir="usr/lib/$(dpkg-architecture -qDEB_HOST_MULTIARCH 2>/dev/null || echo x86_64-linux-gnu)"

packages=(
    qt6-base-dev qt6-base-dev-tools qmake6 qmake6-bin
    libqt6openglwidgets6 libqt6printsupport6 libqt6test6
    libqt6concurrent6 libqt6sql6
    libopengl-dev libvulkan-dev
)

mkdir -p "$prefix" "$debs"
(cd "$debs" && apt-get download "${packages[@]}")
for deb in "$debs"/*.deb; do
    dpkg -x "$deb" "$prefix"
done

# The -dev packages only hold "libFoo.so -> libFoo.so.6" links, and the CMake
# files name the fully versioned libraries; point both at the libraries the
# system already provides.
for lib in "/$arch_dir"/libQt6*.so.*; do
    name="$(basename "$lib")"
    [ -e "$prefix/$arch_dir/$name" ] || ln -sf "$lib" "$prefix/$arch_dir/$name"
done
find "$prefix/$arch_dir" -maxdepth 1 -type l | while read -r link; do
    [ -e "$link" ] && continue
    target="/$arch_dir/$(readlink "$link")"
    if [ -e "$target" ]; then
        ln -sf "$target" "$prefix/$arch_dir/$(readlink "$link")"
    fi
done

echo
echo "Qt 6 development files are in $prefix"
echo "Configure with:  cmake -B build -DCMAKE_PREFIX_PATH=$prefix/usr"
