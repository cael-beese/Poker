/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* lounge_view.c - the main menu and attract mode's title card and demo
 * banner. See draw_view.h.
 *
 * Menu: four neon game tiles picked with HOLD 1-4 (BET ONE steps), each with
 * a little fan of cards for its game; the selected one lifts and glows. HOLD 5
 * toggles the strategy hint, DEAL plays; the fifth choice opens the CONTROLS
 * page (controls_view.c). Key caps and prompts carry the panel icon
 * (ui_panel_glyph) showing where their button is. Attract: the marquee, the
 * paytable of the variant on show with all five columns, two fanned royal flushes and
 * "PRESS DEAL" pulsing; over the demo games, a banner with "PRESS DEAL". The
 * demo games themselves are the real games drawn by their own views
 * (draw_view.c for Draw Poker). */
#include "render/draw_view.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "games/draw/draw_rules.h"
#include "platform/app.h"
#include "platform/screen.h"
#include "platform/session.h"
#include "render/render.h"
#if defined(BPL_HAVE_AUDIO)
#include "audio.h"
#else
enum { SFX_MENU_MOVE, SFX_MENU_SELECT, SFX_HOLD_ON, SFX_HOLD_OFF, SFX_NEON_FLICKER, SFX_CASH_OUT, SFX_BUTTON };
#endif

#define PI_F 3.14159265f

static const Color k_tile_col[MENU_NGAMES] = {
    { 232, 170, 40, 255 }, { 255, 40, 200, 255 }, { 40, 230, 255, 255 }, { 120, 255, 140, 255 },
};
static const char *const k_tile_name[MENU_NGAMES] = { "JACKS OR BETTER", "BONUS POKER", "DEUCES WILD", "TEXAS HOLD'EM" };
static const char *const k_tile_l1[MENU_NGAMES] = { "9/6 FULL PAY", "8/5 BONUS", "FULL PAY", "NO-LIMIT 6-MAX" };
static const char *const k_tile_l2[MENU_NGAMES] = { "99.54% RETURN", "99.17% RETURN", "100.76% RETURN", "SIT & GO" };
/* The fan on each tile: a pair of jacks, four aces, four deuces, pocket aces. */
static const char *const k_tile_fan[MENU_NGAMES][4] = {
    { "Jh", "Js", "Kd", "" }, { "Ah", "Ad", "Ac", "As" }, { "2c", "2d", "2h", "2s" }, { "As", "Ks", "", "" },
};

static struct {
    int      inited;
    Marquee  mq;
    BulbRing bulbs;
    UiButton tile[MENU_NGAMES], hint, play, ctl;
    Spring   lift[MENU_NGAMES];
    Meter    credits;
    int      sel, hint_on;
    double   t;
    float    hint_flash;
    /* attract */
    int      phase;
    float    phase_t;
    Marquee  amq;
    BulbRing abulbs;
} L;

static void lounge_init(void)
{
    if (L.inited) return;
    L.inited = 1;
    marquee_init(&L.mq, 1);
    marquee_init(&L.amq, 1);
    bulbs_init(&L.bulbs, (Rectangle){ 13, 13, PLAY_W - 26, PLAY_H - 26 }, 36);
    bulbs_init(&L.abulbs, (Rectangle){ 13, 13, PLAY_W - 26, PLAY_H - 26 }, 36);
    bulbs_set(&L.bulbs, BULBS_IDLE, 8);
    bulbs_set(&L.abulbs, BULBS_CHASE, 12);
    L.phase = -1;
}

static void msfx(int id, float vol, float pitch)
{
#if defined(BPL_HAVE_AUDIO)
    if (getenv("BPL_SFX_LOG")) fprintf(stderr, "SFX %s vol %.2f pitch %.2f pan 0.00\n", audio_sfx_name((SfxId)id), vol, pitch);
    audio_play((SfxId)id, vol, pitch, 0);
#else
    (void)id; (void)vol; (void)pitch;
#endif
}

static Card card_of(const char *s)
{
    Card c = CARD_NONE;
    if (s && *s) card_parse(s, &c);
    return c;
}

/* ---- menu ------------------------------------------------------------------ */

void menu_view_update(const MenuViewInfo *v, const GameEvent *ev, int nev, float dt)
{
    lounge_init();
    if (v->controls) {
        /* The CONTROLS page: a click for every button, so a press is heard as well as seen. */
        for (int i = 0; i < nev; i++)
            if (ev[i].type == APP_EV_BUTTON) msfx(SFX_BUTTON, 0.6f, 1.0f);
        L.t += dt;
        return;
    }
    if (v->sel != L.sel) {
        for (int i = 0; i < MENU_NGAMES; i++) L.lift[i].v += i == v->sel ? 60.0f : 0.0f;
        L.sel = v->sel;
    }
    if (v->hint_on != L.hint_on) { L.hint_on = v->hint_on; L.hint_flash = 1; }
    for (int i = 0; i < nev; i++) {
        const GameEvent *e = &ev[i];
        if (e->type == APP_EV_STATE && e->a == APP_DRAW && e->b == APP_MENU) {
            /* Cashed out of Draw Poker: the hopper sound (free play, so it
               is only the sound of it). */
            msfx(SFX_CASH_OUT, 0.8f, 1);
        }
        if (e->type != APP_EV_BUTTON) continue;
        uint32_t bit = 1u << e->a;
        for (int k = 0; k < MENU_NGAMES; k++)
            if (bit & (BTN_HOLD1 << k)) ui_button_press(&L.tile[k]);
        if ((bit & (BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT | BTN_BET_ONE)) && v->sel < MENU_NGAMES) ui_button_press(&L.tile[v->sel]);
        if ((bit & (BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT | BTN_BET_ONE)) && v->sel == MENU_SEL_CONTROLS) ui_button_press(&L.ctl);
        if (bit & (BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT | BTN_BET_ONE | BTN_HOLD1 | BTN_HOLD2 | BTN_HOLD3 | BTN_HOLD4))
            msfx(SFX_MENU_MOVE, 0.8f, 1.0f + 0.05f * (float)v->sel);
        else if (bit & BTN_HOLD5) { ui_button_press(&L.hint); msfx(v->hint_on ? SFX_HOLD_ON : SFX_HOLD_OFF, 0.8f, 1); }
        else if (bit & (BTN_DEAL | BTN_OK | BTN_START | BTN_BET_MAX)) { ui_button_press(&L.play); msfx(SFX_MENU_SELECT, 1, 1); }
    }
    L.t += dt;
    marquee_update(&L.mq, dt);
    if (marquee_take_cue(&L.mq)) msfx(SFX_NEON_FLICKER, 0.25f, 1);
    bulbs_update(&L.bulbs, dt);
    for (int i = 0; i < MENU_NGAMES; i++) {
        spring_update(&L.lift[i], i == v->sel ? 1.0f : 0.0f, 16, 0.55f, dt);
        ui_button_update(&L.tile[i], i == v->sel ? BTN_STATE_LIT : BTN_STATE_ON, dt);
    }
    ui_button_update(&L.hint, v->hint_on ? BTN_STATE_LIT : BTN_STATE_ON, dt);
    ui_button_update(&L.play, BTN_STATE_LIT, dt);
    ui_button_update(&L.ctl, v->sel == MENU_SEL_CONTROLS ? BTN_STATE_LIT : BTN_STATE_ON, dt);
    L.hint_flash = fmaxf(0, L.hint_flash - dt * 2);
    if (!L.credits.running && (long long)llround(L.credits.to) != v->credits) {
        if (L.credits.to == 0 && L.credits.shown == 0) meter_set(&L.credits, (double)v->credits);
        else meter_count(&L.credits, (double)v->credits, 0.6f);
    }
    meter_update(&L.credits, dt);
}

static void draw_tile(const MenuViewInfo *v, int i, double time)
{
    int on = v->sel == i;
    float lift = L.lift[i].x;
    const float w = 272, h = 292;
    float cx = 640 + ((float)i - 1.5f) * 292, top = 176 - 12 * lift;
    float sc = 1 + 0.03f * lift;
    Rectangle r = { cx - w * sc / 2, top, w * sc, h * sc };
    Color col = k_tile_col[i];
    float press = L.tile[i].press;
    ui_panel(r, on ? col : (Color){ col.r / 2, col.g / 2, col.b / 2, 255 }, 0.15f + 0.75f * lift + 0.6f * press, 1);

    /* The key cap: which HOLD button picks this game, and where it is. */
    char key[24];
    snprintf(key, sizeof key, "HOLD %d", i + 1);
    const float gh = 16, gw = ui_panel_glyph_w(gh);
    Rectangle kr = { cx - 74, r.y + 14, 148, 30 };
    gfx_nine(sprite_nine(NINE_RRECT), kr.x, kr.y, kr.width, kr.height, 12,
             gfx_cola(on ? col : (Color){ 60, 50, 66, 255 }, on ? 0.9f : 1.0f));
    ui_panel_glyph(kr.x + 8, kr.y + 7, gh, BTN_HOLD1 << i, on ? UI_INK : col, 1);
    TextStyle ts;
    memset(&ts, 0, sizeof ts);
    ts.align = ALIGN_CENTER;
    ts.color = on ? UI_INK : (Color){ 190, 180, 200, 255 };
    text_draw_ex(FONT_DISP_S, key, kr.x + gw + 12 + (kr.width - gw - 16) * 0.5f, kr.y + 5, 18, &ts);

    /* The name in neon. */
    float fs = i == MENU_GAME_JOB ? 30 : 34;
    text_neon(FONT_NEON_M, k_tile_name[i], cx, r.y + 72, fs * sc, col, on ? 1.0f : 0.55f, on ? 1.0f : 0.4f);

    /* The fan of cards. */
    int n = 0;
    while (n < 4 && k_tile_fan[i][n][0]) n++;
    float spread = on ? 0.20f + 0.03f * sinf((float)time * 2) : 0.14f;
    for (int k = 0; k < n; k++) {
        float a = ((float)k - (n - 1) * 0.5f) * spread;
        CardPose p = { 0 };
        p.x = cx + sinf(a) * 150 * 0.45f + a * 30;
        p.y = r.y + 168 - cosf(a) * 12 + fabsf(a) * 20;
        p.rot = a;
        p.scale = 0.52f * sc;
        p.flip = 1;
        p.dim = on ? 0 : 0.45f;
        if (on) card_shimmer(&p, time, k + i * 3);
        card_draw(card_of(k_tile_fan[i][k]), CARD_M, &p);
    }

    /* Two info lines. */
    memset(&ts, 0, sizeof ts);
    ts.align = ALIGN_CENTER;
    ts.color = on ? (Color){ 255, 240, 220, 255 } : (Color){ 170, 155, 150, 255 };
    text_draw_ex(FONT_UI_M, k_tile_l1[i], cx, r.y + 234, 22, &ts);
    char l2[48];
    if (i == MENU_GAME_HOLDEM) snprintf(l2, sizeof l2, "SIT & GO  -  BUY-IN %lld", (long long)v->buyin);
    else snprintf(l2, sizeof l2, "%s", k_tile_l2[i]);
    ts.color = on ? col : (Color){ 130, 120, 120, 255 };
    text_draw_ex(FONT_UI_M, l2, cx, r.y + 258, 20, &ts);
}

void menu_view_draw(const MenuViewInfo *v, double time)
{
    lounge_init();
    double t = L.t;
    (void)time;
    if (v->controls) {
        controls_view_draw(v, t);
        return;
    }
    render_begin();
    ui_background(t, 1);
    text_neon(FONT_NEON_M, "CHOOSE YOUR GAME", 640, 146, 30, UI_CYAN, 0.9f, 0.8f);
    for (int i = 0; i < MENU_NGAMES; i++)
        if (i != v->sel) draw_tile(v, i, t);
    if (v->sel >= 0 && v->sel < MENU_NGAMES) draw_tile(v, v->sel, t);

    /* The hint switch (HOLD 5), and the CONTROLS page (the fifth choice). */
    Rectangle hr = { 196, 494, 540, 44 };
    ui_button_draw_key(&L.hint, hr, v->hint_on ? "HOLD 5   STRATEGY HINT  ON" : "HOLD 5   STRATEGY HINT  OFF",
                       v->hint_on ? UI_CYAN : (Color){ 150, 140, 170, 255 }, v->hint_on ? BTN_STATE_LIT : BTN_STATE_ON, t,
                       BTN_HOLD5);
    int ctl_on = v->sel == MENU_SEL_CONTROLS;
    ui_button_draw(&L.ctl, (Rectangle){ 760, 494, 324, 44 }, "CONTROLS", ctl_on ? UI_GOLD : (Color){ 150, 140, 170, 255 },
                   ctl_on ? BTN_STATE_LIT : BTN_STATE_ON, t);

    float pulse = 0.6f + 0.4f * sinf((float)t * 4);
    const char *game = k_tile_name[v->sel < 0 || v->sel >= MENU_NGAMES ? 0 : v->sel];
    char s[96];
    if (ctl_on) snprintf(s, sizeof s, "PRESS DEAL TO SEE THE CONTROLS");
    else snprintf(s, sizeof s, "PRESS DEAL TO PLAY %s", game);
    /* DEAL's place on the panel beside the prompt. */
    const float dgh = 26, dgw = ui_panel_glyph_w(dgh), stw = text_width(FONT_NEON_M, s, 34, 0);
    float sx0 = 640 - (dgw + 16 + stw) * 0.5f;
    ui_panel_glyph(sx0, 576 - dgh * 0.5f, dgh, BTN_DEAL, UI_GOLD, pulse);
    text_neon(FONT_NEON_M, s, sx0 + dgw + 16 + stw * 0.5f, 576, 34, UI_MAGENTA, pulse, 1.0f);

    meter_draw(&L.credits, (Rectangle){ 30, 614, 232, 76 }, "CREDITS", UI_CYAN);
    TextStyle ts;
    memset(&ts, 0, sizeof ts);
    ts.align = ALIGN_CENTER;
    ts.color = (Color){ 170, 160, 175, 255 };
    const char *legend = "HOLD 1-4 / BET ONE / STICK: CHOOSE    DEAL: PLAY    HOLD 5: HINT    COIN: CREDITS";
    text_draw_ex(FONT_UI_M, legend, 740, 632, 20, &ts);
    if (v->credits <= 0) {
        snprintf(s, sizeof s, "INSERT COIN  -  %lld CREDITS PER COIN", (long long)v->coin_credits);
        ts.color = UI_CYAN;
        text_draw_ex(FONT_UI_M, s, 740, 658, 20, &ts);
    } else if (v->message) {
        ts.color = UI_AMBER;
        text_draw_ex(FONT_UI_M, v->message, 740, 658, 20, &ts);
    } else {
        ts.color = (Color){ 120, 112, 128, 255 };
        text_draw_ex(FONT_UI_S, "FREE PLAY  -  FOR AMUSEMENT ONLY  -  REAL SHUFFLES, REAL ODDS", 740, 660, 17, &ts);
    }
    bulbs_draw(&L.bulbs);
    marquee_draw(&L.mq, 640, 72, 0.86f);
    render_end();
    dv_fill_account(DV_FILL_MENU);
}

/* ---- attract ------------------------------------------------------------------ */

void attract_view_update(const AttractViewInfo *v, const GameEvent *ev, int nev, float dt)
{
    (void)ev; (void)nev;
    lounge_init();
    if (v->phase != L.phase) {
        L.phase = v->phase;
        L.phase_t = 0;
        if (v->phase == ATTRACT_TITLE) marquee_init(&L.amq, 1);
    }
    L.phase_t += dt;
    L.t += dt;
    marquee_update(&L.amq, dt);
    bulbs_update(&L.abulbs, dt);
}

static void royal_fan(float cx, float cy, int suit, float appear, double time, int mirror)
{
    static const int ranks[5] = { 8, 9, 10, 11, 12 };    /* T J Q K A */
    for (int k = 0; k < 5; k++) {
        float kk = clampf(appear * 1.6f - 0.12f * (float)k, 0, 1);
        if (kk <= 0) continue;
        float e = ease(EASE_OUT_BACK, kk);
        float a = ((float)k - 2) * 0.17f * e + (mirror ? 0.1f : -0.1f) + 0.02f * sinf((float)time * 1.3f + k);
        CardPose p = { 0 };
        p.x = cx + sinf(a) * 170;
        p.y = cy - cosf(a) * 170 + 170 + (1 - e) * 60;
        p.rot = a;
        p.scale = 0.62f;
        p.flip = ease(EASE_INOUT_CUBIC, clampf(kk * 1.4f - 0.3f, 0, 1));
        p.back = 1;
        card_shimmer(&p, time, k + mirror * 5);
        card_draw(card_make(ranks[k], suit), CARD_M, &p);
    }
}

void attract_view_draw(const AttractViewInfo *v, double time)
{
    lounge_init();
    double t = L.t;
    (void)time;
    float pulse = 0.7f + 0.3f * sinf((float)t * 4.2f);
    if (v->phase == ATTRACT_TITLE) {
        float ap = clampf(L.phase_t / 0.9f, 0, 1);
        render_begin();
        ui_background(t, 1);
        text_neon(FONT_NEON_M, draw_variant_name(v->variant), 640, 176, 36, UI_CYAN, ap, 1.0f);
        float pe = ease(EASE_OUT_BACK, clampf((L.phase_t - 0.15f) / 0.6f, 0, 1));
        if (pe > 0.01f) {
            float w = 640 * pe, h = 230 * pe;
            dv_paytable((Rectangle){ 640 - w / 2, 316 - h / 2, w, h }, v->variant, 5, DC_NONE, 0, DC_ROYAL_FLUSH,
                        0.5f + 0.5f * sinf((float)t * 1.5f), t);
        }
        royal_fan(166, 332, SUIT_H, clampf((L.phase_t - 0.3f) / 1.0f, 0, 1), t, 0);
        royal_fan(1114, 332, SUIT_S, clampf((L.phase_t - 0.5f) / 1.0f, 0, 1), t, 1);
        {
            /* Where DEAL is, beside the invitation. */
            const float gh = 40, gw = ui_panel_glyph_w(gh), tw = text_width(FONT_NEON_L, "PRESS DEAL", 66, 0);
            float x0 = 640 - (gw + 22 + tw) * 0.5f;
            ui_panel_glyph(x0, 488 - gh * 0.5f, gh, BTN_DEAL, UI_GOLD, pulse);
            text_neon(FONT_NEON_L, "PRESS DEAL", x0 + gw + 22 + tw * 0.5f, 488, 66, UI_CYAN, pulse, 1.2f);
        }
        TextStyle ts;
        memset(&ts, 0, sizeof ts);
        ts.align = ALIGN_CENTER;
        ts.color = UI_HONEY;
        ts.spacing = 2;
        text_draw_ex(FONT_UI_M, "JACKS OR BETTER   *   BONUS POKER   *   DEUCES WILD   *   TEXAS HOLD'EM", 640, 540, 23,
                     &ts);
        ts.color = (Color){ 150, 140, 160, 255 };
        ts.spacing = 1;
        text_draw_ex(FONT_UI_S, "FREE PLAY  -  FOR AMUSEMENT ONLY  -  REAL SHUFFLES, REAL ODDS", 640, 574, 18, &ts);
        char s[48];
        snprintf(s, sizeof s, "CREDITS  %lld", (long long)v->credits);
        text_neon(FONT_NEON_M, s, 640, 640, 30, UI_GOLD, 0.9f, 0.6f);
        bulbs_draw(&L.abulbs);
        marquee_draw(&L.amq, 640, 86, 1.0f);
        render_end();
        dv_fill_account(DV_FILL_TITLE);
        return;
    }
    /* Over a demo game (already drawn and composited): a banner along the
     * bottom, where the button bar would be. Straight into the play space,
     * after the demo's bloom. */
    gfx_begin();
    gfx_rect_vgrad(0, 596, PLAY_W, 124, gfx_cola(BLACK, 0.0f), gfx_cola(BLACK, 0.88f));
    {
        const float gh = 32, gw = ui_panel_glyph_w(gh), tw = text_width(FONT_NEON_L, "PRESS DEAL", 54, 0);
        float x0 = 640 - (gw + 18 + tw) * 0.5f;
        ui_panel_glyph(x0, 646 - gh * 0.5f, gh, BTN_DEAL, UI_GOLD, pulse);
        text_neon(FONT_NEON_L, "PRESS DEAL", x0 + gw + 18 + tw * 0.5f, 646, 54, UI_CYAN, pulse, 1.1f);
    }
    char s[64];
    snprintf(s, sizeof s, "DEMO  -  %s", v->phase == ATTRACT_DRAW ? draw_variant_name(v->variant) : "TEXAS HOLD'EM");
    TextStyle ts;
    memset(&ts, 0, sizeof ts);
    ts.align = ALIGN_CENTER;
    ts.color = (Color){ 255, 150, 230, 255 };
    ts.spacing = 3;
    text_draw_ex(FONT_UI_M, s, 640, 678, 20, &ts);
    gfx_end();
}
