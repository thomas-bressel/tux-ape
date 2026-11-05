# The system TuxAPE's Linux archive is built on (tools/linux-release.sh):
# Ubuntu 22.04, so that the program asks no more of the C library and of
# the C++ one (glibc 2.35, GCC 12's) than the systems of that time have:
# Ubuntu 22.04 and later, Linux Mint 21 and later, Debian 12 and later, and
# the others of 2022 on. Qt 6.10 comes from Qt's own builds, which ask less
# still; SDL2, sound and game controllers only, is built here.
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates git cmake make ninja-build g++-12 pkg-config file rsync python3-venv \
        zlib1g-dev libgl-dev libegl-dev libxkbcommon-dev libxkbcommon-x11-0 libfontconfig1 libfreetype6 \
        libdbus-1-3 libglib2.0-0 libxcb-cursor0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 \
        libxcb-render-util0 libxcb-shape0 libxcb-xinerama0 libxcb-xkb1 libxcb-randr0 libwayland-client0 \
        libwayland-cursor0 libwayland-egl1 \
        libpipewire-0.3-dev libpulse-dev libasound2-dev libudev-dev \
    && rm -rf /var/lib/apt/lists/*
ENV CC=gcc-12 CXX=g++-12

# Qt 6.10, its latest patch, by aqtinstall.
RUN python3 -m venv /opt/aqt && /opt/aqt/bin/pip install --quiet aqtinstall \
    && version=$(/opt/aqt/bin/aqt list-qt linux desktop --spec 6.10 --latest-version) \
    && /opt/aqt/bin/aqt install-qt linux desktop "$version" linux_gcc_64 --outputdir /opt/qt \
    && ln -s "/opt/qt/$version/gcc_64" /opt/qt/current

COPY build-sdl2.sh /opt/build-sdl2.sh
RUN bash /opt/build-sdl2.sh /opt/sdl2
