#!/usr/bin/env bash
# Makes TuxAPE's archive for Linux on x86-64, the one given out: built in a
# Docker container of Ubuntu 22.04 (tools/linux-build.Dockerfile), so that
# it runs on the systems of 2022 and later, with Qt in it and SDL2 in the
# program. The tests run there first; the folder is then made by
# tools/package.cmake, with what the project's dist/ folder holds: its ROMs,
# profiles and Library programs, as they are there.
#
#   tools/linux-release.sh OUT
#
# leaves OUT/tuxape-<version>-linux-x86_64.tar.gz, and the folder it holds.
# Needs Docker, usable without sudo.
set -euo pipefail

image=tuxape-linux-build:22.04

src=/usr/local/share/tuxape
qt=/opt/qt/current

if [ "${1:-}" != --inside ] && [ "${1:-}" != --build ]; then
    [ $# -eq 1 ] || { echo "usage: $0 OUT" >&2; exit 2; }
    root=$(cd "$(dirname "$0")/.." && pwd)
    out=$(realpath -m "$1")
    mkdir -p "$out"
    docker build --quiet -t "$image" -f "$root/tools/linux-build.Dockerfile" "$root/tools" >/dev/null
    # TUXAPE_BUILD_CACHE, a folder, keeps the build from one run to the next.
    cache=()
    [ -n "${TUXAPE_BUILD_CACHE:-}" ] && mkdir -p "$TUXAPE_BUILD_CACHE" && cache=(-v "$(realpath "$TUXAPE_BUILD_CACHE"):/tmp/build")
    exec docker run --rm -v "$root:/mnt/project:ro" -v "$out:/out" "${cache[@]}" \
        -e OWNER="$(id -u):$(id -g)" "$image" bash /mnt/project/tools/linux-release.sh --inside
fi

# ---- In the container, as root ----
# The sources at a place of their own: what the program keeps of the
# folder it was built in (its default ROM folder, for one) names nobody's
# home. Only what the build and the archive need: not the Library's
# programs, not the references.
if [ "$1" = --inside ]; then
mkdir -p "$src"
rsync -a --exclude build --exclude '*.user' /mnt/project/{CMakeLists.txt,CMakePresets.json,README.md,PLUGINS.md,sdk,src,tests,tools,profile} "$src/"
# What the archive holds besides the program is dist/'s, as the user has
# put it there: the ROMs, the profiles, the Library with its programs, the
# plug-ins' README.
[ -d /mnt/project/dist/roms ] && [ -d /mnt/project/dist/profile ] && [ -d /mnt/project/dist/library ] \
    || { echo "dist/ of the project lacks roms, profile or library" >&2; exit 1; }
rsync -a /mnt/project/dist/{roms,library} "$src/"
mkdir -p "$src/plugins" && cp /mnt/project/dist/plugins/README.md "$src/plugins/" 2>/dev/null || true

# The rest as the user who asked: as root, a file made read-only can be
# written all the same, and the tests of write-protected discs would fail.
mkdir -p /tmp/build /tmp/home
chown -R "$OWNER" "$src" /tmp/build /tmp/home
exec setpriv --reuid="${OWNER%:*}" --regid="${OWNER#*:}" --clear-groups env HOME=/tmp/home bash "$0" --build
fi

# ---- As the user ----
cmake -S "$src" -B /tmp/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DTUXAPE_UPDATE_DIST=OFF \
    -DCMAKE_PREFIX_PATH="$qt" -DSDL2_DIR=/opt/sdl2/lib/cmake/SDL2 >/tmp/configure.log
cmake --build /tmp/build --parallel >/tmp/build.log || { grep -A25 -m1 "FAILED:" /tmp/build.log | grep -v "warning" | head -60; exit 1; }
# All the tests, but the one that times the emulation, run alone after.
# Four at a time, as GitHub's builds: a few of the windows' tests compare
# what the running machine shows at two moments, and on a loaded machine a
# cursor's blink may come between; one that fails is run again, twice at
# most (a fault fails each time).
(cd /tmp/build && ctest --timeout 600 --parallel 4 --repeat until-pass:3 -E core_speed >/tmp/test.log 2>&1 &&
    ctest --timeout 600 -R core_speed >>/tmp/test.log 2>&1) \
    || { grep -E "Failed|\*\*\*" /tmp/test.log; (cd /tmp/build && ctest --timeout 600 --output-on-failure -R "^($(sed -nE 's/.*Test +#[0-9]+: ([a-z_0-9]+) \.+\*\*\*Failed.*/\1/p' /tmp/test.log | sort -u | paste -sd'|'))\$" >/out/failed-tests.log 2>&1); echo "the failed tests' output is in failed-tests.log"; exit 1; }
grep -E "tests passed" /tmp/test.log

# The tests know the project's profiles; the archive has dist/'s. One saved
# on the machine that made the archive may name the files of its cards
# there: they go back to the program's own folder.
rsync -a --delete /mnt/project/dist/profile/ "$src/profile/"
sed -i -E '/^(X-MEM Flash|X-MASS Disc)=/d' "$src"/profile/*.tpf

version=$(sed -n 's/^ *VERSION \([0-9.]*\)$/\1/p' "$src/CMakeLists.txt" | head -1)
name="tuxape-$version-linux-x86_64"
rm -rf "/out/$name" "/out/$name.tar.gz"
cmake -DPROGRAM=/tmp/build/src/app/tuxape -DQT="$qt" -DROOT="$src" -DOUT="/out/$name" -DSTRIP=strip -DALL_LIBRARY=ON \
    -P "$src/tools/package.cmake"
tar -C /out --owner=0 --group=0 --numeric-owner -czf "/out/$name.tar.gz" "$name"
echo "/out/$name.tar.gz: $(du -h "/out/$name.tar.gz" | cut -f1)"
