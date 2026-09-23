/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* perf.h - clocks, memory readings, per-frame statistics and the CSV log. */
#ifndef BPL_PLATFORM_PERF_H
#define BPL_PLATFORM_PERF_H

#include <stdint.h>
#include <stddef.h>

int64_t perf_now_ns(void);                 /* CLOCK_MONOTONIC */
double  perf_ms(int64_t from_ns, int64_t to_ns);

/* Milliseconds the process has existed (from exec, via /proc/self/stat
 * starttime and CLOCK_BOOTTIME); -1 if unknown. Used for the cold-start time. */
double  perf_process_age_ms(void);

/* Resident set size in KB from /proc/self/statm; 0 if unknown. */
long    perf_rss_kb(void);

/* One frame's measurements. */
typedef struct {
    uint64_t frame;
    double frame_ms;     /* wall time since the previous frame started          */
    double logic_ms;     /* all ticks run this frame                            */
    double render_ms;    /* present_update + drawing the play space (+ GPU with --gpu-finish) */
    double compose_ms;   /* upscale, side art, overlay (+ GPU with --gpu-finish) */
    double swap_ms;      /* EndDrawing: swap, vsync wait, input poll            */
    unsigned draw_calls;
    int ticks;
} PerfFrame;

/* Rolling window for the overlay. */
#define PERF_WINDOW 120
typedef struct {
    PerfFrame f[PERF_WINDOW];
    int n, head;
} PerfRing;

void perf_ring_push(PerfRing *r, const PerfFrame *f);
/* avg/max of frame_ms, avg of the others, over the window. */
void perf_ring_stats(const PerfRing *r, double *avg_frame, double *max_frame, double *avg_logic,
                     double *avg_render, double *avg_compose, double *avg_swap);

/* CSV frame log (--perf-csv). Buffered; no allocation per frame. */
int  perf_csv_open(const char *path);      /* 0 ok */
void perf_csv_row(const PerfFrame *f, long rss_kb);
void perf_csv_close(void);

#endif
