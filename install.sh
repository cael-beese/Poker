#!/usr/bin/env bash
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md
#
# install.sh - build Beese's Poker Lounge on a Raspberry Pi and install it,
# with a RetroPie "Ports" entry when RetroPie is there.
#
#   ./install.sh               install (or update: it is safe to run again)
#   ./install.sh --dry-run     print what it would do, change nothing
#   ./install.sh --uninstall   remove the program and the Ports entry
#                              (--purge also deletes credits / stats / settings)
#
# Options: --prefix DIR (default /opt/beese-poker), --no-deps, --no-boot-config.
#
# Run it as the user who plays (pi on RetroPie), from the source tree; it
# uses sudo for the steps that need root. Works on Debian 13 RetroPie and
# Raspberry Pi OS Lite (Bookworm), 64-bit.
#
# What it does:
#   1. installs the build dependencies that are missing (apt)
#   2. builds raylib for KMS/DRM (tools/fetch_raylib.sh drm; skipped if built)
#   3. builds the game: -DBPL_PLATFORM=DRM -DBPL_NATIVE=ON, in build-drm/
#   4. installs PREFIX/beese-poker, PREFIX/assets/ and PREFIX/config.ini (an
#      existing config.ini is kept; the new default goes next to it as
#      config.ini.default)
#   5. RetroPie: ~/RetroPie/roms/ports/Beese's Poker Lounge.sh, which starts
#      the game through runcommand like every other port, and
#      /opt/retropie/configs/ports/beese-poker/emulators.cfg, which tells
#      runcommand what to run. The ROM folder may be a vfat USB stick, so the
#      launcher is a plain file (no symlink) and does not rely on the
#      executable bit: EmulationStation runs ports with `bash <file>`.
#   6. /boot/firmware/config.txt (or /boot/config.txt on older layouts): the
#      game needs the full KMS driver (dtoverlay=vc4-kms-v3d) and enough CMA
#      for its buffers. Only MISSING lines are added, after a timestamped
#      backup, and every change is printed. An explicit setting that is
#      already there is never edited. Nothing is added when KMS is on and no
#      explicit CMA is set, because the overlay's own default CMA is enough.
# Saves live in ~/.local/share/beese-poker (the SD card's ext4 root, NOT the
# vfat ROM stick: see docs/SYSTEMS.md). Uninstalling keeps them unless --purge.
set -euo pipefail

PREFIX=/opt/beese-poker
PORT_ID=beese-poker
LAUNCHER_NAME="Beese's Poker Lounge.sh"
DRY=0
UNINSTALL=0
PURGE=0
DO_DEPS=1
DO_BOOT=1

usage() { sed -n '4,16p' "$0" | sed 's/^# \{0,1\}//'; }

while [ $# -gt 0 ]; do
    case "$1" in
        --dry-run) DRY=1 ;;
        --uninstall) UNINSTALL=1 ;;
        --purge) PURGE=1 ;;
        --prefix) PREFIX="${2:?--prefix needs a directory}"; shift ;;
        --no-deps) DO_DEPS=0 ;;
        --no-boot-config) DO_BOOT=0 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "install.sh: unknown option '$1'" >&2; usage >&2; exit 2 ;;
    esac
    shift
done

ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD="$ROOT/build-drm"

# The user the game belongs to (the one whose home has RetroPie/ and saves).
if [ "$(id -u)" -eq 0 ] && [ -n "${SUDO_USER:-}" ] && [ "$SUDO_USER" != root ]; then
    USER_NAME="$SUDO_USER"
else
    USER_NAME="$(id -un)"
fi
USER_HOME="$(getent passwd "$USER_NAME" | cut -d: -f6)"
USER_HOME="${USER_HOME:-$HOME}"
if [ "$(id -u)" -eq 0 ]; then SUDO=""; else SUDO="sudo"; fi

say()  { printf '%s\n' "$*"; }
step() { printf '\n== %s\n' "$*"; }
# Every command that changes the system goes through run / run_root.
run() {
    if [ "$DRY" -eq 1 ]; then printf '  would run: %s\n' "$*"; else "$@"; fi
}
run_root() {
    if [ "$DRY" -eq 1 ]; then printf '  would run (root): %s\n' "$*"; else $SUDO "$@"; fi
}
as_user() {
    if [ "$(id -un)" = "$USER_NAME" ]; then run "$@"
    elif [ "$(id -u)" -eq 0 ]; then run runuser -u "$USER_NAME" -- "$@"
    else run sudo -u "$USER_NAME" "$@"; fi
}
# write_file OWNER PATH CONTENT: only when the content differs.
write_file() {
    local owner="$1" path="$2" content="$3"
    if [ -f "$path" ] && [ "$(cat "$path")" = "$content" ]; then
        say "  unchanged: $path"
        return 0
    fi
    if [ "$DRY" -eq 1 ]; then
        say "  would write $path:"
        printf '%s\n' "$content" | sed 's/^/    | /'
        return 0
    fi
    if [ "$owner" = root ]; then
        printf '%s\n' "$content" | $SUDO tee "$path" > /dev/null
    else
        printf '%s\n' "$content" > "$path"
    fi
    say "  wrote $path"
}

RETROPIE=0
if [ -d /opt/retropie ] && [ -d "$USER_HOME/RetroPie" ]; then RETROPIE=1; fi
PORTS_DIR="$USER_HOME/RetroPie/roms/ports"
PORT_CFG_DIR="/opt/retropie/configs/ports/$PORT_ID"
SAVE_DIR="$USER_HOME/.local/share/beese-poker"

# ---- uninstall ----------------------------------------------------------------

if [ "$UNINSTALL" -eq 1 ]; then
    step "Uninstalling Beese's Poker Lounge"
    if [ -d "$PREFIX" ]; then run_root rm -rf "$PREFIX"; else say "  not installed in $PREFIX"; fi
    if [ -f "$PORTS_DIR/$LAUNCHER_NAME" ]; then run rm -f "$PORTS_DIR/$LAUNCHER_NAME"; fi
    if [ -d "$PORT_CFG_DIR" ]; then run_root rm -rf "$PORT_CFG_DIR"; fi
    if [ "$PURGE" -eq 1 ]; then
        if [ -d "$SAVE_DIR" ]; then run rm -rf "$SAVE_DIR"; fi
    else
        if [ -d "$SAVE_DIR" ]; then say "  kept the saves in $SAVE_DIR (--purge deletes them)"; fi
    fi
    say "  config.txt is left as it is; its backups are config.txt.bak-* next to it"
    say "  Restart EmulationStation to drop the Ports entry."
    exit 0
fi

# ---- 0. where are we ------------------------------------------------------------

step "System"
ARCH="$(uname -m)"
OS="unknown"
if [ -r /etc/os-release ]; then OS="$(. /etc/os-release; echo "${PRETTY_NAME:-$ID}")"; fi
MODEL="$(tr -d '\0' < /proc/device-tree/model 2>/dev/null || echo 'not a Raspberry Pi')"
say "  $MODEL, $OS, $ARCH; user $USER_NAME ($USER_HOME)"
say "  RetroPie: $([ "$RETROPIE" -eq 1 ] && echo yes || echo no)"
if [ "$DRY" -eq 1 ]; then say "  DRY RUN: nothing will be changed"; fi
case "$ARCH" in
    aarch64|armv7l) ;;
    *) say "  WARNING: not an ARM Pi; the DRM build is meant for the Raspberry Pi" ;;
esac

# ---- 1. dependencies ------------------------------------------------------------

# "a|b": either package will do (names differ between Bookworm and trixie).
DEPS=(build-essential cmake pkg-config curl ca-certificates
      "libgles2-mesa-dev|libgles-dev" libegl-dev libgbm-dev libdrm-dev libasound2-dev)
installed() { dpkg-query -W -f='${Status}' "$1" 2>/dev/null | grep -q "install ok installed"; }
available() { apt-cache show "$1" > /dev/null 2>&1; }

step "Build dependencies"
MISSING=()
for d in "${DEPS[@]}"; do
    IFS='|' read -r -a alts <<< "$d"
    ok=0
    for p in "${alts[@]}"; do if installed "$p"; then ok=1; break; fi; done
    if [ "$ok" -eq 0 ]; then
        pick="${alts[0]}"
        for p in "${alts[@]}"; do if available "$p"; then pick="$p"; break; fi; done
        MISSING+=("$pick")
    fi
done
if [ "${#MISSING[@]}" -eq 0 ]; then
    say "  all present"
elif [ "$DO_DEPS" -eq 0 ]; then
    say "  missing (not installing, --no-deps): ${MISSING[*]}"
else
    say "  missing: ${MISSING[*]}"
    run_root apt-get update
    run_root apt-get install -y --no-install-recommends "${MISSING[@]}"
fi

# ---- 2 + 3. build ---------------------------------------------------------------

step "raylib 5.5 for KMS/DRM"
if [ -f "$ROOT/third_party/raylib/drm/lib/libraylib.a" ]; then
    say "  already built: third_party/raylib/drm (tools/fetch_raylib.sh rebuilds it if its patches changed)"
fi
run "$ROOT/tools/fetch_raylib.sh" drm

step "Building the game"
JOBS="$(nproc 2>/dev/null || echo 4)"
run cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DBPL_PLATFORM=DRM -DBPL_NATIVE=ON
run cmake --build "$BUILD" -j"$JOBS" --target beese-poker
BIN="$BUILD/platform/beese-poker"

# ---- 4. install -------------------------------------------------------------------

step "Installing into $PREFIX"
run_root install -d -m 755 "$PREFIX"
run_root install -m 755 "$BIN" "$PREFIX/beese-poker.new"
run_root mv -f "$PREFIX/beese-poker.new" "$PREFIX/beese-poker"
# Assets: copied beside, then swapped in, so a cut never leaves half a folder.
run_root rm -rf "$PREFIX/assets.new"
run_root cp -r "$ROOT/assets" "$PREFIX/assets.new"
run_root rm -rf "$PREFIX/assets.old"
if [ -d "$PREFIX/assets" ]; then run_root mv "$PREFIX/assets" "$PREFIX/assets.old"; fi
run_root mv "$PREFIX/assets.new" "$PREFIX/assets"
run_root rm -rf "$PREFIX/assets.old"
# config.ini: an edited one is kept (the new default goes beside it as
# config.ini.default); one nobody edited - identical to the default installed
# last time, or to any default this installer has shipped - is updated.
SHIPPED_DEFAULTS="20095b89468d1a5fc6a156499efa4d41 70c195c082b41f5e09d2092edc917a4f
a48c7c874a87d9bf845e6992228f95c6 eba89c2dd6c175a86d9c5e6ba24e40a8"
config_unedited() {
    [ -f "$PREFIX/config.ini.default" ] && cmp -s "$PREFIX/config.ini" "$PREFIX/config.ini.default" && return 0
    local sum
    sum="$(md5sum < "$PREFIX/config.ini" | cut -c1-32)"
    case " $(echo $SHIPPED_DEFAULTS) " in *" $sum "*) return 0 ;; esac
    return 1
}
if [ -f "$PREFIX/config.ini" ] && ! cmp -s "$PREFIX/config.ini" "$ROOT/platform/config.ini" && ! config_unedited; then
    say "  keeping your edited $PREFIX/config.ini; the new default is config.ini.default"
else
    [ -f "$PREFIX/config.ini" ] && ! cmp -s "$PREFIX/config.ini" "$ROOT/platform/config.ini" && say "  updating config.ini (you had not edited it)"
    run_root install -m 644 "$ROOT/platform/config.ini" "$PREFIX/config.ini"
fi
run_root install -m 644 "$ROOT/platform/config.ini" "$PREFIX/config.ini.default"
for f in LICENSE.md CREDITS.md README.md; do
    if [ -f "$ROOT/$f" ]; then run_root install -m 644 "$ROOT/$f" "$PREFIX/$f"; fi
done
as_user mkdir -p "$SAVE_DIR"

# ---- 5. RetroPie Ports --------------------------------------------------------------

step "RetroPie Ports entry"
if [ "$RETROPIE" -eq 1 ]; then
    run mkdir -p "$PORTS_DIR"
    write_file user "$PORTS_DIR/$LAUNCHER_NAME" "#!/bin/bash
\"/opt/retropie/supplementary/runcommand/runcommand.sh\" 0 _PORT_ \"$PORT_ID\" \"\""
    # vfat ignores this (and ES runs ports with bash anyway); ext4 honours it.
    if [ "$DRY" -eq 0 ]; then chmod +x "$PORTS_DIR/$LAUNCHER_NAME" 2> /dev/null || true; fi
    run_root install -d -o "$USER_NAME" -g "$(id -gn "$USER_NAME")" -m 755 "$PORT_CFG_DIR"
    write_file user "$PORT_CFG_DIR/emulators.cfg" "$PORT_ID = \"$PREFIX/beese-poker\"
default = \"$PORT_ID\""
    if ! grep -qs "<name>ports</name>" /etc/emulationstation/es_systems.cfg "$USER_HOME/.emulationstation/es_systems.cfg"; then
        say "  NOTE: EmulationStation has no 'ports' system yet. Install any port from"
        say "        RetroPie-Setup once (it adds the system), or add it to es_systems.cfg."
    fi
    say "  Restart EmulationStation to see \"Beese's Poker Lounge\" under Ports."
else
    say "  RetroPie not found; start the game with: $PREFIX/beese-poker"
fi

# ---- 6. boot configuration ----------------------------------------------------------

step "Boot configuration"
CONFIG=""
for c in /boot/firmware/config.txt /boot/config.txt; do
    if [ -f "$c" ] && ! grep -q "DO NOT EDIT THIS FILE" "$c"; then CONFIG="$c"; break; fi
done
CMDLINE="$(dirname "${CONFIG:-/boot/firmware/config.txt}")/cmdline.txt"
if [ "$DO_BOOT" -eq 0 ]; then
    say "  skipped (--no-boot-config)"
elif [ -z "$CONFIG" ]; then
    say "  no config.txt found (not a Raspberry Pi?) - nothing to do"
else
    say "  $CONFIG"
    ADD=()
    active() { grep -E "^[[:space:]]*$1" "$CONFIG" > /dev/null; }
    if active "dtoverlay=vc4-kms-v3d"; then
        say "  ok: $(grep -E '^[[:space:]]*dtoverlay=vc4-kms-v3d' "$CONFIG" | head -1 | tr -d '[:space:]')"
    elif active "dtoverlay=vc4-fkms-v3d"; then
        say "  WARNING: the legacy fake-KMS driver (vc4-fkms-v3d) is enabled. The game needs full"
        say "           KMS; replace that line with dtoverlay=vc4-kms-v3d yourself (not changed here)."
    else
        ADD+=("dtoverlay=vc4-kms-v3d")
    fi
    CMA_SET=""
    if grep -Eq "^[[:space:]]*dtoverlay=(cma|vc4-kms-v3d[^#]*cma)" "$CONFIG"; then
        CMA_SET="config.txt: $(grep -Eo 'cma(-[0-9]+|=[0-9a-zA-Z]+)?' "$CONFIG" | head -1)"
    elif [ -f "$CMDLINE" ] && grep -Eq "(^| )cma=" "$CMDLINE"; then
        CMA_SET="cmdline.txt: $(grep -Eo 'cma=[^ ]+' "$CMDLINE")"
    fi
    CMA_MB=$(awk '/^CmaTotal:/ { print int($2 / 1024) }' /proc/meminfo 2>/dev/null || echo 0)
    if [ -n "$CMA_SET" ]; then
        say "  ok: CMA set explicitly ($CMA_SET), ${CMA_MB} MB now - left as it is"
        if [ "${CMA_MB:-0}" -lt 128 ]; then say "  WARNING: under 128 MB of CMA; the display buffers may not fit"; fi
    elif [ "${#ADD[@]}" -gt 0 ]; then
        say "  CMA: the kms overlay's default will apply once it is enabled"
    elif [ "${CMA_MB:-0}" -ge 128 ]; then
        say "  ok: CMA is the overlay's default, ${CMA_MB} MB (enough; no line needed)"
    else
        ADD+=("dtoverlay=cma,cma-256")
    fi
    if [ "${#ADD[@]}" -eq 0 ]; then
        say "  no change needed"
    else
        STAMP="$(date +%Y%m%d-%H%M%S)"
        say "  missing: ${ADD[*]}"
        run_root cp -p "$CONFIG" "$CONFIG.bak-$STAMP"
        BLOCK="
# Added by Beese's Poker Lounge install.sh ($STAMP): KMS display for the game.
[all]"
        for l in "${ADD[@]}"; do BLOCK="$BLOCK
$l"; done
        if [ "$DRY" -eq 1 ]; then
            say "  would append to $CONFIG:"
            printf '%s\n' "$BLOCK" | sed 's/^/    + /'
        else
            printf '%s\n' "$BLOCK" | $SUDO tee -a "$CONFIG" > /dev/null
            say "  appended to $CONFIG (backup: $CONFIG.bak-$STAMP):"
            printf '%s\n' "$BLOCK" | sed 's/^/    + /'
            say "  REBOOT for this to take effect."
        fi
    fi
fi

step "Done"
say "  program:  $PREFIX/beese-poker   (config: $PREFIX/config.ini)"
say "  saves:    $SAVE_DIR"
if [ "$RETROPIE" -eq 1 ]; then say "  launcher: $PORTS_DIR/$LAUNCHER_NAME"; fi
exit 0
