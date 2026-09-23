#!/usr/bin/env python3
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md
"""perf_summary.py - summarise a beese-poker --perf-csv log.

usage: tools/perf_summary.py FILE.csv [--skip N]

Skips the first N frames (default 60: start-up, shader compiles, first
uploads) and prints mean / p50 / p99 / max frame time, the "1% low" fps
(1000 / mean of the slowest 1% of frame times), the mean of every other
column, and how many frames missed a 60 Hz vblank (frame_ms > 18).
"""
import csv
import sys


def pct(sorted_vals, p):
    if not sorted_vals:
        return 0.0
    k = min(len(sorted_vals) - 1, max(0, int(round(p / 100.0 * (len(sorted_vals) - 1)))))
    return sorted_vals[k]


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    skip = 60
    if "--skip" in argv:
        skip = int(argv[argv.index("--skip") + 1])
    with open(argv[1], newline="") as f:
        rows = list(csv.DictReader(f))[skip:]
    if not rows:
        print("no frames after --skip")
        return 1
    ft = sorted(float(r["frame_ms"]) for r in rows)
    n = len(ft)
    worst = ft[-max(1, n // 100):]
    mean = sum(ft) / n
    print(f"{argv[1]}: {n} frames (skipped {skip})")
    print(f"  frame ms: mean {mean:.3f}  p50 {pct(ft, 50):.3f}  p99 {pct(ft, 99):.3f}  max {ft[-1]:.3f}")
    print(f"  fps: mean {1000.0 / mean:.2f}  1% low {1000.0 / (sum(worst) / len(worst)):.2f}")
    print(f"  frames > 18 ms (missed vblank): {sum(1 for v in ft if v > 18.0)}")
    for col in ("logic_ms", "render_ms", "compose_ms", "swap_ms", "draw_calls", "ticks", "rss_kb"):
        if col in rows[0]:
            vals = [float(r[col]) for r in rows]
            print(f"  {col:10s} mean {sum(vals) / n:10.3f}  max {max(vals):10.3f}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
