#!/bin/bash
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md
#
# draw_shots.sh - scripted screenshots of the Draw Poker presentation, on the
# desktop build under Xvfb (never on the cabinet): idle, dealing, holding with
# the strategy hint, a medium win, the royal-flush takeover, the double-up,
# a losing hand, the menu and attract mode.
#
# Nothing is staged: every hand is the real game from a session seed.
# tools/draw_seed_find.c picks the seeds whose first hand happens to deal the
# category wanted (a royal flush for the takeover), from the same seed stream
# the game uses, and the runs are ordinary --seed runs with --script input.
#
#   tools/draw_shots.sh [build-dir] [out-dir]      (defaults: build, out/shots)
#
# Each run gets a fresh save directory, so credits start at 1000. Set
# BPL_FILL_LOG=1 to print the Draw view's fill per frame, BPL_SFX_LOG=1 to
# print every sound it plays.
set -euo pipefail
cd "$(dirname "$0")/.."
B=${1:-build}
O=${2:-out/shots}
BIN="$B/platform/beese-poker"
mkdir -p "$O"
[ -x "$BIN" ] || { echo "no $BIN - build first" >&2; exit 1; }

gcc -std=c11 -D_DEFAULT_SOURCE -O2 -Wall -Wextra -Werror -I. tools/draw_seed_find.c \
    "$B/games/draw/libbpl_draw.a" "$B/engine/libbpl_engine.a" -lpthread -lm -o "$B/draw_seed_find"
ROYAL=$("$B/draw_seed_find" royal)
FULL=$("$B/draw_seed_find" fullhouse)
JACKS=$("$B/draw_seed_find" jacks)
echo "seeds: royal $ROYAL, full house $FULL, jacks $JACKS"

run() {    # run <name> <beese-poker args...>
    local name=$1; shift
    export XDG_DATA_HOME
    XDG_DATA_HOME=$(mktemp -d)
    xvfb-run -a -s "-screen 0 1280x720x24" "$BIN" --lockstep "$@" > "$O/$name.log" 2>&1 || true
    rm -rf "$XDG_DATA_HOME"
    grep -E "^beese-poker:|^DRAWVIEW" "$O/$name.log" | tail -4 | sed "s/^/  $name: /"
}

# The deal is pressed at tick 30: cards leave the shoe at 30..54 and turn at
# 42..66, holds are live from 66. HOLD 1-5 at 80..96, DRAW at 110; the
# result comes 8 ticks after the last flip.
HOLD_ALL="30:DEAL;80:HOLD1;84:HOLD2;88:HOLD3;92:HOLD4;96:HOLD5;110:DEAL"

run royal --seed "$ROYAL" --mode draw --frames 560 --script "$HOLD_ALL" \
    --shot "20:$O/draw_idle.png,46:$O/draw_dealing.png,100:$O/draw_held.png,122:$O/royal_hit.png,190:$O/royal_takeover.png,300:$O/royal_count.png,540:$O/royal_after.png"

run medium --seed "$FULL" --mode draw --frames 260 --script "$HOLD_ALL" \
    --shot "150:$O/medium_win.png,240:$O/medium_offer.png"

# Double-up after a small win: HOLD 1 doubles, HOLD 1 again calls red.
run double --seed "$JACKS" --mode draw --frames 360 --script "$HOLD_ALL;200:HOLD1;262:HOLD1" \
    --shot "150:$O/small_win.png,240:$O/double_offer.png,275:$O/double_guess.png,300:$O/double_reveal.png,340:$O/double_result.png"

# The strategy hint: BET ONE while holding turns it on; hold what it says.
run hint --seed C0FFEE --mode draw --frames 200 --script "30:DEAL;72:BET_ONE;90:HOLD1;140:DEAL" \
    --shot "84:$O/hint.png,98:$O/hint_held.png,190:$O/after_draw.png"

# Deuces Wild through the menu (HOLD 3, DEAL): four deuces dealt, the other
# jackpot. The menu uses no seed, so the Draw game gets the same one as with
# --mode draw. Then a skip: HOLD 3 (no game meaning in the offer) at 300.
DEUCES=$("$B/draw_seed_find" fourdeuces 2)
run deuces --seed "$DEUCES" --mode menu --frames 380 \
    --script "10:HOLD3;20:DEAL;50:DEAL;100:HOLD1;104:HOLD2;108:HOLD3;112:HOLD4;116:HOLD5;130:DEAL;300:HOLD3" \
    --shot "200:$O/deuces_jackpot.png,318:$O/deuces_skipped.png"

# Bonus Poker (11 paytable rows); BET ONE slides the lit column to 1.
run bonus --seed C0FFEE --mode menu --frames 170 --script "10:HOLD2;20:DEAL;50:BET_ONE;100:DEAL" \
    --shot "54:$O/bonus_bet_slide.png,90:$O/bonus_bet1.png,150:$O/bonus_hold.png"

# Draw calls and frame times of the royal flush sequence (desktop numbers:
# llvmpipe, so only the draw calls mean anything).
run royal_perf --seed "$ROYAL" --mode draw --frames 560 --script "$HOLD_ALL" --perf-csv "$O/royal_perf.csv"

run menu --mode menu --frames 120 --script "40:HOLD2;70:HOLD5" --shot "30:$O/menu.png,100:$O/menu_bonus.png"

# Cash out to the menu and come back: a fresh game, a fresh table.
run reenter --seed "$FULL" --mode draw --frames 300 --script "$HOLD_ALL;200:HOLD5;215:CASH_OUT;250:DEAL" \
    --shot "230:$O/cashed_out_menu.png,290:$O/reentered.png"

run attract --mode attract --frames 2500 \
    --shot "150:$O/attract_title.png,520:$O/attract_demo.png,700:$O/attract_demo2.png,2450:$O/attract_holdem.png"
echo "shots in $O"
