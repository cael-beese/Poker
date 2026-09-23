/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* cards.c - see cards.h. Everything is designed in "card units": a card is
 * 200 x 280 units, scaled to the pixel size of each CardSize. */
#include "render/cards.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "platform/fx_settings.h"
#include "render/art.h"
#include "render/sprites.h"
#include "render/text.h"

#define PI_F 3.14159265f

static const int k_w[CARD_NSIZES] = { CARD_L_W, CARD_M_W };
static const int k_h[CARD_NSIZES] = { CARD_L_H, CARD_M_H };

static int g_face_item[CARD_NSIZES][52], g_back_item[CARD_NSIZES][CARD_BACKS], g_sheen_item;
static Spr g_face[CARD_NSIZES][52], g_back[CARD_NSIZES][CARD_BACKS], g_sheen[CARD_SHEEN_FRAMES];

float card_w(CardSize s) { return (float)k_w[s]; }
float card_h(CardSize s) { return (float)k_h[s]; }

void cards_declare(void)
{
    for (int s = 0; s < CARD_NSIZES; s++) {
        for (int c = 0; c < 52; c++) g_face_item[s][c] = atlas_declare(k_w[s], k_h[s]);
        for (int b = 0; b < CARD_BACKS; b++) g_back_item[s][b] = atlas_declare(k_w[s], k_h[s]);
    }
    g_sheen_item = atlas_declare(CARD_SHEEN_FRAMES * (CARD_L_W / 2 + 2), CARD_L_H / 2);
}

int cards_job_count(void) { return CARD_NSIZES * (52 + CARD_BACKS) + 1; }

/* ---- paints ------------------------------------------------------------- */

typedef struct { float scale; } PaperP;

static RCol paper_paint(float lx, float ly, float d, const void *u)
{
    (void)d; (void)u;
    RCol c = rc_hex(ART_IVORY, 1);
    /* Warmer and a touch darker towards the edges, like real stock under light. */
    float dx = (lx - 100) / 100, dy = (ly - 140) / 140;
    float v = dx * dx + dy * dy;
    c = rc_mix(c, rc_hex(0xEADBC0, 1), 0.55f * v);
    /* A barely-there honeycomb watermark. */
    float e = hex_grid_dist(lx, ly, 9.0f, NULL, NULL);
    if (e < 0.7f) c = rc_mix(c, rc_hex(0xD9B774, 1), 0.10f * (1 - e / 0.7f));
    /* Paper grain. */
    float g = rnoise_value(lx * 0.9f, ly * 0.9f, 11) - 0.5f;
    return rc_scale(c, 1.0f + g * 0.025f);
}

typedef struct { RCol a, b, line; float r; uint32_t seed; RCol cell; } BackP;

static RCol back_paint(float lx, float ly, float d, const void *u)
{
    const BackP *bp = u;
    (void)d;
    float dx = (lx - 100) / 95, dy = (ly - 140) / 135;
    float v = sqrtf(dx * dx + dy * dy);
    RCol c = rc_mix(bp->a, bp->b, v > 1 ? 1 : v);
    int q, r;
    float e = hex_grid_dist(lx - 100, ly - 140, bp->r, &q, &r);
    float h = rnoise_hash(q, r, bp->seed);
    if (h < 0.16f) c = rc_mix(c, bp->cell, 0.25f + 0.35f * rnoise_hash(q, r, bp->seed + 3));
    float cov = 1.0f - e / 1.1f;
    if (cov > 0) c = rc_mix(c, bp->line, 0.75f * cov);
    /* Soft glow along the lines near the medallion. */
    if (e < 3.0f) c = rc_mix(c, bp->line, 0.10f * (1 - e / 3.0f) * (1.2f - v));
    return c;
}

typedef struct { RCol a, b, line; } PanelP;

static RCol panel_paint(float lx, float ly, float d, const void *u)
{
    const PanelP *pp = u;
    (void)d;
    float t = ly / 212.0f;
    RCol c = rc_mix(pp->a, pp->b, fabsf(t - 0.5f) * 2);
    float e = hex_grid_dist(lx, ly, 8.0f, NULL, NULL);
    float cov = 1.0f - e / 0.9f;
    if (cov > 0) c = rc_mix(c, pp->line, 0.28f * cov);
    return c;
}

typedef struct { RCol top, bot, hex; } RobeP;

static RCol robe_paint(float lx, float ly, float d, const void *u)
{
    const RobeP *rp = u;
    float t = (ly - 60) / 46;
    RCol c = rc_mix(rp->top, rp->bot, t < 0 ? 0 : t > 1 ? 1 : t);
    float e = hex_grid_dist(lx, ly, 5.0f, NULL, NULL);
    float cov = 1.0f - e / 0.7f;
    if (cov > 0) c = rc_mix(c, rp->hex, 0.45f * cov);
    /* Darken near the robe's edge for a little volume. */
    if (d > -4) c = rc_scale(c, 0.82f + 0.18f * (-d / 4 > 1 ? 1 : -d / 4 < 0 ? 0 : -d / 4));
    return c;
}

static RCol gold_top(void) { return rc_hex(0xFFE9A0, 1); }
static RCol gold_bot(void) { return rc_hex(0xC07A18, 1); }

/* ---- index -------------------------------------------------------------- */

static void blit_glyph(Canvas *cv, int size_class, char ch, float x, float y, float sx, const Xf *outer, RCol top,
                       RCol bot)
{
    GlyphBitmap g;
    if (text_card_glyph(size_class, ch, &g) != 0) return;
    /* Glyph pixels -> canvas: scale sx horizontally, 1 vertically, at (x, y). */
    Xf gx = { sx, 0, x, 0, 1, y };
    Xf m = xf_mul(*outer, gx);
    Paint p = paint_linear(top, 0, 0, bot, 0, (float)g.h);
    cv_blit_alpha(cv, &m, g.px, g.w, g.h, &p, 1.0f);
}

static float glyph_w(int size_class, char ch)
{
    GlyphBitmap g;
    return text_card_glyph(size_class, ch, &g) == 0 ? (float)g.w : 0;
}

static float glyph_h(int size_class, char ch)
{
    GlyphBitmap g;
    return text_card_glyph(size_class, ch, &g) == 0 ? (float)g.h : 0;
}

/* One corner index (rank over suit), drawn in canvas px through `outer`
 * (identity for the top-left, a point reflection for the bottom-right). */
static void paint_index(Canvas *cv, int sc, float k, int rank, int suit, const Xf *outer)
{
    static const char ranks[] = "23456789TJQKA";
    RCol top, bot;
    art_suit_colors(suit, &top, &bot);
    float colx = 23.5f * k, y = 12.0f * k;
    float h = glyph_h(sc, 'K');
    if (ranks[rank] == 'T') {
        float sx = 0.70f;
        float w1 = glyph_w(sc, '1') * sx, w0 = glyph_w(sc, '0') * sx, gap = -1.0f * k;
        float x = colx - (w1 + w0 + gap) * 0.5f;
        blit_glyph(cv, sc, '1', x, y, sx, outer, top, bot);
        blit_glyph(cv, sc, '0', x + w1 + gap, y, sx, outer, top, bot);
    } else {
        char ch = ranks[rank];
        float sx = ch == 'Q' ? 0.94f : 1.0f;
        float w = glyph_w(sc, ch) * sx;
        blit_glyph(cv, sc, ch, colx - w * 0.5f, y, sx, outer, top, bot);
    }
    /* The suit pip under the rank. */
    float pip = 10.5f * k, py = y + h + 3.0f * k + pip;
    Shape dummy;
    (void)dummy;
    /* art_pip paints through its own transform; compose with outer by hand. */
    Xf at = xf_mul(*outer, xf_make(colx, py, 1, 0));
    float cx = at.tx, cy = at.ty, rot = outer->ax < 0 ? PI_F : 0;
    art_pip(cv, suit, cx, cy, pip, rot, top, bot, NULL);
}

/* ---- pips --------------------------------------------------------------- */

typedef struct { float x, y; } P2;

static int pip_layout(int rank_num, P2 *p)
{
    /* rank_num = 2..10; columns L/M/R, rows 0..1 of the pip field. */
    const float L = 0, M = 0.5f, R = 1;
    int n = 0;
#define PIP(a, b) (p[n].x = (a), p[n].y = (b), n++)
    switch (rank_num) {
    case 2: PIP(M, 0); PIP(M, 1); break;
    case 3: PIP(M, 0); PIP(M, 0.5f); PIP(M, 1); break;
    case 4: PIP(L, 0); PIP(R, 0); PIP(L, 1); PIP(R, 1); break;
    case 5: PIP(L, 0); PIP(R, 0); PIP(M, 0.5f); PIP(L, 1); PIP(R, 1); break;
    case 6: PIP(L, 0); PIP(R, 0); PIP(L, 0.5f); PIP(R, 0.5f); PIP(L, 1); PIP(R, 1); break;
    case 7: PIP(L, 0); PIP(R, 0); PIP(M, 0.25f); PIP(L, 0.5f); PIP(R, 0.5f); PIP(L, 1); PIP(R, 1); break;
    case 8: PIP(L, 0); PIP(R, 0); PIP(M, 0.25f); PIP(L, 0.5f); PIP(R, 0.5f); PIP(M, 0.75f); PIP(L, 1); PIP(R, 1); break;
    case 9:
        PIP(L, 0); PIP(R, 0); PIP(L, 1 / 3.0f); PIP(R, 1 / 3.0f); PIP(M, 0.5f);
        PIP(L, 2 / 3.0f); PIP(R, 2 / 3.0f); PIP(L, 1); PIP(R, 1); break;
    default:
        PIP(L, 0); PIP(R, 0); PIP(M, 1 / 6.0f); PIP(L, 1 / 3.0f); PIP(R, 1 / 3.0f);
        PIP(L, 2 / 3.0f); PIP(R, 2 / 3.0f); PIP(M, 5 / 6.0f); PIP(L, 1); PIP(R, 1); break;
    }
#undef PIP
    return n;
}

static void paint_pips(Canvas *cv, float k, int rank, int suit)
{
    P2 p[10];
    int n = pip_layout(rank + 2, p);
    RCol top, bot;
    art_suit_colors(suit, &top, &bot);
    const float x0 = 62, x1 = 138, y0 = 60, y1 = 220, size = 17.5f;
    for (int i = 0; i < n; i++) {
        float x = (x0 + (x1 - x0) * p[i].x) * k, y = (y0 + (y1 - y0) * p[i].y) * k;
        int flip = p[i].y > 0.51f;
        art_pip(cv, suit, x, y, size * k, flip ? PI_F : 0, flip ? bot : top, flip ? top : bot, NULL);
    }
}

static void paint_ace(Canvas *cv, float k, int suit)
{
    RCol top, bot;
    art_suit_colors(suit, &top, &bot);
    float cx = 100 * k, cy = 140 * k;
    /* A brass hexagon frame and a warm halo behind the big pip. */
    Shape hex[1] = { { SH_HEX, OP_UNION, { 0, 0, 66, 1 }, NULL, 0 } };
    Xf xf = xf_make(cx, cy, k, 0);
    FillOpt halo = { 0 };
    halo.glow = 9 * k;
    halo.opacity = 0.35f;
    Paint hp = paint_solid(rc_hex(ART_HONEY, 1));
    FillOpt ring = { 0 };
    ring.outline = 2.2f * k;
    ring.glow = 0;
    Shape hexin[1] = { { SH_HEX, OP_UNION, { 0, 0, 58, 1 }, NULL, 0 } };
    Paint fillp = paint_radial(rc_hex(0xFFF3D6, 1), 0, 0, rc_hex(0xF3E3C2, 1), 60);
    cv_fill(cv, &xf, hexin, 1, &fillp, NULL);
    cv_fill(cv, &xf, hex, 1, &hp, &halo);
    BrassParams bp = { 0.7f, 5, 0.6f };
    Paint brass = paint_fn(art_brass, &bp);
    cv_fill(cv, &xf, hex, 1, &brass, &ring);
    FillOpt thin = { 0 };
    thin.outline = 0.9f * k;
    Paint tp = paint_solid(rc_hex(0xB07A20, 0.8f));
    cv_fill(cv, &xf, hexin, 1, &tp, &thin);
    float size = suit == SUIT_S ? 46 : 40;
    if (suit == SUIT_S) {
        /* The ace of spades: gold honeycomb inlay in the pip and the bee on top. */
        FillOpt sh = { 0 };
        sh.glow = 3 * k;
        sh.opacity = 0.5f;
        art_pip(cv, suit, cx, cy + 2 * k, size * k, 0, rc(0, 0, 0, 0.6f), rc(0, 0, 0, 0.6f), &sh);
        art_pip(cv, suit, cx, cy, size * k, 0, top, bot, NULL);
        HoneyParams hp2 = { 5.5f * k, rc(0, 0, 0, 0), rc_hex(ART_GOLD, 0.55f), rc(0, 0, 0, 0), 0.6f, 3, 0 };
        (void)hp2;
        art_bee(cv, cx + 44 * k, cy - 66 * k, 15 * k, -0.25f, 1.0f, 1);
    } else {
        art_pip(cv, suit, cx, cy, size * k, 0, top, bot, NULL);
    }
}

/* ---- court figures ------------------------------------------------------ */

static RCol robe_color(int suit, int dark)
{
    static const uint32_t light[4] = { 0x16A6B8, 0xF08A1A, 0xD8214E, 0x7A3AD0 };   /* C D H S */
    static const uint32_t deep[4] = { 0x0A5560, 0x9A4808, 0x80102C, 0x3C1878 };
    return rc_hex(dark ? deep[suit] : light[suit], 1);
}

/* Paints one half figure in panel coordinates (128 x 106, feet on the centre
 * line), through xf, clipped to the half panel. */
static void paint_figure(Canvas *cv, const Xf *xf, float k, int rank, int suit)
{
    const float W = 128, H = 106;
    Shape clip = { SH_RBOX, OP_INTER, { W / 2, H / 2, W / 2 - 0.5f, H / 2 + 0.5f, 0 }, NULL, 0 };
    RCol ink = rc_hex(0x1A1014, 1);
    Paint inkp = paint_solid(ink);
    Paint goldp = paint_linear(gold_top(), 0, 0, gold_bot(), 0, 30);
    RCol skin_t = rc_hex(0xF6D6B0, 1), skin_b = rc_hex(0xE0AE80, 1);
    Paint skin = paint_linear(skin_t, 0, 24, skin_b, 0, 62);
    FillOpt ol = { 0 };
    ol.offset = 1.1f * k;

    /* Queen's hair sits behind everything. */
    if (rank == 10) {
        Shape hair[2] = { { SH_ELLIPSE, OP_UNION, { 64, 52, 24, 34 }, NULL, 0 }, clip };
        Paint hp = paint_linear(rc_hex(0x7A2E1A, 1), 0, 20, rc_hex(0x3A120A, 1), 0, 90);
        cv_fill(cv, xf, hair, 2, &inkp, &ol);
        cv_fill(cv, xf, hair, 2, &hp, NULL);
    }

    /* Robe with shoulders, and the gold trim along its top. */
    static const float robe_pts[] = { 6, 108, 14, 80, 30, 68, 50, 63, 78, 63, 98, 68, 114, 80, 122, 108 };
    Shape robe[2] = { { SH_POLY, OP_UNION, { 0 }, robe_pts, 8 }, clip };
    RobeP rp = { robe_color(suit, 0), robe_color(suit, 1), rc_hex(0x000000, 1) };
    rp.hex = rc_mix(robe_color(suit, 1), rc_hex(ART_GOLD, 1), 0.35f);
    Paint robep = paint_fn(robe_paint, &rp);
    cv_fill(cv, xf, robe, 2, &inkp, &ol);
    cv_fill(cv, xf, robe, 2, &robep, NULL);
    Shape trim[4] = {
        { SH_SEG, OP_UNION, { 14, 80, 30, 68, 1.8f }, NULL, 0 },
        { SH_SEG, OP_UNION, { 30, 68, 50, 63, 1.8f }, NULL, 0 },
        { SH_SEG, OP_UNION, { 78, 63, 98, 68, 1.8f }, NULL, 0 },
        { SH_SEG, OP_UNION, { 98, 68, 114, 80, 1.8f }, NULL, 0 },
    };
    cv_fill(cv, xf, trim, 4, &goldp, NULL);
    /* Ivory collar V with gold edges, and a jewel. */
    static const float vee[] = { 46, 63, 82, 63, 64, 94 };
    Shape collar[2] = { { SH_POLY, OP_UNION, { 0 }, vee, 3 }, clip };
    Paint ivory = paint_linear(rc_hex(0xFFF8EA, 1), 0, 63, rc_hex(0xE6D6B8, 1), 0, 94);
    cv_fill(cv, xf, collar, 2, &ivory, NULL);
    Shape vtrim[2] = { { SH_SEG, OP_UNION, { 46, 63, 64, 94, 1.6f }, NULL, 0 },
                       { SH_SEG, OP_UNION, { 82, 63, 64, 94, 1.6f }, NULL, 0 } };
    cv_fill(cv, xf, vtrim, 2, &goldp, NULL);
    /* Honey-coloured gem on the chest, in the suit's glow colour. */
    Shape gem[1] = { { SH_HEX, OP_UNION, { 64, 99, 4.6f, 1 }, NULL, 0 } };
    Paint gemp = paint_radial(rc_hex(0xFFFFFF, 1), 63, 97, robe_color(suit, 0), 5);
    cv_fill(cv, xf, gem, 1, &inkp, &ol);
    cv_fill(cv, xf, gem, 1, &gemp, NULL);

    /* Neck and head. */
    Shape neck[1] = { { SH_RBOX, OP_UNION, { 64, 58, 6.5f, 8, 2 }, NULL, 0 } };
    cv_fill(cv, xf, neck, 1, &skin, NULL);
    Shape head[1] = { { SH_ELLIPSE, OP_UNION, { 64, 41, 14.5f, 17.5f }, NULL, 0 } };
    cv_fill(cv, xf, head, 1, &inkp, &ol);
    cv_fill(cv, xf, head, 1, &skin, NULL);

    /* Face: almond eyes, brows, a line of a nose, a small mouth. */
    Shape eyes[2] = { { SH_ELLIPSE, OP_UNION, { 58, 41, 2.3f, 1.7f }, NULL, 0 },
                      { SH_ELLIPSE, OP_UNION, { 70, 41, 2.3f, 1.7f }, NULL, 0 } };
    Paint eyep = paint_solid(rc_hex(0x241410, 1));
    cv_fill(cv, xf, eyes, 2, &eyep, NULL);
    Shape brows[2] = { { SH_SEG, OP_UNION, { 54.5f, 37, 60.5f, 36, 0.8f }, NULL, 0 },
                       { SH_SEG, OP_UNION, { 67.5f, 36, 73.5f, 37, 0.8f }, NULL, 0 } };
    Paint browp = paint_solid(rc_hex(0x5A3020, 1));
    cv_fill(cv, xf, brows, 2, &browp, NULL);
    Shape nose[1] = { { SH_SEG, OP_UNION, { 64.5f, 43, 63, 48, 0.6f }, NULL, 0 } };
    Paint nosep = paint_solid(rc_hex(0xB47A58, 1));
    cv_fill(cv, xf, nose, 1, &nosep, NULL);
    Shape mouth[1] = { { SH_ARC, OP_UNION, { 64, 48.5f, 3.6f, 0.8f, PI_F / 2, 0.8f }, NULL, 0 } };
    Paint mouthp = paint_solid(rc_hex(0xB0203C, 1));
    Shape cheeks[2] = { { SH_CIRCLE, OP_UNION, { 56, 47, 2.6f }, NULL, 0 }, { SH_CIRCLE, OP_UNION, { 72, 47, 2.6f }, NULL, 0 } };
    Paint cheekp = paint_solid(rc_hex(0xF08A8A, 0.35f));
    cv_fill(cv, xf, cheeks, 2, &cheekp, NULL);

    if (rank == 11) {   /* King: crown, beard, sceptre */
        Shape beard[2] = { { SH_ELLIPSE, OP_UNION, { 64, 53, 15, 13 }, NULL, 0 },
                           { SH_RBOX, OP_INTER, { 64, 60, 20, 12, 0 }, NULL, 0 } };
        Paint bp = paint_linear(rc_hex(0xFFF4D8, 1), 0, 46, rc_hex(0xD8C090, 1), 0, 66);
        cv_fill(cv, xf, beard, 2, &inkp, &ol);
        cv_fill(cv, xf, beard, 2, &bp, NULL);
        Shape mous[2] = { { SH_ELLIPSE, OP_UNION, { 60, 47, 4.5f, 1.8f }, NULL, 0 }, { SH_ELLIPSE, OP_UNION, { 68, 47, 4.5f, 1.8f }, NULL, 0 } };
        cv_fill(cv, xf, mous, 2, &bp, NULL);
        Shape side[2] = { { SH_ELLIPSE, OP_UNION, { 50, 38, 4.5f, 9 }, NULL, 0 }, { SH_ELLIPSE, OP_UNION, { 78, 38, 4.5f, 9 }, NULL, 0 } };
        cv_fill(cv, xf, side, 2, &bp, NULL);
        static const float crown[] = { 45, 30, 45, 19, 52, 25, 55, 11, 60, 22, 64, 5, 68, 22, 73, 11, 76, 25, 83, 19, 83, 30 };
        Shape cr[1] = { { SH_POLY, OP_UNION, { 0 }, crown, 11 } };
        Paint crp = paint_linear(gold_top(), 0, 5, gold_bot(), 0, 30);
        cv_fill(cv, xf, cr, 1, &inkp, &ol);
        cv_fill(cv, xf, cr, 1, &crp, NULL);
        Shape band[1] = { { SH_RBOX, OP_UNION, { 64, 28, 19, 2.2f, 1 }, NULL, 0 } };
        Paint bandp = paint_solid(rc_hex(0xA86410, 1));
        cv_fill(cv, xf, band, 1, &bandp, NULL);
        Shape jewels[3] = { { SH_CIRCLE, OP_UNION, { 55, 11, 2.3f }, NULL, 0 }, { SH_CIRCLE, OP_UNION, { 64, 5, 2.6f }, NULL, 0 },
                            { SH_CIRCLE, OP_UNION, { 73, 11, 2.3f }, NULL, 0 } };
        Paint jp = paint_solid(robe_color(suit, 0));
        cv_fill(cv, xf, jewels, 3, &jp, NULL);
        /* Sceptre with an orb. */
        Shape sc[2] = { { SH_SEG, OP_UNION, { 104, 30, 104, 108, 2.0f }, NULL, 0 }, clip };
        cv_fill(cv, xf, sc, 2, &inkp, &ol);
        cv_fill(cv, xf, sc, 2, &goldp, NULL);
        Shape orb[1] = { { SH_CIRCLE, OP_UNION, { 104, 25, 5.5f }, NULL, 0 } };
        Paint orbp = paint_radial(rc_hex(0xFFF6C0, 1), 102, 23, rc_hex(0xC07A18, 1), 6);
        cv_fill(cv, xf, orb, 1, &inkp, &ol);
        cv_fill(cv, xf, orb, 1, &orbp, NULL);
        Shape cross[2] = { { SH_SEG, OP_UNION, { 104, 13, 104, 20, 1.1f }, NULL, 0 }, { SH_SEG, OP_UNION, { 101, 16, 107, 16, 1.1f }, NULL, 0 } };
        cv_fill(cv, xf, cross, 2, &goldp, NULL);
        Shape hand[1] = { { SH_CIRCLE, OP_UNION, { 104, 80, 4.8f }, NULL, 0 } };
        cv_fill(cv, xf, hand, 1, &inkp, &ol);
        cv_fill(cv, xf, hand, 1, &skin, NULL);
    } else if (rank == 10) {   /* Queen: tiara, earrings, a flower */
        cv_fill(cv, xf, mouth, 1, &mouthp, NULL);
        Shape tiara[1] = { { SH_ARC, OP_UNION, { 64, 38, 17, 1.8f, -PI_F / 2, 0.95f }, NULL, 0 } };
        cv_fill(cv, xf, tiara, 1, &goldp, NULL);
        Shape tips[3] = { { SH_CIRCLE, OP_UNION, { 64, 18.5f, 3.0f }, NULL, 0 }, { SH_CIRCLE, OP_UNION, { 55, 21.5f, 2.2f }, NULL, 0 },
                          { SH_CIRCLE, OP_UNION, { 73, 21.5f, 2.2f }, NULL, 0 } };
        Paint tipp = paint_radial(rc_hex(0xFFFFFF, 1), 63, 17, robe_color(suit, 0), 3.5f);
        cv_fill(cv, xf, tips, 3, &inkp, &ol);
        cv_fill(cv, xf, tips, 3, &tipp, NULL);
        Shape ear[2] = { { SH_CIRCLE, OP_UNION, { 49.5f, 51, 2.0f }, NULL, 0 }, { SH_CIRCLE, OP_UNION, { 78.5f, 51, 2.0f }, NULL, 0 } };
        Paint earp = paint_solid(rc_hex(ART_CYAN, 1));
        cv_fill(cv, xf, ear, 2, &earp, NULL);
        /* Flower in her hand on the left. */
        Shape stem[2] = { { SH_SEG, OP_UNION, { 26, 80, 30, 108, 1.2f }, NULL, 0 }, clip };
        Paint stemp = paint_solid(rc_hex(0x2F8A3A, 1));
        cv_fill(cv, xf, stem, 2, &stemp, NULL);
        Shape petals[5];
        for (int i = 0; i < 5; i++) {
            float a = -PI_F / 2 + (float)i * 2 * PI_F / 5;
            petals[i] = (Shape){ SH_CIRCLE, OP_UNION, { 26 + cosf(a) * 5, 76 + sinf(a) * 5, 4.2f }, NULL, 0 };
        }
        Paint petp = paint_radial(rc_hex(0xFFD0EA, 1), 26, 76, rc_hex(ART_MAGENTA, 1), 10);
        cv_fill(cv, xf, petals, 5, &inkp, &ol);
        cv_fill(cv, xf, petals, 5, &petp, NULL);
        Shape ctr[1] = { { SH_CIRCLE, OP_UNION, { 26, 76, 2.6f }, NULL, 0 } };
        Paint ctrp = paint_solid(rc_hex(ART_GOLD, 1));
        cv_fill(cv, xf, ctr, 1, &ctrp, NULL);
        Shape hand[1] = { { SH_CIRCLE, OP_UNION, { 29, 90, 4.5f }, NULL, 0 } };
        cv_fill(cv, xf, hand, 1, &inkp, &ol);
        cv_fill(cv, xf, hand, 1, &skin, NULL);
    } else {   /* Jack: beret with a bee-wing feather, honey dipper */
        cv_fill(cv, xf, mouth, 1, &mouthp, NULL);
        Shape hair[2] = { { SH_ELLIPSE, OP_UNION, { 50.5f, 44, 4.5f, 9 }, NULL, 0 }, { SH_ELLIPSE, OP_UNION, { 77.5f, 44, 4.5f, 9 }, NULL, 0 } };
        Paint hairp = paint_linear(rc_hex(0xE0A040, 1), 0, 36, rc_hex(0x9A5A14, 1), 0, 54);
        cv_fill(cv, xf, hair, 2, &inkp, &ol);
        cv_fill(cv, xf, hair, 2, &hairp, NULL);
        Xf fx = xf_mul(*xf, xf_make(84, 15, 1, 0.9f));
        Shape feather[1] = { { SH_ELLIPSE, OP_UNION, { 0, 0, 4.2f, 16 }, NULL, 0 } };
        Paint fp = paint_linear(rc(0.9f, 0.98f, 1, 0.95f), 0, -16, rc(0.45f, 0.85f, 1.0f, 0.85f), 0, 16);
        cv_fill(cv, &fx, feather, 1, &inkp, &ol);
        cv_fill(cv, &fx, feather, 1, &fp, NULL);
        Shape vein[1] = { { SH_SEG, OP_UNION, { 0, -14, 0, 14, 0.5f }, NULL, 0 } };
        Paint vp = paint_solid(rc(0.3f, 0.55f, 0.75f, 0.8f));
        cv_fill(cv, &fx, vein, 1, &vp, NULL);
        Xf bx = xf_mul(*xf, xf_make(62, 26, 1, -0.22f));
        Shape beret[1] = { { SH_ELLIPSE, OP_UNION, { 0, 0, 21, 8.5f }, NULL, 0 } };
        Paint berp = paint_linear(robe_color(suit, 0), 0, -8, robe_color(suit, 1), 0, 8);
        cv_fill(cv, &bx, beret, 1, &inkp, &ol);
        cv_fill(cv, &bx, beret, 1, &berp, NULL);
        Shape bband[1] = { { SH_RBOX, OP_UNION, { 0, 5, 17, 1.6f, 1 }, NULL, 0 } };
        cv_fill(cv, &bx, bband, 1, &goldp, NULL);
        /* Honey dipper: wooden handle, ridged honey-gold head, one drip. */
        Shape stick[2] = { { SH_SEG, OP_UNION, { 104, 42, 104, 108, 1.8f }, NULL, 0 }, clip };
        Paint wood = paint_solid(rc_hex(0x8A5A2A, 1));
        cv_fill(cv, xf, stick, 2, &inkp, &ol);
        cv_fill(cv, xf, stick, 2, &wood, NULL);
        Shape dip[1] = { { SH_ELLIPSE, OP_UNION, { 104, 33, 6.5f, 10 }, NULL, 0 } };
        Paint dipp = paint_radial(rc_hex(0xFFE9A0, 1), 102, 30, rc_hex(0xD88A10, 1), 10);
        cv_fill(cv, xf, dip, 1, &inkp, &ol);
        cv_fill(cv, xf, dip, 1, &dipp, NULL);
        Shape ridges[3] = { { SH_SEG, OP_UNION, { 98.5f, 28, 109.5f, 28, 0.7f }, NULL, 0 },
                            { SH_SEG, OP_UNION, { 97.8f, 33, 110.2f, 33, 0.7f }, NULL, 0 },
                            { SH_SEG, OP_UNION, { 98.5f, 38, 109.5f, 38, 0.7f }, NULL, 0 } };
        Paint rdp = paint_solid(rc_hex(0x8A4A08, 0.9f));
        cv_fill(cv, xf, ridges, 3, &rdp, NULL);
        Shape drip[2] = { { SH_CIRCLE, OP_UNION, { 104, 48, 2.2f }, NULL, 0 },
                          { SH_TRI, OP_SMOOTH, { 102.2f, 47, 105.8f, 47, 104, 42, 0, 0.8f }, NULL, 0 } };
        cv_fill(cv, xf, drip, 2, &dipp, NULL);
        Shape hand[1] = { { SH_CIRCLE, OP_UNION, { 104, 82, 4.8f }, NULL, 0 } };
        cv_fill(cv, xf, hand, 1, &inkp, &ol);
        cv_fill(cv, xf, hand, 1, &skin, NULL);
    }
    (void)ink;
}

static void paint_court(Canvas *cv, float k, int rank, int suit)
{
    const float px = 36, py = 34, pw = 128, ph = 212;
    Xf card = xf_make(0, 0, k, 0);
    /* The dark honeycomb panel itself is part of the court stock. */
    Shape panel[1] = { { SH_RBOX, OP_UNION, { px + pw / 2, py + ph / 2, pw / 2, ph / 2, 5 }, NULL, 0 } };

    Xf top = xf_mul(card, xf_make(px, py, 1, 0));
    Xf bottom = xf_mul(card, xf_mul(xf_rot180_about(100, 140), xf_make(px, py, 1, 0)));
    paint_figure(cv, &top, k, rank, suit);
    paint_figure(cv, &bottom, k, rank, suit);

    /* Centre line and frame, brass. */
    BrassParams bp = { 0.3f, 9, 0.5f };
    Paint brass = paint_fn(art_brass, &bp);
    FillOpt fo = { 0 };
    fo.outline = 2.2f * k;
    cv_fill(cv, &card, panel, 1, &brass, &fo);
    Shape mid[1] = { { SH_SEG, OP_UNION, { px + 2, 140, px + pw - 2, 140, 0.9f }, NULL, 0 } };
    cv_fill(cv, &card, mid, 1, &brass, NULL);

    /* A small suit mark in two corners of the panel, bright enough for the dark ground. */
    RCol t = (suit == SUIT_H || suit == SUIT_D) ? rc_hex(0xFF4A78, 1) : rc_hex(0xF4ECDD, 1);
    RCol b = (suit == SUIT_H || suit == SUIT_D) ? rc_hex(0xD01A48, 1) : rc_hex(0xC8BCA6, 1);
    art_pip(cv, suit, (px + 12) * k, (py + 13) * k, 7.5f * k, 0, t, b, NULL);
    art_pip(cv, suit, (px + pw - 12) * k, (py + ph - 13) * k, 7.5f * k, PI_F, b, t, NULL);
}

/* ---- the card ----------------------------------------------------------- */

/* The brushed-brass rim: a band from the card's edge inwards, so the
 * expensive brass paint only runs where it shows. */
static void paint_rim(Canvas *cv, float k, uint32_t seed)
{
    Xf xf = xf_make(0, 0, k, 0);
    Shape outer[1] = { { SH_RBOX, OP_UNION, { 100, 140, 100, 140, 14 }, NULL, 0 } };
    BrassParams bp = { 0.55f, seed, 0.5f };
    Paint brass = paint_fn(art_brass, &bp);
    FillOpt band = { 0 };
    band.outline = 7.0f * k + 1;
    band.offset = -(7.0f * k + 1) * 0.5f;
    cv_fill(cv, &xf, outer, 1, &brass, &band);
}

static void paint_stock(Canvas *cv, float k)
{
    Xf xf = xf_make(0, 0, k, 0);
    /* Brushed-brass edge, then the ivory face inside it. */
    paint_rim(cv, k, 17);
    Shape inner[1] = { { SH_RBOX, OP_UNION, { 100, 140, 95, 135, 10 }, NULL, 0 } };
    FillOpt shade = { 0 };
    shade.offset = 1.0f * k;
    Paint dark = paint_solid(rc_hex(0x5A3A10, 0.85f));
    cv_fill(cv, &xf, inner, 1, &dark, &shade);
    Paint paper = paint_fn(paper_paint, NULL);
    cv_fill(cv, &xf, inner, 1, &paper, NULL);
}

/* Blank stock shared by many faces, painted once per size and copied:
 * 0 pip cards (with the thin inner frame), 1 court cards (with the dark
 * honeycomb panel), 2 aces (plain). Filled by cards_prepare_job. */
enum { STOCK_PIPS, STOCK_COURT, STOCK_ACE, NSTOCK };
static uint8_t *g_stock[CARD_NSIZES][NSTOCK];

int cards_prepare_count(void) { return CARD_NSIZES * NSTOCK; }

void cards_prepare_job(int i)
{
    int s = i / NSTOCK, kind = i % NSTOCK;
    float k = (float)k_w[s] / 200.0f;
    if (!g_stock[s][kind]) g_stock[s][kind] = calloc((size_t)k_w[s] * k_h[s], 4);
    if (!g_stock[s][kind]) return;
    Canvas cv = { g_stock[s][kind], k_w[s], k_h[s], k_w[s] };
    paint_stock(&cv, k);
    Xf xf = xf_make(0, 0, k, 0);
    if (kind == STOCK_PIPS) {
        Shape frame[1] = { { SH_RBOX, OP_UNION, { 100, 140, 89, 129, 7 }, NULL, 0 } };
        FillOpt thin = { 0 };
        thin.outline = fmaxf(0.8f, 0.9f * k);
        Paint fp = paint_solid(rc_hex(0xB88A3A, 0.45f));
        cv_fill(&cv, &xf, frame, 1, &fp, &thin);
    } else if (kind == STOCK_COURT) {
        const float px = 36, py = 34, pw = 128, ph = 212;
        PanelP pp = { rc_hex(0x2C1532, 1), rc_hex(0x100A14, 1), rc_hex(ART_HONEY, 1) };
        Xf pxf = xf_mul(xf, xf_make(px, py, 1, 0));
        Shape panel_local[1] = { { SH_RBOX, OP_UNION, { pw / 2, ph / 2, pw / 2, ph / 2, 5 }, NULL, 0 } };
        Paint ppaint = paint_fn(panel_paint, &pp);
        cv_fill(&cv, &pxf, panel_local, 1, &ppaint, NULL);
    }
}

void cards_prepare_free(void)
{
    for (int s = 0; s < CARD_NSIZES; s++)
        for (int k = 0; k < NSTOCK; k++) {
            free(g_stock[s][k]);
            g_stock[s][k] = NULL;
        }
}

static void copy_stock(Canvas *cv, int s, int kind)
{
    const uint8_t *src = g_stock[s][kind];
    if (!src) { paint_stock(cv, (float)k_w[s] / 200.0f); return; }
    for (int y = 0; y < cv->h; y++)
        memcpy(cv->px + (size_t)y * cv->stride * 4, src + (size_t)y * k_w[s] * 4, (size_t)k_w[s] * 4);
}

static void paint_face(Canvas *cv, int size_class, Card c)
{
    float k = (float)k_w[size_class] / 200.0f;
    int rank = card_rank(c), suit = card_suit(c);
    copy_stock(cv, size_class, rank == 12 ? STOCK_ACE : rank >= 9 ? STOCK_COURT : STOCK_PIPS);

    if (rank == 12) paint_ace(cv, k, suit);
    else if (rank >= 9) paint_court(cv, k, rank, suit);
    else paint_pips(cv, k, rank, suit);

    Xf id = xf_identity();
    paint_index(cv, size_class, k, rank, suit, &id);
    Xf rot = xf_rot180_about(k_w[size_class] * 0.5f, k_h[size_class] * 0.5f);
    paint_index(cv, size_class, k, rank, suit, &rot);
}

static void paint_back(Canvas *cv, int size_class, int variant)
{
    float k = (float)k_w[size_class] / 200.0f;
    Xf xf = xf_make(0, 0, k, 0);
    paint_rim(cv, k, 23);
    BrassParams bp = { 0.55f, 23, 0.5f };
    Paint brass = paint_fn(art_brass, &bp);
    RCol neon = variant ? rc_hex(ART_CYAN, 1) : rc_hex(ART_MAGENTA, 1);
    Shape inner[1] = { { SH_RBOX, OP_UNION, { 100, 140, 95, 135, 10 }, NULL, 0 } };
    BackP bk = { variant ? rc_hex(0x0E2A36, 1) : rc_hex(0x3A1242, 1), rc_hex(0x0C080E, 1), rc_hex(ART_HONEY, 1), 10.5f,
                 (uint32_t)(31 + variant), neon };
    Paint bpaint = paint_fn(back_paint, &bk);
    cv_fill(cv, &xf, inner, 1, &bpaint, NULL);
    /* Neon inner border. */
    Shape border[1] = { { SH_RBOX, OP_UNION, { 100, 140, 86, 126, 8 }, NULL, 0 } };
    FillOpt nb = { 0 };
    nb.outline = 1.6f * k;
    nb.glow = 2.5f * k;
    Paint np = paint_solid(rc_mix(neon, rc(1, 1, 1, 1), 0.35f));
    cv_fill(cv, &xf, border, 1, &np, &nb);
    /* Brass medallion with the bee. */
    Shape medal[1] = { { SH_HEX, OP_UNION, { 100, 140, 46, 1 }, NULL, 0 } };
    FillOpt halo = { 0 };
    halo.glow = 8 * k;
    halo.opacity = 0.6f;
    Paint hp = paint_solid(neon);
    cv_fill(cv, &xf, medal, 1, &hp, &halo);
    Paint mp = paint_radial(rc_hex(0x3A2238, 1), 100, 128, rc_hex(0x120A12, 1), 50);
    cv_fill(cv, &xf, medal, 1, &mp, NULL);
    FillOpt ring = { 0 };
    ring.outline = 4.0f * k;
    cv_fill(cv, &xf, medal, 1, &brass, &ring);
    art_bee(cv, 100 * k, 142 * k, 27 * k, -0.12f, 1.0f, 1);
}

static void paint_sheen(void)
{
    Canvas all = atlas_canvas(g_sheen_item);
    const int fw = CARD_L_W / 2, fh = CARD_L_H / 2;
    for (int f = 0; f < CARD_SHEEN_FRAMES; f++) {
        Canvas cv = all;
        cv.px += (size_t)f * (fw + 2) * 4;
        cv.w = fw;
        float pos = -0.25f + 1.5f * (float)f / (CARD_SHEEN_FRAMES - 1);   /* along the diagonal */
        for (int y = 0; y < fh; y++)
            for (int x = 0; x < fw; x++) {
                float u = ((float)x + 0.5f) / fw, v = ((float)y + 0.5f) / fh;
                /* Mask: the card's rounded rectangle. */
                float qx = fabsf(x + 0.5f - fw * 0.5f) - fw * 0.5f + 7, qy = fabsf(y + 0.5f - fh * 0.5f) - fh * 0.5f + 7;
                float mx = fmaxf(qx, 0), my = fmaxf(qy, 0);
                float d = sqrtf(mx * mx + my * my) + fminf(fmaxf(qx, qy), 0) - 7;
                float m = 0.5f - d;
                m = m < 0 ? 0 : m > 1 ? 1 : m;
                float s = (u * 0.6f + v * 0.4f) - pos;
                float band = expf(-s * s / 0.012f) * 0.75f + expf(-s * s / 0.0008f) * 0.5f;
                float a = band * m;
                if (a > 1) a = 1;
                uint8_t *p = cv.px + ((size_t)y * cv.stride + x) * 4;
                p[0] = (uint8_t)(255 * a);
                p[1] = (uint8_t)(248 * a);
                p[2] = (uint8_t)(228 * a);
                p[3] = (uint8_t)(255 * a);
            }
    }
}

void cards_paint_job(int i)
{
    int per = 52 + CARD_BACKS;
    if (i >= CARD_NSIZES * per) { paint_sheen(); return; }
    int s = i / per, j = i % per;
    if (j < 52) {
        Canvas cv = atlas_canvas(g_face_item[s][j]);
        paint_face(&cv, s, (Card)j);
    } else {
        Canvas cv = atlas_canvas(g_back_item[s][j - 52]);
        paint_back(&cv, s, j - 52);
    }
}

void cards_finish(void)
{
    for (int s = 0; s < CARD_NSIZES; s++) {
        for (int c = 0; c < 52; c++) g_face[s][c] = atlas_spr(g_face_item[s][c]);
        for (int b = 0; b < CARD_BACKS; b++) g_back[s][b] = atlas_spr(g_back_item[s][b]);
    }
    for (int f = 0; f < CARD_SHEEN_FRAMES; f++)
        g_sheen[f] = atlas_sub(g_sheen_item, (float)f * (CARD_L_W / 2 + 2), 0, CARD_L_W / 2, CARD_L_H / 2);
}

const Spr *card_face_spr(Card c, CardSize s) { return &g_face[s][c < 52 ? c : 0]; }
const Spr *card_back_spr(CardSize s, int v) { return &g_back[s][v >= 0 && v < CARD_BACKS ? v : 0]; }

/* ---- drawing ------------------------------------------------------------ */

static void rot_pts(Vector2 *p, int n, float cx, float cy, float rot)
{
    if (rot == 0) return;
    float c = cosf(rot), s = sinf(rot);
    for (int i = 0; i < n; i++) {
        float x = p[i].x - cx, y = p[i].y - cy;
        p[i].x = cx + x * c - y * s;
        p[i].y = cy + x * s + y * c;
    }
}

/* The card quad in 4 vertical strips, so the fake perspective (one edge
 * taller than the other) does not show the diagonal seam of two triangles. */
static void draw_strips(const Spr *s, float cx, float cy, float w, float h, float hl, float hr, float rot, PCol col)
{
    const int N = 4;
    for (int i = 0; i < N; i++) {
        float a = (float)i / N, b = (float)(i + 1) / N;
        float xa = cx - w * 0.5f + w * a, xb = cx - w * 0.5f + w * b;
        float ha = lerpf(hl, hr, a) * 0.5f, hb = lerpf(hl, hr, b) * 0.5f;
        Vector2 p[4] = { { xa, cy - ha }, { xa, cy + ha }, { xb, cy + hb }, { xb, cy - hb } };
        (void)h;
        rot_pts(p, 4, cx, cy, rot);
        Spr q = *s;
        q.u0 = lerpf(s->u0, s->u1, a);
        q.u1 = lerpf(s->u0, s->u1, b);
        gfx_quad(&q, p, col);
    }
}

void card_draw(Card c, CardSize sz, const CardPose *p)
{
    float sc = p->scale > 0 ? p->scale : 1.0f, alpha = p->alpha > 0 ? p->alpha : 1.0f;
    float w = card_w(sz) * sc, h = card_h(sz) * sc;
    float cx = p->x, cy = p->y - p->lift;
    float theta = (1.0f - clampf(p->flip, 0, 1)) * PI_F;
    float cs = cosf(theta), sn = sinf(theta);
    int face = cs >= 0 && c != CARD_NONE;
    float xs = fabsf(cs);
    if (xs < 0.02f) xs = 0.02f;
    float persp = 0.10f * sn;               /* the edge turning towards us grows */
    float hl = h * (1 + persp), hr = h * (1 - persp);
    float ww = w * xs;
    float k = sc * (sz == CARD_L ? 1.0f : 0.6f);

    /* Soft shadow on the felt, further and softer the higher the card floats. */
    if (!p->no_shadow) {
        float off = 6 * k + p->lift * 0.35f, spread = 10 * k + p->lift * 0.25f;
        gfx_nine(sprite_nine(NINE_SHADOW), p->x - ww * 0.5f - spread + off * 0.4f, p->y - h * 0.5f - spread + off,
                 ww + 2 * spread, h + 2 * spread, 44 * k, gfx_cola((Color){ 0, 0, 0, 255 }, 0.55f * alpha));
    }
    /* Held glow behind the card. */
    if (p->glow > 0.01f) {
        Color gc = p->glow_color.a ? p->glow_color : (Color){ 255, 190, 60, 255 };
        float g = 22 * k;
        gfx_nine(sprite_nine(NINE_GLOW), cx - ww * 0.5f - g, cy - h * 0.5f - g, ww + 2 * g, h + 2 * g, 40 * k,
                 gfx_add(gc, 1.25f * p->glow * alpha));
    }
    if (p->highlight > 0.01f) {
        float g = 22 * k * (1 + 0.15f * p->highlight);
        gfx_nine(sprite_nine(NINE_GLOW), cx - ww * 0.5f - g, cy - h * 0.5f - g, ww + 2 * g, h + 2 * g, 40 * k,
                 gfx_add((Color){ 255, 120, 230, 255 }, 1.4f * p->highlight * alpha));
    }

    /* Edge-on cards catch less light. */
    float shade = 0.62f + 0.38f * fabsf(cs);
    if (p->dim > 0) shade *= 1.0f - 0.55f * clampf(p->dim, 0, 1);
    Color tc = { (unsigned char)(255 * shade), (unsigned char)(255 * shade), (unsigned char)(255 * shade), 255 };
    const Spr *s = face ? card_face_spr(c, sz) : card_back_spr(sz, p->back);
    draw_strips(s, cx, cy, ww, h, hl, hr, p->rot, gfx_cola(tc, alpha));

    /* Specular sweep while turning, the idle shimmer, and the win sheen. */
    float band = -1, bk = 0;
    if (g_effects.card_specular && p->flip > 0.02f && p->flip < 0.98f) {
        float f = clampf(p->flip, 0, 1);
        band = f;
        bk = 0.9f * sn + 0.2f;
    } else if (p->sheen > 0 && p->sheen_k > 0) {
        band = p->sheen;
        bk = p->sheen_k;
    }
    if (p->highlight > 0.01f && band < 0) {
        band = 0.5f + 0.5f * sinf(p->highlight * 6.0f);
        bk = 0.25f * p->highlight;
    }
    if (band >= 0 && bk > 0) {
        int fr = (int)(clampf(band, 0, 1) * (CARD_SHEEN_FRAMES - 1) + 0.5f);
        draw_strips(&g_sheen[fr], cx, cy, ww, h, hl, hr, p->rot, gfx_add((Color){ 255, 250, 235, 255 }, bk * alpha));
    }
}

/* ---- motion ------------------------------------------------------------- */

void card_motion_deal(CardMotion *m, Vec2f shoe, Vec2f to, float delay, float dur, float arc_px, float rot_from,
                      float rot_to, float flip_after, float flip_dur)
{
    memset(m, 0, sizeof *m);
    m->from = shoe;
    m->to = to;
    /* Control point above the midpoint: the card rises, then drops onto its spot. */
    m->ctrl = (Vec2f){ (shoe.x + to.x) * 0.5f, fminf(shoe.y, to.y) - arc_px };
    m->delay = delay;
    m->dur = dur > 0.05f ? dur : 0.05f;
    m->rot_from = rot_from;
    m->rot_to = rot_to;
    m->scale_from = 0.72f;
    m->flip_at = flip_after;
    m->flip_dur = flip_dur;
    m->state = 1;
}

float card_flip_curve(float t) { return ease(EASE_INOUT_CUBIC, t); }

int card_motion_update(CardMotion *m, float dt, CardPose *p)
{
    m->events = 0;
    if (m->state == 0) return 0;
    m->t += dt;
    float t = m->t - m->delay;
    if (m->state == 1) {
        if (t < 0) {
            p->x = m->from.x; p->y = m->from.y; p->rot = m->rot_from; p->scale = m->scale_from; p->flip = 0;
            return 0;
        }
        m->state = 2;
        m->events |= 1;
    }
    if (m->state == 2) {
        float k = clampf(t / m->dur, 0, 1);
        float e = ease(EASE_OUT_CUBIC, k);
        Vec2f q = bezier2(m->from, m->ctrl, m->to, e);
        p->x = q.x;
        p->y = q.y;
        p->rot = lerpf(m->rot_from, m->rot_to, ease(EASE_OUT_BACK, k));
        p->scale = lerpf(m->scale_from, 1.0f, ease(EASE_OUT_QUART, k));
        p->flip = 0;
        if (k >= 1) { m->state = 3; m->events |= 2; }
        return m->events;
    }
    float after = t - m->dur;
    p->x = m->to.x;
    p->y = m->to.y;
    p->rot = m->rot_to;
    p->scale = 1;
    if (m->state == 3) {
        p->flip = 0;
        if (m->flip_dur <= 0) { m->state = 5; return m->events; }
        if (after >= m->flip_at) { m->state = 4; m->events |= 4; }
    }
    if (m->state == 4) {
        float k = clampf((after - m->flip_at) / m->flip_dur, 0, 1);
        p->flip = card_flip_curve(k);
        p->lift = 14.0f * sinf(k * PI_F);
        if (k >= 1) { m->state = 5; m->events |= 8; p->lift = 0; }
    }
    if (m->state == 5 && m->flip_dur > 0) p->flip = 1;
    return m->events;
}

void card_shimmer(CardPose *p, double time, int seed)
{
    p->sheen = 0;
    p->sheen_k = 0;
    if (!g_effects.shimmer) return;
    /* One pass every 2.6 s, staggered per card. */
    const float period = 2.6f, pass = 0.9f;
    float t = fmodf((float)time + (float)seed * 0.37f, period);
    if (t > pass) return;
    float k = t / pass;
    p->sheen = ease(EASE_INOUT_SINE, k);
    p->sheen_k = 0.55f * sinf(k * PI_F);
}
