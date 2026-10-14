#!/usr/bin/env bash
# Makes dist/: TuxAPE ready to run, with its folders beside it.
#
#   dist/tuxape      the program, built for it in build-dist/
#   dist/lib/        the Qt libraries it was built with that come from .deps/
#   dist/ROM/        WinAPE's ROM images, dist/Profile/ its profiles and yours
#   dist/roms/       your own ROMs
#   dist/plugins/    the plugins installed
#   dist/library/    the Library window's programs (hard links: no room taken)
#   dist/assembly/   assembler sources
#   dist/dsk/        disc images
#   dist/img/        what the cards keep: the X-MASS's disc, the X-MEM's flash
#
# Run again, it brings what is newer in the project's folders: a file
# newer in dist/ is left as it is, and nothing in dist/ is deleted.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
dist="$root/dist"
build="$root/build-dist"

cmake -S "$root" -B "$build" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$root/.deps/qt6/usr" \
    -DTUXAPE_BUILD_TESTS=OFF \
    -DCMAKE_EXE_LINKER_FLAGS=-s \
    -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON \
    -DCMAKE_INSTALL_RPATH='$ORIGIN/lib' >/dev/null
cmake --build "$build" -j"$(nproc)" --target tuxape

mkdir -p "$dist/lib" "$dist/dsk" "$dist/img"
cp -f "$build/src/app/tuxape" "$dist/tuxape"

# The libraries found in .deps/ go with it; those of the system stay there.
deps="$root/.deps/qt6/usr/lib/x86_64-linux-gnu"
LD_LIBRARY_PATH="$deps" ldd "$dist/tuxape" | sed -n 's|.*=> \(.*/\.deps/.*\) (0x.*|\1|p' | while read -r library; do
    cp -fL "$library" "$dist/lib/"
done
if ldd "$dist/tuxape" | grep -q "not found"; then
    echo "Missing libraries:" >&2
    ldd "$dist/tuxape" | grep "not found" >&2
fi

copy() {  # copy FROM TO: FROM's content into TO, nothing removed from TO
    [ -d "$1" ] && rsync -a --update "$1/" "$2/"
    return 0
}
copy "$root/WinAPE20B2/ROM" "$dist/ROM"
copy "$root/WinAPE20B2/Profile" "$dist/Profile"
copy "$root/roms" "$dist/roms"
copy "$root/plugins" "$dist/plugins"
copy "$root/assembly" "$dist/assembly"
# The Library is large: its files are hard links to the project's.
[ -d "$root/library" ] && rsync -a --update --link-dest="$root/library" "$root/library/" "$dist/library/"
# The disc images lying at the project's root.
find "$root" -maxdepth 1 -type f \( -iname '*.dsk' -o -iname '*.hfe' -o -iname '*.ipf' \) \
    -exec rsync -a --update {} "$dist/dsk/" \;

echo "TuxAPE is in $dist: start it with $dist/tuxape"
