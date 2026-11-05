#!/usr/bin/env bash
# Tries TuxAPE's Linux archive (tools/linux-release.sh) on a system it was
# not built on, in a Docker container of it: what any desktop has is put in
# (an X server, OpenGL, fonts, D-Bus, GTK 3 and what it needs), not Qt nor
# SDL; then TuxAPE starts under a virtual X server, its machine boots, and
# the window's picture is saved. The libraries it still lacks are listed.
#
#   tools/linux-try.sh ARCHIVE IMAGE PICTURE
#
# IMAGE: ubuntu:22.04, debian:12, ubuntu:24.04, ubuntu:26.04... (apt-get ones)
set -euo pipefail

if [ "${1:-}" != --inside ]; then
    [ $# -eq 3 ] || { echo "usage: $0 ARCHIVE IMAGE PICTURE" >&2; exit 2; }
    archive=$(realpath "$1")
    picture=$(realpath -m "$3")
    mkdir -p "$(dirname "$picture")"
    exec docker run --rm -v "$archive:/tmp/tuxape.tar.gz:ro" -v "$(dirname "$picture"):/out" \
        -v "$(realpath "$0"):/tmp/try.sh:ro" -e PICTURE="$(basename "$picture")" -e OWNER="$(id -u):$(id -g)" \
        "$2" bash /tmp/try.sh --inside
fi

export DEBIAN_FRONTEND=noninteractive
apt-get update -qq >/dev/null
apt-get install -y -qq --no-install-recommends xvfb xauth libgl1 libegl1 fonts-dejavu-core libdbus-1-3 \
    libgtk-3-0 >/dev/null 2>&1 || apt-get install -y -qq --no-install-recommends xvfb xauth libgl1 libegl1 \
    fonts-dejavu-core libdbus-1-3 libgtk-3-0t64 >/dev/null
. /etc/os-release
echo "== $PRETTY_NAME, glibc $(ldd --version | head -1 | awk '{print $NF}')"
mkdir -p /opt/try && tar -xzf /tmp/tuxape.tar.gz -C /opt/try
cd /opt/try/tuxape-*
missing=$(for f in tuxape qt/plugins/platforms/libqxcb.so qt/plugins/platforms/libqwayland.so; do ldd "$f"; done |
          grep "not found" | sort -u || true)
echo "missing libraries: ${missing:-none}"
# Started as a user would, from its folder, a picture taken after the boot.
HOME=/tmp/home xvfb-run -a -s "-screen 0 1280x1024x24" ./tuxape --grab "/out/$PICTURE" --grab-after 6000 \
    >/tmp/run.log 2>&1 || { echo "TuxAPE failed:"; tail -20 /tmp/run.log; exit 1; }
chown "$OWNER" "/out/$PICTURE"
echo "started, picture in $PICTURE"
grep -v -i "propagateSizeHints\|xdg_runtime\|XDG_RUNTIME" /tmp/run.log | head -8 || true
