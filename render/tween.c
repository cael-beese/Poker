/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* tween.c - see tween.h. Curves after Robert Penner's easing equations. */
#include "render/tween.h"

#include <math.h>

#define PI_F 3.14159265f

float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
float lerpf(float a, float b, float t) { return a + (b - a) * t; }

float smooth01(float e0, float e1, float x)
{
    float t = clampf((x - e0) / (e1 - e0), 0, 1);
    return t * t * (3 - 2 * t);
}

static float bounce_out(float t)
{
    const float n = 7.5625f, d = 2.75f;
    if (t < 1 / d) return n * t * t;
    if (t < 2 / d) { t -= 1.5f / d; return n * t * t + 0.75f; }
    if (t < 2.5f / d) { t -= 2.25f / d; return n * t * t + 0.9375f; }
    t -= 2.625f / d;
    return n * t * t + 0.984375f;
}

float ease(Ease e, float t)
{
    t = clampf(t, 0, 1);
    const float c1 = 1.70158f, c2 = c1 * 1.525f, c3 = c1 + 1;
    switch (e) {
    case EASE_LINEAR: return t;
    case EASE_IN_QUAD: return t * t;
    case EASE_OUT_QUAD: return 1 - (1 - t) * (1 - t);
    case EASE_INOUT_QUAD: return t < 0.5f ? 2 * t * t : 1 - powf(-2 * t + 2, 2) / 2;
    case EASE_IN_CUBIC: return t * t * t;
    case EASE_INOUT_CUBIC: return t < 0.5f ? 4 * t * t * t : 1 - powf(-2 * t + 2, 3) / 2;
    case EASE_IN_QUART: return t * t * t * t;
    case EASE_OUT_QUART: return 1 - powf(1 - t, 4);
    case EASE_INOUT_QUART: return t < 0.5f ? 8 * t * t * t * t : 1 - powf(-2 * t + 2, 4) / 2;
    case EASE_OUT_QUINT: return 1 - powf(1 - t, 5);
    case EASE_IN_SINE: return 1 - cosf(t * PI_F / 2);
    case EASE_OUT_SINE: return sinf(t * PI_F / 2);
    case EASE_INOUT_SINE: return -(cosf(PI_F * t) - 1) / 2;
    case EASE_IN_EXPO: return t <= 0 ? 0 : powf(2, 10 * t - 10);
    case EASE_OUT_EXPO: return t >= 1 ? 1 : 1 - powf(2, -10 * t);
    case EASE_INOUT_EXPO:
        if (t <= 0 || t >= 1) return t;
        return t < 0.5f ? powf(2, 20 * t - 10) / 2 : (2 - powf(2, -20 * t + 10)) / 2;
    case EASE_OUT_CIRC: return sqrtf(1 - (t - 1) * (t - 1));
    case EASE_IN_BACK: return c3 * t * t * t - c1 * t * t;
    case EASE_OUT_BACK: return 1 + c3 * powf(t - 1, 3) + c1 * powf(t - 1, 2);
    case EASE_INOUT_BACK:
        return t < 0.5f ? (powf(2 * t, 2) * ((c2 + 1) * 2 * t - c2)) / 2
                        : (powf(2 * t - 2, 2) * ((c2 + 1) * (t * 2 - 2) + c2) + 2) / 2;
    case EASE_OUT_ELASTIC:
        if (t <= 0 || t >= 1) return t;
        return powf(2, -10 * t) * sinf((t * 10 - 0.75f) * (2 * PI_F / 3)) + 1;
    case EASE_OUT_BOUNCE: return bounce_out(t);
    case EASE_OUT_CUBIC:
    default: return 1 - powf(1 - t, 3);
    }
}

void tw_start(Tween *tw, float a, float b, float dur, float delay, Ease e)
{
    tw->a = a;
    tw->b = b;
    tw->dur = dur > 1e-4f ? dur : 1e-4f;
    tw->delay = delay;
    tw->t = 0;
    tw->ease = e;
    tw->running = 1;
}

void tw_to(Tween *tw, float b, float dur, Ease e) { tw_start(tw, tw_value(tw), b, dur, 0, e); }

float tw_progress(const Tween *tw)
{
    if (tw->dur <= 0) return 1;
    return clampf((tw->t - tw->delay) / tw->dur, 0, 1);
}

float tw_value(const Tween *tw)
{
    if (tw->dur <= 0) return tw->b;
    return lerpf(tw->a, tw->b, ease(tw->ease, tw_progress(tw)));
}

float tw_update(Tween *tw, float dt)
{
    if (tw->running) {
        tw->t += dt;
        if (tw->t >= tw->delay + tw->dur) tw->running = 0;
    }
    return tw_value(tw);
}

int tw_done(const Tween *tw) { return !tw->running; }

void tw_finish(Tween *tw)
{
    tw->t = tw->delay + tw->dur;
    tw->running = 0;
}

void spring_update(Spring *s, float target, float omega, float damping, float dt)
{
    /* Semi-implicit integration of x'' = -w^2 (x - target) - 2 zeta w x',
     * sub-stepped so large dt stays stable. */
    int n = (int)ceilf(dt / (1.0f / 240.0f));
    if (n < 1) n = 1;
    float h = dt / (float)n;
    for (int i = 0; i < n; i++) {
        float a = -omega * omega * (s->x - target) - 2 * damping * omega * s->v;
        s->v += a * h;
        s->x += s->v * h;
    }
}

Vec2f bezier2(Vec2f p0, Vec2f p1, Vec2f p2, float t)
{
    float u = 1 - t;
    return (Vec2f){ u * u * p0.x + 2 * u * t * p1.x + t * t * p2.x, u * u * p0.y + 2 * u * t * p1.y + t * t * p2.y };
}
