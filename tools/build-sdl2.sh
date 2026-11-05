#!/usr/bin/env bash
# Builds the SDL2 that TuxAPE's Linux packages are linked with, into PREFIX:
# a static library of sound and game controllers only. TuxAPE draws with
# Qt, so SDL's video is left out, and with it the X11, Wayland and decor
# libraries a distribution's SDL2 needs. The sound systems (PipeWire,
# PulseAudio, ALSA) and udev are opened when TuxAPE starts, if the
# machine has them: none of them is needed to run.
#
#   tools/build-sdl2.sh PREFIX
#
# Needs git, cmake, and the headers of those sound systems and of udev
# (libpipewire-0.3-dev, libpulse-dev, libasound2-dev, libudev-dev).
set -euo pipefail

prefix=$1
version=release-2.32.10
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

git clone --quiet --depth 1 --branch "$version" https://github.com/libsdl-org/SDL.git "$work/SDL"
cmake -S "$work/SDL" -B "$work/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_STATIC_PIC=ON -DSDL_TEST=OFF \
    -DSDL_VIDEO=OFF -DSDL_RENDER=OFF -DSDL_X11=OFF -DSDL_WAYLAND=OFF -DSDL_KMSDRM=OFF \
    -DSDL_OPENGL=OFF -DSDL_OPENGLES=OFF -DSDL_VULKAN=OFF -DSDL_LIBSAMPLERATE=OFF \
    -DSDL_JACK=OFF -DSDL_SNDIO=OFF -DSDL_ESD=OFF -DSDL_ARTS=OFF -DSDL_NAS=OFF \
    -DSDL_PIPEWIRE_SHARED=ON -DSDL_PULSEAUDIO_SHARED=ON -DSDL_ALSA_SHARED=ON >/dev/null
cmake --build "$work/build" --parallel >/dev/null
cmake --install "$work/build" >/dev/null
echo "SDL2 ($version, sound and game controllers) is in $prefix"
