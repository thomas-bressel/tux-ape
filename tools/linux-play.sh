#!/usr/bin/env bash
# Starts TuxAPE's Linux archive in a Docker container of another system, on
# this machine's screen, with its sound and its graphics card: to try it by
# hand as someone who has just downloaded it would. Nothing of this
# machine's TuxAPE is seen: the container has its own home, empty at each
# start, so its own settings; the archive is unpacked afresh each time.
#
#   tools/linux-play.sh [IMAGE] [ARCHIVE] [-- TUXAPE ARGUMENTS]
#
# IMAGE: ubuntu:22.04 (the default), ubuntu:24.04, debian:12, fedora:42...
# ARCHIVE: the newest of release/ by default.
# TUXAPE_PLAY_HOME=folder keeps the container's home there, from one start
# to the next (settings, quick saves).
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)

if [ "${1:-}" = --inside ]; then
    # ---- In the container, as root ----
    mkdir -p /opt/tuxape
    tar -xzf /tmp/tuxape.tar.gz -C /opt/tuxape --strip-components=1
    chown -R "$OWNER" /opt/tuxape
    mkdir -p /home/player && chown "$OWNER" /home/player
    cd /opt/tuxape
    shift
    exec setpriv --reuid="${OWNER%:*}" --regid="${OWNER#*:}" --keep-groups \
        env HOME=/home/player XDG_RUNTIME_DIR=/tmp ./tuxape "$@"
fi

image=ubuntu:22.04
archive=
while [ $# -gt 0 ] && [ "$1" != -- ]; do
    case "$1" in
        *.tar.gz) archive=$1 ;;
        *) image=$1 ;;
    esac
    shift
done
[ "${1:-}" = -- ] && shift
if [ -z "$archive" ]; then
    archive=$(ls -t "$root"/release/tuxape-*-linux-*.tar.gz 2>/dev/null | head -1 || true)
    [ -n "$archive" ] || { echo "no archive in release/: tools/linux-release.sh makes one" >&2; exit 1; }
fi
archive=$(realpath "$archive")

# What a desktop has, put in once for each system: an image of its own.
tag="tuxape-play:${image//[:\/]/-}"
if ! docker image inspect "$tag" >/dev/null 2>&1; then
    echo "Preparing $image (once)..."
    docker build --quiet -t "$tag" - >/dev/null <<EOF
FROM $image
RUN if command -v apt-get >/dev/null; then \
        export DEBIAN_FRONTEND=noninteractive && apt-get update -qq && \
        (apt-get install -y -qq --no-install-recommends libgl1 libegl1 libgl1-mesa-dri fonts-dejavu-core \
            libdbus-1-3 libgtk-3-0 libpulse0 libasound2 util-linux 2>/dev/null || \
         apt-get install -y -qq --no-install-recommends libgl1 libegl1 libgl1-mesa-dri fonts-dejavu-core \
            libdbus-1-3 libgtk-3-0t64 libpulse0 libasound2t64 util-linux) && rm -rf /var/lib/apt/lists/*; \
    else \
        dnf install -y -q --setopt=install_weak_deps=False tar gzip util-linux mesa-libGL mesa-libEGL \
            mesa-dri-drivers dejavu-sans-fonts dbus-libs gtk3 pulseaudio-libs alsa-lib && dnf clean all; \
    fi
EOF
fi

# The screen (X11, through XWayland on a Wayland desktop), the sound
# (PulseAudio's socket, which PipeWire offers too) and the graphics card.
run=(-v /tmp/.X11-unix:/tmp/.X11-unix:ro -e DISPLAY="${DISPLAY:-:0}" -e QT_QPA_PLATFORM=xcb)
if [ -n "${XAUTHORITY:-}" ] && [ -f "$XAUTHORITY" ]; then
    run+=(-v "$XAUTHORITY:/tmp/.Xauthority:ro" -e XAUTHORITY=/tmp/.Xauthority)
fi
pulse="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/pulse/native"
[ -S "$pulse" ] && run+=(-v "$pulse:/tmp/pulse-native" -e PULSE_SERVER=unix:/tmp/pulse-native)
if [ -d /dev/dri ]; then
    run+=(--device /dev/dri)
    for group in video render; do
        gid=$(getent group "$group" | cut -d: -f3 || true)
        [ -n "$gid" ] && run+=(--group-add "$gid")
    done
fi
if [ -n "${TUXAPE_PLAY_HOME:-}" ]; then
    mkdir -p "$TUXAPE_PLAY_HOME"
    run+=(-v "$(realpath "$TUXAPE_PLAY_HOME"):/home/player")
fi

echo "TuxAPE $(basename "$archive") on $image"
exec docker run --rm --init -v "$archive:/tmp/tuxape.tar.gz:ro" -v "$root/tools/linux-play.sh:/tmp/play.sh:ro" \
    -e OWNER="$(id -u):$(id -g)" -e LANG=C.UTF-8 "${run[@]}" "$tag" bash /tmp/play.sh --inside "$@"
