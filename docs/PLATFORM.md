<!-- SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md -->
# Platform: build, run, measure, plug in

The platform is the executable `beese-poker`: display bring-up, the fixed-step
main loop, input, the app state machine, the F1 overlay, the texture registry
and the perf / screenshot / script tooling that every later milestone uses to
see and measure its work. Code: `platform/`. Tools: `tools/`.

## 1. Build

raylib 5.5 is built once from source into `third_party/` (gitignored) by
`tools/fetch_raylib.sh`, which downloads the tag tarball, checks its pinned
SHA-256, applies our patches (`tools/raylib-5.5-*.patch`, section 8) and
builds a static library per variant. A change to a patch rebuilds the variants.

| variant | raylib build | used by |
|---|---|---|
| `desktop` | PLATFORM_DESKTOP, OpenGL 3.3 | WSL / Linux desktop, Xvfb screenshots |
| `drm` | PLATFORM_DRM, OpenGL ES 2.0 | the Pi (default) |
| `drm-gles3` | PLATFORM_DRM, OpenGL ES 3.0 | the Pi, `-DBPL_GLES3=ON` |

**WSL / desktop** (Debian; frame times here mean nothing):

```sh
sudo apt install build-essential cmake libx11-dev libxrandr-dev libxinerama-dev \
    libxcursor-dev libxi-dev libgl1-mesa-dev libgl1-mesa-dri libasound2-dev xvfb xauth
tools/fetch_raylib.sh desktop
cmake -S . -B build-desktop -DBPL_PLATFORM=DESKTOP
cmake --build build-desktop -j
```

**The Pi** (Debian 13 / Pi OS; needs `libgles2-mesa-dev libegl-dev libgbm-dev
libdrm-dev libasound2-dev cmake`):

```sh
tools/fetch_raylib.sh drm            # add drm-gles3 for the ES3 build
cmake -S . -B build-drm -DBPL_PLATFORM=DRM            # -DBPL_GLES3=ON for ES3
cmake --build build-drm -j4
```

Measured on the cabinet (Pi 4, -j4): raylib 20-24 s per variant (49 s on the
first, cold build), the platform from clean 21 s. The binary is
`build-*/platform/beese-poker`; the build copies `platform/config.ini` next to it.

## 2. Run

Desktop, headless:

```sh
xvfb-run -a -s "-screen 0 1280x720x24" build-desktop/platform/beese-poker --lockstep \
    --frames 200 --shot 60:out/attract.png
# the cabinet's layout at desktop resolution:
xvfb-run -a -s "-screen 0 3440x1440x24" build-desktop/platform/beese-poker --size 3440x1440 \
    --frames 40 --shot 30:screen:out/ultrawide.png
```

On the Pi the display belongs to EmulationStation (or a running RetroArch
game). `tools/cabinet_run.sh` takes it over, runs a command and **always**
gives it back. Run it on the Pi, detached from the SSH session (a plink call
chained to a background job can hang):

```sh
ssh pi@<pi-address>        # or: plink -batch -pwfile <pwfile> pi@<pi-address> '...'
cd /path/to/BeesePokerLounge
setsid -f nohup tools/cabinet_run.sh build-drm/platform/beese-poker --frames 3600 \
    --mode gputest --perf-csv out/gpu.csv > run.log 2>&1 < /dev/null
```

What it does, if you need to do it by hand:

```sh
# take over: stop a running game and EmulationStation, without the ES
# wrapper relaunching it, and wait until both are gone
rm -f /tmp/es-restart
pkill -f "^/opt/retropie/emulators/retroarch/bin/retroarch"
pkill -f "supplementary/emulationstation/emulationstation$"
# ... run the game (with --frames N or under `timeout`) ...
# give it back: tty1 autologin starts EmulationStation again
sudo systemctl restart getty@tty1
pgrep -f "supplementary/emulationstation/emulationstation$"    # must print a pid
```

Never use `openvt`. Exits: ESC, COIN+START held together (RetroPie's own
select+start), SIGINT/SIGTERM/SIGHUP, or `--frames`. All of them close the
display cleanly, so launched from EmulationStation as a Port the game returns
to it.

## 3. Command line

| option | |
|---|---|
| `--config FILE` | settings (default: `config.ini` next to the binary, else `platform/config.ini`) |
| `--mode NAME` | start state: `attract menu draw holdem service gputest` |
| `--seed HEX` | session seed (default: OS entropy); printed at start |
| `--frames N` | exit after N rendered frames |
| `--perf-csv FILE` | per-frame log, see section 7 |
| `--shot N:FILE[,...]` | save the 1280x720 play space after frame N; `N:screen:FILE` saves the whole screen (see below) |
| `--script "T:BTN[+BTN];A-B:BTN;..."` | inject logical buttons; T / A-B are **tick** numbers (60 per second, from 0), a range holds the buttons down; names as in `engine/input.h` without `BTN_` (`HOLD1`, `DEAL`, `BET_MAX`, ...) |
| `--lockstep` | exactly one tick per frame, dt = 1/60: frames == ticks, runs and screenshots are reproducible |
| `--gpu-finish` | `glFinish` after the play space and after compose, so `render_ms` / `compose_ms` are true GPU times (costs throughput; for measuring only) |
| `--overlay` | start with the F1 overlay on |
| `--present auto\|plane\|gpu` | how the play space reaches the display (section 4) |
| `--scale auto\|integer\|fit\|1x`, `--filter auto\|point\|bilinear`, `--no-side-art` | layout (section 4) |
| `--size WxH` | window size (desktop) or display mode (DRM; default: the display's preferred mode) |
| `--sprites N`, `--no-shader` | GPU test scene knobs |
| `--no-vsync`, `--verbose`, `--help` | |

Screenshot frames are rendered-frame numbers, script times are ticks; with
`--lockstep` the two are the same. A shot frame is slow (PNG encoding) and
shows up in the CSV. With the plane path (section 4) the composed screen does
not exist in GPU memory, so `screen:` captures the play space too.

Example, a reproducible demo capture:

```sh
beese-poker --lockstep --seed C0FFEE --frames 200 \
  --script "30:DEAL;60:DOWN;90:UP;100:OK;130:HOLD1;140:HOLD3;150-170:HOLD5" \
  --shot 20:out/attract.png,70:out/menu.png,160:out/draw.png
```

## 4. Display: the play space, scaling, side art

Everything is drawn in a **1280x720 play space**. The layout
(`screen_plan()`) puts it on the display: `scale auto` takes a whole-number
scale when that fills at least 90% of a fractional fit (3440x1440: 2x =
2560x1440 at x = 440), otherwise a 16:9 fractional fit (1920x1080: 1.5x);
`filter auto` is point sampling at whole-number scales and bilinear otherwise.
The margins (440 px each side on the cabinet; letterbox bars on taller
screens) belong to the **side-art hook** (`screen_set_side_art()`); the
placeholder is a neon honeycomb.

Two presentation paths, chosen by `present` (default `auto`):

- **plane** (the Pi): raylib's screen *is* the 1280x720 play space. The Pi's
  display controller (HVS) scales it at scan-out onto the layout rectangle, on
  an overlay plane, with nearest-neighbour filtering at integer scales. The
  side art sits on the primary plane as a static background framebuffer,
  drawn once by the hook (time = 0) and redrawn by
  `screen_refresh_side_art()`. The upscale costs the GPU nothing.
- **gpu** (desktop, and the fallback when the driver has no atomic KMS or free
  overlay plane): the play space is a render target; every frame the screen
  is cleared, the side-art hook draws the margins (animated) and the play
  space is drawn scaled with blending off.

Why the plane path exists (measured on the cabinet, `--gpu-finish`, GPU ms
per frame): presenting any 3440x1440 frame costs 8.7 ms (clearing and storing
20 MB of framebuffer: memory bandwidth), the 2x upscale adds 3.9 ms and the
side art 2.9 ms, **15.5 ms of the 16.7 ms frame** before the game draws
anything. At 1920x1080 the same present costs 4.3 ms. With the plane the
compose step costs 0.0 ms.

Consequences for renderers: draw only inside the play space; do not assume the
default framebuffer is bigger than 1280x720 (`GetScreenWidth()` is 1280 on the
cabinet, the display size is `screen_layout()->screen_w`); animated side art
on the plane path means redrawing the background (a CPU copy of 20 MB), so
keep side-art animation for the gpu path or make it rare.

## 5. The app state machine and the mode interface (`platform/app.h`)

States: `ATTRACT -> MENU -> DRAW | HOLDEM`, `SERVICE` from any state with the
SERVICE button (BACK / SERVICE returns), and `GPUTEST` (`--mode gputest`, the
GPU smoke test). Each state is served by an `AppMode`:

```c
typedef struct AppMode {
    const char *name;
    void (*init)(AppCtx *ctx);                        /* once, after the window exists */
    void (*enter)(AppCtx *ctx, AppState from);        /* each switch to this state */
    void (*leave)(AppCtx *ctx, AppState to);
    void (*tick)(AppCtx *ctx, const InputFrame *in);  /* 60 Hz, deterministic */
    void (*present_update)(const AppCtx *ctx, const GameEvent *ev, int nev, float dt);
    void (*present_draw)(const AppCtx *ctx);          /* inside the 1280x720 play space */
    void (*shutdown)(void);
} AppMode;                                            /* any pointer may be NULL */
```

- `tick` is the only place game state changes. It gets the tick's
  `InputFrame`, pushes events to `ctx->events` (cleared before every tick) and
  switches state only with `app_request(ctx, APP_MENU)` (applied after the
  tick; `APP_EV_STATE` is pushed). The wallet is `ctx->wallet`; the session
  seed `ctx->session_seed` (derive your own streams from it).
- `present_update` runs once per rendered frame with **every** event pushed
  by the frame's ticks (0..4 of them) and the frame's dt; `present_draw` runs
  right after, inside the play space, already cleared to charcoal.
  `ctx->input` is the last tick's input with `pressed` merged over the frame,
  for button feedback. `ctx->time` is presentation time (seconds).
- To plug a mode in: define `const AppMode mode_draw = {...}` in your module
  and point its slot in **`platform/app_modes.c`** at it (replacing the
  placeholder). Each placeholder shows a live logical-button panel, which is
  the quickest way to check input on the cabinet.

Timing of a press: raylib and the joystick reader poll the devices at the end
of frame N-1; the input is sampled at the start of frame N, right before its
ticks, and the first tick of frame N gets every edge since the previous tick
(edges are latched, so a frame that runs no tick loses nothing, and a tap
shorter than a frame still reads as `down` for one tick). The press is
therefore visible in the frame drawn right after it is read.

## 6. Input and `config.ini`

`platform/config.ini` is the documented default (sections `[input]`,
`[display]`, `[debug]`). Each logical button takes up to six physical inputs:
`KEY_<name>`, `JOY<n>_B<k>`, `JOY<n>_AXIS<k>+/-`, `JOY<n>_HAT<k>_UP/...`,
with `JOY*` for any joystick. `EXIT` (ESC) and `EXIT_COMBO` (`COIN+START`)
leave the game; `SLIDER` names an analogue axis for the Hold'em slider; `TOUCH`
maps a touchscreen (or the desktop mouse) into play-space coordinates.

Joysticks are read from evdev by `platform/joy_evdev.c`, not through raylib:
raylib 5.5's DRM backend drops generic joystick buttons (`BTN_TRIGGER..`),
which is what the cabinet's two DragonRise "USB gamepad" encoders send.
Numbering follows SDL, i.e. the ids in EmulationStation's `es_input.cfg`
(select = B8, start = B9). Hot-plug is picked up with inotify. The physical
panel layout of the cabinet is not mapped yet: the service screen lists each
joystick and the buttons held, which is how to fill in `config.ini`.

## 7. Measuring: F1 overlay, perf CSV, draw calls, textures

**F1 overlay** (the `DEBUG` button): FPS and frame time (last, rolling
average and max over 120 frames), logic / render / compose / swap ms, draw
calls of the previous frame, RSS, registered texture memory, state, tick,
seed, layout, GL version, and the start-up time.

**`--perf-csv FILE`**, one row per frame:
`frame,frame_ms,logic_ms,render_ms,draw_calls,compose_ms,swap_ms,ticks,rss_kb`
(`frame_ms` of row N is the time from frame N-1's start to frame N's start;
RSS is sampled every 30 frames). `tools/perf_summary.py FILE.csv [--skip N]`
prints mean / p50 / p99 / max frame time, the 1% low fps (1000 / mean of the
slowest 1% of frames), missed vblanks and the means of the other columns.

**Draw calls** are counted at the GL entry points, so every batch flush,
render-target switch and the final compose count: on DRM by link-time
wrapping of `glDrawElements` / `glDrawArrays` (`-Wl,--wrap`), on the desktop
by swapping glad's function pointers (`platform/gpustats.c`).

**Texture registry** (`platform/texreg.h`): the overlay's texture figure is
the sum of what is registered, so create and free every texture and render
target through it:

```c
Texture2D t = texreg_load_image("cards atlas", img);       /* LoadTextureFromImage */
RenderTexture2D rt = texreg_load_rt("bloom 1/4", 320, 180, 0);  /* 0 = no depth buffer */
texreg_unload(t);  texreg_unload_rt(rt);
Font f = LoadFontEx(...);  texreg_add_texture("ui font", f.texture);   /* made by raylib */
texreg_remove(f.texture.id);  UnloadFont(f);
```

Sizes are estimated from size, format and mip levels (+4 bytes/px for a depth
buffer). `texreg_dump()` lists them (`--verbose` prints it at exit). Not
textures and not counted: the display buffers (plane path: 3 x 3.5 MB GBM
surface buffers and the 19.8 MB background; gpu path: the 3440x1440 surface).

**Start-up**: the first frame logs `STARTUP: first ATTRACT frame on screen
N ms after exec` with a breakdown (before main, options + probe, InitWindow,
platform init, modes init, first frame).

## 8. raylib patches (`tools/raylib-5.5-*.patch`)

- **orphan-vbo**: rlgl re-uploaded its one vertex buffer in place for every
  batch flush; on the Pi's tile-based V3D that makes Mesa flush and wait,
  splitting the render pass and storing / reloading the whole render target
  for every extra draw call. Orphaning (`glBufferData`) removes the stall:
  22 ms of CPU stall per frame gone, the 3440x1440 compose went from 30.8 to
  15.5 ms of GPU time.
- **drm-plane**: the scaled-plane presentation of section 4, with atomic KMS
  page flips and one framebuffer per buffer (raylib did `drmModeAddFB` +
  `drmModeSetCrtc` + `drmModeRmFB` every frame).
- `fetch_raylib.sh` also compiles out raylib's F12 screenshot and GIF
  hotkeys (a cabinet must not write files on a key press).
- GLES3 build: raylib 5.5 calls `glDrawBuffersEXT`, which Mesa does not
  export; `platform/gpustats.c` forwards it to `glDrawBuffers`.

## 9. Measured on the cabinet (2026-09-23)

Pi 4 at 1.8 GHz / V3D 500 MHz, Debian 13, Mesa 26.2 (an OpenGL ES 3.1 context
for both raylib builds), 3440x1440@60 on HDMI-A-2, EmulationStation stopped.
`gputest` = full-screen neon shader + N alpha-blended sprites of 32-112 px
from a 256x256 atlas (1,000 sprites cover the play space about 4.5 times).

GPU cost of the play space (plane path, `--gpu-finish`, ms per frame):

| scene | GPU ms |
|---|---|
| empty frame (clear + store 1280x720) | 1.9 |
| + full-screen shader | 6.4 |
| 500 sprites, no shader | 7.4 |
| 700 sprites + shader | 14.2 |
| 800 sprites + shader | 15.2 (max 22.8) |
| 1,000 sprites + shader | 17.7 |
| placeholder attract (39 draw calls, CPU+GPU, no finish) | 3.1 |

Rule of thumb: ~2.2 ms per million pixels of alpha-blended textured fill,
~4.5 ms for one full-screen pass of a moderately heavy shader. Keep the
whole play space under ~14 ms of GPU time for a locked 60.

Frame pacing (`tools/perf_summary.py`, first 60 frames skipped):

| run | mean ms | p99 ms | 1% low fps | missed vblanks | draw calls |
|---|---|---|---|---|---|
| gputest 700 + shader, 20 s | 16.69 | 16.78 | 54.6 | 1 (the one-off at ~2.8 s) | 3 |
| gputest 800 + shader, 60 s | 16.73 | 16.79 | 44.5 | 12 | 3 |
| gputest 1,000 + shader, 60 s, ES2 | 31.49 | 33.46 | 29.8 | GPU-bound: 30 fps | 3 |
| same, ES3 build | 31.19 | 33.47 | 29.9 | GPU-bound: 30 fps | 3 |
| same scene, gpu path (compose on GPU), 30 s | 33.95 | 48.54 | 20.0 | every frame | 6 |
| placeholder attract, 10 s | 16.67 | 16.77 | 59.6 | 0 | 39 |

Every busy run has exactly one missed frame about 2.8 s after start (never in
light scenes); it looks like the firmware raising the V3D clock under load
(`gpu_freq_min=250`) and is not a steady-state cost.

Presenting at 3440x1440: gpu path 15.5 ms of GPU per frame (8.7 base, +3.9
the 2x upscale, +2.9 side art; 8.9 ms at 1x without side art); plane path
0.0 ms. ES2 vs ES3 builds: no measurable difference; ES2 stays the default.

Memory: RSS 72.5 MB (plane path, gputest); registered textures 0.28 MB
(gputest atlas + raylib font); display buffers outside the registry: 19.8 MB
background + 3 x 3.5 MB surface buffers.

Start-up to the first frame on screen: 2.87-3.14 s with the page cache
dropped (InitWindow, i.e. loading Mesa and EGL from the SD card, is 2.3-2.6 s
of it), 0.75-0.81 s warm (InitWindow 0.33 s, drawing and uploading the side
art 0.35 s). Budget: 4 s.

Builds: raylib 20-24 s per variant (49 s cold); the platform from clean 21 s.

## 10. Placeholders and open points

- `CMakeLists.txt` (top level) and `engine/{input,event,wallet,step}.h`,
  `engine/event.c` are placeholders written by the platform branch so it
  builds alone; the engine branch owns them. `input.h`, `event.h` and
  `wallet.h` follow CONTRACT.md section 2/3 verbatim. `step.h` is not in the
  contract: here it is a pure accumulator (`step_init`, `step_advance(ns)`,
  `step_alpha`); if the engine's differs, only `platform/main.c` calls it.
  The platform expects a `bpl_engine` target and includes as `"engine/x.h"`
  from the repository root.
- The session seed comes from `getrandom()` in `platform/main.c` until the
  engine's `rng_os_seed()` is available.
- The attract / menu / draw / hold'em / service screens are placeholders
  (`platform/modes_placeholder.c`); credits are a fixed 1,000 until
  persistence exists.
- The cabinet's panel-to-button mapping in `config.ini` is a first guess.
- On the plane path the side art is static (redraw with
  `screen_refresh_side_art()`); the gpu path animates it.
