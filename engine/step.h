/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
#ifndef BPL_ENGINE_STEP_H
#define BPL_ENGINE_STEP_H

#include <stdint.h>

/* Fixed-timestep accumulator: turns wall-clock frame times into a whole
   number of logic ticks to run this frame.

   Time is kept in units of (1 / (hz * 1e9)) s, so one tick is exactly 1e9
   units and a 60 Hz tick (16.666... ms) accumulates with no rounding drift.

   Snapping: a frame time within snap_ns of an exact whole number of ticks
   (1..4) is treated as exactly that many. On a 60 Hz vsynced display the
   measured dt jitters around 16.67 ms; without snapping the accumulator
   sometimes crosses a tick boundary twice in one frame and zero times in the
   next, which shows as judder. 0 disables it.

   Catch-up cap: after a long stall (loading, a debugger, a hiccup on the Pi)
   at most max_ticks run in one frame and the rest of the backlog is dropped,
   so the game slows down briefly instead of spiralling. */
typedef struct {
    int64_t  acc;        /* accumulated time, 1e9 units per tick            */
    int64_t  hz;         /* ticks per second                                */
    int64_t  snap_ns;    /* snap tolerance in ns (0 = off)                  */
    int      max_ticks;  /* catch-up cap per frame                          */
    uint64_t ticks;      /* ticks handed out so far                         */
    uint64_t dropped;    /* ticks discarded by the cap                      */
} FixedStep;

#define STEP_DEFAULT_HZ        60
#define STEP_DEFAULT_MAX_TICKS 4
#define STEP_DEFAULT_SNAP_NS   200000   /* 0.2 ms */

/* hz <= 0 means 60; max_ticks <= 0 means 4. Snapping starts at the default. */
void  step_init(FixedStep *s, int hz, int max_ticks);
void  step_set_snap(FixedStep *s, int64_t snap_ns);

/* Adds one frame's elapsed time and returns how many ticks to run now
   (0..max_ticks). Negative dt counts as 0. */
int   step_advance_ns(FixedStep *s, int64_t dt_ns);
int   step_advance(FixedStep *s, double dt_seconds);

/* How far into the next tick we are, 0..1, for render interpolation. */
float step_alpha(const FixedStep *s);

#endif
