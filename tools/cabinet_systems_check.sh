#!/usr/bin/env bash
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md
#
# cabinet_systems_check.sh - the on-display checks of the systems milestone,
# for when the cabinet is free: the RetroPie round trip (launch the installed
# port through its launcher and runcommand, a scripted few seconds, exit with
# COIN+START, EmulationStation back), cold start to attract, and RSS in every
# mode. Run it ON the Pi, after ./install.sh, detached from SSH:
#
#   setsid -f nohup tools/cabinet_systems_check.sh > out/systems_check.log 2>&1 < /dev/null
#
# Safety: it takes /tmp/bpl-display.lock (and gives up if someone holds it),
# and it REFUSES to run while RetroArch or runcommand is running - it never
# stops a game. It stops only EmulationStation, and always restarts it
# (getty@tty1) and releases the lock on the way out. It touches no other
# port's files; the only file it changes is this port's own emulators.cfg,
# which it backs up and restores. Measurement runs use a scratch save
# directory, so the real credits are not spent.
set -u

PREFIX=${PREFIX:-/opt/beese-poker}
BIN="$PREFIX/beese-poker"
LAUNCHER="$HOME/RetroPie/roms/ports/Beese's Poker Lounge.sh"
CFG=/opt/retropie/configs/ports/beese-poker/emulators.cfg
LOCK=/tmp/bpl-display.lock
ES_PAT='supplementary/emulationstation/emulationstation$'
OUT="${OUT:-$PWD/out}"
mkdir -p "$OUT"

if ! mkdir "$LOCK" 2> /dev/null; then
    echo "display busy: $(cat "$LOCK/owner" 2> /dev/null)"
    exit 1
fi
echo systems > "$LOCK/owner"

restore() {
    [ -f "$CFG.check-bak" ] && mv -f "$CFG.check-bak" "$CFG"
    if ! pgrep -f "$ES_PAT" > /dev/null; then
        sudo systemctl restart getty@tty1
        for _ in $(seq 1 100); do pgrep -f "$ES_PAT" > /dev/null && break; sleep 0.2; done
    fi
    if pgrep -f "$ES_PAT" > /dev/null; then echo "EmulationStation: running"; else echo "WARNING: EmulationStation did not come back"; fi
    rm -rf "$LOCK"
}
trap restore EXIT
trap 'exit 130' INT TERM HUP

if pgrep -f '/opt/retropie/emulators/retroarch/bin/retroarch|runcommand.sh' > /dev/null; then
    echo "a game is running - not touching the display"
    exit 1
fi
[ -x "$BIN" ] || { echo "not installed: $BIN (run ./install.sh first)"; exit 1; }

rm -f /tmp/es-restart
pkill -f "$ES_PAT"
for _ in $(seq 1 100); do pgrep -f "$ES_PAT" > /dev/null || break; sleep 0.1; done

# 1. Cold start to attract, from a dropped page cache.
sync
echo 3 | sudo tee /proc/sys/vm/drop_caches > /dev/null
XDG_DATA_HOME=/tmp/bpl-check-save "$BIN" --frames 240 2>&1 | grep -E "STARTUP|SESSION|beese-poker:" | tee "$OUT/check_coldstart.txt"

# 2. RSS per mode (rss_kb is sampled every 30 frames in the CSV).
run_mode() {
    local name="$1"; shift
    XDG_DATA_HOME=/tmp/bpl-check-save "$BIN" --lockstep --frames 1500 --perf-csv "$OUT/check_$name.csv" "$@" 2>&1 \
        | grep -E "beese-poker:|SAVE:" | sed "s/^/$name: /"
    awk -F, -v n="$name" 'NR > 1 && $9 > m { m = $9 } END { printf "%s: peak RSS %.1f MB\n", n, m / 1024 }' "$OUT/check_$name.csv"
}
run_mode attract
run_mode menu --mode menu
run_mode draw --mode draw --script "30:BET_MAX;120:HOLD1;130:DEAL;400:BET_MAX;500:DEAL;800:BET_MAX;900:DEAL"
run_mode holdem --mode holdem --script "30:DEAL;300:HOLD2;500:HOLD2;700:HOLD2;900:HOLD2;1100:HOLD2;1300:HOLD2"
run_mode service --mode service --script "30:HOLD1;40:HOLD1;50:HOLD1;60:DEAL"
rm -rf /tmp/bpl-check-save

# 3. The RetroPie round trip: the launcher, through runcommand, as ES runs it
#    (bash <rom>), with this port's command made scripted for the test:
#    attract, DEAL to the menu, then COIN+START (RetroPie's exit) at tick 300.
if [ -f "$LAUNCHER" ] && [ -f "$CFG" ]; then
    cp -p "$CFG" "$CFG.check-bak"
    # runcommand evals the command, so the script's ';' must be quoted.
    printf '%s\n' "beese-poker-check = \"$BIN --script '120:DEAL;300-320:COIN+START' --frames 1200\"" \
                  'default = "beese-poker-check"' > "$CFG"
    t0=$(date +%s.%N)
    bash "$LAUNCHER" < /dev/null
    rc=$?
    t1=$(date +%s.%N)
    mv -f "$CFG.check-bak" "$CFG"
    echo "round trip: launcher exit $rc after $(echo "$t1 - $t0" | bc) s"
    grep -E "STARTUP|SAVE|beese-poker:" /dev/shm/runcommand.log 2> /dev/null | sed 's/^/runcommand.log: /'
else
    echo "round trip skipped: $LAUNCHER or $CFG missing"
fi
# restore() brings EmulationStation back and releases the lock.
