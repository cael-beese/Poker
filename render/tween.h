/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* tween.h - easing curves, tweens and springs. Pure C, no raylib.
 *
 * SPEC: "easing on everything, no linear motion". EASE_LINEAR exists for
 * things that are not motion (a colour ramp, a progress bar), and nothing
 * defaults to it: a zeroed Tween uses EASE_OUT_CUBIC. */
#ifndef BPL_RENDER_TWEEN_H
#define BPL_RENDER_TWEEN_H

typedef enum {
    EASE_OUT_CUBIC = 0,     /* the default: fast start, soft landing */
    EASE_LINEAR,
    EASE_IN_QUAD, EASE_OUT_QUAD, EASE_INOUT_QUAD,
    EASE_IN_CUBIC, EASE_INOUT_CUBIC,
    EASE_IN_QUART, EASE_OUT_QUART, EASE_INOUT_QUART,
    EASE_OUT_QUINT,
    EASE_IN_SINE, EASE_OUT_SINE, EASE_INOUT_SINE,
    EASE_IN_EXPO, EASE_OUT_EXPO, EASE_INOUT_EXPO,
    EASE_OUT_CIRC,
    EASE_IN_BACK, EASE_OUT_BACK, EASE_INOUT_BACK,   /* overshoot */
    EASE_OUT_ELASTIC,                               /* springy settle */
    EASE_OUT_BOUNCE,                                /* lands and bounces */
    EASE_COUNT
} Ease;

float ease(Ease e, float t);                 /* t clamped to 0..1 */
float lerpf(float a, float b, float t);
float clampf(float v, float lo, float hi);
float smooth01(float edge0, float edge1, float x);  /* smoothstep */

/* A one-shot tween of a float from a to b over dur seconds after delay. */
typedef struct {
    float a, b, dur, delay, t;
    Ease  ease;
    int   running;
} Tween;

void  tw_start(Tween *tw, float a, float b, float dur, float delay, Ease e);
void  tw_to(Tween *tw, float b, float dur, Ease e);    /* from the current value */
float tw_update(Tween *tw, float dt);                  /* returns the value */
float tw_value(const Tween *tw);
float tw_progress(const Tween *tw);                    /* 0..1 of the eased span, ignoring delay */
int   tw_done(const Tween *tw);
void  tw_finish(Tween *tw);                            /* jump to the end */

/* A critically damped spring: follows a moving target without overshoot
 * (or with it, for damping < 1). omega = responsiveness (rad/s). */
typedef struct { float x, v; } Spring;
void spring_update(Spring *s, float target, float omega, float damping, float dt);

/* Quadratic bezier point, for arcs (deal paths). */
typedef struct { float x, y; } Vec2f;
Vec2f bezier2(Vec2f p0, Vec2f p1, Vec2f p2, float t);

#endif
