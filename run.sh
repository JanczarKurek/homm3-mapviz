#!/usr/bin/env bash
#
# run.sh - render a Heroes III map as an animated wallpaper using the Docker
# image, with YOUR Heroes III data mounted in. This is the easy "just make me a
# wallpaper" entry point; it wraps `docker run ... vcmiwallpaper ...`.
#
# Example:
#   ./run.sh --data "/path/to/HoMM 3" --map "/path/to/HoMM 3/Maps/Adventure.h3m" \
#            --out out/adventure.webp -- --frames 48 --resolution 1920x1080
#
# Everything after `--` is passed straight to vcmiwallpaper (see its --help:
#   docker run --rm vcmi-map-wallpaper vcmi/build/bin/vcmiwallpaper --help
# for --walk / --pan-to / --scale / --follow / etc.).
#
set -euo pipefail

IMAGE="${IMAGE:-vcmi-map-wallpaper}"
DATA=""
MAP=""
OUT="wallpaper.webp"
EXTRA=()

usage() {
    cat <<'EOF'
Usage: ./run.sh --data <H3_DIR> --map <MAP_FILE> [--out <FILE>] [--image <TAG>] [-- <vcmiwallpaper args>]

  --data  <dir>   Heroes III install directory containing a Data/ subdir
                  (and usually Maps/). Your own game data - never bundled here.
  --map   <file>  Path to the map to render (.h3m / .vmap). May live anywhere.
  --out   <file>  Output path on the host (default: wallpaper.webp). The format
                  is chosen by extension: .webp .gif .mp4 .webm
  --image <tag>   Docker image to use (default: vcmi-map-wallpaper; built on
                  demand if missing).
  --              Everything after this is forwarded verbatim to vcmiwallpaper.

Examples:
  ./run.sh --data "$HOME/HoMM3" --map "$HOME/HoMM3/Maps/Dragons.h3m"
  ./run.sh --data "$HOME/HoMM3" --map maps/Isle.h3m --out out/isle.mp4 -- \
           --resolution 1920x1080 --frames 48 --scale 2
  ./run.sh --data "$HOME/HoMM3" --map maps/Big.h3m --out out/tour.mp4 -- --walk
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        --data)  DATA="$2"; shift 2 ;;
        --map)   MAP="$2"; shift 2 ;;
        --out|-o) OUT="$2"; shift 2 ;;
        --image) IMAGE="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        --) shift; EXTRA=("$@"); break ;;
        *) echo "error: unknown argument '$1'" >&2; echo >&2; usage >&2; exit 1 ;;
    esac
done

# --- validate -------------------------------------------------------------
[ -n "$DATA" ] || { echo "error: --data <H3_DIR> is required" >&2; exit 1; }
[ -n "$MAP" ]  || { echo "error: --map <MAP_FILE> is required" >&2; exit 1; }
[ -d "$DATA" ] || { echo "error: data dir not found: $DATA" >&2; exit 1; }
if [ ! -d "$DATA/Data" ]; then
    echo "error: '$DATA' has no Data/ subdir - point --data at your Heroes III" >&2
    echo "       install (the folder containing Data/, Maps/, Mp3/)." >&2
    exit 1
fi
[ -f "$MAP" ] || { echo "error: map file not found: $MAP" >&2; exit 1; }

command -v docker >/dev/null 2>&1 || { echo "error: docker not found on PATH" >&2; exit 1; }

# --- resolve absolute paths ----------------------------------------------
DATA="$(cd "$DATA" && pwd)"
MAP_DIR="$(cd "$(dirname "$MAP")" && pwd)"
MAP_BASE="$(basename "$MAP")"
OUT_DIR="$(dirname "$OUT")"; mkdir -p "$OUT_DIR"; OUT_DIR="$(cd "$OUT_DIR" && pwd)"
OUT_BASE="$(basename "$OUT")"

# --- build the image if it's missing -------------------------------------
if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
    echo ">> Image '$IMAGE' not found - building it (one-time, slow)..."
    docker build -t "$IMAGE" "$(cd "$(dirname "$0")" && pwd)"
fi

# --- mounts ---------------------------------------------------------------
# H3 Data (required) and Maps (optional) go next to the binary so VCMI's
# development mode finds them; the map's own dir is mounted at /maps; the output
# dir is mounted writable (intermediate PNG frames also land there).
BIN=/src/vcmi/build/bin
MOUNTS=(
    -v "$DATA/Data":"$BIN/Data":ro
    -v "$MAP_DIR":/maps:ro
    -v "$OUT_DIR":/out
)
[ -d "$DATA/Maps" ] && MOUNTS+=( -v "$DATA/Maps":"$BIN/Maps":ro )
[ -d "$DATA/Mp3" ]  && MOUNTS+=( -v "$DATA/Mp3":"$BIN/Mp3":ro )

echo ">> Rendering '$MAP_BASE' -> $OUT_DIR/$OUT_BASE"
docker run --rm \
    --user "$(id -u):$(id -g)" \
    -e HOME=/tmp \
    "${MOUNTS[@]}" \
    "$IMAGE" \
    vcmi/build/bin/vcmiwallpaper --map "/maps/$MAP_BASE" --out "/out/$OUT_BASE" "${EXTRA[@]}"

echo ">> Done: $OUT_DIR/$OUT_BASE"
