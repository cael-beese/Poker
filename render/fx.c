/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* fx.c - see fx.h. */
#include "render/fx.h"

#include <math.h>

#include "platform/fx_settings.h"
#include "render/tween.h"

static Rng g_rng = { { 0x9E3779B97F4A7C15ULL, 0xBF58476D1CE4E5B9ULL, 0x94D049BB133111EBULL, 0x2545F4914F6CDD1DULL } };

Rng *fx_rng(void) { return &g_rng; }
void fx_rng_seed(uint64_t seed) { rng_seed(&g_rng, seed ^ 0x72656e6465722121ULL); }   /* "render!!" */

float fx_randf(float lo, float hi) { return lo + (hi - lo) * (float)rng_unit(&g_rng); }

int fx_randi(int lo, int hi)
{
    if (hi <= lo) return lo;
    return lo + (int)rng_below(&g_rng, (uint32_t)(hi - lo + 1));
}

void shake_init(Shake *s)
{
    s->trauma = 0;
    s->decay = 1.4f;
    s->max_px = 18;
    s->max_deg = 1.6f;
    s->t = 0;
}

void shake_add(Shake *s, float trauma)
{
    s->trauma = clampf(s->trauma + trauma, 0, 1);
}

void shake_update(Shake *s, float dt)
{
    s->t += dt;
    s->trauma = fmaxf(0, s->trauma - s->decay * dt);
}

/* Smooth pseudo-noise: a few incommensurate sines, cheap and never repeats visibly. */
static float wobble(float t, float seed)
{
    return 0.5f * sinf(t * 37.0f + seed) + 0.3f * sinf(t * 61.3f + seed * 2.1f) + 0.2f * sinf(t * 97.7f + seed * 3.7f);
}

void shake_offset(const Shake *s, float *dx, float *dy, float *deg)
{
    *dx = *dy = *deg = 0;
    if (!g_effects.shake || s->trauma <= 0) return;
    float k = s->trauma * s->trauma;
    *dx = s->max_px * k * wobble(s->t, 1.3f);
    *dy = s->max_px * k * wobble(s->t, 5.1f);
    *deg = s->max_deg * k * wobble(s->t, 9.7f);
}

void clock_init(FxClock *c)
{
    c->pause_frames = 0;
    c->scale = 1;
    c->slow_on = 0;
    c->time = 0;
}

void clock_hitpause(FxClock *c, int frames)
{
    if (!g_effects.hitpause) return;
    if (frames > c->pause_frames) c->pause_frames = frames;
}

void clock_slowmo(FxClock *c, float scale, float hold, float recover)
{
    c->slow_scale = scale;
    c->slow_hold = hold;
    c->slow_recover = recover > 0.01f ? recover : 0.01f;
    c->slow_t = 0;
    c->slow_on = 1;
    c->scale = scale;
}

void clock_cancel(FxClock *c)
{
    c->slow_on = 0;
    c->scale = 1;
    c->pause_frames = 0;
}

float clock_step(FxClock *c, float dt)
{
    if (c->pause_frames > 0) {
        c->pause_frames--;
        return 0;
    }
    if (c->slow_on) {
        c->slow_t += dt;   /* real time drives the recovery */
        if (c->slow_t < c->slow_hold) {
            c->scale = c->slow_scale;
        } else {
            float k = (c->slow_t - c->slow_hold) / c->slow_recover;
            c->scale = lerpf(c->slow_scale, 1.0f, ease(EASE_INOUT_SINE, k));
            if (k >= 1) { c->slow_on = 0; c->scale = 1; }
        }
    }
    float sdt = dt * c->scale;
    c->time += sdt;
    return sdt;
}

int clock_paused(const FxClock *c) { return c->pause_frames > 0; }

void flash_fire(Flash *f, float strength, float seconds)
{
    if (strength > f->v) f->v = strength;
    f->decay = seconds > 0.01f ? strength / seconds : 100.0f;
}

float flash_update(Flash *f, float dt)
{
    f->v = fmaxf(0, f->v - f->decay * dt);
    return f->v;
}
