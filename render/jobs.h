/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* jobs.h - a parallel for-loop for start-up generation.
 *
 * The Pi has four cores and start-up has a time budget, so the painters
 * (cards, sprites, glyphs, background, side art) run on all of them. Jobs
 * must only write their own atlas regions and must not call raylib's GL
 * functions. Used at start-up only; nothing here runs per frame. */
#ifndef BPL_RENDER_JOBS_H
#define BPL_RENDER_JOBS_H

typedef void (*JobFn)(int index, void *user);

/* Runs fn(0..n-1) on up to `threads` threads (0 = number of CPUs, max 8)
 * and returns when all are done. Indices are handed out in order, so put
 * the most expensive jobs first. */
void jobs_run(JobFn fn, void *user, int n, int threads);

int jobs_cpus(void);

#endif
