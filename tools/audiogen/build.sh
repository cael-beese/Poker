#!/usr/bin/env bash
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md
#
# build.sh - regenerate every audio asset from source, deterministically.
#
#   tools/audiogen/build.sh          render + encode into assets/, then report
#   tools/audiogen/build.sh --check  render into out/ only and compare with
#                                    the committed assets (bit-identical?)
#
# Needs a C compiler, make, and oggenc/oggdec (Debian: vorbis-tools).
# Intermediate WAVs go to out/audiogen/ (gitignored).
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="$ROOT/out/audiogen"
CHECK=0
[ "${1:-}" = "--check" ] && CHECK=1

make -s -C "$HERE"

if [ "$CHECK" = 1 ]; then
    SFX="$OUT/check/sfx"; MUS="$OUT/check/music"
else
    SFX="$ROOT/assets/sfx"; MUS="$ROOT/assets/music"
fi
mkdir -p "$SFX" "$MUS" "$OUT/wav" "$OUT/decoded"

"$HERE/audiogen" "$SFX" "$OUT/wav"

# Fixed serial numbers make the Ogg streams byte-identical from run to run.
# The music is q4 (about 128 kb/s); the ambience is noise-like and quiet, so
# q2 is transparent for it and half the size.
oggenc -Q -q 4 --serial 101 -o "$MUS/lounge_loop.ogg"     "$OUT/wav/lounge_loop.wav"
oggenc -Q -q 4 --serial 102 -o "$MUS/attract_loop.ogg"    "$OUT/wav/attract_loop.wav"
oggenc -Q -q 2 --serial 103 -o "$MUS/ambience_lounge.ogg" "$OUT/wav/ambience_lounge.wav"

if [ "$CHECK" = 1 ]; then
    diff -r "$SFX" "$ROOT/assets/sfx" && diff -r "$MUS" "$ROOT/assets/music" \
        && echo "audiogen: regenerated assets are bit-identical to the committed ones"
    exit
fi

# The report: every SFX, then the decoded loops with the seam check.
for f in "$MUS"/*.ogg; do
    oggdec -Q -o "$OUT/decoded/$(basename "${f%.ogg}").wav" "$f"
done
echo "== sound effects"
"$HERE/analyze" "$SFX"/*.wav
echo "== loops (source WAV, then decoded OGG)"
"$HERE/analyze" --loop "$OUT/wav"/*.wav "$OUT/decoded"/*.wav
echo "== sizes"
du -cb "$SFX"/*.wav "$MUS"/*.ogg | tail -1 | awk '{ printf "total %d bytes (%.2f MB)\n", $1, $1/1048576 }'
