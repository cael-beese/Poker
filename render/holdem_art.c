/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* holdem_art.c - see holdem_art.h. Everything here runs once, at start-up,
 * on the renderer's job threads; each job paints a disjoint region. */
#include "render/holdem_art.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "platform/screen.h"
#include "platform/texreg.h"
#include "render/art.h"
#include "render/raster.h"

#define PI_F 3.14159265f

const int hart_chip_value[HCHIP_N] = { 1, 5, 25, 100, 500, 1000, 5000 };

/* ---- atlas bookkeeping ---------------------------------------------------- */

static int g_chip_id[HCHIP_N];
static int g_av_id[AV_N][AVL_N];
static int g_part_id[HPART_N];
static Spr g_chip[HCHIP_N], g_av[AV_N][AVL_N], g_part[HPART_N];
static int g_ready;

#define TABLE_STRIPS 16
static uint8_t *g_table_px;
static Texture2D g_table;

#define OVER_W 80
#define OVER_H 36

static const int k_part_size[HPART_N][2] = {
    [HPART_WING] = { 40, 56 }, [HPART_ANTENNA] = { 18, 46 }, [HPART_EYE] = { 20, 24 },
    [HPART_PUPIL] = { 12, 12 }, [HPART_BROW] = { 24, 8 }, [HPART_PUCK] = { 40, 34 },
};

/* ---- the rigs --------------------------------------------------------------- */

#define RIG_DEFAULT(theme_, antc_, browc_, over_) \
    { 1, { 52, 76 }, 57, 7.5f, 9.0f, 4.2f, { 53, 75 }, 36, { -0.38f, 0.38f }, antc_, 44, browc_, 24, 40, over_, theme_ }

static const AvatarRig k_rig[AV_N] = {
    [AV_YOU] = { 0, { 0, 0 }, 0, 0, 0, 0, { 0, 0 }, 0, { 0, 0 }, { 0 }, 0, { 0 }, 0, 0, 0, { 255, 210, 90, 255 } },
    [AV_BUZZ] = RIG_DEFAULT(((Color){ 40, 230, 255, 255 }), ((Color){ 43, 29, 20, 255 }), ((Color){ 60, 36, 20, 255 }), 0),
    [AV_HONEY] = RIG_DEFAULT(((Color){ 255, 90, 200, 255 }), ((Color){ 43, 29, 20, 255 }), ((Color){ 150, 90, 30, 255 }), 1),
    [AV_STINGER] = { 1, { 52, 76 }, 57, 7.5f, 9.0f, 4.2f, { 49, 79 }, 24, { -0.42f, 0.42f }, { 30, 22, 18, 255 }, 44,
                     { 20, 14, 12, 255 }, 24, 40, 1, { 255, 70, 70, 255 } },
    [AV_DRONE] = { 1, { 51, 77 }, 60, 7.5f, 8.5f, 4.0f, { 52, 76 }, 34, { -0.30f, 0.30f }, { 43, 29, 20, 255 }, 47,
                   { 70, 50, 30, 255 }, 24, 43, 1, { 90, 140, 255, 255 } },
    [AV_QUEENIE] = RIG_DEFAULT(((Color){ 190, 110, 255, 255 }), ((Color){ 60, 20, 70, 255 }), ((Color){ 90, 40, 60, 255 }), 1),
    [AV_BEE] = RIG_DEFAULT(((Color){ 255, 170, 40, 255 }), ((Color){ 43, 29, 20, 255 }), ((Color){ 60, 36, 20, 255 }), 0),
};

const AvatarRig *hart_rig(int who) { return &k_rig[who >= 0 && who < AV_N ? who : AV_BEE]; }

/* ---- small shape helpers ---------------------------------------------------- */

static Shape circ(float x, float y, float r) { return (Shape){ SH_CIRCLE, OP_UNION, { x, y, r }, NULL, 0 }; }
static Shape ell(float x, float y, float rx, float ry) { return (Shape){ SH_ELLIPSE, OP_UNION, { x, y, rx, ry }, NULL, 0 }; }
static Shape rbox(float x, float y, float hw, float hh, float r) { return (Shape){ SH_RBOX, OP_UNION, { x, y, hw, hh, r }, NULL, 0 }; }
static Shape seg(float x0, float y0, float x1, float y1, float hw) { return (Shape){ SH_SEG, OP_UNION, { x0, y0, x1, y1, hw }, NULL, 0 }; }
static Shape ring(float x, float y, float r, float hw) { return (Shape){ SH_RING, OP_UNION, { x, y, r, hw }, NULL, 0 }; }
static Shape arc(float x, float y, float r, float hw, float mid, float half)
{
    return (Shape){ SH_ARC, OP_UNION, { x, y, r, hw, mid, half }, NULL, 0 };
}
static Shape tri(float x0, float y0, float x1, float y1, float x2, float y2)
{
    return (Shape){ SH_TRI, OP_UNION, { x0, y0, x1, y1, x2, y2 }, NULL, 0 };
}
static Shape hexs(float x, float y, float r, int pointy) { return (Shape){ SH_HEX, OP_UNION, { x, y, r, (float)pointy }, NULL, 0 }; }
static Shape op(Shape s, int o) { s.op = (uint8_t)o; return s; }

static void fill_n(Canvas *cv, const Shape *s, int n, Paint p, const FillOpt *o)
{
    Xf x = xf_identity();
    cv_fill(cv, &x, s, n, &p, o);
}
static void fill1(Canvas *cv, Shape s, Paint p) { fill_n(cv, &s, 1, p, NULL); }
static void fill1o(Canvas *cv, Shape s, Paint p, const FillOpt *o) { fill_n(cv, &s, 1, p, o); }
static void outline1(Canvas *cv, Shape s, RCol c, float px)
{
    FillOpt o = { 0 };
    o.offset = px;
    fill1o(cv, s, paint_solid(c), &o);
}
static Paint solid(uint32_t hex, float a) { return paint_solid(rc_hex(hex, a)); }

/* ---- chips ------------------------------------------------------------------ */

typedef struct { uint32_t base, dark, spot; } ChipLook;
static const ChipLook k_chip_look[HCHIP_N] = {
    { 0xF2EAD8, 0xB8AE9A, 0x3A3346 },   /* 1     ivory   */
    { 0xD01A48, 0x8A0C2C, 0xF8F1E2 },   /* 5     ruby    */
    { 0x15A89A, 0x0A6A60, 0xF8F1E2 },   /* 25    teal    */
    { 0x2A2630, 0x131117, 0xE8AA28 },   /* 100   ink     */
    { 0x8E2AC8, 0x561680, 0xF8F1E2 },   /* 500   violet  */
    { 0xE8AA28, 0x9A6A10, 0x1E1A24 },   /* 1000  honey   */
    { 0xFF7A10, 0xA84400, 0xF8F1E2 },   /* 5000  amber   */
};

typedef struct { RCol base, dark, spot; } ChipEdge;

/* The chip's rim, seen from the side: stripes where the face has spots. */
static RCol chip_edge_paint(float lx, float ly, float d, const void *user)
{
    const ChipEdge *e = user;
    (void)d;
    float u = (lx - 22.0f) / 20.0f;
    if (u < -1) u = -1;
    if (u > 1) u = 1;
    float th = acosf(u);                               /* 0..pi across the front */
    float ph = fmodf(th * 8.0f / PI_F + 0.25f, 2.0f);  /* 8 spots per turn: 4 visible */
    RCol c = rc_mix(e->dark, e->base, 0.35f + 0.4f * sinf(th));
    if (ph < 0.62f) c = rc_mix(c, e->spot, 0.85f);
    /* Lit from above: the top of the edge is brighter. */
    float k = (ly - 15.0f) / 12.0f;
    return rc_scale(c, 1.05f - 0.35f * (k < 0 ? 0 : k > 1 ? 1 : k));
}

static void paint_chip(int denom)
{
    Canvas cv = atlas_canvas(g_chip_id[denom]);
    const ChipLook *L = &k_chip_look[denom];
    RCol base = rc_hex(L->base, 1), dark = rc_hex(L->dark, 1), spot = rc_hex(L->spot, 1);
    const float cx = 22, cy = 15, rx = 20, ry = 12.5f, t = HCHIP_THICK;
    /* Silhouette outline for contrast on dark felt. */
    Shape sil[3] = { ell(cx, cy, rx, ry), op(rbox(cx, cy + t * 0.5f, rx, t * 0.5f, 0), OP_UNION), op(ell(cx, cy + t, rx, ry), OP_UNION) };
    FillOpt ol = { 0 };
    ol.offset = 1.1f;
    fill_n(&cv, sil, 3, paint_solid(rc(0.02f, 0.01f, 0.03f, 0.85f)), &ol);
    ChipEdge ce = { base, dark, spot };
    Shape side[2] = { rbox(cx, cy + t * 0.5f, rx, t * 0.5f, 0), op(ell(cx, cy + t, rx, ry), OP_UNION) };
    fill_n(&cv, side, 2, paint_fn(chip_edge_paint, &ce), NULL);
    /* The face, in unit-circle space squashed to the ellipse. */
    Xf fx = xf_scale2(cx, cy, rx, ry, 0);
    Shape face[1] = { circ(0, 0, 1) };
    Paint fp = paint_radial(rc_mix(base, rc(1, 1, 1, 1), 0.18f), -0.3f, -0.4f, base, 1.3f);
    cv_fill(&cv, &fx, face, 1, &fp, NULL);
    Shape dash[8];
    for (int k = 0; k < 8; k++) dash[k] = arc(0, 0, 0.84f, 0.13f, (float)k * PI_F / 4 + PI_F / 8, 0.17f);
    Paint sp = paint_solid(spot);
    cv_fill(&cv, &fx, dash, 8, &sp, NULL);
    Shape inner[1] = { ring(0, 0, 0.58f, 0.035f) };
    Paint ip = paint_solid(rc_mix(spot, base, 0.35f));
    cv_fill(&cv, &fx, inner, 1, &ip, NULL);
    Shape inlay[1] = { circ(0, 0, 0.52f) };
    Paint inl = paint_radial(rc_mix(base, rc(1, 1, 1, 1), 0.28f), 0, -0.2f, rc_mix(base, dark, 0.15f), 0.6f);
    cv_fill(&cv, &fx, inlay, 1, &inl, NULL);
    Shape hx[1] = { hexs(0, 0, 0.26f, 0) };
    FillOpt ho = { 0 };
    ho.outline = 1.2f;
    Paint hp = paint_solid(rc_mix(spot, base, 0.25f));
    cv_fill(&cv, &fx, hx, 1, &hp, &ho);
    /* A rim highlight at the top of the face. */
    Shape rim[1] = { arc(0, 0, 0.97f, 0.035f, -PI_F / 2, 1.0f) };
    Paint rp = paint_solid(rc(1, 1, 1, 0.35f));
    cv_fill(&cv, &fx, rim, 1, &rp, NULL);
}

/* The dealer puck: a thick ivory disc with a brass ring. */
static void paint_puck(Canvas *cv)
{
    const float cx = 20, cy = 13, rx = 18, ry = 11, t = 8;
    Shape sil[3] = { ell(cx, cy, rx, ry), op(rbox(cx, cy + t * 0.5f, rx, t * 0.5f, 0), OP_UNION), op(ell(cx, cy + t, rx, ry), OP_UNION) };
    FillOpt ol = { 0 };
    ol.offset = 1.1f;
    fill_n(cv, sil, 3, paint_solid(rc(0.02f, 0.01f, 0.03f, 0.9f)), &ol);
    Shape side[2] = { rbox(cx, cy + t * 0.5f, rx, t * 0.5f, 0), op(ell(cx, cy + t, rx, ry), OP_UNION) };
    fill_n(cv, side, 2, paint_linear(rc_hex(0xD8CCB0, 1), 0, cy, rc_hex(0x8A7E68, 1), 0, cy + t + ry), NULL);
    Xf fx = xf_scale2(cx, cy, rx, ry, 0);
    Shape face[1] = { circ(0, 0, 1) };
    Paint fp = paint_radial(rc_hex(0xFFFDF4, 1), -0.3f, -0.4f, rc_hex(0xE8DEC8, 1), 1.3f);
    cv_fill(cv, &fx, face, 1, &fp, NULL);
    Shape rr[1] = { ring(0, 0, 0.84f, 0.07f) };
    BrassParams bp = { 0.3f, 9, 0.7f };
    Paint br = paint_fn(art_brass, &bp);
    cv_fill(cv, &fx, rr, 1, &br, NULL);
}

/* ---- avatar parts ------------------------------------------------------------ */

static void paint_parts(void)
{
    for (int p = 0; p < HPART_N; p++) {
        Canvas cv = atlas_canvas(g_part_id[p]);
        switch (p) {
        case HPART_WING: {
            Shape w = ell(20, 28, 15.5f, 25);
            fill1(&cv, w, paint_linear(rc(0.92f, 0.98f, 1.0f, 0.78f), 20, 4, rc(0.55f, 0.82f, 1.0f, 0.5f), 20, 52));
            FillOpt o = { 0 };
            o.outline = 1.4f;
            fill1o(&cv, w, paint_solid(rc(0.35f, 0.62f, 0.85f, 0.75f)), &o);
            Shape v[2] = { seg(20, 50, 16, 14, 0.7f), op(seg(18, 32, 28, 16, 0.6f), OP_UNION) };
            fill_n(&cv, v, 2, paint_solid(rc(0.35f, 0.62f, 0.85f, 0.45f)), NULL);
            Shape gl = ell(14, 18, 4, 8);
            fill1(&cv, gl, paint_solid(rc(1, 1, 1, 0.45f)));
            break;
        }
        case HPART_ANTENNA: {
            Shape a[2] = { seg(9, 44, 9, 11, 1.9f), op(circ(9, 8, 5.2f), OP_UNION) };
            fill_n(&cv, a, 2, paint_solid(rc(1, 1, 1, 1)), NULL);
            Shape g = circ(7.4f, 6.4f, 1.6f);
            fill1(&cv, g, paint_solid(rc(1, 1, 1, 1)));
            break;
        }
        case HPART_EYE: {
            Shape e = ell(10, 12, 8.3f, 10.3f);
            outline1(&cv, e, rc_hex(0x2B1D14, 1), 1.3f);
            fill1(&cv, e, paint_linear(rc(1, 1, 1, 1), 0, 2, rc_hex(0xE4E0EA, 1), 0, 22));
            break;
        }
        case HPART_PUPIL: {
            fill1(&cv, circ(6, 6, 4.8f), paint_radial(rc_hex(0x3A2418, 1), 6, 7, rc_hex(0x0C0806, 1), 5));
            fill1(&cv, circ(4.4f, 4.2f, 1.5f), paint_solid(rc(1, 1, 1, 1)));
            break;
        }
        case HPART_BROW: fill1(&cv, seg(4, 4, 20, 4, 2.6f), paint_solid(rc(1, 1, 1, 1))); break;
        case HPART_PUCK: paint_puck(&cv); break;
        default: break;
        }
    }
}

/* ---- avatars ------------------------------------------------------------------ */

typedef struct { uint32_t light, dark; } Theme;
static const Theme k_theme[AV_N] = {
    [AV_YOU] = { 0x4A3014, 0x120A06 },   [AV_BUZZ] = { 0x145A66, 0x061418 },   [AV_HONEY] = { 0x6A1450, 0x1A0614 },
    [AV_STINGER] = { 0x5A1418, 0x160406 }, [AV_DRONE] = { 0x1C2A66, 0x060A1C }, [AV_QUEENIE] = { 0x44146A, 0x10061A },
    [AV_BEE] = { 0x5A3A0C, 0x160E04 },
};

static void paint_back(Canvas *cv, int who)
{
    const Theme *t = &k_theme[who];
    fill1(cv, circ(64, 64, 58), paint_radial(rc_hex(t->light, 1), 64, 38, rc_hex(t->dark, 1), 78));
    HoneyParams hp = { 9, rc(0, 0, 0, 0), rc_hex(ART_HONEY, 0.13f), rc(0, 0, 0, 0), 1.0f, (uint32_t)(11 + who), 0 };
    fill1(cv, circ(64, 64, 57), paint_fn(art_honeycomb, &hp));
    /* A soft key light behind the head. */
    FillOpt g = { 0 };
    g.glow = 10;
    g.opacity = 0.35f;
    fill1o(cv, circ(64, 56, 22), paint_solid(rc_mix(rc_hex(t->light, 1), rc(1, 0.9f, 0.7f, 1), 0.5f)), &g);
    BrassParams bp = { 0.8f, (uint32_t)(31 + who), 0.6f };
    fill1(cv, ring(64, 64, 59.5f, 3.6f), paint_fn(art_brass, &bp));
    fill1(cv, ring(64, 64, 55.4f, 0.8f), paint_solid(rc(0, 0, 0, 0.6f)));
}

/* Clip a body shape to the portrait disc. */
static void body(Canvas *cv, Shape s, Paint p)
{
    Shape b[2] = { s, op(circ(64, 64, 55.5f), OP_INTER) };
    fill_n(cv, b, 2, p, NULL);
}

static void head(Canvas *cv, float x, float y, float r, uint32_t light, uint32_t dark)
{
    outline1(cv, circ(x, y, r), rc_hex(ART_BROWN, 1), 1.8f);
    fill1(cv, circ(x, y, r), paint_radial(rc_hex(light, 1), x - 8, y - 12, rc_hex(dark, 1), r * 1.35f));
}

static void cheeks(Canvas *cv, float y, float a)
{
    fill1(cv, ell(44, y, 6.5f, 4.2f), paint_solid(rc_hex(0xFF5F9A, a)));
    fill1(cv, ell(84, y, 6.5f, 4.2f), paint_solid(rc_hex(0xFF5F9A, a)));
}

static void smile(Canvas *cv, float x, float y, float r, float w, uint32_t col)
{
    fill1(cv, arc(x, y, r, w, PI_F / 2, 0.85f), solid(col, 1));
}

/* Striped bee torso visible at the neckline. */
static RCol stripe_paint(float lx, float ly, float d, const void *user)
{
    (void)lx; (void)d; (void)user;
    return fmodf(ly + 200.0f, 9.0f) < 4.5f ? rc_hex(0xF2B530, 1) : rc_hex(0x2B1D14, 1);
}

static void neck_stripes(Canvas *cv)
{
    Shape v[2] = { tri(50, 88, 78, 88, 64, 112), op(circ(64, 64, 55.5f), OP_INTER) };
    fill_n(cv, v, 2, paint_fn(stripe_paint, NULL), NULL);
}

static void paint_you(Canvas *cv)
{
    /* The player's badge: a brass hex medallion with a honey silhouette. */
    Shape hx = hexs(64, 64, 44, 1);
    fill1(cv, hx, paint_radial(rc_hex(0x3A2410, 1), 64, 40, rc_hex(0x100A06, 1), 60));
    Shape sil[3] = { circ(64, 50, 15), op(ell(64, 102, 32, 24), OP_UNION), op(hexs(64, 64, 41, 1), OP_INTER) };
    fill_n(cv, sil, 3, paint_linear(rc_hex(0xFFE28A, 1), 0, 34, rc_hex(0xE0850E, 1), 0, 110), NULL);
    FillOpt g = { 0 };
    g.outline = 1.4f;
    g.glow = 3;
    fill_n(cv, sil, 3, paint_solid(rc_hex(0xFFF2C0, 0.8f)), &g);
    BrassParams bp = { 0.5f, 77, 0.8f };
    FillOpt ro = { 0 };
    ro.outline = 5;
    fill1o(cv, hx, paint_fn(art_brass, &bp), &ro);
}

static void paint_front(Canvas *cv, int who)
{
    switch (who) {
    case AV_YOU: paint_you(cv); return;
    case AV_BUZZ: {
        body(cv, ell(64, 122, 42, 32), paint_linear(rc_hex(0xE0309A, 1), 0, 92, rc_hex(0x8A145C, 1), 0, 128));
        neck_stripes(cv);
        fill1(cv, seg(56, 96, 55, 116, 1.3f), solid(0xF8F1E2, 1));
        fill1(cv, seg(72, 96, 73, 116, 1.3f), solid(0xF8F1E2, 1));
        head(cv, 64, 60, 30, 0xFFE27A, 0xE0981C);
        /* Backwards cap: the dome, the strap band and the button. */
        Shape dome[2] = { circ(64, 60, 31.5f), op(rbox(64, 28, 40, 16, 0), OP_INTER) };
        fill_n(cv, dome, 2, paint_linear(rc_hex(0xFF4AB8, 1), 0, 28, rc_hex(0xB0187A, 1), 0, 44), NULL);
        fill1(cv, rbox(64, 43, 28, 2.6f, 2), solid(0x7A0E52, 1));
        fill1(cv, rbox(64, 39.5f, 6, 3, 1.5f), solid(0x28E6FF, 1));
        fill1(cv, circ(64, 29, 2.4f), solid(0x28E6FF, 1));
        cheeks(cv, 70, 0.5f);
        for (int i = 0; i < 3; i++) {
            fill1(cv, circ(40 + i * 3.5f, 66 + (i & 1) * 2.5f, 0.9f), solid(0x9A5A14, 0.8f));
            fill1(cv, circ(81 + i * 3.5f, 66 + (i & 1) * 2.5f, 0.9f), solid(0x9A5A14, 0.8f));
        }
        /* The grin. */
        Shape m[2] = { ell(64, 70, 11, 8), op(rbox(64, 78, 16, 8, 0), OP_INTER) };
        fill_n(cv, m, 2, solid(0x3A1208, 1), NULL);
        fill1(cv, rbox(64, 71.6f, 8.5f, 1.8f, 1), solid(0xFFFFFF, 1));
        fill1(cv, ell(64, 76.5f, 5, 2), solid(0xE0506A, 1));
        return;
    }
    case AV_HONEY: {
        /* Hair behind the head, and the bun. */
        fill1(cv, circ(64, 58, 36.5f), paint_linear(rc_hex(0xF6C45A, 1), 0, 24, rc_hex(0xB87414, 1), 0, 94));
        fill1(cv, circ(64, 25, 13), paint_radial(rc_hex(0xFFD878, 1), 60, 20, rc_hex(0xC8801C, 1), 16));
        body(cv, ell(64, 124, 40, 30), paint_linear(rc_hex(0xFF9A30, 1), 0, 94, rc_hex(0xB8520A, 1), 0, 128));
        fill1(cv, arc(64, 96, 10, 1.6f, PI_F / 2, 1.2f), solid(0xFFE6A0, 1));
        head(cv, 64, 61, 29, 0xFFE27A, 0xE0981C);
        /* Fringe. */
        Shape fr[2] = { circ(64, 61, 30.5f), op(rbox(64, 36, 40, 8.5f, 0), OP_INTER) };
        fill_n(cv, fr, 2, paint_linear(rc_hex(0xFFD070, 1), 0, 30, rc_hex(0xD08A20, 1), 0, 45), NULL);
        fill1(cv, arc(50, 44, 6, 3.5f, PI_F / 2, 1.3f), solid(0xE4A43C, 1));
        fill1(cv, arc(64, 45, 6, 3.5f, PI_F / 2, 1.3f), solid(0xE4A43C, 1));
        fill1(cv, arc(78, 44, 6, 3.5f, PI_F / 2, 1.3f), solid(0xE4A43C, 1));
        /* A flower in the bun. */
        for (int k = 0; k < 5; k++) {
            float a = (float)k * 2 * PI_F / 5 - PI_F / 2;
            fill1(cv, circ(82 + 5.2f * cosf(a), 27 + 5.2f * sinf(a), 3.8f), solid(0xFF3CB4, 1));
        }
        fill1(cv, circ(82, 27, 2.8f), solid(0xFFE06A, 1));
        cheeks(cv, 70, 0.6f);
        smile(cv, 64, 67, 8, 1.7f, 0x6A1A1A);
        fill1(cv, ell(64, 75.6f, 3.4f, 1.6f), solid(0xE8306A, 0.9f));
        return;
    }
    case AV_STINGER: {
        body(cv, ell(64, 122, 44, 32), paint_linear(rc_hex(0x2C2A34, 1), 0, 92, rc_hex(0x0E0C12, 1), 0, 128));
        neck_stripes(cv);
        /* Lapels. */
        fill1(cv, tri(50, 90, 60, 90, 56, 118), solid(0x3C3A46, 1));
        fill1(cv, tri(78, 90, 68, 90, 72, 118), solid(0x3C3A46, 1));
        head(cv, 64, 61, 30, 0xF0C050, 0xB8741A);
        /* Stubble. */
        for (int i = 0; i < 26; i++) {
            float a = PI_F * 0.18f + PI_F * 0.64f * (float)i / 25.0f;
            float rr = 20 + (float)(i % 3) * 2.6f;
            fill1(cv, circ(64 + rr * cosf(a), 64 + rr * sinf(a) * 0.72f, 0.8f), solid(0x4A2A10, 0.55f));
        }
        /* Fedora: brim, crown, band. */
        fill1(cv, ell(64, 37, 41, 6.5f), paint_linear(rc_hex(0x3A3844, 1), 0, 31, rc_hex(0x16141C, 1), 0, 44));
        fill1(cv, rbox(64, 24, 23, 12, 7), paint_linear(rc_hex(0x403E4C, 1), 0, 12, rc_hex(0x1C1A22, 1), 0, 36));
        fill1(cv, rbox(64, 18, 12, 2, 2), solid(0x121016, 0.8f));
        fill1(cv, rbox(64, 31.5f, 23, 3, 1), solid(0x28E6FF, 1));
        /* Scar, smirk, toothpick. */
        fill1(cv, seg(85, 66, 90, 78, 1.1f), solid(0x9A2A2A, 0.8f));
        fill1(cv, arc(68, 68, 9, 1.6f, PI_F / 2 - 0.25f, 0.6f), solid(0x3A1208, 1));
        fill1(cv, seg(74, 76, 92, 70, 1.0f), solid(0xE8D0A0, 1));
        return;
    }
    case AV_DRONE: {
        body(cv, ell(64, 124, 44, 32), paint_linear(rc_hex(0x5A6070, 1), 0, 94, rc_hex(0x262A34, 1), 0, 128));
        fill1(cv, ell(64, 98, 14, 6), solid(0x3A3E4A, 1));
        head(cv, 64, 62, 33, 0xFFEEA8, 0xE0A838);
        /* Headphones. */
        fill1(cv, arc(64, 62, 37, 3.6f, -PI_F / 2, 1.28f), paint_linear(rc_hex(0x5A5E6A, 1), 0, 22, rc_hex(0x22242C, 1), 0, 50));
        for (int s = 0; s < 2; s++) {
            float x = s ? 100 : 28;
            outline1(cv, rbox(x, 64, 7.5f, 13, 5), rc_hex(0x101218, 1), 1.4f);
            fill1(cv, rbox(x, 64, 7.5f, 13, 5), paint_linear(rc_hex(0x6AF0FF, 1), 0, 52, rc_hex(0x1890B0, 1), 0, 77));
            fill1(cv, rbox(x + (s ? -2.5f : 2.5f), 64, 2, 9, 1), solid(0xFFFFFF, 0.35f));
        }
        cheeks(cv, 73, 0.35f);
        fill1(cv, seg(57, 79, 71, 78, 1.5f), solid(0x3A1208, 1));
        return;
    }
    case AV_QUEENIE: {
        body(cv, ell(64, 124, 44, 32), paint_linear(rc_hex(0x8A2AC8, 1), 0, 94, rc_hex(0x40105E, 1), 0, 128));
        /* Ermine collar. */
        body(cv, rbox(64, 101, 38, 6, 5), solid(0xF8F1E2, 1));
        for (int i = 0; i < 7; i++) fill1(cv, ell(36 + i * 9.3f, 101 + (i & 1) * 1.5f, 1.1f, 2), solid(0x14101A, 1));
        head(cv, 64, 61, 29, 0xFFE27A, 0xE0981C);
        /* Pearls. */
        for (int i = 0; i < 9; i++) {
            float a = PI_F * 0.22f + PI_F * 0.56f * (float)i / 8.0f;
            fill1(cv, circ(64 + 22 * cosf(a), 76 + 16 * sinf(a), 1.9f), paint_radial(rc(1, 1, 1, 1), -0.5f, -0.5f, rc_hex(0xD8D0E0, 1), 2.5f));
        }
        /* Crown: band, three points, gems. */
        Paint gold = paint_linear(rc_hex(0xFFE890, 1), 0, 12, rc_hex(0xC88A18, 1), 0, 40);
        Shape cr[4] = { rbox(64, 35, 21, 4.5f, 1.5f), op(tri(44, 34, 52, 34, 47, 17), OP_UNION),
                        op(tri(58, 34, 70, 34, 64, 12), OP_UNION), op(tri(76, 34, 84, 34, 81, 17), OP_UNION) };
        FillOpt o = { 0 };
        o.offset = 1.2f;
        fill_n(cv, cr, 4, paint_solid(rc_hex(0x5A3A08, 1)), &o);
        fill_n(cv, cr, 4, gold, NULL);
        fill1(cv, circ(47, 17, 2.4f), solid(0x28E6FF, 1));
        fill1(cv, circ(64, 12, 2.8f), solid(0xD01A48, 1));
        fill1(cv, circ(81, 17, 2.4f), solid(0x28E6FF, 1));
        fill1(cv, circ(64, 35, 2.2f), solid(0xD01A48, 1));
        cheeks(cv, 70, 0.5f);
        smile(cv, 64, 66, 8.5f, 2.2f, 0xD01A6A);
        return;
    }
    default: {   /* AV_BEE: the house bee, dealer visor and bow tie */
        body(cv, ell(64, 124, 42, 32), paint_linear(rc_hex(0x201A26, 1), 0, 94, rc_hex(0x0C0A10, 1), 0, 128));
        body(cv, tri(52, 92, 76, 92, 64, 124), solid(0xF8F1E2, 1));
        fill1(cv, tri(52, 98, 52, 108, 64, 103), solid(0xFF28C8, 1));
        fill1(cv, tri(76, 98, 76, 108, 64, 103), solid(0xFF28C8, 1));
        fill1(cv, circ(64, 103, 2.6f), solid(0xB0108A, 1));
        head(cv, 64, 60, 30, 0xFFE27A, 0xE0981C);
        /* Visor: band and translucent green brim. */
        fill1(cv, rbox(64, 37, 29, 3.2f, 2), solid(0x16603A, 1));
        Shape brim[2] = { ell(64, 41, 35, 9), op(rbox(64, 47, 40, 6, 0), OP_INTER) };
        fill_n(cv, brim, 2, paint_solid(rc_hex(0x2AD07A, 0.72f)), NULL);
        fill1(cv, arc(64, 41, 34, 0.9f, PI_F / 2, 1.1f), solid(0xB0FFD0, 0.8f));
        cheeks(cv, 70, 0.5f);
        smile(cv, 64, 66, 9, 1.8f, 0x3A1208);
        return;
    }
    }
}

/* Lids and lashes, over the eyes. Coordinates are the over layer's own
 * (its top-left sits at rig over_x, over_y in the avatar). */
static void paint_over(Canvas *cv, int who)
{
    const AvatarRig *r = &k_rig[who];
    for (int s = 0; s < 2; s++) {
        float ex = r->eye_x[s] - r->over_x, ey = r->eye_y - r->over_y;
        float rx = r->eye_rx + 1.3f, ry = r->eye_ry + 1.3f;
        if (who == AV_STINGER || who == AV_DRONE) {
            float cover = who == AV_DRONE ? 0.62f : 0.44f;     /* of the eye's height, from the top */
            float top = ey - ry - 3, bottom = ey - ry + 2 * ry * cover;
            float hh = (bottom - top) * 0.5f;
            Shape lid[2] = { ell(ex, ey, rx, ry), op(rbox(ex, top + hh, rx + 3, hh, 0), OP_INTER) };
            fill_n(cv, lid, 2, paint_solid(rc_hex(who == AV_DRONE ? 0xF6D478 : 0xE8B040, 1)), NULL);
            fill1(cv, seg(ex - rx + 0.5f, bottom, ex + rx - 0.5f, bottom, 1.2f), solid(0x2B1D14, 1));
        } else {
            /* Three lashes at the outer top of each eye. */
            float dir = s ? 1.0f : -1.0f;
            for (int k = 0; k < 3; k++) {
                float a = -PI_F / 2 + dir * (0.45f + 0.35f * (float)k);
                float x0 = ex + cosf(a) * (rx - 1.0f), y0 = ey + sinf(a) * (ry - 1.0f);
                float x1 = ex + cosf(a) * (rx + 3.8f), y1 = ey + sinf(a) * (ry + 3.2f);
                fill1(cv, seg(x0, y0, x1 + dir * 1.2f, y1, 1.0f), solid(0x2B1D14, 1));
            }
        }
    }
}

static void paint_avatar(int who)
{
    Canvas b = atlas_canvas(g_av_id[who][AVL_BACK]);
    paint_back(&b, who);
    Canvas f = atlas_canvas(g_av_id[who][AVL_FRONT]);
    paint_front(&f, who);
    if (g_av_id[who][AVL_OVER] >= 0) {
        Canvas o = atlas_canvas(g_av_id[who][AVL_OVER]);
        paint_over(&o, who);
    }
}

/* ---- the table backdrop -------------------------------------------------------- */

static float stadium(float x, float y, float inset)
{
    /* Signed distance to the table's stadium outline (straight sides, round
       ends), moved inwards by inset. */
    float r = HT_HY - inset;
    float qx = fabsf(x - HT_CX) - (HT_HX - HT_HY), qy = fabsf(y - HT_CY);
    if (qx <= 0) return qy - r;
    return sqrtf(qx * qx + qy * qy) - r;
}

static float sat01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

static RCol room_color(float fx, float fy, int x, int y)
{
    RCol top = rc_hex(0x1D1026, 1), bot = rc_hex(0x09070B, 1), warm = rc_hex(0x3A1C2A, 1);
    float t = fy / PLAY_H;
    RCol c = rc_mix(top, bot, t * (1.6f - 0.6f * t));
    float wx = (fx - 640) / 640, wy = (fy - 380) / 420;
    float w = 1 - (wx * wx + wy * wy);
    if (w > 0) c = rc_mix(c, warm, 0.45f * w * w);
    int q, r;
    float e = hex_grid_dist(fx, fy, 36, &q, &r);
    float h = rnoise_hash(q, r, 77);
    if (h < 0.08f) c = rc_mix(c, rc_hex(ART_AMBER, 1), 0.05f);
    else if (h < 0.10f) c = rc_mix(c, rc_hex(ART_MAGENTA, 1), 0.04f);
    float cov = 1 - e / 1.3f;
    if (cov > 0) c = rc_mix(c, rc_hex(ART_HONEY, 1), 0.09f * cov);
    float vx = (fx - 640) / 760, vy = (fy - 360) / 520;
    float vig = 1 - 0.6f * (vx * vx + vy * vy);
    c = rc_scale(c, vig < 0.25f ? 0.25f : vig);
    float g = (rnoise_hash(x, y, 5) - 0.5f) * 0.012f;
    return rc(c.r + g, c.g + g, c.b + g, 1);
}

static RCol felt_color(float fx, float fy, float d, int x, int y)
{
    /* Charcoal felt, warmer and lighter where the lamp over the table falls. */
    float lx = (fx - HT_CX) / 520, ly = (fy - (HT_CY - 10)) / 250;
    float l = 1 - (lx * lx + ly * ly);
    l = l < 0 ? 0 : l;
    RCol c = rc_mix(rc_hex(0x0F0D12, 1), rc_hex(0x2A1C26, 1), 0.85f * l * (1.4f - 0.4f * l));
    /* Felt grain. */
    float g = (rnoise_hash(x, y, 13) - 0.5f) * 0.03f;
    c = rc(c.r + g, c.g + g, c.b + g, 1);
    /* A faint honeycomb woven into the cloth. */
    int q, r;
    float e = hex_grid_dist(fx + 11, fy + 7, 22, &q, &r);
    float cov = 1 - e / 1.1f;
    if (cov > 0) c = rc_mix(c, rc_hex(ART_HONEY, 1), (0.035f + 0.05f * l) * cov);
    else if (e < 3.5f) c = rc_scale(c, 0.97f);
    /* The rail's shadow on the cloth. */
    float sh = d > -HT_RAIL - 60 ? expf((d + HT_RAIL) / 11.0f) : 0;
    c = rc_scale(c, 1 - 0.55f * sat01(sh));
    /* The neon inlay (baked dim; the presentation lights it). */
    float dn = fabsf(d + HT_NEON);
    RCol mag = rc_hex(ART_MAGENTA, 1);
    c = rc_mix(c, rc_mix(mag, rc(1, 1, 1, 1), 0.25f), 0.42f * sat01(1.3f - dn));
    if (dn < 30) c = rc_mix(c, mag, 0.16f * expf(-dn / 4.5f));
    /* The betting line. */
    float db = fabsf(d + HT_BETLINE);
    c = rc_mix(c, rc_hex(ART_HONEY, 1), 0.20f * sat01(1.1f - db));
    return c;
}

static RCol rail_color(float fx, float fy, float d)
{
    static const BrassParams outer = { 0.2f, 57, 0.6f }, inner = { 0.2f, 61, 0.4f };
    float e = -d;   /* depth into the rail, 0 at the outer edge */
    if (e < 6.0f) return art_brass(fx, fy, d, &outer);
    if (e >= HT_RAIL - 4.0f) return art_brass(fx, fy, e - HT_RAIL, &inner);
    /* The padded leather cushion: rounded profile, lit from above. */
    float u = (e - 6.0f) / (HT_RAIL - 10.0f);
    float cushion = sinf(u * PI_F);
    float top = fy < HT_CY ? 1.0f : 0.55f;
    RCol c = rc_mix(rc_hex(0x0C080B, 1), rc_hex(0x3A2830, 1), 0.65f * sqrtf(cushion) * (0.6f + 0.4f * top));
    float spec = expf(-((u - 0.32f) * (u - 0.32f)) / 0.012f);
    c = rc_mix(c, rc_hex(0xFFE6D0, 1), 0.10f * spec * top);
    float g = (rnoise_value(fx * 0.6f, fy * 0.6f, 23) - 0.5f) * 0.03f;
    return rc(c.r + g, c.g + g, c.b + g, 1);
}

static void table_strip(int s)
{
    int y0 = PLAY_H * s / TABLE_STRIPS, y1 = PLAY_H * (s + 1) / TABLE_STRIPS;
    for (int y = y0; y < y1; y++) {
        uint8_t *row = g_table_px + (size_t)y * PLAY_W * 4;
        for (int x = 0; x < PLAY_W; x++) {
            float fx = x + 0.5f, fy = y + 0.5f;
            float d = stadium(fx, fy, 0);
            RCol c;
            if (d > 1.0f) {
                c = room_color(fx, fy, x, y);
                /* The table's shadow on the floor. */
                float ds = stadium(fx, fy - 16, 0);
                float sh = ds < 160 ? expf(-(ds > 0 ? ds : 0) / 26.0f) : 0;
                c = rc_scale(c, 1 - 0.8f * sh);
            } else if (d > -HT_RAIL) {
                c = rail_color(fx, fy, d);
                if (d > 0) c = rc_mix(c, rc_scale(room_color(fx, fy, x, y), 0.2f), d);
                float din = d + HT_RAIL;        /* soft edge to the felt */
                if (din < 1.0f) c = rc_mix(felt_color(fx, fy, d, x, y), c, sat01(din));
            } else {
                c = felt_color(fx, fy, d, x, y);
            }
            uint8_t *p = row + x * 4;
            p[0] = (uint8_t)(sat01(c.r) * 255);
            p[1] = (uint8_t)(sat01(c.g) * 255);
            p[2] = (uint8_t)(sat01(c.b) * 255);
            p[3] = 255;
        }
    }
    /* Details, clipped to this strip: the house emblem under the board. */
    Canvas cv = { g_table_px + (size_t)y0 * PLAY_W * 4, PLAY_W, y1 - y0, PLAY_W };
    float oy = -(float)y0;
    Xf xf = xf_make(0, oy, 1, 0);
    Shape em[1] = { hexs(HT_CX, HT_CY + 6, 128, 0) };
    FillOpt o = { 0 };
    o.outline = 2.0f;
    o.opacity = 0.16f;
    Paint hp = paint_solid(rc_hex(ART_HONEY, 1));
    cv_fill(&cv, &xf, em, 1, &hp, &o);
    Shape em2[1] = { hexs(HT_CX, HT_CY + 6, 120, 0) };
    o.outline = 1.0f;
    o.opacity = 0.10f;
    cv_fill(&cv, &xf, em2, 1, &hp, &o);
    art_bee(&cv, HT_CX + 6, HT_CY + 150 + oy, 30, -0.08f, 0.10f, 0);
}

/* ---- start-up interface -------------------------------------------------------- */

void holdem_art_declare(void)
{
    g_ready = 0;
    for (int i = 0; i < HCHIP_N; i++) g_chip_id[i] = atlas_declare(HCHIP_W, HCHIP_H);
    for (int a = 0; a < AV_N; a++) {
        g_av_id[a][AVL_BACK] = atlas_declare(HAV_SIZE, HAV_SIZE);
        g_av_id[a][AVL_FRONT] = atlas_declare(HAV_SIZE, HAV_SIZE);
        g_av_id[a][AVL_OVER] = k_rig[a].has_over ? atlas_declare(OVER_W, OVER_H) : -1;
    }
    for (int p = 0; p < HPART_N; p++) g_part_id[p] = atlas_declare(k_part_size[p][0], k_part_size[p][1]);
    g_table_px = calloc((size_t)PLAY_W * PLAY_H, 4);
}

/* Jobs: table strips (the heavy ones first), then avatars, chips, parts. */
int holdem_art_job_count(void) { return TABLE_STRIPS + AV_N + 2; }

void holdem_art_paint_job(int i)
{
    int ok = g_chip_id[0] >= 0;
    if (i < TABLE_STRIPS) {
        if (g_table_px) table_strip(i);
        return;
    }
    if (!ok) return;
    i -= TABLE_STRIPS;
    if (i < AV_N) { paint_avatar(i); return; }
    i -= AV_N;
    if (i == 0) {
        for (int c = 0; c < HCHIP_N; c++) paint_chip(c);
    } else {
        paint_parts();
    }
}

void holdem_art_finish(void)
{
    for (int i = 0; i < HCHIP_N; i++) g_chip[i] = atlas_spr(g_chip_id[i]);
    for (int a = 0; a < AV_N; a++)
        for (int l = 0; l < AVL_N; l++)
            if (g_av_id[a][l] >= 0) g_av[a][l] = atlas_spr(g_av_id[a][l]);
    for (int p = 0; p < HPART_N; p++) g_part[p] = atlas_spr(g_part_id[p]);
    if (g_table_px) {
        Image img = { g_table_px, PLAY_W, PLAY_H, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
        g_table = texreg_load_image("holdem table 1280x720", img);
        SetTextureFilter(g_table, TEXTURE_FILTER_POINT);
        free(g_table_px);
        g_table_px = NULL;
    }
    g_ready = 1;
}

void holdem_art_shutdown(void)
{
    if (g_table.id) texreg_unload(g_table);
    memset(&g_table, 0, sizeof g_table);
    g_ready = 0;
}

const Spr *hart_chip(int denom) { return &g_chip[denom < 0 ? 0 : denom >= HCHIP_N ? HCHIP_N - 1 : denom]; }

const Spr *hart_avatar(int who, int layer)
{
    if (who < 0 || who >= AV_N || layer < 0 || layer >= AVL_N || g_av_id[who][layer] < 0) return NULL;
    return &g_av[who][layer];
}

const Spr *hart_part(int part) { return &g_part[part < 0 ? 0 : part >= HPART_N ? HPART_N - 1 : part]; }

unsigned hart_table_tex(void) { return g_ready ? g_table.id : 0; }
