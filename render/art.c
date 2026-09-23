/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* art.c - see art.h. */
#include "render/art.h"

#include <math.h>
#include <string.h>

#include "engine/card.h"

static const float k_diamond[] = { 0, -1, 0.40f, -0.42f, 0.76f, 0, 0.40f, 0.42f, 0, 1, -0.40f, 0.42f, -0.76f, 0, -0.40f, -0.42f };
static const float k_stem[] = { -0.07f, 0.18f, 0.07f, 0.18f, 0.20f, 0.78f, 0.36f, 0.98f, -0.36f, 0.98f, -0.20f, 0.78f };

static int suit_shapes(int suit, Shape *s)
{
    memset(s, 0, sizeof(Shape) * 6);
    switch (suit) {
    case SUIT_H:
        s[0] = (Shape){ SH_HEART, OP_UNION, { 0, 0.02f, 1.0f, 0 }, NULL, 0 };
        return 1;
    case SUIT_D:
        s[0] = (Shape){ SH_POLY, OP_UNION, { 0 }, k_diamond, 8 };
        return 1;
    case SUIT_S:
        s[0] = (Shape){ SH_HEART, OP_UNION, { 0, -0.16f, 0.84f, 1 }, NULL, 0 };
        s[1] = (Shape){ SH_POLY, OP_SMOOTH, { 0, 0, 0, 0, 0, 0, 0, 0.12f }, k_stem, 6 };
        return 2;
    default: /* clubs */
        s[0] = (Shape){ SH_CIRCLE, OP_UNION, { 0, -0.50f, 0.38f }, NULL, 0 };
        s[1] = (Shape){ SH_CIRCLE, OP_SMOOTH, { -0.46f, 0.12f, 0.38f, 0, 0, 0, 0, 0.08f }, NULL, 0 };
        s[2] = (Shape){ SH_CIRCLE, OP_SMOOTH, { 0.46f, 0.12f, 0.38f, 0, 0, 0, 0, 0.08f }, NULL, 0 };
        s[3] = (Shape){ SH_CIRCLE, OP_SMOOTH, { 0, 0.0f, 0.22f, 0, 0, 0, 0, 0.1f }, NULL, 0 };
        s[4] = (Shape){ SH_POLY, OP_SMOOTH, { 0, 0, 0, 0, 0, 0, 0, 0.10f }, k_stem, 6 };
        return 5;
    }
}

void art_suit_colors(int suit, RCol *top, RCol *bottom)
{
    if (suit == SUIT_H || suit == SUIT_D) {
        *top = rc_hex(0xE8264F, 1);
        *bottom = rc_hex(0xA80E36, 1);
    } else {
        *top = rc_hex(0x3A3346, 1);
        *bottom = rc_hex(0x141118, 1);
    }
}

void art_pip(Canvas *cv, int suit, float cx, float cy, float size, float rot, RCol top, RCol bottom,
             const FillOpt *opt)
{
    Shape s[6];
    int n = suit_shapes(suit, s);
    Xf xf = xf_make(cx, cy, size, rot);
    Paint p = paint_linear(top, 0, -1, bottom, 0, 1);
    cv_fill(cv, &xf, s, n, &p, opt);
}

/* ---- paints ------------------------------------------------------------- */

RCol art_brass(float lx, float ly, float d, const void *user)
{
    const BrassParams *bp = user;
    float c = cosf(bp->angle), s = sinf(bp->angle);
    float u = lx * c + ly * s, v = -lx * s + ly * c;
    /* Brushing: fine streaks along u, from 1-D noise across v. */
    float streak = 0.55f * rnoise_value(v * 0.9f, u * 0.02f, bp->seed) + 0.45f * rnoise_value(v * 3.1f, u * 0.05f, bp->seed + 7);
    float band = 0.5f + 0.5f * sinf(u * 0.035f + v * 0.05f);
    RCol dark = rc_hex(0x8A5E1C, 1), mid = rc_hex(0xC9982F, 1), light = rc_hex(0xF6DC8A, 1);
    float t = 0.35f * streak + 0.45f * band + 0.2f * bp->light;
    RCol col = t < 0.5f ? rc_mix(dark, mid, t * 2) : rc_mix(mid, light, (t - 0.5f) * 2);
    /* Bevel: brighter right at the outer edge, darker a little inside. */
    float e = d < 0 ? -d : d;
    if (e < 1.5f) col = rc_mix(col, light, 0.35f * (1.5f - e) / 1.5f);
    return col;
}

RCol art_honeycomb(float lx, float ly, float d, const void *user)
{
    (void)d;
    const HoneyParams *hp = user;
    int q, r;
    float e = hex_grid_dist(lx, ly, hp->r, &q, &r);
    RCol c = hp->base;
    if (hp->cell_prob > 0 && rnoise_hash(q, r, hp->seed) < hp->cell_prob) {
        float k = 0.5f + 0.5f * rnoise_hash(q, r, hp->seed + 1);
        c = rc_mix(c, hp->cell, k);
    }
    float cov = 1.0f - e / hp->line_w;
    if (cov > 0) c = rc_mix(c, hp->line, cov > 1 ? 1 : cov);
    return c;
}

/* ---- the bee ------------------------------------------------------------ */

typedef struct { float alpha; } BeeBody;

static RCol bee_body_paint(float lx, float ly, float d, const void *user)
{
    const BeeBody *b = user;
    (void)d;
    RCol top = rc_hex(0xFFD85C, b->alpha), bot = rc_hex(0xE0850E, b->alpha);
    RCol c = rc_mix(top, bot, (ly + 0.3f) / 0.8f < 0 ? 0 : (ly + 0.3f) / 0.8f > 1 ? 1 : (ly + 0.3f) / 0.8f);
    /* Three dark bands across the abdomen, softened at their edges. */
    float u = lx - 0.02f;
    float ph = fmodf(u + 3.4f, 0.34f);
    float band = 0;
    if (u > -0.22f) band = ph < 0.13f ? 1.0f : 0.0f;
    if (band > 0) c = rc_mix(c, rc_hex(ART_BROWN, b->alpha), 0.92f);
    /* A soft highlight on the upper body. */
    float hx = lx - 0.05f, hy = ly + 0.18f;
    float h = 1.0f - (hx * hx / 0.09f + hy * hy / 0.012f);
    if (h > 0) c = rc_mix(c, rc_hex(0xFFF6C8, b->alpha), 0.55f * h);
    return c;
}

void art_bee(Canvas *cv, float cx, float cy, float scale, float rot, float alpha, int outline)
{
    Xf xf = xf_make(cx, cy, scale, rot);
    RCol ink = rc_hex(ART_BROWN, alpha);
    FillOpt ol = { 0 };
    ol.offset = fmaxf(1.2f, scale * 0.06f);

    Shape wing1[1] = { { SH_ELLIPSE, OP_UNION, { 0.10f, -0.52f, 0.26f, 0.44f }, NULL, 0 } };
    Shape wing2[1] = { { SH_ELLIPSE, OP_UNION, { 0.44f, -0.44f, 0.22f, 0.38f }, NULL, 0 } };
    static const float sting[] = { 0.70f, 0.02f, 1.0f, 0.18f, 0.70f, 0.30f };
    Shape body[2] = {
        { SH_ELLIPSE, OP_UNION, { 0.18f, 0.14f, 0.58f, 0.42f }, NULL, 0 },
        { SH_POLY, OP_SMOOTH, { 0, 0, 0, 0, 0, 0, 0, 0.06f }, sting, 3 },
    };
    Shape head[1] = { { SH_CIRCLE, OP_UNION, { -0.50f, 0.04f, 0.31f }, NULL, 0 } };
    Shape ant[4] = {
        { SH_SEG, OP_UNION, { -0.60f, -0.22f, -0.80f, -0.62f, 0.035f }, NULL, 0 },
        { SH_CIRCLE, OP_UNION, { -0.82f, -0.66f, 0.075f }, NULL, 0 },
        { SH_SEG, OP_UNION, { -0.42f, -0.25f, -0.40f, -0.68f, 0.035f }, NULL, 0 },
        { SH_CIRCLE, OP_UNION, { -0.40f, -0.72f, 0.075f }, NULL, 0 },
    };

    /* Wings sit behind the body; each gets its own tilt. */
    Xf w1 = xf_mul(xf, xf_make(0.10f, -0.52f, 1, -0.45f)), w2 = xf_mul(xf, xf_make(0.44f, -0.44f, 1, -0.9f));
    Shape wl1[1] = { { SH_ELLIPSE, OP_UNION, { 0, 0, 0.26f, 0.44f }, NULL, 0 } };
    Shape wl2[1] = { { SH_ELLIPSE, OP_UNION, { 0, 0, 0.22f, 0.38f }, NULL, 0 } };
    (void)wing1; (void)wing2;
    if (outline) {
        Paint pi = paint_solid(ink);
        cv_fill(cv, &w1, wl1, 1, &pi, &ol);
        cv_fill(cv, &w2, wl2, 1, &pi, &ol);
        cv_fill(cv, &xf, body, 2, &pi, &ol);
        cv_fill(cv, &xf, head, 1, &pi, &ol);
        cv_fill(cv, &xf, ant, 4, &pi, &ol);
    }
    Paint wp = paint_linear(rc(0.88f, 0.98f, 1.0f, 0.92f * alpha), 0, -0.4f, rc(0.55f, 0.86f, 1.0f, 0.7f * alpha), 0, 0.4f);
    cv_fill(cv, &w1, wl1, 1, &wp, NULL);
    cv_fill(cv, &w2, wl2, 1, &wp, NULL);
    FillOpt vein = { 0 };
    vein.outline = fmaxf(0.8f, scale * 0.02f);
    Paint vp = paint_solid(rc(0.3f, 0.6f, 0.8f, 0.5f * alpha));
    cv_fill(cv, &w1, wl1, 1, &vp, &vein);
    cv_fill(cv, &w2, wl2, 1, &vp, &vein);

    BeeBody bb = { alpha };
    Paint bp = paint_fn(bee_body_paint, &bb);
    cv_fill(cv, &xf, body, 2, &bp, NULL);

    Paint ip = paint_solid(ink);
    cv_fill(cv, &xf, ant, 4, &ip, NULL);
    Paint hp = paint_radial(rc_hex(0x4A3526, alpha), -0.58f, -0.06f, rc_hex(ART_BROWN, alpha), 0.34f);
    cv_fill(cv, &xf, head, 1, &hp, NULL);

    Shape eye[1] = { { SH_CIRCLE, OP_UNION, { -0.58f, -0.02f, 0.105f }, NULL, 0 } };
    Shape pupil[1] = { { SH_CIRCLE, OP_UNION, { -0.615f, -0.01f, 0.06f }, NULL, 0 } };
    Shape glint[1] = { { SH_CIRCLE, OP_UNION, { -0.63f, -0.04f, 0.022f }, NULL, 0 } };
    Shape cheek[1] = { { SH_CIRCLE, OP_UNION, { -0.43f, 0.15f, 0.06f }, NULL, 0 } };
    Shape smile[1] = { { SH_ARC, OP_UNION, { -0.56f, 0.10f, 0.09f, 0.018f, 1.9f, 0.7f }, NULL, 0 } };
    Paint white = paint_solid(rc(1, 1, 1, alpha)), dark = paint_solid(rc_hex(0x0C0806, alpha));
    Paint blush = paint_solid(rc_hex(0xFF6FA8, 0.6f * alpha)), smilep = paint_solid(rc_hex(0xFFD85C, 0.9f * alpha));
    cv_fill(cv, &xf, eye, 1, &white, NULL);
    cv_fill(cv, &xf, pupil, 1, &dark, NULL);
    cv_fill(cv, &xf, glint, 1, &white, NULL);
    cv_fill(cv, &xf, cheek, 1, &blush, NULL);
    cv_fill(cv, &xf, smile, 1, &smilep, NULL);
}
