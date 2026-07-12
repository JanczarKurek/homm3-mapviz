# vcmiwallpaper

Renders a region of a Heroes III map as a seamlessly looping **animated wallpaper**,
using VCMI's real in-game adventure-map renderer. Animated water, lava, flags,
windmills and monsters look pixel-identical to the game. It can also render a
**hero walking the map and picking up resources** (`--walk`, see below).

The output format is chosen from the output file extension. **All formats are encoded
crisp** — lossless, no chroma subsampling, no smoothing — so the pixel grid stays sharp
instead of the blurry result lossy/`yuv420p` encoding gives on pixel art:

| extension | format | notes |
| --- | --- | --- |
| `.webp` (default) | animated WebP, **lossless**, full colour, infinite loop | what the [`awww`](https://github.com/LGFae/awww) wayland wallpaper daemon (and other image-based tools) display directly |
| `.gif` | animated GIF, no dithering | portable, 256-colour |
| `.webm` | VP9, **lossless**, `yuv444p` | |
| `.mp4` | H.264, **lossless** (qp 0), `yuv444p` | |

Note: `awww` plays **animated images** (GIF / animated WebP / APNG), not video — so use
`.webp` (or `.gif`) for it, e.g. `awww img wallpaper.webp`. `.webm`/`.mp4` are for video players.

### Keeping it crisp on screen (important)

A wallpaper daemon scales the image to fill your monitor. By default `awww` resizes with
`Lanczos3`, a **smoothing** filter, so a native 32px-per-tile render gets blurred when
upscaled to your screen. Two ways to keep pixels sharp:

- Tell the daemon not to smooth: `awww img wallpaper.webp --filter Nearest`
- And/or pre-upscale the render with `--scale N` (nearest-neighbour, see below) so the
  output is close to your screen resolution and the daemon barely scales it.

For the sharpest result, do both.

## Build

`vcmiwallpaper` is built together with the client (it is gated by `ENABLE_CLIENT`).
With a normal VCMI build it appears as the `vcmiwallpaper` target, e.g.:

```sh
cmake --build build --target vcmiwallpaper
```

It links only `vcmiclientcommon`; it needs SDL2, SDL2_image and `ffmpeg` on `PATH`
(ffmpeg is used to assemble the frames into the chosen format).

## Running

The tool needs the same data as VCMI itself (original Heroes III data + the built-in
VCMI mod). It runs **headless** — no display is required; it forces the SDL `offscreen`
video driver and `dummy` audio driver automatically.

Internally it starts a real (headless, all-AI) game on the map via `CGameState::init`, so
**random objects are resolved to concrete ones** — random dwellings/towns/monsters get their
real faction-specific appearance, just like in the game (the map editor leaves them as
"Random Dwelling" placeholders). As a side effect the rendered state is the game's turn-0
state, so AI starting heroes appear in/next to their towns.

```sh
vcmiwallpaper --map "/path/to/Map.h3m" \
              --x 20 --y 20 --width 40 --height 25 --level 0 \
              --frames 8 \
              --out wallpaper.webp
```

| option | meaning |
| --- | --- |
| `--map, -m` | path to the map file (`.h3m` / `.vmap`) — **required** |
| `--x`, `--y` | top-left tile of the region (default 0,0) |
| `--width, -w`, `--height` | region size in tiles, `0` = to the map edge |
| `--level, -z` | `0` = surface, `1` = underground |
| `--scale, -s` | integer nearest-neighbour upscale of the output (default 1 = native 32px tiles; e.g. `2` → 64px tiles). Keeps pixels crisp while raising resolution toward your screen size |
| `--resolution` | exact output pixel size, e.g. `1920x1080`. Auto-picks the tiles needed to cover it (honouring `--scale`), pulls the origin back to stay on the map, and center-crops to the exact size. Overrides `--width`/`--height`; use `--scale` for zoom |
| `--pan-to` | slow-pan mode: tile `X,Y` the view eases toward and back from (seamless ping-pong), e.g. `80,60`. `--x`/`--y` is the start, `--width`/`--height` the viewport (smaller than the map). More `--frames` = slower, smoother pan |
| `--walk` | hero-walk mode: a hero tours the level and picks up every reachable resource pile (one-shot). By default the camera auto-frames the whole tour — see below |
| `--region` | walk mode: confine the hero, path and piles to a tile rectangle `X,Y,W,H` (e.g. `40,30,30,20`). The camera is that rectangle; the hero and the piles it tours are picked from inside it |
| `--follow` | walk mode: moving camera that keeps the hero centred (the map scrolls). `--width`/`--height` set the viewport size (default ~26 tiles) |
| `--hero` | walk mode: index of the hero to animate among those found inside the window/region (default 0 = first) |
| `--step-frames` | walk mode: animation sub-frames per one-tile step (default 8; higher = smoother and slower) |
| `--pickup-pause` | walk mode: frames to hold (hero idle) when a resource is collected (default 6) |
| `--frames, -n` | number of 180 ms animation phases = loop length (default 24). For a seamless loop use a multiple of the on-screen animation periods — see below |
| `--fps` | output framerate (default `1000/180 ≈ 5.56`, the native game speed) |
| `--out, -o` | output path; format chosen by extension (default `wallpaper.webp`) |
| `--ffmpeg` | path to the `ffmpeg` executable used to encode the frames (default: look it up on `PATH`) |
| `--keep-frames` | keep the intermediate `*_frames/frame_NNNN.png` |

### Rendering for a specific screen, e.g. 1920×1080

Tiles are 32 px, so not every screen size is a whole number of tiles (`1080/32 = 33.75`).
Use `--resolution` and the tool covers the request with whole tiles then center-crops to
the exact size:

```sh
# native 1:1 pixels, maximum map detail (renders 60×34 tiles = 1920×1088, crops to 1080)
vcmiwallpaper --map "Maps/Foo.h3m" --x 30 --y 20 --resolution 1920x1080 --out wp.webp

# chunky 2× "zoomed" pixels (renders 30×17 tiles, upscales ×2 nearest, crops to 1080)
vcmiwallpaper --map "Maps/Foo.h3m" --x 30 --y 20 --resolution 1920x1080 --scale 2 --out wp.webp
```

`--x`/`--y` pick the top-left of the area; the origin is pulled back automatically so the
region stays on the map. If the map is too small to fill the resolution, the rest is padded
with black bars (a warning is logged). For `awww`, you can also skip `--resolution` and let
the daemon crop: render a hair larger than the screen and run `awww img wp.webp --filter Nearest`.

Each tile is 32 px, so a `40×25` region produces a `1280×800` video (multiplied by
`--scale`: e.g. `--scale 2` → `2560×1600`). Tiles are
revealed (no fog) and the whole map is treated as visible.

### Frame count and seamless loops

The adventure-map animation is **discrete**: each H3 sprite changes phase every 180 ms, and
its frame index is `(time / 180) % frameCount`. The tool renders one phase per frame, so
each frame is a genuinely different engine state — there are no in-between states to render,
and asking for a higher `--fps` (finer time steps) only produces duplicate frames.

The loop is **seamless only when `--frames` is a multiple of every on-screen animation's
period** (water, flags, windmills, monsters…). Those periods are usually 4/6/8/12, whose
LCM is 24 — so **`--frames 24`** (the default, a ~4.3 s loop) wraps cleanly on most maps;
use `48` for a longer loop. Arbitrary counts (e.g. 20) leave a visible jump at the wrap
because some animations haven't completed a whole cycle. The exact ideal count is
map-dependent; you can find it by rendering with `--keep-frames` and comparing each frame
to frame 0 — the first that matches is the period.

### Slow panning across the map

`--pan-to X,Y` makes the view drift slowly from the start region (`--x`/`--y`, sized by
`--width`/`--height`) to the target tile and back, while the water/creatures keep animating:

```sh
# a 30x18-tile viewport that eases from (20,40) across to (70,40) and back, over a long loop
vcmiwallpaper --map "Maps/Foo.h3m" --x 20 --y 40 --width 30 --height 18 \
              --pan-to 70,40 --frames 120 --out wp.webp
```

How it works and what to expect:

- The motion is an **eased ping-pong** (cosine): the view reaches the target at the loop's
  midpoint and returns exactly to the start at the end, with zero velocity at both turns —
  so it loops seamlessly with no jump and no jerk. (A one-way pan can't loop without a jump.)
- Panning is **per-pixel** (sub-tile), not per-tile, so it stays smooth; the renderer draws
  one extra row/column and crops at the sub-tile offset.
- The viewport must be **smaller than the map** in the pan direction, or there's no room to
  move (you'll get a warning and a static render). The path is clamped to stay on the map.
- Use **more `--frames`** for a slower, smoother pan (e.g. `96` or `120`). Keep it a multiple
  of 24 so the animation stays seamless too. Pan distance per frame ≈ `2 x distance / frames`
  at mid-speed, so pick frames to keep that down to a few pixels.
- Combines with `--scale` and `--resolution` (each frame is cropped/scaled the same way).

### Hero walk — a hero collecting resources

`--walk` turns the wallpaper into a little scene: a hero strolls the level and picks up every
**land-reachable** resource pile, just like the game — it draws the iconic **movement-arrow
path** ahead of itself (with the destination cross), slides tile to tile facing the right way,
and **collects each pile from the adjacent tile** (resources are block-visitable in H3, so the
hero stops *next to* a pile and "visits" it rather than stepping onto it). The pile then vanishes.

```sh
# auto-framed on the hero's whole tour (recommended)
vcmiwallpaper --map "Maps/Foo.h3m" --walk --out walk.mp4

# just give an output resolution: a window of that size is auto-placed on the hero
# with the most piles around it, and the hero + path + piles are confined to it
vcmiwallpaper --map "Maps/Foo.h3m" --walk --resolution 1920x1080 --out walk.webp

# pick the rectangle yourself: hero, path and piles are all chosen from inside it
vcmiwallpaper --map "Maps/Foo.h3m" --walk --region 40,30,30,20 --out walk.mp4

# moving camera that tracks the hero, 26x26-tile viewport, smoother/slower steps
vcmiwallpaper --map "Maps/Foo.h3m" --walk --follow --width 26 --height 26 \
              --step-frames 12 --pickup-pause 4 --out walk.mp4
```

What to expect:

- **One-shot, not seamless.** Because piles disappear as they're collected, the clip can't
  loop perfectly — it plays the tour once (it still restarts if looped, with a visible jump).
- **Camera / bounds.** Several ways to choose what's in frame:
  - *default* — **auto-frames the whole tour** (a fixed window sized to the hero's start + every
    collected pile), so nothing is cut off.
  - **`--resolution WxH`** or **`--width`/`--height`** *(no position)* — a window of that size is
    **auto-placed on the hero with the most piles inside it**, and the hero, path and piles are
    all confined to that window. This is what makes a bare `--walk --resolution 1920x1080` produce
    a nice, self-contained path.
  - **`--region X,Y,W,H`** (or `--x`/`--y` with a size) — *you* pick the rectangle; the hero and
    the piles it tours are taken from inside it. If no hero is in the rectangle the tool warns and
    auto-places the window instead.
  - **`--follow`** — a moving camera that keeps the hero centred (the map scrolls) over the whole
    connected tour.
- The hero is picked from inside the window/region (`--hero N` to choose among them); the route is
  a greedy nearest-neighbour tour over **land-reachable** piles. The hero walks only on clear tiles
  (it can't pass through obstacles, piles, monsters or water — same as the game), and any pile with
  no walkable tile beside it reachable on foot (across water, on another landmass, or only via
  teleport/boat) is skipped. When a window bounds the walk, the path is kept **inside** it, so it
  never wanders off-frame.
- Walk frames are 50 ms apart, so `--fps` defaults to **20** in this mode. Frame count grows
  with the tour length — on a big, pile-rich map the auto-framed view can be large and produce
  **thousands** of frames, so use `--follow` (smaller fixed viewport) or a small map to try it
  out. Progress is logged per leg and every ~25 frames so you can see it working.

### Data setup for a development build

When run from a build directory VCMI uses *development mode*: the current working
directory becomes the only data root. The build already copies `config/` and `Mods/`
into `build/bin/`. Add the original Heroes III data and a binary marker there:

```sh
cd build/bin
ln -s "/path/to/Heroes3/Data"  Data
ln -s "/path/to/Heroes3/Maps"  Maps
ln -s "/path/to/Heroes3/Mp3"   Mp3    # optional (music, unused for rendering)
# a vcmiclient / vcmiserver / vcmieditor binary must exist here to enable dev mode
./vcmiwallpaper --map Maps/SomeMap.h3m --frames 8 --out ~/wallpaper.webp
```

### Rendering modded maps (HotA, etc.)

A map declares the mods it needs; if one isn't installed and enabled you get
`Fatal error: Failed to find mod <id>`. Note this is the VCMI **mod**, which is separate
from the original game's data archives (e.g. owning HotA's `.lod` files is not enough — you
still need the VCMI "Horn of the Abyss" mod that reads/ships the converted content).

In development mode the tool only scans `build/bin/Mods/`, so install the mod there and
enable it in the active preset. For HotA (from VCMI's official mod repository):

```sh
# 1. download + extract into the dev Mods dir as 'hota' (folder name = mod id)
curl -L -o /tmp/hota.zip \
  https://github.com/vcmi-mods/horn-of-the-abyss/releases/download/1.8/horn-of-the-abyss-vcmi-1.8.zip
unzip -q /tmp/hota.zip -d build/bin/Mods/
mv build/bin/Mods/horn-of-the-abyss-vcmi-1.8 build/bin/Mods/hota

# 2. enable it: add "hota" to the active preset's mod list in
#    ~/.config/vcmi/modSettings.json   ->  "mods": [ "vcmi", "core", "hota" ]
#    (VCMI auto-enables its sub-mods on next run)

./vcmiwallpaper --map "Maps/[HotA] Some Map.h3m" --out ~/wallpaper.webp
```

Notes:
- Enabling HotA is global (also affects normal VCMI) and HotA replaces some base graphics,
  so even non-HotA maps then render with HotA's art. Remove `"hota"` from the preset to
  revert. Loading the ~0.5 GB mod also makes every render's start-up noticeably slower.
- A normal VCMI install with the Launcher can install the mod via its mod manager instead;
  then symlink that mod into `build/bin/Mods/` for the dev tool to see it.
