/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* sprites.c - see sprites.h. */
#include "render/sprites.h"

#include <math.h>
#include <string.h>

#include "engine/card.h"
#include "render/art.h"

#define PI_F 3.14159265f

static const struct { short w, h; } k_size[SPR_COUNT] = {
    [SPR_WHITE] = { 4, 4 },
    [SPR_GLOW] = { 64, 64 },
    [SPR_SPARK] = { 64, 16 },
    [SPR_STAR] = { 48, 48 },
    [SPR_COIN0] = { 40, 40 }, [SPR_COIN0 + 1] = { 40, 40 }, [SPR_COIN0 + 2] = { 40, 40 }, [SPR_COIN0 + 3] = { 40, 40 },
    [SPR_COIN0 + 4] = { 40, 40 }, [SPR_COIN0 + 5] = { 40, 40 }, [SPR_COIN0 + 6] = { 40, 40 }, [SPR_COIN7] = { 40, 40 },
    [SPR_DROP] = { 24, 32 },
    [SPR_CONFETTI] = { 16, 10 },
    [SPR_BULB_ON] = { 28, 28 },
    [SPR_BULB_OFF] = { 28, 28 },
    [SPR_BEAM] = { 128, 512 },
    [SPR_RAYS] = { 256, 256 },
    [SPR_RING] = { 128, 128 },
    [SPR_BEE] = { 128, 112 },
    [SPR_HEX_GLOW] = { 96, 96 },
    [SPR_PIP_C] = { 48, 48 }, [SPR_PIP_D] = { 48, 48 }, [SPR_PIP_H] = { 48, 48 }, [SPR_PIP_S] = { 48, 48 },
    [SPR_PIP_NEON_C] = { 64, 64 }, [SPR_PIP_NEON_D] = { 64, 64 }, [SPR_PIP_NEON_H] = { 64, 64 }, [SPR_PIP_NEON_S] = { 64, 64 },
};
static const struct { short w, h, corner; } k_nine[NINE_COUNT] = {
    [NINE_RRECT] = { 48, 48, 16 },
    [NINE_RRECT_LINE] = { 48, 48, 16 },
    [NINE_GLOW] = { 96, 96, 40 },
    [NINE_SHADOW] = { 96, 96, 44 },
    [NINE_PANEL] = { 48, 64, 16 },
};

static int g_item[SPR_COUNT], g_nine_item[NINE_COUNT];
static Spr g_spr[SPR_COUNT];
static Nine g_nine[NINE_COUNT];

void sprites_declare(void)
{
    for (int i = 0; i < SPR_COUNT; i++) g_item[i] = atlas_declare(k_size[i].w, k_size[i].h);
    for (int i = 0; i < NINE_COUNT; i++) g_nine_item[i] = atlas_declare(k_nine[i].w, k_nine[i].h);
}

int sprites_job_count(void) { return SPR_COUNT + NINE_COUNT; }

/* Per-pixel painter for radial / analytic lights: f returns premultiplied
 * white intensity (alpha = intensity) at pixel centre (x, y). */
typedef float (*LightFn)(float x, float y, float w, float h);

static void paint_light(Canvas *cv, LightFn f, RCol tint)
{
    for (int y = 0; y < cv->h; y++)
        for (int x = 0; x < cv->w; x++) {
            float a = f(x + 0.5f, y + 0.5f, (float)cv->w, (float)cv->h);
            a = a < 0 ? 0 : a > 1 ? 1 : a;
            uint8_t *p = cv->px + ((size_t)y * cv->stride + x) * 4;
            p[0] = (uint8_t)(255 * a * tint.r);
            p[1] = (uint8_t)(255 * a * tint.g);
            p[2] = (uint8_t)(255 * a * tint.b);
            p[3] = (uint8_t)(255 * a);
        }
}

static float edge_fade(float r, float rmax) { float t = 1 - r / rmax; return t <= 0 ? 0 : t < 0.2f ? t / 0.2f : 1; }

static float f_glow(float x, float y, float w, float h)
{
    float dx = x - w / 2, dy = y - h / 2, r = sqrtf(dx * dx + dy * dy);
    return expf(-r * r / (2 * 11.0f * 11.0f)) * edge_fade(r, w / 2);
}

static float f_spark(float x, float y, float w, float h)
{
    float u = (x - w / 2) / (w / 2), v = (y - h / 2);
    float along = 1 - u * u;
    if (along < 0) return 0;
    float across = expf(-v * v / (2 * 2.6f * 2.6f));
    float core = expf(-v * v / (2 * 0.9f * 0.9f)) * (1 - fabsf(u)) * 0.8f;
    return along * across * 0.85f + core;
}

static float f_star(float x, float y, float w, float h)
{
    float dx = fabsf(x - w / 2), dy = fabsf(y - h / 2), r = sqrtf(dx * dx + dy * dy);
    float a = expf(-dx / 1.4f) * expf(-dy / 9.0f), b = expf(-dy / 1.4f) * expf(-dx / 9.0f);
    float c = expf(-r * r / 18.0f);
    return (fmaxf(a, b) + c) * edge_fade(r, w / 2);
}

static float smooth_start(float v)
{
    float t = v / 0.06f;
    if (t > 1) t = 1;
    return t * t * (3 - 2 * t);
}

static float f_beam(float x, float y, float w, float h)
{
    float v = y / h, u = (x - w / 2) / (w / 2);
    float wid = 0.06f + 0.94f * v;
    float across = u / wid;
    float i = expf(-across * across * 2.8f);
    i *= smooth_start(v);
    i *= 1.0f - 0.6f * v;
    return i * 0.85f;
}

static float f_rays(float x, float y, float w, float h)
{
    float dx = x - w / 2, dy = y - h / 2, r = sqrtf(dx * dx + dy * dy) / (w / 2);
    if (r >= 1) return 0;
    float a = atan2f(dy, dx);
    float ray = powf(0.5f + 0.5f * cosf(a * 14), 5);
    float fall = (1 - r) * (1 - r);
    return ray * fall * 0.9f + expf(-r * r * 30) * 0.5f;
}

static float f_ring(float x, float y, float w, float h)
{
    float dx = x - w / 2, dy = y - h / 2, r = sqrtf(dx * dx + dy * dy);
    float d = fabsf(r - 50);
    return (expf(-d * d / 3.0f) + 0.45f * expf(-d / 6.0f)) * edge_fade(r, w / 2);
}

static void paint_coin(Canvas *cv, int frame)
{
    float phi = (float)frame * PI_F / 8.0f;
    float xs = fabsf(cosf(phi));
    if (xs < 0.13f) xs = 0.13f;
    float cx = 20, cy = 20, R = 17.5f;
    /* Thickness: a darker coin behind, shifted toward the side turning away. */
    float edge = 3.2f * sinf(phi);
    Shape e[1] = { { SH_CIRCLE, OP_UNION, { 0, 0, R }, NULL, 0 } };
    Xf back = xf_scale2(cx + edge * 0.5f, cy, xs, 1, 0);
    Paint rim = paint_linear(rc_hex(0xB0701A, 1), 0, -R, rc_hex(0x6A3E08, 1), 0, R);
    cv_fill(cv, &back, e, 1, &rim, NULL);
    Xf front = xf_scale2(cx - edge * 0.5f, cy, xs, 1, 0);
    Paint face = paint_radial(rc_hex(0xFFF0A8, 1), -R * 0.35f, -R * 0.4f, rc_hex(0xD8900E, 1), R * 1.4f);
    cv_fill(cv, &front, e, 1, &face, NULL);
    Shape ring[1] = { { SH_RING, OP_UNION, { 0, 0, R * 0.80f, 0.9f }, NULL, 0 } };
    Paint ringp = paint_solid(rc_hex(0xA86A10, 0.9f));
    cv_fill(cv, &front, ring, 1, &ringp, NULL);
    Shape hex[1] = { { SH_HEX, OP_UNION, { 0, 0, R * 0.45f, 1 }, NULL, 0 } };
    Paint hexp = paint_linear(rc_hex(0xFFE890, 1), 0, -R * 0.45f, rc_hex(0xC07A10, 1), 0, R * 0.45f);
    FillOpt hol = { 0 };
    hol.offset = 1.0f;
    Paint dark = paint_solid(rc_hex(0x8A5008, 0.9f));
    cv_fill(cv, &front, hex, 1, &dark, &hol);
    cv_fill(cv, &front, hex, 1, &hexp, NULL);
    /* Glint. */
    Shape glint[1] = { { SH_ELLIPSE, OP_UNION, { -R * 0.35f, -R * 0.45f, R * 0.30f, R * 0.12f }, NULL, 0 } };
    Xf gx = xf_mul(front, xf_make(0, 0, 1, -0.6f));
    Paint gp = paint_solid(rc(1, 1, 0.92f, 0.75f));
    cv_fill(cv, &gx, glint, 1, &gp, NULL);
}

static void paint_one(int id)
{
    Canvas cv = atlas_canvas(g_item[id]);
    RCol white = rc(1, 1, 1, 1);
    switch (id) {
    case SPR_WHITE: cv_clear(&cv, white); break;
    case SPR_GLOW: paint_light(&cv, f_glow, white); break;
    case SPR_SPARK: paint_light(&cv, f_spark, white); break;
    case SPR_STAR: paint_light(&cv, f_star, white); break;
    case SPR_BEAM: paint_light(&cv, f_beam, white); break;
    case SPR_RAYS: paint_light(&cv, f_rays, white); break;
    case SPR_RING: paint_light(&cv, f_ring, white); break;
    case SPR_DROP: {
        Shape d[2] = { { SH_CIRCLE, OP_UNION, { 12, 21, 9 }, NULL, 0 },
                       { SH_TRI, OP_SMOOTH, { 12, 2, 4.5f, 17, 19.5f, 17, 0, 2.0f }, NULL, 0 } };
        Paint p = paint_radial(rc_hex(0xFFE27A, 0.95f), 9, 15, rc_hex(0xD06A06, 0.92f), 14);
        cv_fill(&cv, NULL, d, 2, &p, NULL);
        Shape hl[1] = { { SH_ELLIPSE, OP_UNION, { 8.5f, 18, 2.2f, 3.6f }, NULL, 0 } };
        Paint hp = paint_solid(rc(1, 1, 0.95f, 0.85f));
        cv_fill(&cv, NULL, hl, 1, &hp, NULL);
        break;
    }
    case SPR_CONFETTI: {
        Shape r[1] = { { SH_RBOX, OP_UNION, { 8, 5, 7, 4, 1.5f }, NULL, 0 } };
        Paint p = paint_linear(white, 0, 1, rc(0.8f, 0.8f, 0.8f, 1), 0, 9);
        cv_fill(&cv, NULL, r, 1, &p, NULL);
        break;
    }
    case SPR_BULB_ON: {
        Shape c[1] = { { SH_CIRCLE, OP_UNION, { 14, 14, 6.5f }, NULL, 0 } };
        FillOpt g = { 0 };
        g.glow = 3.2f;
        Paint p = paint_radial(rc(1, 1, 0.95f, 1), 13, 13, rc_hex(0xFFD070, 1), 7);
        cv_fill(&cv, NULL, c, 1, &p, &g);
        break;
    }
    case SPR_BULB_OFF: {
        Shape c[1] = { { SH_CIRCLE, OP_UNION, { 14, 14, 7.5f }, NULL, 0 } };
        Paint p = paint_radial(rc_hex(0x6A4A2A, 1), 12, 11, rc_hex(0x241408, 1), 9);
        cv_fill(&cv, NULL, c, 1, &p, NULL);
        FillOpt o = { 0 };
        o.outline = 1.2f;
        Paint rim = paint_solid(rc_hex(0xB08A40, 0.8f));
        cv_fill(&cv, NULL, c, 1, &rim, &o);
        Shape hl[1] = { { SH_CIRCLE, OP_UNION, { 11.5f, 11, 1.8f }, NULL, 0 } };
        Paint hp = paint_solid(rc(1, 1, 1, 0.45f));
        cv_fill(&cv, NULL, hl, 1, &hp, NULL);
        break;
    }
    case SPR_BEE: art_bee(&cv, 66, 62, 48, -0.08f, 1.0f, 1); break;
    case SPR_HEX_GLOW: {
        Shape h[1] = { { SH_HEX, OP_UNION, { 48, 48, 30, 1 }, NULL, 0 } };
        FillOpt g = { 0 };
        g.glow = 7;
        g.opacity = 0.9f;
        Paint p = paint_radial(rc(1, 1, 1, 0.5f), 48, 48, rc(1, 1, 1, 0.9f), 30);
        cv_fill(&cv, NULL, h, 1, &p, &g);
        break;
    }
    default:
        if (id >= SPR_COIN0 && id <= SPR_COIN7) {
            paint_coin(&cv, id - SPR_COIN0);
        } else if (id >= SPR_PIP_C && id <= SPR_PIP_S) {
            int suit = id - SPR_PIP_C;
            RCol t, b;
            art_suit_colors(suit, &t, &b);
            if (suit == SUIT_C || suit == SUIT_S) { t = rc_hex(0xF4ECDD, 1); b = rc_hex(0xC0B498, 1); }
            art_pip(&cv, suit, 24, 24, 20, 0, t, b, NULL);
        } else if (id >= SPR_PIP_NEON_C && id <= SPR_PIP_NEON_S) {
            FillOpt o = { 0 };
            o.outline = 2.6f;
            o.glow = 4.5f;
            art_pip(&cv, id - SPR_PIP_NEON_C, 32, 32, 22, 0, white, white, &o);
        }
        break;
    }
}

static void paint_nine(int id)
{
    Canvas cv = atlas_canvas(g_nine_item[id]);
    RCol white = rc(1, 1, 1, 1);
    Paint wp = paint_solid(white);
    switch (id) {
    case NINE_RRECT: {
        Shape r[1] = { { SH_RBOX, OP_UNION, { 24, 24, 24, 24, 14 }, NULL, 0 } };
        cv_fill(&cv, NULL, r, 1, &wp, NULL);
        break;
    }
    case NINE_RRECT_LINE: {
        Shape r[1] = { { SH_RBOX, OP_UNION, { 24, 24, 22.5f, 22.5f, 12.5f }, NULL, 0 } };
        FillOpt o = { 0 };
        o.outline = 3;
        cv_fill(&cv, NULL, r, 1, &wp, &o);
        break;
    }
    case NINE_GLOW: {
        Shape r[1] = { { SH_RBOX, OP_UNION, { 48, 48, 24, 24, 12 }, NULL, 0 } };
        FillOpt o = { 0 };
        o.outline = 2;
        o.glow = 7;
        cv_fill(&cv, NULL, r, 1, &wp, &o);
        break;
    }
    case NINE_SHADOW: {
        Shape r[1] = { { SH_RBOX, OP_UNION, { 48, 48, 22, 22, 12 }, NULL, 0 } };
        FillOpt o = { 0 };
        o.feather = 14;
        cv_fill(&cv, NULL, r, 1, &wp, &o);
        break;
    }
    case NINE_PANEL: {
        Shape r[1] = { { SH_RBOX, OP_UNION, { 24, 32, 24, 32, 14 }, NULL, 0 } };
        Paint p = paint_linear(rc(1, 1, 1, 1), 0, 0, rc(0.55f, 0.55f, 0.6f, 1), 0, 64);
        cv_fill(&cv, NULL, r, 1, &p, NULL);
        break;
    }
    }
}

void sprites_paint_job(int i)
{
    if (i < SPR_COUNT) paint_one(i);
    else paint_nine(i - SPR_COUNT);
}

void sprites_finish(void)
{
    for (int i = 0; i < SPR_COUNT; i++) g_spr[i] = atlas_spr(g_item[i]);
    g_spr[SPR_WHITE] = atlas_texel(g_item[SPR_WHITE], 1.5f, 1.5f);
    for (int i = 0; i < NINE_COUNT; i++) {
        g_nine[i].s = atlas_spr(g_nine_item[i]);
        g_nine[i].corner = k_nine[i].corner;
    }
    gfx_set_white(g_spr[SPR_WHITE]);
}

const Spr *sprite(SpriteId id) { return &g_spr[id]; }
const Nine *sprite_nine(NineId id) { return &g_nine[id]; }
