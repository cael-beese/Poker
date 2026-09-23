/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* ui.c - see ui.h. */
#include "render/ui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/card.h"
#include "platform/fx_settings.h"
#include "platform/screen.h"
#include "platform/texreg.h"
#include "render/art.h"
#include "render/fx.h"
#include "render/sprites.h"

#define PI_F 3.14159265f

/* ---- start-up images: background and side art ---------------------------- */

#define BG_W PLAY_W
#define BG_H PLAY_H
#define SIDE_W 440
#define SIDE_H 1440
#define STRIPS 8

static uint8_t *g_bg_px, *g_side_px;
static Texture2D g_bg, g_side;
static const float k_hex_r = 36.0f;

void ui_declare(void)
{
    g_bg_px = calloc((size_t)BG_W * BG_H, 4);
    g_side_px = calloc((size_t)SIDE_W * SIDE_H, 4);
}

int ui_job_count(void) { return 2 * STRIPS; }

static void bg_strip(int s)
{
    int y0 = BG_H * s / STRIPS, y1 = BG_H * (s + 1) / STRIPS;
    RCol top = rc_hex(0x1D1026, 1), bot = rc_hex(0x09070B, 1), warm = rc_hex(0x3A1C2A, 1);
    RCol honey = rc_hex(ART_HONEY, 1), amber = rc_hex(ART_AMBER, 1), mag = rc_hex(ART_MAGENTA, 1);
    for (int y = y0; y < y1; y++) {
        uint8_t *row = g_bg_px + (size_t)y * BG_W * 4;
        for (int x = 0; x < BG_W; x++) {
            float fx = x + 0.5f, fy = y + 0.5f;
            float t = fy / BG_H;
            t = t * (1.6f - 0.6f * t);   /* ~ t^0.8, without powf */
            RCol c = rc_mix(top, bot, t);
            float wx = (fx - 640) / 620, wy = (fy - 440) / 420;
            float w = 1 - (wx * wx + wy * wy);
            if (w > 0) c = rc_mix(c, warm, 0.55f * w * w);
            int q, r;
            float e = hex_grid_dist(fx, fy, k_hex_r, &q, &r);
            float h = rnoise_hash(q, r, 77);
            if (h < 0.08f) c = rc_mix(c, amber, 0.05f);
            else if (h < 0.10f) c = rc_mix(c, mag, 0.04f);
            float cov = 1 - e / 1.3f;
            float vx = (fx - 640) / 760, vy = (fy - 360) / 520;
            float vig = 1 - 0.6f * (vx * vx + vy * vy);
            if (cov > 0) c = rc_mix(c, honey, 0.11f * cov * (0.5f + 0.5f * vig));
            /* Inner bevel: a faint darker line just inside each cell edge. */
            if (e > 1.3f && e < 3.0f) c = rc_scale(c, 0.93f);
            c = rc_scale(c, vig < 0.25f ? 0.25f : vig);
            float g = (rnoise_hash(x, y, 5) - 0.5f) * 0.012f;
            uint8_t *p = row + x * 4;
            p[0] = (uint8_t)(fminf(fmaxf(c.r + g, 0), 1) * 255);
            p[1] = (uint8_t)(fminf(fmaxf(c.g + g, 0), 1) * 255);
            p[2] = (uint8_t)(fminf(fmaxf(c.b + g, 0), 1) * 255);
            p[3] = 255;
        }
    }
}

static void side_strip(int s)
{
    int y0 = SIDE_H * s / STRIPS, y1 = SIDE_H * (s + 1) / STRIPS;
    RCol top = rc_hex(0x1A0E20, 1), bot = rc_hex(0x07060A, 1);
    RCol honey = rc_hex(ART_HONEY, 1), amber = rc_hex(ART_AMBER, 1), mag = rc_hex(ART_MAGENTA, 1), cyan = rc_hex(ART_CYAN, 1);
    for (int y = y0; y < y1; y++) {
        uint8_t *row = g_side_px + (size_t)y * SIDE_W * 4;
        for (int x = 0; x < SIDE_W; x++) {
            float fx = x + 0.5f, fy = y + 0.5f;
            float near = 1 - fx / SIDE_W;                /* 1 at the play space */
            float vy = (fy - SIDE_H * 0.5f) / (SIDE_H * 0.5f);
            RCol c = rc_mix(top, bot, 0.5f + 0.5f * vy * vy);
            int q, r;
            float e = hex_grid_dist(fx + 20, fy, 40, &q, &r);
            float h = rnoise_hash(q, r, 91);
            float glow = 0.15f + 0.85f * near * near;
            if (h < 0.10f) c = rc_mix(c, amber, 0.10f * glow);
            else if (h < 0.13f) c = rc_mix(c, mag, 0.08f * glow);
            float cov = 1 - e / 1.6f;
            if (cov > 0) c = rc_mix(c, honey, (0.10f + 0.35f * glow) * cov);
            else if (e < 6) c = rc_mix(c, honey, 0.05f * glow * (1 - e / 6));
            /* Neon rails along the play-space edge. */
            if (fx < 64) {
                float d1 = fabsf(fx - 9), d2 = fabsf(fx - 20);
                c = rc_mix(c, rc_mix(mag, rc(1, 1, 1, 1), 0.4f), fminf(1, expf(-d1 * d1 / 2.5f) + 0.35f * expf(-d1 / 7)));
                c = rc_mix(c, cyan, fminf(1, 0.8f * expf(-d2 * d2 / 1.2f) + 0.15f * expf(-d2 / 5)));
            }
            uint8_t *p = row + x * 4;
            p[0] = (uint8_t)(fminf(c.r, 1) * 255);
            p[1] = (uint8_t)(fminf(c.g, 1) * 255);
            p[2] = (uint8_t)(fminf(c.b, 1) * 255);
            p[3] = 255;
        }
    }
    /* Details, each clipped to this strip. */
    Canvas cv = { g_side_px + (size_t)y0 * SIDE_W * 4, SIDE_W, y1 - y0, SIDE_W };
    float oy = -(float)y0;
    Xf xf = xf_make(0, oy, 1, 0);
    Shape medal[1] = { { SH_HEX, OP_UNION, { 232, 720, 150, 1 }, NULL, 0 } };
    FillOpt halo = { 0 };
    halo.glow = 26;
    halo.opacity = 0.55f;
    Paint amberp = paint_solid(amber);
    cv_fill(&cv, &xf, medal, 1, &amberp, &halo);
    Paint dark = paint_radial(rc_hex(0x2E1A30, 1), 232, 690, rc_hex(0x0C080E, 1), 160);
    cv_fill(&cv, &xf, medal, 1, &dark, NULL);
    HoneyParams hp = { 16, rc(0, 0, 0, 0), rc_hex(ART_HONEY, 1), rc(0, 0, 0, 0), 1.0f, 3, 0 };
    (void)hp;
    BrassParams bp = { 0.5f, 41, 0.6f };
    Paint brass = paint_fn(art_brass, &bp);
    FillOpt ring = { 0 };
    ring.outline = 9;
    cv_fill(&cv, &xf, medal, 1, &brass, &ring);
    Shape inner[1] = { { SH_HEX, OP_UNION, { 232, 720, 132, 1 }, NULL, 0 } };
    FillOpt thin = { 0 };
    thin.outline = 2.5f;
    thin.glow = 5;
    Paint magp = paint_solid(rc_mix(mag, rc(1, 1, 1, 1), 0.3f));
    cv_fill(&cv, &xf, inner, 1, &magp, &thin);
    art_bee(&cv, 236, 726 + oy, 92, -0.12f, 1.0f, 1);
    /* Neon suit signs above and below. */
    static const struct { int suit; float y; uint32_t col; } signs[] = {
        { SUIT_H, 230, ART_MAGENTA }, { SUIT_S, 400, ART_CYAN }, { SUIT_D, 1040, ART_AMBER }, { SUIT_C, 1210, 0x7CFF5A },
    };
    for (int i = 0; i < 4; i++) {
        FillOpt o = { 0 };
        o.outline = 5;
        o.glow = 12;
        RCol c = rc_mix(rc_hex(signs[i].col, 1), rc(1, 1, 1, 1), 0.25f);
        art_pip(&cv, signs[i].suit, 232, signs[i].y + oy, 58, 0, c, c, &o);
    }
}

void ui_paint_job(int i)
{
    if (i < STRIPS) bg_strip(i);
    else side_strip(i - STRIPS);
}

void ui_finish(void)
{
    Image bg = { g_bg_px, BG_W, BG_H, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    g_bg = texreg_load_image("background 1280x720", bg);
    SetTextureFilter(g_bg, TEXTURE_FILTER_POINT);
    Image side = { g_side_px, SIDE_W, SIDE_H, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    g_side = texreg_load_image("side art 440x1440", side);
    SetTextureFilter(g_side, TEXTURE_FILTER_BILINEAR);
    free(g_bg_px);
    free(g_side_px);
    g_bg_px = g_side_px = NULL;
}

void ui_shutdown(void)
{
    if (g_bg.id) texreg_unload(g_bg);
    if (g_side.id) texreg_unload(g_side);
    memset(&g_bg, 0, sizeof g_bg);
    memset(&g_side, 0, sizeof g_side);
}

/* ---- background --------------------------------------------------------- */

void ui_background(double time, float dim)
{
    Spr s = { 0, 0, 1, 1, BG_W, BG_H, g_bg.id };
    unsigned char v = (unsigned char)(255 * clampf(dim, 0, 1));
    gfx_spr(&s, 0, 0, BG_W, BG_H, (PCol){ v, v, v, 255 });
    /* A few hex cells breathing with light, on the background's own grid. */
    const float w = 1.7320508f * k_hex_r;
    static const short cells[][2] = { { 2, 3 }, { 5, 9 }, { 9, 2 }, { 13, 11 }, { 16, 5 }, { 19, 12 }, { 4, 13 }, { 11, 7 }, { 20, 2 } };
    static const Color cols[] = { UI_AMBER, UI_MAGENTA, UI_HONEY, UI_CYAN };
    for (int i = 0; i < (int)(sizeof cells / sizeof cells[0]); i++) {
        float x = cells[i][0] * w + (cells[i][1] & 1) * w * 0.5f, y = cells[i][1] * 1.5f * k_hex_r;
        float p = 0.5f + 0.5f * sinf((float)time * (0.5f + 0.13f * i) + i * 1.7f);
        float sz = 96.0f * k_hex_r / 30.0f;
        gfx_spr(sprite(SPR_HEX_GLOW), x - sz / 2, y - sz / 2, sz, sz, gfx_add(cols[i & 3], 0.10f * p * p * dim));
    }
}

/* ---- side art ----------------------------------------------------------- */

static void side_art(const ScreenLayout *L, float time, void *user)
{
    (void)user;
    if (!(L->play.x > 0) || !g_side.id) {
        screen_side_art_honeycomb(L, time, NULL);
        return;
    }
    /* Painted for the right margin (the play space on its left); the left
     * margin is its mirror image, so the bee faces the game on both sides. */
    const Rectangle *a = &L->margin[0], *b = &L->margin[1];
    float sw = (float)SIDE_W, sh = (float)SIDE_H;
    DrawTexturePro(g_side, (Rectangle){ 0, 0, -sw, sh }, *a, (Vector2){ 0, 0 }, 0, WHITE);
    DrawTexturePro(g_side, (Rectangle){ 0, 0, sw, sh }, *b, (Vector2){ 0, 0 }, 0, WHITE);
}

void ui_side_art_install(void) { screen_set_side_art(side_art, NULL); }

/* ---- panels and buttons ------------------------------------------------- */

void ui_panel(Rectangle r, Color neon, float glow_k, float alpha)
{
    const Nine *fill = sprite_nine(NINE_RRECT), *line = sprite_nine(NINE_RRECT_LINE), *glow = sprite_nine(NINE_GLOW);
    if (glow_k > 0) gfx_nine(glow, r.x - 24, r.y - 24, r.width + 48, r.height + 48, 40, gfx_add(neon, 0.8f * glow_k * alpha));
    gfx_nine(fill, r.x, r.y, r.width, r.height, 16, gfx_cola((Color){ 16, 10, 20, 255 }, 0.86f * alpha));
    /* Glass: a lighter top fading down. */
    gfx_nine(sprite_nine(NINE_PANEL), r.x + 2, r.y + 2, r.width - 4, fminf(r.height * 0.5f, 60), 14,
             gfx_cola((Color){ 255, 255, 255, 255 }, 0.05f * alpha));
    gfx_nine(line, r.x, r.y, r.width, r.height, 16, gfx_cola(neon, alpha));
    gfx_nine(line, r.x + 1, r.y + 1, r.width - 2, r.height - 2, 15, gfx_add(WHITE, 0.25f * alpha));
}

void ui_button_press(UiButton *b) { b->press = 1.0f; }

void ui_button_update(UiButton *b, UiButtonState st, float dt)
{
    b->press = fmaxf(0, b->press - dt / 0.22f);
    float target = st == BTN_STATE_LIT ? 1.0f : st == BTN_STATE_ON ? 0.45f : 0.0f;
    b->lit += (target - b->lit) * clampf(dt * 10, 0, 1);
}

void ui_button_draw(const UiButton *b, Rectangle r, const char *label, Color neon, UiButtonState st, double time)
{
    float p = ease(EASE_OUT_QUAD, b->press);
    float sc = 1.0f - 0.06f * p;
    float cx = r.x + r.width / 2, cy = r.y + r.height / 2;
    Rectangle q = { cx - r.width * sc / 2, cy - r.height * sc / 2 + 2 * p, r.width * sc, r.height * sc };
    float pulse = st == BTN_STATE_LIT ? 0.65f + 0.35f * sinf((float)time * 5.0f) : 1.0f;
    float glow = (0.25f + 0.75f * b->lit) * pulse + 1.2f * p;
    Color face = st == BTN_STATE_OFF ? (Color){ 70, 64, 72, 255 } : neon;
    const Nine *fill = sprite_nine(NINE_RRECT), *line = sprite_nine(NINE_RRECT_LINE);
    gfx_nine(sprite_nine(NINE_SHADOW), q.x - 20, q.y - 16, q.width + 40, q.height + 40, 40, gfx_cola(BLACK, 0.6f));
    if (st != BTN_STATE_OFF)
        gfx_nine(sprite_nine(NINE_GLOW), q.x - 24, q.y - 24, q.width + 48, q.height + 48, 40, gfx_add(neon, 0.7f * glow));
    /* Face: dark body with the neon colour glowing through from below. */
    PCol top = gfx_col((Color){ 38, 26, 44, 255 }), bot = gfx_col((Color){ 12, 8, 14, 255 });
    if (st != BTN_STATE_OFF) {
        float k = 0.18f + 0.3f * b->lit + 0.5f * p;
        bot = gfx_mixp(bot, gfx_col(neon), clampf(k, 0, 1));
    }
    gfx_nine_vgrad(fill, q.x, q.y, q.width, q.height, 14, top, bot);
    gfx_nine(sprite_nine(NINE_PANEL), q.x + 3, q.y + 3, q.width - 6, q.height * 0.45f, 12, gfx_cola(WHITE, 0.08f + 0.1f * p));
    gfx_nine(line, q.x, q.y, q.width, q.height, 16, gfx_col(face));
    if (p > 0) gfx_nine(fill, q.x, q.y, q.width, q.height, 14, gfx_add(WHITE, 0.45f * p));
    TextStyle ts;
    memset(&ts, 0, sizeof ts);
    ts.align = ALIGN_CENTER;
    ts.color = st == BTN_STATE_OFF ? (Color){ 120, 112, 120, 255 } : (Color){ 255, 250, 240, 255 };
    ts.shadow = BLACK;
    ts.shadow_k = 0.8f;
    float size = fminf(q.height * 0.42f, 30) * sc;
    text_draw_ex(FONT_DISP_S, label, cx, cy - size * 0.5f + 2 * p, size, &ts);
}

/* ---- marquee ------------------------------------------------------------ */

static const char k_script[] = "Beese's";
static const char k_block[] = "POKER LOUNGE";
#define M_SCRIPT 7
#define M_LETTERS (7 + 12)

/* Flicker patterns: power over ~0.5 s. */
static float flicker_power(int pat, float t)
{
    static const float a[] = { 0.00f, 0.05f, 0.09f, 0.16f, 0.22f, 0.30f, 0.36f, 0.50f };
    static const float pa[] = { 0.1f, 0.9f, 0.2f, 1.0f, 0.15f, 0.8f, 0.3f, 1.0f };
    static const float pb[] = { 0.0f, 0.6f, 0.0f, 0.0f, 0.9f, 0.1f, 1.0f, 1.0f };
    const float *p = pat ? pb : pa;
    for (int i = 7; i >= 0; i--)
        if (t >= a[i]) return p[i];
    return 1;
}

void marquee_init(Marquee *m, int intro)
{
    memset(m, 0, sizeof *m);
    for (int i = 0; i < 24; i++) { m->pow[i] = 1; m->flick_t[i] = -1; }
    m->next = 2.0f;
    m->intro = intro ? 0 : 1;
}

void marquee_update(Marquee *m, float dt)
{
    m->t += dt;
    if (m->intro < 1) m->intro = fminf(1, m->intro + dt / 1.4f);
    m->next -= dt;
    if (m->next <= 0) {
        m->next = fx_randf(2.5f, 6.5f);
        if (g_effects.marquee_flicker) {
            int pat = fx_randi(0, 1);
            if (fx_randf(0, 1) < 0.15f) {
                int from = fx_randf(0, 1) < 0.5f ? 0 : M_SCRIPT, to = from ? M_LETTERS : M_SCRIPT;
                for (int i = from; i < to; i++) { m->flick_t[i] = 0; m->flick_pat[i] = pat; }
            } else {
                int i = fx_randi(0, M_LETTERS - 1);
                if (i == M_SCRIPT + 5) i++;   /* the space in POKER LOUNGE */
                m->flick_t[i] = 0;
                m->flick_pat[i] = pat;
            }
            m->cue = 1;
        }
    }
    for (int i = 0; i < M_LETTERS; i++) {
        float p = 1;
        if (m->flick_t[i] >= 0) {
            m->flick_t[i] += dt;
            p = flicker_power(m->flick_pat[i], m->flick_t[i]);
            if (m->flick_t[i] > 0.5f || !g_effects.marquee_flicker) { m->flick_t[i] = -1; p = 1; }
        }
        /* The power-up sweep: letters strike left to right. */
        float on = clampf(m->intro * (M_LETTERS + 4) - (float)i, 0, 1);
        m->pow[i] = p * on;
    }
}

int marquee_take_cue(Marquee *m)
{
    int c = m->cue;
    m->cue = 0;
    return c;
}

typedef struct { const Marquee *m; int base; int lit; Color tube; } MqCtx;

static void mq_glyph(int i, float *dx, float *dy, float *sc, float *al, Color *tint, void *user)
{
    const MqCtx *c = user;
    (void)dx; (void)dy; (void)sc;
    float p = c->m->pow[c->base + i < 24 ? c->base + i : 23];
    if (c->lit) {
        *al = p;
        *tint = (Color){ (unsigned char)(c->tube.r + (255 - c->tube.r) * 0.6f), (unsigned char)(c->tube.g + (255 - c->tube.g) * 0.6f),
                         (unsigned char)(c->tube.b + (255 - c->tube.b) * 0.6f), 255 };
    }
}

static void mq_text(const Marquee *m, FontId f, const char *s, int base, float x, float y, float size, Color tube, float breathe)
{
    MqCtx c = { m, base, 0, tube };
    TextStyle st;
    memset(&st, 0, sizeof st);
    st.align = ALIGN_LEFT;
    /* Unlit glass tubes first, always visible... */
    st.color = (Color){ (unsigned char)(tube.r * 0.25f + 22), (unsigned char)(tube.g * 0.25f + 16), (unsigned char)(tube.b * 0.25f + 24), 255 };
    st.shadow = BLACK;
    st.shadow_k = 0.7f;
    st.fn = mq_glyph;
    st.user = &c;
    text_draw_ex(f, s, x, y, size, &st);
    /* ...then the lit gas with its glow, faded per letter by power. */
    c.lit = 1;
    st.shadow_k = 0;
    st.glow = tube;
    st.glow_k = 1.15f * breathe;
    text_draw_ex(f, s, x, y, size, &st);
}

void marquee_draw(const Marquee *m, float cx, float cy, float scale)
{
    float w = 820 * scale, h = 104 * scale;
    Rectangle r = { cx - w / 2, cy - h / 2, w, h };
    /* The sign board: dark glass in a brass frame with a warm under-glow. */
    gfx_nine(sprite_nine(NINE_SHADOW), r.x - 26, r.y - 18, r.width + 52, r.height + 52, 44, gfx_cola(BLACK, 0.7f));
    gfx_nine(sprite_nine(NINE_GLOW), r.x - 24, r.y - 24, r.width + 48, r.height + 48, 40, gfx_add(UI_AMBER, 0.35f));
    gfx_nine(sprite_nine(NINE_RRECT), r.x, r.y, r.width, r.height, 16, gfx_col((Color){ 20, 11, 22, 255 }));
    gfx_nine(sprite_nine(NINE_PANEL), r.x + 3, r.y + 3, r.width - 6, r.height * 0.5f, 12, gfx_cola(WHITE, 0.05f));
    gfx_nine(sprite_nine(NINE_RRECT_LINE), r.x, r.y, r.width, r.height, 16, gfx_col((Color){ 214, 162, 64, 255 }));
    gfx_nine(sprite_nine(NINE_RRECT_LINE), r.x + 5, r.y + 5, r.width - 10, r.height - 10, 12, gfx_cola((Color){ 120, 80, 30, 255 }, 0.8f));
    /* Rivets. */
    for (int i = 0; i < 4; i++) {
        float x = i & 1 ? r.x + r.width - 14 : r.x + 14, y = i & 2 ? r.y + r.height - 14 : r.y + 14;
        gfx_spr_rot(sprite(SPR_BULB_OFF), x, y, 10 * scale, 10 * scale, 0, gfx_col((Color){ 255, 220, 150, 255 }));
    }
    float breathe = 0.92f + 0.08f * sinf(m->t * 1.3f);
    float ss = 84 * scale, bs = 60 * scale;
    float w1 = text_width(FONT_SCRIPT, k_script, ss, 0), w2 = text_width(FONT_NEON_L, k_block, bs, 2 * scale);
    float gap = 26 * scale, x = cx - (w1 + gap + w2) / 2;
    mq_text(m, FONT_SCRIPT, k_script, 0, x, cy - ss * 0.58f, ss, UI_MAGENTA, breathe);
    TextStyle dummy;
    (void)dummy;
    MqCtx c = { m, M_SCRIPT, 0, UI_GOLD };
    (void)c;
    float bx = x + w1 + gap;
    {
        MqCtx cc = { m, M_SCRIPT, 0, UI_GOLD };
        TextStyle st;
        memset(&st, 0, sizeof st);
        st.spacing = 2 * scale;
        st.color = (Color){ 80, 60, 30, 255 };
        st.shadow = BLACK;
        st.shadow_k = 0.7f;
        st.fn = mq_glyph;
        st.user = &cc;
        text_draw_ex(FONT_NEON_L, k_block, bx, cy - bs * 0.5f, bs, &st);
        cc.lit = 1;
        st.shadow_k = 0;
        st.glow = UI_AMBER;
        st.glow_k = 1.1f * breathe;
        text_draw_ex(FONT_NEON_L, k_block, bx, cy - bs * 0.5f, bs, &st);
    }
    /* A little bee over the script's B. */
    gfx_spr(sprite(SPR_BEE), x - 44 * scale, cy - 58 * scale, 64 * scale, 56 * scale, gfx_col(WHITE));
}

/* ---- bulbs -------------------------------------------------------------- */

void bulbs_init(BulbRing *r, Rectangle rect, float spacing)
{
    memset(r, 0, sizeof *r);
    r->color = (Color){ 255, 214, 130, 255 };
    r->speed = 8;
    float per = 2 * (rect.width + rect.height);
    int n = (int)(per / spacing);
    if (n > BULBS_MAX) n = BULBS_MAX;
    r->n = n;
    for (int i = 0; i < n; i++) {
        float d = per * (float)i / (float)n;
        Vector2 p;
        if (d < rect.width) p = (Vector2){ rect.x + d, rect.y };
        else if ((d -= rect.width) < rect.height) p = (Vector2){ rect.x + rect.width, rect.y + d };
        else if ((d -= rect.height) < rect.width) p = (Vector2){ rect.x + rect.width - d, rect.y + rect.height };
        else { d -= rect.width; p = (Vector2){ rect.x, rect.y + rect.height - d }; }
        r->pos[i] = p;
        r->b[i] = 0.5f;
    }
}

void bulbs_set(BulbRing *r, BulbPattern p, float speed)
{
    r->pattern = p;
    r->speed = speed;
}

void bulbs_update(BulbRing *r, float dt)
{
    r->t += dt;
    int step = (int)(r->t * r->speed);
    for (int i = 0; i < r->n; i++) {
        float target;
        if (!g_effects.bulb_chase) {
            target = r->pattern == BULBS_OFF ? 0.0f : 0.8f;       /* steady */
        } else {
            switch (r->pattern) {
            case BULBS_CHASE: target = ((i + step) % 4 == 0 || (i + step) % 4 == 1) ? 1.0f : 0.08f; break;
            case BULBS_ALTERNATE: target = ((i + step) & 1) ? 1.0f : 0.1f; break;
            case BULBS_FLASH: target = (step & 1) ? 1.0f : 0.05f; break;
            case BULBS_SPARKLE: target = fx_randf(0, 1) < 0.08f ? 1.0f : r->b[i] > 0.5f ? 0.0f : 0.05f; break;
            case BULBS_OFF: target = 0; break;
            default: /* idle: a slow wave running round */
                target = 0.35f + 0.65f * powf(0.5f + 0.5f * sinf((float)i * 0.35f - r->t * 2.2f), 3);
                break;
            }
        }
        /* Filaments warm up fast and cool a little slower. */
        float rate = target > r->b[i] ? 30.0f : 12.0f;
        r->b[i] += (target - r->b[i]) * clampf(rate * dt, 0, 1);
    }
}

void bulbs_draw(const BulbRing *r)
{
    const Spr *off = sprite(SPR_BULB_OFF), *on = sprite(SPR_BULB_ON), *glow = sprite(SPR_GLOW);
    for (int i = 0; i < r->n; i++) gfx_spr_rot(off, r->pos[i].x, r->pos[i].y, 18, 18, 0, gfx_col(WHITE));
    for (int i = 0; i < r->n; i++) {
        float b = r->b[i];
        if (b < 0.04f) continue;
        gfx_spr_rot(glow, r->pos[i].x, r->pos[i].y, 50, 50, 0, gfx_add(r->color, 0.55f * b));
        gfx_spr_rot(on, r->pos[i].x, r->pos[i].y, 22, 22, 0, gfx_cola(WHITE, b));
    }
}

/* ---- meter -------------------------------------------------------------- */

void meter_set(Meter *m, double v)
{
    m->shown = m->from = m->to = v;
    m->running = 0;
}

void meter_count(Meter *m, double to, float seconds)
{
    m->from = m->shown;
    m->to = to;
    m->t = 0;
    m->dur = seconds > 0.05f ? seconds : 0.05f;
    m->running = 1;
    double range = fabs(to - m->from);
    int ticks = (int)(seconds * 16.0f);
    if (ticks > (int)range) ticks = (int)range;
    m->ticks_total = ticks < 1 ? 1 : ticks;
    m->ticks_done = 0;
}

void meter_skip(Meter *m)
{
    if (!m->running) return;
    m->t = m->dur;
}

void meter_update(Meter *m, float dt)
{
    m->bump = fmaxf(0, m->bump - dt * 5);
    if (!m->running) return;
    m->t += dt;
    float k = clampf(m->t / m->dur, 0, 1);
    /* Fast at first, slowing into the final amount: the classic count-up. */
    float e = ease(EASE_OUT_QUART, k);
    m->shown = m->from + (m->to - m->from) * e;
    int due = (int)(e * m->ticks_total);
    if (due > m->ticks_done && k < 1) {
        m->ticks_done = due;
        m->bump = fmaxf(m->bump, 0.25f);
        if (m->on_tick) m->on_tick(m->user, 1.0f + e);
    }
    if (k >= 1) {
        m->shown = m->to;
        m->running = 0;
        m->bump = 1.0f;
        if (m->on_end) m->on_end(m->user);
    }
}

void meter_draw(const Meter *m, Rectangle r, const char *label, Color neon)
{
    ui_panel(r, neon, 0.5f, 1.0f);
    TextStyle ts;
    memset(&ts, 0, sizeof ts);
    ts.align = ALIGN_CENTER;
    ts.color = neon;
    ts.spacing = 2;
    float ls = fminf(r.height * 0.26f, 22);
    text_draw_ex(FONT_UI_M, label, r.x + r.width / 2, r.y + 4, ls, &ts);
    char buf[32];
    snprintf(buf, sizeof buf, "%lld", (long long)llround(m->shown));
    float size = fminf(r.height * 0.52f, 52) * (1.0f + 0.08f * m->bump);
    float cy = r.y + ls + 4 + (r.height - ls - 4) * 0.5f;
    TextStyle g;
    memset(&g, 0, sizeof g);
    g.align = ALIGN_CENTER;
    g.color = (Color){ 255, 246, 196, 255 };
    g.color2 = (Color){ 240, 150, 30, 255 };
    g.glow = UI_AMBER;
    g.glow_k = 0.35f + 0.6f * m->bump;
    g.shadow = BLACK;
    g.shadow_k = 0.8f;
    text_draw_ex(FONT_DISP_M, buf, r.x + r.width / 2, cy - size * 0.5f, size, &g);
}

/* ---- banners and tags --------------------------------------------------- */

void ui_banner(const char *text, float cx, float cy, float size, float appear, float vanish, int rays, double time)
{
    float a = ease(EASE_OUT_BACK, appear);
    float v = ease(EASE_IN_BACK, vanish);
    float sc = a * (1 - v);
    if (sc <= 0.01f) return;
    float op = clampf(appear * 3, 0, 1) * (1 - clampf(vanish, 0, 1));
    if (rays) {
        float rs = 620 * sc;
        gfx_spr_rot(sprite(SPR_RAYS), cx, cy, rs, rs, (float)time * 0.35f, gfx_add(UI_GOLD, 0.55f * op));
        gfx_spr_rot(sprite(SPR_RAYS), cx, cy, rs * 0.8f, rs * 0.8f, -(float)time * 0.22f, gfx_add(UI_MAGENTA, 0.35f * op));
    }
    gfx_spr_rot(sprite(SPR_GLOW), cx, cy, size * 7 * sc, size * 2.4f * sc, 0, gfx_cola(BLACK, 0.55f * op));
    text_gold(size > 60 ? FONT_DISP_L : FONT_DISP_M, text, cx, cy, size * sc, op, 0.9f);
    /* A glint running across the letters. */
    float gx = cx + (fmodf((float)time * 0.7f, 1.6f) - 0.8f) * text_width(FONT_DISP_L, text, size, 0) * 1.1f;
    gfx_spr_rot(sprite(SPR_STAR), gx, cy - size * 0.25f, size * 0.9f * sc, size * 0.9f * sc, 0, gfx_add(WHITE, 0.8f * op));
}

void ui_held_tag(float cx, float cy, float on, double time)
{
    if (on <= 0.01f) return;
    float s = ease(EASE_OUT_BACK, on);
    Rectangle r = { cx - 56 * s, cy - 18 * s, 112 * s, 36 * s };
    gfx_nine(sprite_nine(NINE_GLOW), r.x - 24, r.y - 24, r.width + 48, r.height + 48, 40,
             gfx_add(UI_HONEY, 0.8f * on * (0.85f + 0.15f * sinf((float)time * 6))));
    gfx_nine(sprite_nine(NINE_RRECT), r.x, r.y, r.width, r.height, 14 * s, gfx_cola((Color){ 40, 22, 6, 255 }, on));
    gfx_nine(sprite_nine(NINE_RRECT_LINE), r.x, r.y, r.width, r.height, 14 * s, gfx_cola(UI_GOLD, on));
    text_neon(FONT_NEON_M, "HELD", cx, cy + 1, 30 * s, UI_GOLD, on, 1.0f);
}
