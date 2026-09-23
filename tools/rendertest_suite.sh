#!/usr/bin/env bash
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md
#
# rendertest_suite.sh - the renderer's on-display measurements on the cabinet:
# RENDERTEST's jackpot celebration for 60 s with every effect on, then with
# each effect off in turn, the bloom alternatives, the particle stress scene
# at 500 / 1000 / 2000, and one --gpu-finish run for true GPU times.
#
# Run it ON the Pi from the repository root, only when the owner has agreed
# that the display may be taken over. It never stops a game: if RetroArch /
# runcommand is running it refuses to start. It takes the shared display lock
# (/tmp/bpl-display.lock), runs everything under ONE takeover through
# tools/cabinet_run.sh (EmulationStation is restored afterwards, always),
# and writes out/suite/<name>.csv plus out/suite/summary.txt.
#
#   setsid -f nohup tools/rendertest_suite.sh > out/suite.log 2>&1 < /dev/null
#
# SECONDS_PER_RUN (default 60) shortens it for a smoke test. A keyboard-only
# config is used, so the cabinet's buttons cannot change a scene mid-run.
set -u
cd "$(dirname "$0")/.." || exit 1
BIN=build-drm/platform/beese-poker
OUT=out/suite
SECS=${SECONDS_PER_RUN:-60}
FRAMES=$(( (SECS + 1) * 60 ))
mkdir -p "$OUT"

if pgrep -f '^/opt/retropie/emulators/retroarch/bin/retroarch' > /dev/null || pgrep -f 'runcommand.sh' > /dev/null; then
    echo "rendertest_suite: a game is running; not taking over the display" >&2
    exit 3
fi
if ! mkdir /tmp/bpl-display.lock 2>/dev/null; then
    echo "rendertest_suite: display busy ($(cat /tmp/bpl-display.lock/owner 2>/dev/null))" >&2
    exit 4
fi
echo render-suite > /tmp/bpl-display.lock/owner
trap 'rm -rf /tmp/bpl-display.lock' EXIT

# Keyboard-only input.
cat > "$OUT/bench.ini" <<'EOF'
[input]
HOLD1 = KEY_ONE
HOLD2 = KEY_TWO
HOLD3 = KEY_THREE
HOLD4 = KEY_FOUR
HOLD5 = KEY_FIVE
DEAL = KEY_ENTER
BET_ONE = KEY_A
BET_MAX = KEY_S
CASH_OUT = KEY_Q
SERVICE = KEY_F2
UP = KEY_UP
DOWN = KEY_DOWN
LEFT = KEY_LEFT
RIGHT = KEY_RIGHT
OK = KEY_SPACE
BACK = KEY_BACKSPACE
COIN = KEY_INSERT
START = KEY_HOME
DEBUG = KEY_F1
EXIT = KEY_ESCAPE
SLIDER = NONE
TOUCH = off
EOF

# name | extra arguments. Script ticks: RIGHT at 1 = the jackpot scene.
RUNS=$(cat <<EOF
jackpot_all|--script 1:RIGHT
jackpot_no_bloom|--script 1:RIGHT --fx bloom=off
jackpot_no_particles|--script 1:RIGHT --fx particles=off
jackpot_no_shake|--script 1:RIGHT --fx shake=off
jackpot_no_hitpause|--script 1:RIGHT --fx hitpause=off
jackpot_no_marquee_flicker|--script 1:RIGHT --fx marquee_flicker=off
jackpot_no_bulb_chase|--script 1:RIGHT --fx bulb_chase=off
jackpot_no_shimmer|--script 1:RIGHT --fx shimmer=off
jackpot_no_card_specular|--script 1:RIGHT --fx card_specular=off
jackpot_bloom_q8|--script 1:RIGHT;3:UP
jackpot_bloom_q4_fast|--script 1:RIGHT;3:UP;5:UP
particles_500|--script 1:RIGHT;3:RIGHT;5:RIGHT;7:RIGHT;9:DOWN
particles_1000|--script 1:RIGHT;3:RIGHT;5:RIGHT;7:RIGHT
particles_2000|--script 1:RIGHT;3:RIGHT;5:RIGHT;7:RIGHT;9:UP
jackpot_gpu_finish|--script 1:RIGHT --gpu-finish
EOF
)

cat > "$OUT/run_all.sh" <<EOF
#!/usr/bin/env bash
set -u
while IFS='|' read -r name args; do
    [ -n "\$name" ] || continue
    echo "== \$name"
    # shellcheck disable=SC2086
    $BIN --config $OUT/bench.ini --mode rendertest --frames $FRAMES --perf-csv $OUT/\$name.csv \$args \
        > $OUT/\$name.log 2>&1
done <<'LIST'
$RUNS
LIST
EOF
chmod +x "$OUT/run_all.sh"

tools/cabinet_run.sh "$OUT/run_all.sh"

{
    echo "rendertest suite, $(date), ${SECS} s per run"
    while IFS='|' read -r name args; do
        [ -n "$name" ] || continue
        echo "== $name ($args)"
        grep -E '^(RENDER|STARTUP|beese-poker:)' "$OUT/$name.log" | sed 's/^/   /'
        [ -f "$OUT/$name.csv" ] && python3 tools/perf_summary.py "$OUT/$name.csv" --skip 120 | sed 's/^/   /'
    done <<< "$RUNS"
} > "$OUT/summary.txt"
echo "rendertest_suite: done, see $OUT/summary.txt"
