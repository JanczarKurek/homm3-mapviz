#!/usr/bin/env bash
# Configure and build the vcmiwallpaper tool natively.
# Requires a C++ toolchain + VCMI's client build deps on PATH (see README / Dockerfile).
set -euo pipefail

cd "$(dirname "$0")"

# Populate the VCMI fork submodule if it isn't already.
if [ ! -f vcmi/CMakeLists.txt ]; then
    git submodule update --init vcmi
fi

BUILD_TYPE="${BUILD_TYPE:-Release}"

# Prefer Ninja if available, else fall back to Make.
if command -v ninja >/dev/null 2>&1; then
    GEN=(-G Ninja)
    BUILD=(ninja -C vcmi/build vcmiwallpaper)
else
    GEN=()
    BUILD=(cmake --build vcmi/build --target vcmiwallpaper -j"$(nproc)")
fi

cmake -S vcmi -B vcmi/build "${GEN[@]}" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DENABLE_LAUNCHER=OFF \
    -DENABLE_EDITOR=OFF \
    -DENABLE_TRANSLATIONS=OFF \
    -DENABLE_DISCORD=OFF \
    -DENABLE_TEST=OFF \
    -DENABLE_MMAI=OFF

"${BUILD[@]}"

# Enable VCMI development mode (data resolved from the binary's own directory):
# dev mode needs config/ + Mods/ (produced by the build) and a binary named
# `vcmiclient` present -- a symlink to vcmiwallpaper satisfies the existence check.
ln -sf vcmiwallpaper vcmi/build/bin/vcmiclient

echo
echo "Built: $(pwd)/vcmi/build/bin/vcmiwallpaper"
echo "Run 'vcmi/build/bin/vcmiwallpaper --help' for options (needs H3 data at runtime; see README)."
