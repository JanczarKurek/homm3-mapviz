# Build-test image for vcmiwallpaper on a generic Linux (Debian 12 "bookworm").
#
# Purpose: prove the tool compiles from a clean distro with only apt packages.
# It does NOT render anything by itself -- rendering needs your own (copyrighted)
# Heroes III data mounted at runtime. See README.md "Run".
#
# Build context must contain the populated `vcmi/` submodule (COPY . below),
# so no network access or pushed fork is required just to test the build:
#     git submodule update --init          # populate vcmi/ first
#     docker build -t vcmi-map-wallpaper .
FROM debian:12

# VCMI (client) build deps + ffmpeg (the CLI binary the tool shells out to for
# encoding webp/gif/mp4/webm). Qt is intentionally omitted -- launcher/editor
# are disabled below.
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        ninja-build \
        git \
        ca-certificates \
        pkg-config \
        libsdl2-dev \
        libsdl2-image-dev \
        libsdl2-mixer-dev \
        libsdl2-ttf-dev \
        libboost-all-dev \
        zlib1g-dev \
        libminizip-dev \
        libsquish-dev \
        libtbb-dev \
        libluajit-5.1-dev \
        libavformat-dev \
        libavcodec-dev \
        libavutil-dev \
        libswscale-dev \
        libswresample-dev \
        ffmpeg \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . /src

# Only VCMI's client lib and the wallpaper tool are needed. Launcher/editor
# (Qt), tests, translations, Discord and the ML AI are all disabled, so VCMI's
# own nested submodules (googletest / innoextract / dependencies / discord-presence)
# are not required -- no `--recursive` submodule init needed.
RUN cmake -S vcmi -B vcmi/build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DENABLE_LAUNCHER=OFF \
        -DENABLE_EDITOR=OFF \
        -DENABLE_TRANSLATIONS=OFF \
        -DENABLE_DISCORD=OFF \
        -DENABLE_TEST=OFF \
        -DENABLE_MMAI=OFF \
    && ninja -C vcmi/build vcmiwallpaper \
    && test -x vcmi/build/bin/vcmiwallpaper \
    && echo "OK: vcmiwallpaper built at vcmi/build/bin/vcmiwallpaper"

# The built binary lives at /src/vcmi/build/bin/vcmiwallpaper.
# To actually render, run the container with your H3 data mounted next to it,
# e.g. as /src/vcmi/build/bin/Data and /src/vcmi/build/bin/Maps (see README).
CMD ["vcmi/build/bin/vcmiwallpaper", "--help"]
