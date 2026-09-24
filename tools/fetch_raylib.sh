#!/usr/bin/env bash
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md
# fetch_raylib.sh - download raylib 5.5 and build it as a static library.
#
# usage: tools/fetch_raylib.sh [VARIANT...]
#   VARIANT   desktop     PLATFORM_DESKTOP (GLFW, OpenGL 3.3) - WSL / Xvfb development
#             drm         PLATFORM_DRM, OpenGL ES 2.0          - the Pi (default on aarch64)
#             drm-gles3   PLATFORM_DRM, OpenGL ES 3.0          - the Pi, alternative
#   With no argument: "drm" on aarch64/armv7, "desktop" everywhere else.
#
# Result: third_party/raylib/<VARIANT>/{include,lib/libraylib.a,BUILD_INFO}
# which platform/CMakeLists.txt picks up. The tarball is checked against a
# pinned SHA-256. A variant that is already built is skipped (delete its
# directory to rebuild). BUILD_INFO records the flags and the build time.
set -euo pipefail

VER=5.5
URL="https://github.com/raysan5/raylib/archive/refs/tags/${VER}.tar.gz"
SHA256="aea98ecf5bc5c5e0b789a76de0083a21a70457050ea4cc2aec7566935f5e258e"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TP="$ROOT/third_party"
TARBALL="$TP/raylib-${VER}.tar.gz"
SRC="$TP/raylib-${VER}"

if [ "$#" -eq 0 ]; then
    case "$(uname -m)" in
        aarch64|armv7l|armv6l) set -- drm ;;
        *) set -- desktop ;;
    esac
fi

mkdir -p "$TP"

if [ ! -f "$TARBALL" ]; then
    echo "fetch_raylib: downloading $URL"
    curl -fsSL --retry 3 -o "$TARBALL.part" "$URL"
    mv "$TARBALL.part" "$TARBALL"
fi

got="$(sha256sum "$TARBALL" | cut -d' ' -f1)"
if [ "$got" != "$SHA256" ]; then
    echo "fetch_raylib: SHA-256 mismatch for $TARBALL" >&2
    echo "  expected $SHA256" >&2
    echo "  got      $got" >&2
    exit 1
fi

if [ ! -d "$SRC" ]; then
    tar -xzf "$TARBALL" -C "$TP"
fi

JOBS="$(nproc 2>/dev/null || echo 2)"
# A change to any patch rebuilds every variant.
PATCH_SUM="$(cat "$ROOT"/tools/raylib-${VER}-*.patch 2>/dev/null | sha256sum | cut -c1-16)"

build_variant() {
    local variant="$1" platform graphics extra_cflags=""
    case "$variant" in
        desktop)   platform=PLATFORM_DESKTOP; graphics=GRAPHICS_API_OPENGL_33 ;;
        drm)       platform=PLATFORM_DRM;     graphics=GRAPHICS_API_OPENGL_ES2 ;;
        drm-gles3) platform=PLATFORM_DRM;     graphics=GRAPHICS_API_OPENGL_ES3 ;;
        *) echo "fetch_raylib: unknown variant '$variant'" >&2; exit 2 ;;
    esac
    # raylib's Makefile adds -O only for its desktop, web and Android
    # platforms, so PLATFORM_DRM came out at -O0; ask for it explicitly.
    extra_cflags="-O2"
    # Building natively on the Pi, so tune for the CPU we are on.
    case "$(uname -m)" in
        aarch64) extra_cflags="$extra_cflags -mcpu=native" ;;
    esac

    local out="$TP/raylib/$variant"
    # Rebuilt when the patches or the flags change.
    if [ -f "$out/lib/libraylib.a" ] && grep -qx "patches $PATCH_SUM" "$out/BUILD_INFO" 2>/dev/null \
        && grep -qxF "custom_cflags $extra_cflags" "$out/BUILD_INFO"; then
        echo "fetch_raylib: $variant already built ($out)"
        return
    fi
    rm -rf "$out"

    # Each variant builds in its own copy of src/, because raylib's Makefile
    # puts objects next to the sources and the variants use different flags.
    local work="$TP/raylib-build-$variant"
    rm -rf "$work"
    cp -a "$SRC/src" "$work"

    # Our fixes to raylib, each explained at the top of its patch file.
    local p
    for p in "$ROOT"/tools/raylib-${VER}-*.patch; do
        [ -f "$p" ] || continue
        echo "fetch_raylib: applying $(basename "$p")"
        patch -d "$work" -p1 --quiet --no-backup-if-mismatch < "$p"
    done

    # A cabinet must not write files when someone presses F12 / Ctrl+F12, so
    # raylib's screenshot and GIF hotkeys are compiled out. The game has its
    # own --shot option for captures.
    sed -i -e 's|^\([[:space:]]*\)#define SUPPORT_SCREEN_CAPTURE[[:space:]].*|\1// SUPPORT_SCREEN_CAPTURE disabled by fetch_raylib.sh|' \
           -e 's|^\([[:space:]]*\)#define SUPPORT_GIF_RECORDING[[:space:]].*|\1// SUPPORT_GIF_RECORDING disabled by fetch_raylib.sh|' \
           "$work/config.h"

    echo "fetch_raylib: building $variant ($platform, $graphics) with -j$JOBS"
    local t0 t1
    t0="$(date +%s.%N)"
    make -C "$work" -j"$JOBS" \
        PLATFORM="$platform" GRAPHICS="$graphics" \
        RAYLIB_LIBTYPE=STATIC RAYLIB_BUILD_MODE=RELEASE \
        RAYLIB_RELEASE_PATH="$work" RAYLIB_SRC_PATH="$work" \
        CUSTOM_CFLAGS="$extra_cflags" > "$work/build.log" 2>&1 || {
            tail -40 "$work/build.log" >&2
            echo "fetch_raylib: build failed, log in $work/build.log" >&2
            exit 1
        }
    t1="$(date +%s.%N)"

    mkdir -p "$out/lib" "$out/include"
    cp "$work/libraylib.a" "$out/lib/"
    cp "$work/raylib.h" "$work/raymath.h" "$work/rlgl.h" "$out/include/"
    {
        echo "raylib $VER"
        echo "variant $variant"
        echo "platform $platform"
        echo "graphics $graphics"
        echo "custom_cflags $extra_cflags"
        echo "patches $PATCH_SUM"
        echo "jobs $JOBS"
        echo "host $(uname -srm)"
        printf 'build_seconds %.1f\n' "$(echo "$t1 - $t0" | bc -l 2>/dev/null || awk "BEGIN{print $t1-$t0}")"
    } > "$out/BUILD_INFO"
    cat "$out/BUILD_INFO"
}

for v in "$@"; do
    build_variant "$v"
done
