/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
#include "step.h"

#define UNITS_PER_TICK 1000000000ll

void step_init(FixedStep *s, int hz, int max_ticks)
{
    s->acc = 0;
    s->hz = hz > 0 ? hz : STEP_DEFAULT_HZ;
    s->snap_ns = STEP_DEFAULT_SNAP_NS;
    s->max_ticks = max_ticks > 0 ? max_ticks : STEP_DEFAULT_MAX_TICKS;
    s->ticks = 0;
    s->dropped = 0;
}

void step_set_snap(FixedStep *s, int64_t snap_ns)
{
    s->snap_ns = snap_ns > 0 ? snap_ns : 0;
}

int step_advance_ns(FixedStep *s, int64_t dt_ns)
{
    int64_t units, n;

    if (dt_ns < 0) dt_ns = 0;
    /* Anything longer than 10 s is a stall, not a frame; clamping it here
       also keeps dt_ns * hz far from overflow. */
    if (dt_ns > 10000000000ll) dt_ns = 10000000000ll;

    units = dt_ns * s->hz;
    if (s->snap_ns > 0) {
        int64_t k;
        for (k = 1; k <= 4; k++) {
            int64_t diff = units - k * UNITS_PER_TICK;
            if (diff < 0) diff = -diff;
            if (diff <= s->snap_ns * s->hz) {
                units = k * UNITS_PER_TICK;
                break;
            }
        }
    }

    s->acc += units;
    n = s->acc / UNITS_PER_TICK;
    s->acc -= n * UNITS_PER_TICK;
    if (n > s->max_ticks) {
        s->dropped += (uint64_t)(n - s->max_ticks);
        n = s->max_ticks;
    }
    s->ticks += (uint64_t)n;
    return (int)n;
}

int step_advance(FixedStep *s, double dt_seconds)
{
    if (!(dt_seconds > 0.0)) return step_advance_ns(s, 0);
    if (dt_seconds > 10.0) dt_seconds = 10.0;
    return step_advance_ns(s, (int64_t)(dt_seconds * 1e9 + 0.5));
}

float step_alpha(const FixedStep *s)
{
    return (float)((double)s->acc / (double)UNITS_PER_TICK);
}
