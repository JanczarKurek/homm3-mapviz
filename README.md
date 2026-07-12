# vcmi-map-wallpaper

Render a region of a **Heroes III of Might and Magic** map as a seamlessly
looping **animated wallpaper**, using [VCMI](https://vcmi.eu/)'s real in-game adventure-map renderer.

> **Note:** This does **not** ship any Heroes III data. You must own a copy of
> the game and provide its data files at runtime (see [Run](#run)).

---

## How this repo is put together

The tool itself lives in [`src/`](src) and is an ordinary CMake project that
builds one executable, `vcmiwallpaper`, on top of VCMI's client library.

VCMI comes in as the [`vcmi/`](https://github.com/JanczarKurek/vcmi/tree/wallpaper)
submodule. That's a fork, but a deliberately thin one: it carries two patches and
nothing else.

1. VCMI uses `CMAKE_SOURCE_DIR` in places where it means `PROJECT_SOURCE_DIR`,
   which is harmless while VCMI is the top-level project and breaks the moment it
   is embedded in another one. Fixing that is what lets this repo do
   `add_subdirectory(vcmi)` at all.
2. The adventure-map renderer reaches through the player interface for a handful
   of values that are also available from the loaded map. Sourcing them from the
   map instead lets the renderer run with a map loaded but no game started.

Both are upstreamable, so the fork is meant to be temporary. Nothing
wallpaper-specific was added to VCMI.

## Build & deps

Mostly VCMI's own dependencies, plus `ffmpeg` to encode the video.

### Native (Linux)

Install VCMI's client build dependencies (Debian/Ubuntu example):

```sh
sudo apt-get install build-essential cmake ninja-build git pkg-config \
    libsdl2-dev libsdl2-image-dev libsdl2-mixer-dev libsdl2-ttf-dev \
    libboost-all-dev zlib1g-dev libminizip-dev libsquish-dev libtbb-dev libluajit-5.1-dev \
    libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libswresample-dev ffmpeg
```

Then:

```sh
git clone --recursive <this-repo-url> vcmi-map-wallpaper
cd vcmi-map-wallpaper
./build.sh
```

The binary is produced at `build/bin/vcmiwallpaper`.

> The `ffmpeg` **CLI** is a runtime dependency — the tool renders PNG frames and
> shells out to it to encode them. If it isn't on `PATH`, pass `--ffmpeg /path/to/ffmpeg`.

### Docker

There is a docker container you can use to build this if you are running a funny distro.

```sh
git submodule update --init          # populate vcmi/ (already there after --recursive clone)
docker build -t vcmi-map-wallpaper .
```

### Windows

Every push builds `vcmiwallpaper.exe` — grab the `vcmiwallpaper-windows-x64`
artifact from the [Actions tab](../../actions/workflows/windows.yml). It ships
with the DLLs it needs, but **not** `ffmpeg.exe`; install that separately
(`winget install ffmpeg`) or point `--ffmpeg` at it.

To build it yourself you need the same setup VCMI uses — MSVC plus its prebuilt
Conan dependencies. [`.github/workflows/windows.yml`](.github/workflows/windows.yml)
is the executable version of those instructions; the short form is:

```sh
pipx install conan
source vcmi/CI/install_conan_dependencies.sh dependencies-windows-x64
conan install vcmi --output-folder=vcmi/conan-generated --build=never \
    --profile=vcmi/dependencies/conan_profiles/msvc-x64 \
    --conf="tools.cmake.cmaketoolchain:generator=Ninja" \
    -s "&:build_type=RelWithDebInfo" -o "&:target_pre_windows10=True"
cmake --preset windows-msvc-ninja-release
cmake --build --preset windows-msvc-ninja-release
```

Note this needs `git submodule update --init --recursive`: the Conan profiles
live in VCMI's own `dependencies` submodule.

## Run

You supply your own Heroes III data (the folder containing `Data/`, `Maps/`,
`Mp3/`). The tool resolves game data from the binary's own directory. On Linux
that means VCMI's **development mode**, which `build.sh` and the Dockerfile
enable for you by dropping a `vcmiclient` symlink next to the binary; on Windows
it's the default and needs no setup.

### Easiest: `run.sh` (Docker)

One command — mounts your data, renders, drops the file on your host. Builds the
image on first use:

```sh
./run.sh --data "/path/to/HoMM 3" --map "/path/to/HoMM 3/Maps/SomeMap.h3m" \
         --out out/wallpaper.webp -- --frames 48 --resolution 1920x1080
```

Everything after `--` is passed straight to `vcmiwallpaper`. `./run.sh --help`
explains the wrapper flags. A few recipes:

```sh
# quick default webp of a map
./run.sh --data ~/HoMM3 --map ~/HoMM3/Maps/Dragons.h3m

# crisp 1080p mp4, longer loop, 2x zoom
./run.sh --data ~/HoMM3 --map ~/HoMM3/Maps/Isle.h3m --out out/isle.mp4 -- \
         --resolution 1920x1080 --frames 48 --scale 2

# hero walks the level collecting resources (one-shot video)
./run.sh --data ~/HoMM3 --map ~/HoMM3/Maps/Big.h3m --out out/tour.mp4 -- --walk
```

### Native

Symlink your data next to the binary, then run it directly:

```sh
cd build/bin
ln -s "/path/to/HoMM 3/Data"  Data
ln -s "/path/to/HoMM 3/Maps"  Maps
ln -s "/path/to/HoMM 3/Mp3"   Mp3    # optional (music, not needed for rendering)
./vcmiwallpaper --map "Maps/SomeMap.h3m" --frames 24 --out wallpaper.webp
```

On Windows it's the same idea without the symlinks: unzip the artifact, put
`Data/` (and `Maps/`) next to `vcmiwallpaper.exe`, and run it from that folder.

`--help` lists every option. Highlights (full details in
[`docs/vcmiwallpaper.md`](docs/vcmiwallpaper.md)):

- `--x/--y/--width/--height` or `--resolution 1920x1080` — pick the region / exact output size
- `--scale N` — crisp integer nearest-neighbour upscale
- `--pan-to X,Y` — slow seamless ping-pong pan across the loop
- `--walk` — a hero tours the level collecting every reachable resource pile (one-shot video)
- `--out` extension picks the format: `.webp` (animated, wallpaper-daemon friendly), `.gif`, `.mp4`, `.webm`

> Keep the output path ASCII on Windows. VCMI narrows paths through the active
> code page while SDL expects UTF-8, so accented characters in the output path
> (a `Pulpit`/Desktop under a name like `Michał`) can land the file somewhere
> unexpected.

### Modded maps (HotA etc.)

A map may require a VCMI mod. Install the mod under `build/bin/Mods/<modid>/`
(e.g. Horn of the Abyss into `.../Mods/hota/`) and enable it in your VCMI
`modSettings.json`. Mods are large and are **not** included here.

## License

GPL-2.0-or-later, matching VCMI. See [LICENSE](LICENSE). Heroes III game data is
not included and is not covered by this license.
