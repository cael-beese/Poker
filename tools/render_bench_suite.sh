#!/usr/bin/env bash
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md
#
# render_bench_suite.sh - the renderer's GPU costs on the Pi WITHOUT taking
# over the display: build-drm/render/render-bench renders the RENDERTEST
# scenes headless on the GPU's render node and reports GPU ms per frame.
# Run it from the repository root on the Pi when the cabinet is idle (it
# shares the GPU with EmulationStation for ~3 minutes). It refuses to start,
# and each step stops, while a game (RetroArch / runcommand) is running.
# Output: out/render_bench.txt
set -u
cd "$(dirname "$0")/.." || exit 1
B=build-drm/render/render-bench
mkdir -p out
O=out/render_bench.txt
game() { pgrep -f '^/opt/retropie/emulators/retroarch/bin/retroarch' > /dev/null || pgrep -f 'runcommand.sh' > /dev/null; }
{
echo "== $(date) render-bench (headless, render node)"
game && { echo "a game is running: not measuring"; exit 0; }
echo "-- jackpot layers (skip masks: 0 all, 128 no bloom, 2 no game layer, 4 no beams, 8 no particles, 16 no banner/meter, 32 no bulbs, 64 no marquee, 255 nothing)"
$B --scene 1 --profile 0,128,2,4,8,16,32,64,255 --seg 480 --frames 4320
for q in 1 2; do
    game && { echo "a game started: stopping"; exit 0; }
    echo "-- jackpot, bloom quality $q (1 = 1/8, 2 = 1/4 5-tap)"
    $B --scene 1 --quality $q --frames 540
done
for fx in particles shake hitpause marquee_flicker bulb_chase shimmer card_specular; do
    game && { echo "a game started: stopping"; exit 0; }
    echo "-- jackpot, $fx off"
    $B --scene 1 --fx "$fx=off" --frames 540
done
for n in 500 1000 2000; do
    game && { echo "a game started: stopping"; exit 0; }
    echo "-- particle stress $n"
    $B --scene 4 --particles "$n" --frames 360
done
echo "-- showcase (deal, hold, all four win tiers)"
$B --scene 0 --frames 1200
echo "== done $(date)"
} > "$O" 2>&1
echo "render_bench_suite: see $O"
