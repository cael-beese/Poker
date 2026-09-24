#!/usr/bin/env bash
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md
#
# cabinet_run.sh - run a command on the cabinet's real display, then give the
# display back to EmulationStation. Run it ON the Pi (as pi, which has
# passwordless sudo), detached from the SSH session:
#
#   setsid -f nohup tools/cabinet_run.sh ./build-drm/platform/beese-poker --frames 600 \
#       > run.log 2>&1 < /dev/null
#
# What it does:
#   1. refuses (exit 3) if a game is running: RetroArch or runcommand. It
#      never stops someone's game; ask the owner to quit it first;
#   2. stops EmulationStation. /tmp/es-restart is removed first so the ES
#      wrapper loop does not relaunch ES, and it waits until ES has stayed
#      gone for 3 s (the display is free);
#   3. runs the command (give it --frames or wrap it in `timeout`);
#   4. always, even on failure or Ctrl-C: `sudo systemctl restart getty@tty1`,
#      which autologs tty1 in and starts EmulationStation again, then checks
#      that ES is running.
# Never use openvt for this: it leaves ES on the wrong VT.
set -u

RA_PAT='^/opt/retropie/emulators/retroarch/bin/retroarch'
RC_PAT='^bash /opt/retropie/supplementary/runcommand/runcommand.sh'
ES_PAT='supplementary/emulationstation/emulationstation$'

game_running() {
    pgrep -f "$RA_PAT" > /dev/null || pgrep -f "$RC_PAT" > /dev/null
}

stop_frontend() {
    rm -f /tmp/es-restart
    # Signalled on every pass, not once: right after a restart of tty1 (the
    # previous run's restore) the autologin is still starting ES, and a single
    # pkill can land before ES exists. So it counts as stopped only after 3 s
    # without it.
    local clear=0
    for _ in $(seq 1 300); do
        if game_running; then
            echo "cabinet_run: a game was started meanwhile; giving up" >&2
            return 1
        fi
        pkill -f "$ES_PAT"
        sleep 0.1
        if pgrep -f "$ES_PAT" > /dev/null; then clear=0; else clear=$((clear + 1)); fi
        [ "$clear" -ge 30 ] && return 0
    done
    echo "cabinet_run: EmulationStation did not stop" >&2
    return 1
}

restore_frontend() {
    sudo systemctl restart getty@tty1
    for _ in $(seq 1 100); do
        if pgrep -f "$ES_PAT" > /dev/null; then
            echo "cabinet_run: EmulationStation is running again"
            return 0
        fi
        sleep 0.2
    done
    echo "cabinet_run: WARNING EmulationStation did not come back" >&2
    return 1
}

if [ "$#" -eq 0 ]; then
    echo "usage: $0 COMMAND [ARGS...]" >&2
    exit 2
fi
if game_running; then
    echo "cabinet_run: a game is running; not taking over the display" >&2
    exit 3
fi

trap restore_frontend EXIT
trap 'exit 130' INT TERM HUP
stop_frontend || exit 1
"$@"
rc=$?
echo "cabinet_run: command exited with $rc"
exit "$rc"
