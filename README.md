# vcmi-map-wallpaper

Render a region of a **Heroes III of Might and Magic** map as a seamlessly
looping **animated wallpaper** (or a hero-walk video), using
[VCMI](https://vcmi.eu/)'s real in-game adventure-map renderer — so creatures,
flags, water and terrain animate exactly as they do in the game.

This repo is a thin packaging layer around the `vcmiwallpaper` tool. VCMI itself
is pinned as a git submodule (a small fork carrying the tool + a few
behavior-preserving renderer guards); this repo owns the build script,
`Dockerfile`, and docs.

> **Note:** This does **not** ship any Heroes III data. You must own a copy of
> the game and provide its data files at runtime (see [Run](#run)).

---

## Repository layout

```
vcmi-map-wallpaper/
├── vcmi/            # git submodule: VCMI fork (branch `wallpaper`) with the tool in vcmi/wallpaperapp/
├── Dockerfile       # reproducible Debian 12 build test
├── build.sh         # native build convenience wrapper
├── LICENSE          # GPL-2.0-or-later (matches VCMI)
└── README.md
```

## Build

### Native (Linux)

Install VCMI's client build dependencies (Debian/Ubuntu names):

```sh
sudo apt-get install build-essential cmake ninja-build git pkg-config \
    libsdl2-dev libsdl2-image-dev libsdl2-mixer-dev libsdl2-ttf-dev \
    libboost-all-dev zlib1g-dev libtbb-dev libluajit-5.1-dev \
    libavformat-dev libavcodec-dev libavutil-dev libswscale-dev ffmpeg
```

Then:

```sh
git clone --recursive <this-repo-url> vcmi-map-wallpaper
cd vcmi-map-wallpaper
./build.sh
```

The binary is produced at `vcmi/build/bin/vcmiwallpaper`.

> `ffmpeg` (the CLI) must be on `PATH` at **run** time — the tool shells out to
> it to encode the animation.

### Docker (generic-Linux build test)

Proves the tool compiles on a clean distro with nothing but apt packages:

```sh
git submodule update --init          # populate vcmi/ (already there after --recursive clone)
docker build -t vcmi-map-wallpaper .
```

A successful build ends with `OK: vcmiwallpaper built ...`. The image is a
build-verification image; to render inside it you'd mount your H3 data into the
container next to the binary (see below).

## Run

VCMI runs the tool in **development mode**, so it looks for game data next to
the binary. Provide your own Heroes III data by placing (or symlinking) it into
`vcmi/build/bin/`:

```sh
cd vcmi/build/bin
ln -s "/path/to/HoMM 3/Data"  Data
ln -s "/path/to/HoMM 3/Maps"  Maps
ln -s "/path/to/HoMM 3/Mp3"   Mp3    # optional (music, not needed for rendering)
```

Then render a map:

```sh
./vcmiwallpaper --map "Maps/SomeMap.h3m" --frames 24 --out wallpaper.webp
```

`--help` lists every option. Highlights (full details in
[`vcmi/wallpaperapp/README.md`](vcmi/wallpaperapp/README.md)):

- `--x/--y/--width/--height` or `--resolution 1920x1080` — pick the region / exact output size
- `--scale N` — crisp integer nearest-neighbour upscale
- `--pan-to X,Y` — slow seamless ping-pong pan across the loop
- `--walk` — a hero tours the level collecting every reachable resource pile (one-shot video)
- `--out` extension picks the format: `.webp` (animated, wallpaper-daemon friendly), `.gif`, `.mp4`, `.webm`

### Modded maps (HotA etc.)

A map may require a VCMI mod. Install the mod under `vcmi/build/bin/Mods/<modid>/`
(e.g. Horn of the Abyss into `.../Mods/hota/`) and enable it in your VCMI
`modSettings.json`. Mods are large and are **not** included here.

## Sharing this repo

The submodule currently points at the fork by a **local path** so it builds on
the machine it was created on. Before others can clone it:

1. Push the VCMI fork branch `wallpaper` to a public remote, e.g.
   `https://github.com/<your-github-user>/vcmi.git`.
2. Update the submodule URL to that remote:
   ```sh
   git config -f .gitmodules submodule.vcmi.url https://github.com/<your-github-user>/vcmi.git
   git submodule sync
   git add .gitmodules && git commit -m "Point vcmi submodule at public fork"
   ```
3. Push this outer repo.

## License

GPL-2.0-or-later, matching VCMI. See [LICENSE](LICENSE). Heroes III game data is
not included and is not covered by this license.
