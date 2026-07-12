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
else
    GEN=()
fi

# VCMI is embedded as a subproject by our top-level CMakeLists.txt, which is also
# where the parts of VCMI we don't need (launcher, editor, tests, ...) get turned off.
cmake -S . -B build "${GEN[@]}" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
cmake --build build --target vcmiwallpaper -j"$(nproc)"

# Enable VCMI development mode (data resolved from the binary's own directory).
# On Linux dev mode needs config/ + Mods/ (produced by the build) and a binary named
# `vcmiclient` to be present -- it only checks the name exists, so a symlink to
# vcmiwallpaper is enough. Windows needs none of this: it always resolves data
# relative to the binary.
ln -sf vcmiwallpaper build/bin/vcmiclient

echo
echo "Built: $(pwd)/build/bin/vcmiwallpaper"
echo "Run 'build/bin/vcmiwallpaper --help' for options (needs H3 data at runtime; see README)."
