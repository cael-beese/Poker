/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* engine/step.h - fixed-timestep accumulator for the 60 Hz logic.
 *
 * PLACEHOLDER written by the platform branch (CONTRACT.md names the fixed
 * step but does not declare its API). The engine agent owns engine/; if its
 * step.h differs, platform/main.c is the only caller to adapt.
 *
 * It is pure: the caller measures wall time and feeds it in, so nothing here
 * reads a clock. Time is kept exactly as nanoseconds * hz, which makes a tick
 * exactly 1e9 units long and avoids drift from rounding 1/60 s. */
#ifndef BPL_ENGINE_STEP_H
#define BPL_ENGINE_STEP_H

#include <stdint.h>

#define STEP_HZ 60

typedef struct {
    int64_t  acc;        /* unspent time, in ns * hz                          */
    int      hz;         /* ticks per second                                  */
    int      max_ticks;  /* cap per advance so a stall cannot spiral          */
    uint64_t ticks;      /* ticks handed out so far                           */
} Stepper;

static inline void step_init(Stepper *s, int hz, int max_ticks)
{
    s->acc = 0;
    s->hz = hz;
    s->max_ticks = max_ticks;
    s->ticks = 0;
}

/* Adds elapsed_ns of wall time and returns how many ticks to run now. When
 * more than max_ticks are due the surplus is dropped: the game slows down
 * instead of trying to catch up forever after a long stall. */
static inline int step_advance(Stepper *s, int64_t elapsed_ns)
{
    const int64_t tick = 1000000000LL;
    if (elapsed_ns < 0) elapsed_ns = 0;
    s->acc += elapsed_ns * s->hz;
    int64_t n = s->acc / tick;
    if (n > s->max_ticks) {
        n = s->max_ticks;
        s->acc = 0;
    } else {
        s->acc -= n * tick;
    }
    s->ticks += (uint64_t)n;
    return (int)n;
}

/* Fraction of the next tick already elapsed, 0..1, for render interpolation. */
static inline float step_alpha(const Stepper *s)
{
    return (float)((double)s->acc / 1e9);
}

#endif
