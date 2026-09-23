/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* particles.c - see particles.h. */
#include "render/particles.h"

#include <math.h>
#include <string.h>

#include "platform/fx_settings.h"
#include "render/fx.h"
#include "render/gfx.h"
#include "render/sprites.h"

#define PI_F 3.14159265f

typedef struct {
    float x, y, vx, vy;
    float g, drag;         /* gravity px/s^2, linear drag 1/s */
    float age, life;
    float rot, vrot, phase, size;
    Color c;
    uint8_t kind, bounces;
} Particle;

typedef struct {
    EmitterDesc d;
    float t, acc;
    int on;
} Emitter;

static Particle g_p[PFX_MAX];
static int g_n;
static Emitter g_em[PFX_MAX_EMITTERS];
static float g_floor = 720;
static int g_stress;
static float g_stress_t;

static const Color k_neon[] = {
    { 255, 40, 200, 255 }, { 40, 230, 255, 255 }, { 255, 210, 90, 255 }, { 255, 128, 16, 255 },
    { 255, 250, 235, 255 }, { 160, 255, 90, 255 },
};
#define NNEON ((int)(sizeof k_neon / sizeof k_neon[0]))

void pfx_init(void) { pfx_clear(); }

void pfx_clear(void)
{
    g_n = 0;
    memset(g_em, 0, sizeof g_em);
}

void pfx_stop_emitters(void)
{
    for (int i = 0; i < PFX_MAX_EMITTERS; i++) g_em[i].on = 0;
}

void pfx_set_floor(float y) { g_floor = y; }
int pfx_count(void) { return g_n; }

static Particle *spawn(ParticleKind kind, float x, float y, float vx, float vy, Color c)
{
    if (!g_effects.particles || g_n >= PFX_MAX) return NULL;
    Particle *p = &g_p[g_n++];
    memset(p, 0, sizeof *p);
    p->kind = (uint8_t)kind;
    p->x = x;
    p->y = y;
    p->vx = vx;
    p->vy = vy;
    p->c = c;
    p->rot = fx_randf(0, 2 * PI_F);
    p->phase = fx_randf(0, 2 * PI_F);
    switch (kind) {
    case PK_COIN:
        p->g = 980; p->drag = 0.25f; p->life = fx_randf(2.4f, 3.4f);
        p->size = fx_randf(24, 36); p->vrot = fx_randf(8, 16);
        break;
    case PK_SPARK:
        p->g = 260; p->drag = 1.6f; p->life = fx_randf(0.55f, 1.15f); p->size = fx_randf(5, 9);
        break;
    case PK_DROP:
        p->g = 1100; p->drag = 0.2f; p->life = fx_randf(1.2f, 2.0f); p->size = fx_randf(16, 26);
        break;
    case PK_CONFETTI:
        p->g = 160; p->drag = 2.0f; p->life = fx_randf(3.0f, 4.6f); p->size = fx_randf(11, 17);
        p->vrot = fx_randf(-5, 5);
        if (!c.a) p->c = k_neon[fx_randi(0, NNEON - 1)];
        break;
    case PK_SHELL:
        p->g = 420; p->drag = 0.3f; p->life = fx_randf(0.9f, 1.25f); p->size = 10;
        if (!c.a) p->c = k_neon[fx_randi(0, 3)];
        break;
    case PK_TWINKLE:
        p->g = 0; p->drag = 1.0f; p->life = fx_randf(0.6f, 1.2f); p->size = fx_randf(18, 34);
        break;
    case PK_GLOW:
        p->g = 0; p->drag = 0; p->life = 0.45f; p->size = 60;
        break;
    default: break;
    }
    if (!p->c.a) p->c = (Color){ 255, 214, 120, 255 };
    return p;
}

static void burst_at(ParticleKind kind, float x, float y, int n, float speed, float angle, float spread, Color c)
{
    for (int i = 0; i < n; i++) {
        float a = angle + fx_randf(-spread, spread) * 0.5f;
        float v = speed * fx_randf(0.45f, 1.0f);
        spawn(kind, x, y, cosf(a) * v, sinf(a) * v, c);
    }
}

void pfx_burst(ParticleKind kind, float x, float y, int n, float speed, float angle, float spread, Color color)
{
    burst_at(kind, x, y, n, speed, angle, spread, color);
}

int pfx_emit(const EmitterDesc *d)
{
    if (!g_effects.particles) return -1;
    for (int i = 0; i < PFX_MAX_EMITTERS; i++) {
        if (g_em[i].on) continue;
        g_em[i].d = *d;
        g_em[i].t = 0;
        g_em[i].acc = 0;
        g_em[i].on = 1;
        if (d->type == EM_BURST) {
            burst_at(d->kind, d->x, d->y, (int)d->rate, d->speed, d->angle, d->spread, d->color);
            g_em[i].on = 0;
        }
        return i;
    }
    return -1;
}

void pfx_emitter_stop(int slot)
{
    if (slot >= 0 && slot < PFX_MAX_EMITTERS) g_em[slot].on = 0;
}

/* ---- canned effects ----------------------------------------------------- */

void pfx_coin_burst(float x, float y, float k)
{
    burst_at(PK_COIN, x, y, (int)(24 + 60 * k), 520 + 360 * k, -PI_F / 2, 1.9f, (Color){ 0 });
    burst_at(PK_SPARK, x, y, (int)(20 + 40 * k), 600, -PI_F / 2, 2 * PI_F, (Color){ 255, 200, 90, 255 });
    spawn(PK_GLOW, x, y, 0, 0, (Color){ 255, 190, 80, 255 });
}

void pfx_coin_shower(float seconds, float k)
{
    EmitterDesc d = { EM_SHOWER, PK_COIN, 40, -40, 1200, 30 + 60 * k, 60, PI_F / 2, 0.6f, seconds, { 0 }, 0 };
    pfx_emit(&d);
}

void pfx_confetti_burst(float x, float y, float k)
{
    burst_at(PK_CONFETTI, x, y, (int)(60 + 120 * k), 700, -PI_F / 2, 2.2f, (Color){ 0 });
}

void pfx_fireworks(float seconds, float k)
{
    EmitterDesc d = { EM_FIREWORKS, PK_SHELL, 160, 720, 960, 1.4f + 1.6f * k, 760, -PI_F / 2, 0.35f, seconds, { 0 }, 0 };
    pfx_emit(&d);
}

void pfx_honey_fountain(float x, float y, float seconds, float k)
{
    float ang = x < 640 ? -PI_F / 2 + 0.35f : -PI_F / 2 - 0.35f;
    EmitterDesc d = { EM_FOUNTAIN, PK_DROP, x, y, 0, 20 + 40 * k, 820, ang, 0.5f, seconds, { 0 }, 0 };
    pfx_emit(&d);
}

void pfx_sparkle(float x, float y, float radius, int n)
{
    for (int i = 0; i < n; i++) {
        float a = fx_randf(0, 2 * PI_F), r = radius * sqrtf(fx_randf(0, 1));
        Particle *p = spawn(PK_TWINKLE, x + cosf(a) * r, y + sinf(a) * r, 0, -20, (Color){ 255, 236, 180, 255 });
        if (p) p->age = -fx_randf(0, 0.4f);   /* staggered starts */
    }
}

/* ---- update ------------------------------------------------------------- */

static void shell_burst(const Particle *s)
{
    Color c = s->c;
    int n = fx_randi(70, 100);
    for (int i = 0; i < n; i++) {
        float a = (float)i / (float)n * 2 * PI_F + fx_randf(-0.05f, 0.05f);
        float v = fx_randf(260, 420);
        Particle *p = spawn(PK_SPARK, s->x, s->y, cosf(a) * v, sinf(a) * v, c);
        if (p) { p->life = fx_randf(0.8f, 1.3f); p->g = 140; p->drag = 1.4f; }
    }
    Particle *g = spawn(PK_GLOW, s->x, s->y, 0, 0, c);
    if (g) g->size = 150;
}

static void run_emitters(float dt)
{
    for (int i = 0; i < PFX_MAX_EMITTERS; i++) {
        Emitter *e = &g_em[i];
        if (!e->on) continue;
        e->t += dt;
        if (e->t > e->d.duration) { e->on = 0; continue; }
        e->acc += e->d.rate * dt;
        while (e->acc >= 1) {
            e->acc -= 1;
            const EmitterDesc *d = &e->d;
            switch (d->type) {
            case EM_SHOWER: {
                float a = d->angle + fx_randf(-d->spread, d->spread) * 0.5f;
                spawn(d->kind, d->x + fx_randf(0, d->w), d->y, cosf(a) * d->speed * fx_randf(0.3f, 1.0f),
                      sinf(a) * d->speed * fx_randf(0.3f, 1.0f), d->color);
                break;
            }
            case EM_FIREWORKS: {
                float x = d->x + fx_randf(0, d->w);
                float a = d->angle + fx_randf(-d->spread, d->spread) * 0.5f;
                float v = d->speed * fx_randf(0.8f, 1.05f);
                spawn(PK_SHELL, x, d->y, cosf(a) * v, sinf(a) * v, (Color){ 0 });
                break;
            }
            default: {
                float a = d->angle + fx_randf(-d->spread, d->spread) * 0.5f;
                float v = d->speed * fx_randf(0.6f, 1.0f);
                spawn(d->kind, d->x, d->y, cosf(a) * v, sinf(a) * v, d->color);
                break;
            }
            }
        }
    }
}

static void stress_fill(void)
{
    static const ParticleKind kinds[] = { PK_COIN, PK_SPARK, PK_CONFETTI, PK_DROP };
    int k = 0;
    while (g_n < g_stress) {
        ParticleKind kind = kinds[(g_n + k++) & 3];
        float x = fx_randf(40, 1240), y = fx_randf(40, 680);
        float a = fx_randf(0, 2 * PI_F), v = fx_randf(40, 220);
        Particle *p = spawn(kind, x, y, cosf(a) * v, sinf(a) * v, (Color){ 0 });
        if (!p) break;
        p->g = 0;
        p->drag = 0;
        p->life = fx_randf(1.5f, 3.0f);
        if (kind == PK_SPARK) p->c = k_neon[fx_randi(0, NNEON - 1)];
    }
}

void pfx_stress(int n, float t)
{
    g_stress = n < 0 ? 0 : n > PFX_MAX ? PFX_MAX : n;
    g_stress_t = t;
    while (g_n > g_stress) g_n--;
}

void pfx_update(float dt)
{
    if (dt <= 0) return;
    if (!g_effects.particles) { g_n = 0; pfx_stop_emitters(); return; }
    run_emitters(dt);
    for (int i = 0; i < g_n;) {
        Particle *p = &g_p[i];
        p->age += dt;
        if (p->age >= p->life) {
            if (p->kind == PK_SHELL) {
                Particle s = *p;
                *p = g_p[--g_n];
                shell_burst(&s);
            } else {
                *p = g_p[--g_n];
            }
            continue;
        }
        if (p->age < 0) { i++; continue; }
        float drag = 1.0f - p->drag * dt;
        if (drag < 0) drag = 0;
        p->vx *= drag;
        p->vy = p->vy * drag + p->g * dt;
        if (p->kind == PK_CONFETTI) p->vx += sinf(p->phase) * 90 * dt;
        p->x += p->vx * dt;
        p->y += p->vy * dt;
        p->rot += p->vrot * dt;
        p->phase += dt * (p->kind == PK_CONFETTI ? 7.0f : 1.0f);
        if (p->kind == PK_COIN && p->y > g_floor - p->size * 0.5f && p->vy > 0) {
            p->y = g_floor - p->size * 0.5f;
            p->vy *= -0.42f;
            p->vx *= 0.7f;
            p->bounces++;
        }
        if (p->kind == PK_SHELL && i < g_n && fx_randf(0, 1) < 0.5f) {
            /* A short fading trail behind the rising shell. */
            Particle *t = spawn(PK_SPARK, p->x, p->y, p->vx * 0.1f, p->vy * 0.1f, (Color){ 255, 200, 120, 255 });
            if (t) { t->life = 0.35f; t->size = 4; t->g = 60; }
            p = &g_p[i];
        }
        i++;
    }
    if (g_stress) stress_fill();
}

/* ---- draw --------------------------------------------------------------- */

void pfx_draw(void)
{
    for (int i = 0; i < g_n; i++) {
        const Particle *p = &g_p[i];
        if (p->age < 0) continue;
        float lt = p->age / p->life;
        float fade = lt > 0.8f ? (1 - lt) / 0.2f : 1.0f;
        switch (p->kind) {
        case PK_COIN: {
            int f = ((int)(p->rot * 8.0f / PI_F) % 8 + 8) % 8;
            gfx_spr_rot(sprite((SpriteId)(SPR_COIN0 + f)), p->x, p->y, p->size, p->size, 0,
                        gfx_cola(WHITE, fade));
            break;
        }
        case PK_SPARK: {
            float sp = sqrtf(p->vx * p->vx + p->vy * p->vy);
            float len = sp * 0.045f;
            if (len < p->size) len = p->size;
            if (len > 46) len = 46;
            float k = (1 - lt);
            Vector2 a = { p->x - p->vx / (sp + 1e-3f) * len, p->y - p->vy / (sp + 1e-3f) * len }, b = { p->x, p->y };
            gfx_streak(sprite(SPR_SPARK), a, b, p->size * (0.6f + 0.6f * k), gfx_add(p->c, 1.3f * k));
            break;
        }
        case PK_DROP: {
            float sp = sqrtf(p->vx * p->vx + p->vy * p->vy);
            /* Only a little stretch: honey is thick, and long streaks read as flames. */
            float stretch = 1.0f + fminf(sp / 2400.0f, 0.3f);
            float ang = atan2f(p->vy, p->vx) - PI_F / 2;
            gfx_spr_rot(sprite(SPR_DROP), p->x, p->y, p->size * 0.75f / sqrtf(stretch), p->size * stretch, ang,
                        gfx_cola(WHITE, fade));
            break;
        }
        case PK_CONFETTI: {
            float flip = cosf(p->phase * 1.7f);
            Color c = p->c;
            float shade = 0.6f + 0.4f * fabsf(flip);
            c.r = (unsigned char)(c.r * shade);
            c.g = (unsigned char)(c.g * shade);
            c.b = (unsigned char)(c.b * shade);
            gfx_spr_rot(sprite(SPR_CONFETTI), p->x, p->y, p->size * (0.15f + 0.85f * fabsf(flip)), p->size * 0.62f,
                        p->rot, gfx_cola(c, fade));
            break;
        }
        case PK_SHELL:
            gfx_spr_rot(sprite(SPR_GLOW), p->x, p->y, 26, 26, 0, gfx_add(p->c, 1.4f));
            gfx_spr_rot(sprite(SPR_GLOW), p->x, p->y, 10, 10, 0, gfx_add(WHITE, 1.0f));
            break;
        case PK_TWINKLE: {
            float s = sinf(lt * PI_F);
            gfx_spr_rot(sprite(SPR_STAR), p->x, p->y, p->size * s, p->size * s, p->rot * 0.2f, gfx_add(p->c, 1.2f * s));
            break;
        }
        case PK_GLOW: {
            float s = 1 - lt;
            float sz = p->size * (0.6f + 0.8f * lt);
            gfx_spr_rot(sprite(SPR_GLOW), p->x, p->y, sz, sz, 0, gfx_add(p->c, 1.1f * s * s));
            break;
        }
        default: break;
        }
    }
}
