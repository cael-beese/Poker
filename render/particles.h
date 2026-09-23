/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* particles.h - one pooled particle system for every celebration effect.
 *
 * A fixed pool of PFX_MAX particles and PFX_MAX_EMITTERS emitters, allocated
 * statically: spawning when full recycles nothing and simply drops the new
 * particle, so nothing is ever allocated after start-up. All kinds draw
 * from the atlas in one pass with premultiplied blending, so additive
 * sparks and alpha-blended coins share the batch.
 *
 * Kinds: gold coins (8 spin frames, bounce on the floor line), sparks
 * (additive, velocity-stretched), honey droplets (stretch as they fall),
 * confetti (flutter), firework shells (rise, burst into sparks) and
 * twinkles. Emitters make bursts, showers (from the top edge) and
 * fountains, with intensity scaled by the win tier.
 *
 * Respects g_effects.particles: when it is off, spawning does nothing and
 * the pool drains. Uses the renderer's cosmetic Rng (fx.h). */
#ifndef BPL_RENDER_PARTICLES_H
#define BPL_RENDER_PARTICLES_H

#include "raylib.h"

#define PFX_MAX 2000
#define PFX_MAX_EMITTERS 32

typedef enum { PK_COIN, PK_SPARK, PK_DROP, PK_CONFETTI, PK_SHELL, PK_TWINKLE, PK_GLOW, PK_COUNT } ParticleKind;

typedef enum { EM_BURST, EM_SHOWER, EM_FOUNTAIN, EM_FIREWORKS } EmitterType;

typedef struct {
    EmitterType type;
    ParticleKind kind;
    float x, y;          /* origin (shower: y = top edge, x..x+w = span)      */
    float w;             /* shower width / fountain spread                     */
    float rate;          /* particles per second (burst: count)                */
    float speed;         /* initial speed px/s                                 */
    float angle, spread; /* direction (radians, 0 = right, -pi/2 = up), cone   */
    float duration;      /* seconds the emitter runs (burst: ignored)          */
    Color color;         /* tint (a = 0: the kind's own palette)               */
    float size;          /* 0 = the kind's default                             */
} EmitterDesc;

void pfx_init(void);
void pfx_clear(void);                          /* everything gone at once        */
void pfx_stop_emitters(void);                  /* no new particles; live ones finish */

/* Emit n particles right now. */
void pfx_burst(ParticleKind kind, float x, float y, int n, float speed, float angle, float spread, Color color);
/* Start a running emitter; returns its slot or -1. */
int  pfx_emit(const EmitterDesc *d);
void pfx_emitter_stop(int slot);

/* Canned celebration effects, scaled by intensity (0..1). */
void pfx_coin_burst(float x, float y, float intensity);
void pfx_coin_shower(float seconds, float intensity);
void pfx_confetti_burst(float x, float y, float intensity);
void pfx_fireworks(float seconds, float intensity);      /* shells over the upper half */
void pfx_honey_fountain(float x, float y, float seconds, float intensity);
void pfx_sparkle(float x, float y, float radius, int n); /* small-win twinkles */

/* Floor line for bouncing coins (default: the bottom of the play space). */
void pfx_set_floor(float y);

void pfx_update(float dt);
void pfx_draw(void);
int  pfx_count(void);                          /* live particles */

/* Test hook: keep exactly n particles alive (the RENDERTEST stress scene). */
void pfx_stress(int n, float t);

#endif
