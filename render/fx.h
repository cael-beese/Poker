/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* fx.h - game-feel helpers: screen shake, hit-pause, slow motion, flashes,
 * and the renderer's cosmetic random stream.
 *
 * All of these act on PRESENTATION time only. The game keeps ticking at
 * 60 Hz underneath (CONTRACT.md section 3); a hit-pause freezes the
 * presentation's own animations for a few rendered frames, it never delays
 * a tick. Each respects its toggle in g_effects (shake, hitpause). */
#ifndef BPL_RENDER_FX_H
#define BPL_RENDER_FX_H

#include "engine/rng.h"

/* ---- cosmetic randomness (never the game's Rng) ------------------------- */
Rng  *fx_rng(void);                      /* the renderer's own stream        */
void  fx_rng_seed(uint64_t seed);
float fx_randf(float lo, float hi);      /* uniform in [lo, hi)              */
int   fx_randi(int lo, int hi);          /* uniform in [lo, hi]              */

/* ---- screen shake: trauma model (offset ~ trauma^2, smooth noise) -------- */
typedef struct {
    float trauma;       /* 0..1, decays                      */
    float decay;        /* trauma per second (default 1.4)    */
    float max_px;       /* offset at trauma 1 (default 18)    */
    float max_deg;      /* rotation at trauma 1 (default 1.6) */
    float t;
} Shake;

void shake_init(Shake *s);
void shake_add(Shake *s, float trauma);                  /* adds, clamps to 1 */
void shake_update(Shake *s, float dt);
/* The offset to apply this frame (zero when the shake toggle is off). */
void shake_offset(const Shake *s, float *dx, float *dy, float *deg);

/* ---- the presentation clock: hit-pause and slow motion ------------------- */
typedef struct {
    int   pause_frames;     /* rendered frames left frozen                  */
    float scale;            /* current time scale (1 = normal)              */
    float slow_scale;       /* slow-motion target                           */
    float slow_hold, slow_recover, slow_t;
    int   slow_on;
    double time;            /* presentation seconds, scaled                  */
} FxClock;

void  clock_init(FxClock *c);
/* Freeze the presentation for n frames (2-3 on a big reveal). */
void  clock_hitpause(FxClock *c, int frames);
/* Drop to `scale` at once, hold for `hold` s, ease back to 1 over `recover` s. */
void  clock_slowmo(FxClock *c, float scale, float hold, float recover);
void  clock_cancel(FxClock *c);                          /* back to real time */
/* Call once per rendered frame with the frame's dt; returns the scaled dt
 * the presentation should animate with (0 while hit-paused). */
float clock_step(FxClock *c, float dt);
int   clock_paused(const FxClock *c);

/* ---- a decaying flash value (0..1), e.g. a white flash on a reveal ------- */
typedef struct { float v, decay; } Flash;
void  flash_fire(Flash *f, float strength, float seconds);
float flash_update(Flash *f, float dt);

#endif
