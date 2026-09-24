#!/bin/bash
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md
#
# readme_shots.sh - the screenshots in README.md, as JPEGs in
# docs/screenshots/. Desktop build under Xvfb (never the cabinet); every
# frame is the real game from a seed and a --script of button presses, like
# tools/draw_shots.sh, which this runs first for the Draw Poker scenes.
#
#   tools/readme_shots.sh [build-dir]        (default: build)
#
# Needs xvfb-run and ImageMagick's convert.
set -euo pipefail
cd "$(dirname "$0")/.."
B=${1:-build}
BIN="$B/platform/beese-poker"
T=out/readme
D=docs/screenshots
mkdir -p "$T" "$D"
[ -x "$BIN" ] || { echo "no $BIN - build first" >&2; exit 1; }

tools/draw_shots.sh "$B" "$T" > "$T/draw_shots.log"

run() {    # run <name> <beese-poker args...>
    local name=$1; shift
    export XDG_DATA_HOME
    XDG_DATA_HOME=$(mktemp -d)
    xvfb-run -a -s "-screen 0 3440x1440x24" "$BIN" --lockstep "$@" > "$T/$name.log" 2>&1 < /dev/null || true
    rm -rf "$XDG_DATA_HOME"
}

# The menu with CONTROLS chosen, and the CONTROLS page with HOLD 2 held.
run controls --mode menu --frames 200 \
    --script "10:BET_ONE;14:BET_ONE;18:BET_ONE;22:BET_ONE;30:DEAL;100-190:HOLD2" \
    --shot "26:$T/menu_controls.png,150:$T/controls.png"
# Hold'em: the first decision; then CHECK / CALL (HOLD 2) every second, so
# the hand plays on to the flop, turn and river.
C="10:DEAL"; for i in $(seq 0 24); do C="$C;$((420 + i * 60)):HOLD2"; done
run holdem --mode holdem --seed 5EED --frames 1900 --script "$C" \
    --shot "400:$T/holdem_turn.png,1450:$T/holdem_later.png,1880:$T/holdem_later2.png"
# The Hold'em hand-of-the-night takeover (the perf test hook).
BPL_HOLDEM_TAKEOVER=1 run holdem_takeover --mode holdem --frames 400 --script "10:DEAL" \
    --shot "300:$T/holdem_takeover.png,360:$T/holdem_takeover2.png"
# The LEARN PANEL wizard, from the service menu (14 lines down, DEAL).
S=""; for i in $(seq 0 13); do S="$S;$((10 + i * 4)):HOLD2"; done
run learn --mode service --frames 120 --script "${S#;};80:DEAL" --shot "110:$T/learn_panel.png"
# The whole cabinet screen: 3440x1440, the play space 2x with the side art.
run cabinet --size 3440x1440 --mode draw --seed 107CA4 --frames 200 \
    --script "30:DEAL;80:HOLD1;84:HOLD2;88:HOLD3;92:HOLD4;96:HOLD5;110:DEAL" --shot "190:screen:$T/cabinet.png"

jpg() {    # jpg <source.png> <name> [resize]
    convert "$1" ${3:+-resize "$3"} -strip -quality 86 "$D/$2.jpg"
}
jpg "$T/attract_title.png" attract
jpg "$T/menu.png" menu
jpg "$T/controls.png" controls
jpg "$T/hint.png" draw-hint
jpg "$T/royal_takeover.png" royal-flush
jpg "$T/royal_count.png" royal-count
jpg "$T/double_guess.png" double-up
jpg "$T/deuces_jackpot.png" deuces-wild
jpg "$T/holdem_turn.png" holdem
jpg "$T/holdem_later2.png" holdem-showdown
jpg "$T/holdem_takeover.png" holdem-takeover
jpg "$T/learn_panel.png" learn-panel
jpg "$T/cabinet.png" cabinet 1720x720
ls -la "$D"
