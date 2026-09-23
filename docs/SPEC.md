# Beese's Poker Lounge - specification

This is the owner's brief, verbatim, followed by the adaptations agreed for the
actual target cabinet. Where they differ, **the adaptations win**.

---

# ROLE
You are a senior game engineer who ships 2D arcade and casino titles on embedded
Linux hardware. You write tight, profiled C, verify every claim against real
measurements, and never fake results.

# GOAL
Build "Beese's Poker Lounge": a standalone arcade video poker cabinet game with two
modes, Jacks-or-Better Draw Poker and No-Limit Texas Hold'em vs AI opponents. It
must look and feel like a premium modern casino machine: neon, gold, motion, and
celebration. It must also play honestly: real shuffles and real odds, with no
manipulated outcomes. All excitement comes from presentation, never from rigging.
Credits are free-play/bar credits only; no real-money functionality.

# TARGET HARDWARE (hard constraints)
- Raspberry Pi 4, 1.8 GHz quad Cortex-A72, **1 GB RAM**, VideoCore VI (OpenGL ES 3.1)
- Raspberry Pi OS Lite 64-bit (Bookworm), **no X11/Wayland desktop**
- Output: 1080p TV over HDMI
- Must also launch cleanly as a RetroPie "Ports" entry from EmulationStation and
  return to it on exit

# TECH STACK (use exactly this unless profiling proves otherwise)
- C11, raylib 5.x built with `PLATFORM=PLATFORM_DRM`, GLES backend (KMS/DRM, no desktop)
- raylib's built-in miniaudio for sound; OGG for music (streamed), WAV for short SFX
- No other runtime dependencies. Single binary plus an `assets/` folder
- CMake build; must compile natively on the Pi in under 5 minutes

# PERFORMANCE BUDGET (acceptance criteria, measured on the Pi, not assumed)
- Locked 60 fps during the heaviest effect (jackpot celebration), 1% lows >= 55 fps
- Render at 1280x720 into a render texture, then upscale to the display.
  Offer native 1080p only if profiling shows >= 30% frame-time headroom
- Resident memory <= 150 MB; GPU textures <= 64 MB total
- Cold start to attract mode <= 4 seconds
- Include an F1 debug overlay showing frame time, draw calls, RSS, and texture memory
- Batch all sprites through texture atlases; target < 100 draw calls per frame
- All post-processing runs at half or quarter resolution (the Pi's GPU is limited
  by pixel-shading throughput)

# 1. CORE ENGINE
- Shuffle: Fisher-Yates driven by a seeded 64-bit PRNG (xoshiro256**), with the
  seed taken from `getrandom()`. Log the seed per hand for replay/debug
- Hand evaluator: small-footprint lookup (Cactus Kev perfect-hash for 5-card,
  best-5-of-7 for Hold'em). **Do NOT use the 2+2 130 MB lookup table**, since it
  would take up a large share of this Pi's RAM
- Fixed-timestep game logic (60 Hz) decoupled from rendering; deterministic game
  state that can be replayed from seed + input log
- Game state machine per mode; render layer only reads state, never mutates it

# 2. VISUAL DESIGN: "neon honey lounge"
Palette: deep charcoal/black felt, honey gold, amber, electric magenta and cyan
neon accents. Motif: honeycomb hex patterns, a subtle bee mascot glyph (original
design), brushed-brass trim.

Required effects (each individually toggleable in settings for perf testing):
- **Cards:** generated procedurally at startup into an atlas at the output
  resolution (vector-drawn pips and faces, original art, crisp at any scale).
  Deal animation flies cards from the shoe along eased arcs; flips use a fake-3D
  perspective scale on X with a specular sweep
- **Bloom:** threshold, downsample to 1/4 res, 2-pass separable blur, additive
  composite. This is what makes the neon glow
- **Particles:** pooled (max 2,000, no per-frame allocation). Gold coin showers,
  sparks, honey droplets, confetti; intensity scales with win size
- **Win tiers:** small win = pulse and chime; medium = coin burst and banner;
  big = screen shake, light-chase border, count-up meter; royal flush / big pot =
  full takeover: slow-mo, spotlight sweep, fireworks, 6-8 s fanfare, skippable
  with any button
- **Chips (Hold'em):** stacked chip sprites that slide into the pot with slight
  overshoot and clatter; pot visibly grows
- **Marquee:** animated neon title sign with flicker, chasing bulb border around
  the play area, idle shimmer on held cards
- **Game feel:** every button press gets instant visual + audio feedback (< 1 frame);
  hit-pause (2-3 frames) on big reveals; easing on everything, no linear motion
- **Attract mode:** after 60 s idle, demo gameplay loops with the paytable, flashy
  highlights, and "PRESS DEAL" pulsing, like a real bar machine

# 3. GAME MODES

## A. Draw Poker (video poker)
- Variants: Jacks or Better (9/6 full-pay), Bonus Poker, Deuces Wild (full-pay)
- Bet 1-5 credits, "BET MAX" pays the 4,000-credit royal bonus at 5 credits
- Paytable always on screen with the current bet column lit; winning row flashes
- HOLD buttons per card, DEAL/DRAW, optional double-up (red/black) after wins
- Optional "Strategy Hint" toggle: highlights the optimal hold using exact
  expected-value calculation over all 32 hold patterns

## B. Texas Hold'em (No-Limit, 6-max table)
- Player vs 5 AI opponents, sit-and-go format with rising blinds
- Full rules: blinds, all 4 betting rounds, min-raise rules, all-in, side pots,
  split pots, correct showdown order, muck option
- Bet slider plus quick-bet buttons (1/2 pot, 3/4 pot, pot, all-in)
- **AI (must never see hidden cards or the deck):**
  - Hand strength via Monte Carlo equity (time-boxed to 30 ms per decision on a
    worker thread), plus pot odds, position, and stack depth
  - Preflop starting-hand charts per position
  - 4 personalities with distinct parameters: Tight-Passive "Rock", Loose-
    Aggressive "Maniac", Tight-Aggressive "Shark", Calling Station "Fish"
  - Bluffs at personality-appropriate frequencies; mixed strategies with
    randomness so they're not exploitable by simple patterns
  - "Think time" of 0.4-2.0 s scaled by decision difficulty, with avatar tells
    (subtle animation), so the table feels alive
- Difficulty setting scales how many Shark/Rock seats vs Fish seats there are

# 4. AUDIO
- Punchy, layered SFX: card snap, flip, chip clatter, button click, credit
  count-up tick (rising pitch), win stingers per tier, crowd murmur ambience
- Lounge/synthwave music loop, ducked under win fanfares
- All audio original or CC0, with sources listed in CREDITS.md
- Master/music/SFX volume in settings; total audio assets <= 15 MB

# 5. INPUT
- Keyboard (for arcade USB encoders), gamepad (SDL mappings via raylib), and
  optional touchscreen
- Default arcade layout: 5 HOLD buttons, DEAL/DRAW, BET ONE, BET MAX, CASH OUT,
  plus a service button
- Remappable input in a config file (`config.ini`); ESC or a coin+start combo
  exits back to EmulationStation

# 6. PERSISTENCE & ROBUSTNESS (bar environment: expect sudden power cuts)
- Credits, stats (hands played, biggest win, royal count), and settings saved via
  write-to-temp + fsync + atomic rename. Never corrupt on power loss
- Save directory configurable; game must still work if the SD card is mounted
  read-only (falls back to RAM, warns in the service menu)
- Service menu (hidden button): reset credits, add credits, view stats, set
  denomination, run the self-test

# 7. VERIFICATION (build these tests before any graphics work)
- **Evaluator exhaustive test:** enumerate all 2,598,960 five-card hands and
  confirm exact category counts: Royal 4, Straight flush 36, Quads 624,
  Full house 3,744, Flush 5,108, Straight 10,200, Trips 54,912,
  Two pair 123,552, Pair 1,098,240, High card 1,302,540
- **7-card test:** best-of-7 evaluation matches brute-force best-of-21-combos on
  10M random hands
- **Paytable return test:** simulate 10M hands of 9/6 JoB with optimal holds;
  return must converge to ~99.54%
- **Shuffle uniformity:** chi-square test on card-position frequencies
- **Hold'em rules tests:** side pots with 3+ all-ins, split pots, odd-chip
  distribution, min-raise edge cases
- **AI integrity test:** confirm AI decision functions take no argument that
  exposes opponent hole cards or deck order
- **On-device perf test:** scripted jackpot celebration run for 60 s, frame-time
  log written to CSV, must meet the budget above

# 8. DELIVERABLES
1. Complete source with CMake build
2. `install.sh`: installs deps, builds raylib for DRM, builds the game, adds the
   RetroPie Ports launcher script, and sets the needed `/boot/firmware/config.txt`
   entries (`dtoverlay=vc4-kms-v3d`, sensible CMA), with backups of anything it edits
3. README: build, run, controls, config, performance tuning, troubleshooting
4. Test suite runnable with `ctest`
5. CREDITS.md for all assets and fonts (OFL/CC0 only)

# 9. BUILD ORDER (report measured results at each milestone)
1. Engine core + evaluator + all tests passing
2. Draw Poker playable with placeholder graphics
3. Rendering pipeline: atlas, procedural cards, bloom, particles; profile on Pi
4. Full Draw Poker presentation and audio
5. Hold'em rules engine + tests, then AI
6. Hold'em presentation
7. Attract mode, service menu, persistence, RetroPie integration
8. Final on-device perf pass against the budget

# WORKING RULES
- Profile before optimizing, and measure on the actual Pi before declaring
  anything done or blocked. If a technique misses budget, try at least two
  alternatives (lower internal res, fewer blur taps, baked glow sprites) before
  cutting it
- No outcome manipulation of any kind: no near-miss engineering, no adaptive
  odds, no "due for a win" logic
- Keep the code modular: `engine/`, `games/draw/`, `games/holdem/`, `ai/`,
  `render/`, `audio/`, `platform/`

---

# Adaptations for the actual cabinet (agreed 2026-09-23)

The game runs on the owner's existing RetroPie arcade cabinet, not a fresh
Pi OS Lite install. Measured facts about that machine:

| | |
|---|---|
| board | Raspberry Pi 4 Model B rev 1.5, 4x Cortex-A72 at 1.8 GHz, 905 MB RAM |
| OS | Debian 13 "trixie" aarch64 (Raspberry Pi kernel 6.18), RetroPie with EmulationStation + RetroArch |
| display | **3440x1440 ultrawide** on HDMI-A-2 (also offers 1920x1080 and 1720x1440); KMS/DRM, no desktop |
| already present | gcc, cmake 3.31, git, libgles2-mesa-dev, libegl-dev, libgbm-dev, libdrm-dev, libasound2-dev |
| boot config | `dtoverlay=vc4-kms-v3d` already set; `max_framebuffers=2` |
| memory in use | EmulationStation stays resident (~470 MB used, ~430 MB available) |

1. **Display.** The game renders at 1280x720 as specified and is shown 16:9 in
   the middle of the ultrawide (2560x1440, an exact 2x scale), with honeycomb /
   neon side art filling the edges (440 px each side). The "native 1080p"
   option becomes a "native 1440p play area" option under the same >= 30%
   headroom rule. On a plain 16:9 display the side art simply is not visible.
2. **OS.** `install.sh` must work on this Debian 13 RetroPie box and on Pi OS
   Lite Bookworm. It only adds `/boot/firmware/config.txt` lines that are
   missing (this box needs none), always backing the file up first.
3. **RetroPie.** The game is a Ports entry and returns to EmulationStation on
   exit. EmulationStation stays resident while it runs, so the <= 150 MB RSS
   budget matters.
4. **On-device testing** may take over the cabinet display (stopping
   EmulationStation or a running game) whenever needed, always restoring
   EmulationStation afterwards.
