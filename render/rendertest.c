/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* rendertest.c - RENDERTEST: the renderer's showcase and profiling scene.
 *
 * Scenes (LEFT / RIGHT):
 *   0 SHOWCASE   a Draw Poker style screen that deals, holds and wins,
 *                cycling small -> medium -> big -> jackpot
 *   1 JACKPOT    the jackpot celebration at full intensity, restarted as
 *                soon as it ends: the heaviest scene, for --perf-csv
 *   2 DECK       all 52 faces and the backs (medium size) for art review
 *   3 HOLDEM     a Hold'em layout: medium board cards, large hole cards
 *   4 PARTICLES  N particles kept alive (UP / DOWN: 500, 1000, 2000)
 * Effect toggles, one button each (the F1 overlay shows the state):
 *   HOLD1 bloom  HOLD2 particles  HOLD3 shake  HOLD4 hitpause
 *   HOLD5 marquee flicker  BET_ONE bulb chase  BET_MAX shimmer  CASH_OUT card specular
 * DEAL: next step (showcase) / skip the celebration (jackpot);
 * UP / DOWN elsewhere: bloom quality (1/4 9-tap, 1/8, 1/4 5-tap);
 * START: legend on/off; BACK: menu.
 *
 * For measuring on the Pi, e.g. the jackpot scene for 60 s:
 *   beese-poker --mode rendertest --script "1:RIGHT" --frames 3660 --perf-csv jp.csv
 * and with an effect off: add --fx bloom=off.
 *
 * Layer profiling (development): BPL_RT_PROFILE="0,1,2,..." runs the jackpot
 * scene in segments of BPL_RT_SEG frames (default 480 = one celebration at
 * --lockstep), restarting the celebration at each segment, and in segment i
 * skips the layers in mask i: 1 background, 2 game layer, 4 spotlights,
 * 8 particles, 16 banner + meter + flash, 32 bulbs, 64 marquee, 128 bloom.
 * With --gpu-finish, render_ms per segment is each layer's GPU cost. */
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "raylib.h"
#include "platform/app.h"
#include "platform/fx_settings.h"
#include "platform/screen.h"
#include "render/render.h"
#if defined(BPL_HAVE_AUDIO)
#include "audio.h"
#endif

#define PI_F 3.14159265f

enum { SC_SHOWCASE, SC_JACKPOT, SC_DECK, SC_HOLDEM, SC_PARTICLES, SC_COUNT };
static const char *const k_scene_name[SC_COUNT] = { "SHOWCASE", "JACKPOT", "DECK", "HOLDEM", "PARTICLES" };

static struct {
    int scene, legend, audio;
    FxClock clock;
    Shake shake;
    BulbRing bulbs;
    Marquee marquee;
    Celebration cel;
    Meter credits;
    UiButton btn[3];
    CardMotion motion[5];
    CardPose pose[5];
    Card hand[5];
    float held[5];
    int hold_mask;
    float phase_t;
    int phase, round;
    int stress_n;
    double time;
    long long credits_v;
} R;

static const Vec2f k_shoe = { 1180, -120 };

static int g_prof_mask[32], g_prof_n, g_prof_seg = 480, g_skip;
static long g_frame;

static void profile_parse(void)
{
    const char *s = getenv("BPL_RT_PROFILE");
    const char *seg = getenv("BPL_RT_SEG");
    if (seg) g_prof_seg = atoi(seg) > 0 ? atoi(seg) : 480;
    while (s && *s && g_prof_n < 32) {
        char *end;
        g_prof_mask[g_prof_n++] = (int)strtol(s, &end, 0);
        s = *end == ',' ? end + 1 : NULL;
    }
}

static float card_x(int i) { return 640.0f + (float)(i - 2) * 224.0f; }
#define CARD_Y 414.0f

/* ---- audio cues --------------------------------------------------------- */

static void sfx(int id, float vol, float pitch)
{
#if defined(BPL_HAVE_AUDIO)
    if (R.audio) audio_play((SfxId)id, vol, pitch, 0);
#else
    (void)id; (void)vol; (void)pitch;
#endif
}

static void on_cue(void *user, CelebCue cue, float a)
{
    (void)user;
#if defined(BPL_HAVE_AUDIO)
    switch (cue) {
    case CUE_CHIME: sfx(SFX_WIN_SMALL, 1, 1); break;
    case CUE_COINS: sfx(SFX_WIN_MEDIUM, 1, 1); break;
    case CUE_BIG: sfx(SFX_WIN_BIG, 1, 1); break;
    case CUE_JACKPOT: sfx(SFX_WIN_JACKPOT, 1, 1); break;
    case CUE_REVEAL: sfx(SFX_REVEAL, 1, 1); break;
    case CUE_TICK: sfx(SFX_CREDIT_TICK, 0.5f, a); break;
    case CUE_COUNT_END: sfx(SFX_CREDIT_END, 0.9f, 1); break;
    case CUE_SKIP: if (R.audio) audio_stop(SFX_WIN_JACKPOT); break;
    default: break;
    }
#else
    (void)cue; (void)a;
#endif
}

/* ---- the showcase hand -------------------------------------------------- */

static const char *const k_hands[4][5] = {
    { "Jh", "Js", "4c", "9d", "2s" },     /* small: jacks or better        */
    { "7c", "7d", "Qs", "7h", "3h" },     /* medium: three of a kind        */
    { "Kc", "Kd", "Ks", "5h", "Kh" },     /* big: four of a kind            */
    { "As", "Ks", "Qs", "Js", "Ts" },     /* jackpot: royal flush           */
};
static const int k_holds[4] = { 0x3, 0xB, 0x17, 0x1F };
static const long long k_pay[4] = { 5, 15, 125, 4000 };

static void deal_hand(int round, float delay0)
{
    int tier = round & 3;
    for (int i = 0; i < 5; i++) {
        card_parse(k_hands[tier][i], &R.hand[i]);
        memset(&R.pose[i], 0, sizeof R.pose[i]);
        card_motion_deal(&R.motion[i], k_shoe, (Vec2f){ card_x(i), CARD_Y }, delay0 + 0.11f * (float)i, 0.42f, 160,
                         -0.9f, fx_randf(-0.025f, 0.025f), 0.18f + 0.06f * (float)(4 - i), 0.32f);
        R.held[i] = 0;
    }
    R.hold_mask = 0;
}

static void start_scene(int s)
{
    R.scene = s;
    pfx_clear();
    pfx_stress(0, 0);
    clock_cancel(&R.clock);
    celeb_init(&R.cel, &R.shake, &R.clock, &R.bulbs, on_cue, NULL);
    bulbs_set(&R.bulbs, BULBS_IDLE, 8);
    R.phase = 0;
    R.phase_t = 0;
    R.round = s == SC_JACKPOT ? 3 : 0;
    deal_hand(R.round, 0.3f);
    if (s == SC_PARTICLES) pfx_stress(R.stress_n, 0);
    if (s == SC_JACKPOT) {
        /* Straight into the celebration with the royal already face up. */
        for (int i = 0; i < 5; i++) {
            R.motion[i].state = 0;
            R.pose[i] = (CardPose){ .x = card_x(i), .y = CARD_Y, .flip = 1, .scale = 1 };
        }
        R.hold_mask = 0x1F;
        for (int i = 0; i < 5; i++) R.held[i] = 1;
        R.phase = 2;
    }
}

/* ---- AppMode ------------------------------------------------------------ */

static void rt_init(AppCtx *ctx)
{
    render_init(ctx->session_seed);
    memset(&R, 0, sizeof R);
    clock_init(&R.clock);
    shake_init(&R.shake);
    bulbs_init(&R.bulbs, (Rectangle){ 13, 13, PLAY_W - 26, PLAY_H - 26 }, 33);
    marquee_init(&R.marquee, 1);
    R.stress_n = 1000;
    R.credits_v = 1000;
    profile_parse();
    meter_set(&R.credits, (double)R.credits_v);
}

static void rt_enter(AppCtx *ctx, AppState from)
{
    (void)ctx; (void)from;
#if defined(BPL_HAVE_AUDIO)
    static int tried;
    if (!tried) {
        tried = 1;
        AudioStats st;
        audio_get_stats(&st);
        if (st.sounds_loaded == 0) {
            char p[512];
            render_asset_path(p, sizeof p, "sfx", "card_deal_1.wav");
            char *cut = strstr(p, "/sfx/card_deal_1.wav");
            if (cut) *cut = '\0';
            R.audio = audio_init(p);
        } else {
            R.audio = 1;
        }
    }
#endif
    marquee_init(&R.marquee, 1);
    start_scene(R.scene);
}

static void rt_tick(AppCtx *ctx, const InputFrame *in)
{
    if ((in->pressed & BTN_BACK) && !g_prof_n) app_request(ctx, APP_MENU);
}

static void celebrate_round(void)
{
    int tier = R.round & 3;
    float fx = 640, fy = CARD_Y;
    celeb_start(&R.cel, (WinTier)(WIN_SMALL + tier), k_pay[tier] * 5, tier == 3 ? "ROYAL FLUSH" : NULL,
                (Vector2){ fx, fy });
    R.credits_v += k_pay[tier] * 5;
}

static void update_showcase(float dt)
{
    R.phase_t += dt;
    int landed = 0, flipped = 0;
    for (int i = 0; i < 5; i++) {
        int ev = card_motion_update(&R.motion[i], dt, &R.pose[i]);
        if (ev & 2) landed++;
        if (ev & 4) flipped++;
    }
    if (landed) sfx(SFX_CARD_DEAL, 0.8f, 1);
    if (flipped) sfx(SFX_CARD_FLIP, 0.7f, 1);
    switch (R.phase) {
    case 0:   /* dealing */
        if (R.phase_t > 2.3f) { R.phase = 1; R.phase_t = 0; }
        break;
    case 1:   /* holding, one card at a time */
        for (int i = 0; i < 5; i++) {
            int want = (k_holds[R.round & 3] >> i) & 1;
            if (want && !(R.hold_mask & (1 << i)) && R.phase_t > 0.18f * (float)i) {
                R.hold_mask |= 1 << i;
                sfx(SFX_HOLD_ON, 0.8f, 1);
            }
        }
        if (R.phase_t > 1.4f) { R.phase = 2; R.phase_t = 0; }
        break;
    case 2:   /* the win */
        celebrate_round();
        R.phase = 3;
        R.phase_t = 0;
        break;
    case 3:
        if (!R.cel.active && R.phase_t > 1.2f) {
            if (R.scene == SC_JACKPOT) {
                R.phase = 2;       /* again, at full intensity */
            } else {
                R.round++;
                meter_count(&R.credits, (double)R.credits_v, 0.8f);
                deal_hand(R.round, 0.2f);
                R.phase = 0;
                R.phase_t = 0;
            }
        }
        break;
    }
    for (int i = 0; i < 5; i++) {
        float target = (R.hold_mask >> i) & 1 ? 1.0f : 0.0f;
        R.held[i] += (target - R.held[i]) * clampf(dt * 12, 0, 1);
    }
}

static void rt_update(const AppCtx *ctx, const GameEvent *ev, int nev, float real_dt)
{
    (void)ev; (void)nev;
    uint32_t pr = ctx->input.pressed;
    static const uint32_t fx_btn[EFFECT_COUNT] = { BTN_HOLD1, BTN_HOLD2, BTN_HOLD3, BTN_HOLD4, BTN_HOLD5,
                                                   BTN_BET_ONE, BTN_BET_MAX, BTN_CASH_OUT };
    for (int i = 0; i < EFFECT_COUNT; i++)
        if (pr & fx_btn[i]) *fx_settings_ptr(&g_effects, i) ^= 1;
    if (pr & BTN_RIGHT) start_scene((R.scene + 1) % SC_COUNT);
    if (pr & BTN_LEFT) start_scene((R.scene + SC_COUNT - 1) % SC_COUNT);
    if (pr & BTN_START) R.legend ^= 1;
    if (pr & (BTN_UP | BTN_DOWN)) {
        if (R.scene == SC_PARTICLES) {
            static const int steps[] = { 500, 1000, 2000 };
            int k = R.stress_n == 500 ? 0 : R.stress_n == 1000 ? 1 : 2;
            k = (k + ((pr & BTN_UP) ? 1 : 2)) % 3;
            R.stress_n = steps[k];
            pfx_stress(R.stress_n, 0);
        } else {
            BloomParams *bp = post_params();
            bp->quality = (BloomQuality)((bp->quality + ((pr & BTN_UP) ? 1 : BLOOM_NQUALITY - 1)) % BLOOM_NQUALITY);
        }
    }
    if (pr & BTN_DEAL) {
        if (R.cel.active) celeb_skip(&R.cel);
        else if (R.scene == SC_SHOWCASE && R.phase < 2) { R.phase = 2; }
        ui_button_press(&R.btn[2]);
    }
    if (pr & BTN_BET_ONE) ui_button_press(&R.btn[0]);
    if (pr & BTN_BET_MAX) ui_button_press(&R.btn[1]);

    if (g_prof_n) {
        long seg = g_frame / g_prof_seg;
        g_skip = g_prof_mask[seg < g_prof_n ? seg : g_prof_n - 1];
        g_effects.bloom = !(g_skip & 128);
        if (g_frame % g_prof_seg == 0) start_scene(SC_JACKPOT);
    }
    float dt = clock_step(&R.clock, real_dt);
    R.time = R.clock.time;
    shake_update(&R.shake, real_dt);
    marquee_update(&R.marquee, real_dt);
    if (marquee_take_cue(&R.marquee)) sfx(SFX_NEON_FLICKER, 0.35f, 1);
    bulbs_update(&R.bulbs, real_dt);
    meter_update(&R.credits, dt);
    for (int i = 0; i < 3; i++) ui_button_update(&R.btn[i], i == 2 ? BTN_STATE_LIT : BTN_STATE_ON, real_dt);
    if (R.scene == SC_SHOWCASE || R.scene == SC_JACKPOT || R.scene == SC_HOLDEM) update_showcase(dt);
    celeb_update(&R.cel, dt, real_dt);
    pfx_update(dt);
#if defined(BPL_HAVE_AUDIO)
    if (R.audio) audio_update(real_dt);
#endif
}

/* ---- drawing ------------------------------------------------------------ */

static void draw_paytable(float dim)
{
    Rectangle r = { 90, 122, 1100, 132 };
    ui_panel(r, UI_MAGENTA, 0.35f, 1.0f);
    static const char *const rows[10] = { "ROYAL FLUSH", "STRAIGHT FLUSH", "4 OF A KIND", "FULL HOUSE", "FLUSH",
                                          "STRAIGHT", "3 OF A KIND", "TWO PAIR", "JACKS OR BETTER", "" };
    static const int pays[10] = { 800, 50, 25, 9, 6, 4, 3, 2, 1, 0 };
    int win_row = -1;
    if (R.cel.active || R.phase == 3) {
        static const int tier_row[4] = { 8, 6, 2, 0 };
        win_row = tier_row[R.round & 3];
    }
    for (int i = 0; i < 9; i++) {
        int col = i / 5, row = i % 5;
        float x = r.x + 30 + col * 560, y = r.y + 9 + row * 23.5f;
        int lit = i == win_row && fmodf((float)R.time * 3, 1.0f) < 0.6f;
        if (lit) gfx_rect(x - 12, y - 1, 520, 23, gfx_add(UI_MAGENTA, 0.35f));
        Color c = lit ? WHITE : (Color){ 255, 214, 130, 255 };
        text_draw(FONT_UI_M, rows[i], x, y - 2, 23, c);
        char buf[16];
        snprintf(buf, sizeof buf, "%d", i == 0 ? 4000 : pays[i] * 5);
        TextStyle ts;
        memset(&ts, 0, sizeof ts);
        ts.align = ALIGN_RIGHT;
        ts.color = lit ? WHITE : UI_GOLD;
        text_draw_ex(FONT_UI_M, buf, x + 500, y - 2, 23, &ts);
    }
    (void)dim;
}

static void draw_hand(float dim)
{
    for (int i = 0; i < 5; i++) {
        int win = R.cel.active && ((k_holds[R.round & 3] >> i) & 1);
        gfx_set_tint(win ? 1 : dim, win ? 1 : dim, win ? 1 : dim);
        CardPose p = R.pose[i];
        float h = R.held[i];
        p.lift += 16 * ease(EASE_OUT_BACK, h);
        p.glow = h;
        if (R.hold_mask & (1 << i)) card_shimmer(&p, R.time, i);
        if (R.cel.active && ((k_holds[R.round & 3] >> i) & 1)) p.highlight = 0.5f + 0.5f * sinf((float)R.time * 6);
        else if (R.cel.active) p.dim = 0.6f;
        card_draw(R.hand[i], CARD_L, &p);
    }
    gfx_set_tint(dim, dim, dim);
    for (int i = 0; i < 5; i++) ui_held_tag(card_x(i), CARD_Y + 170, R.held[i], R.time);
}

static void draw_bottom_bar(void)
{
    meter_draw(&R.credits, (Rectangle){ 90, 612, 250, 84 }, "CREDITS", UI_CYAN);
    Meter bet;
    meter_set(&bet, 5);
    meter_draw(&bet, (Rectangle){ 356, 612, 130, 84 }, "BET", UI_AMBER);
    ui_button_draw(&R.btn[0], (Rectangle){ 506, 622, 170, 66 }, "BET ONE", UI_AMBER, BTN_STATE_ON, R.time);
    ui_button_draw(&R.btn[1], (Rectangle){ 692, 622, 170, 66 }, "BET MAX", UI_MAGENTA, BTN_STATE_ON, R.time);
    ui_button_draw(&R.btn[2], (Rectangle){ 878, 622, 312, 66 }, "DEAL", UI_GOLD, BTN_STATE_LIT, R.time);
}

static void draw_deck(void)
{
    const float s = 0.56f, w = CARD_M_W * s, h = CARD_M_H * s;
    for (int suit = 0; suit < 4; suit++)
        for (int rank = 0; rank < 13; rank++) {
            CardPose p = { .x = 56 + rank * (w + 6) + w / 2, .y = 132 + suit * (h + 8) + h / 2, .flip = 1, .scale = s };
            card_draw(card_make(rank, 3 - suit), CARD_M, &p);
        }
    for (int b = 0; b < CARD_BACKS; b++) {
        CardPose p = { .x = 1084 + b * 112, .y = 250, .flip = 0, .scale = 0.82f, .back = b };
        card_draw(0, CARD_M, &p);
    }
    for (int i = 0; i < 4; i++) {
        CardPose p = { .x = 1084 + (i & 1) * 112, .y = 470 + (i >> 1) * 0 , .flip = 1, .scale = 0.5f, .rot = (i - 1.5f) * 0.05f };
        static const char *const big[4] = { "As", "Kh", "Qc", "Jd" };
        Card c;
        card_parse(big[i], &c);
        if (i >= 2) { p.y = 610; }
        p.scale = 0.42f;
        card_draw(c, CARD_L, &p);
    }
}

static void draw_holdem(void)
{
    Rectangle t = { 110, 150, 1060, 420 };
    gfx_nine(sprite_nine(NINE_SHADOW), t.x - 30, t.y - 20, t.width + 60, t.height + 60, 44, gfx_cola(BLACK, 0.8f));
    gfx_nine(sprite_nine(NINE_RRECT), t.x, t.y, t.width, t.height, 16, gfx_col((Color){ 96, 64, 22, 255 }));
    gfx_nine(sprite_nine(NINE_RRECT), t.x + 14, t.y + 14, t.width - 28, t.height - 28, 16, gfx_col((Color){ 14, 56, 60, 255 }));
    gfx_spr_rot(sprite(SPR_GLOW), 640, 360, 1100, 420, 0, gfx_add((Color){ 40, 180, 170, 255 }, 0.25f));
    gfx_nine(sprite_nine(NINE_RRECT_LINE), t.x + 12, t.y + 12, t.width - 24, t.height - 24, 16, gfx_add(UI_CYAN, 0.7f));
    for (int i = 0; i < 5; i++) {
        CardPose p = R.pose[i];
        p.x = 640 + (i - 2) * 132.0f;
        p.y = 330;
        p.lift = 0;
        card_draw(R.hand[i], CARD_M, &p);
    }
    static const float seats[5][2] = { { 250, 230 }, { 470, 190 }, { 810, 190 }, { 1030, 230 }, { 1030, 470 } };
    for (int s = 0; s < 5; s++)
        for (int k = 0; k < 2; k++) {
            CardPose p = { .x = seats[s][0] + k * 26, .y = seats[s][1], .scale = 0.5f, .rot = (k - 0.5f) * 0.2f, .back = 1 };
            card_draw(0, CARD_M, &p);
        }
    for (int k = 0; k < 2; k++) {
        Card c;
        card_parse(k ? "Ah" : "Ad", &c);
        CardPose p = { .x = 600 + k * 90, .y = 560, .scale = 0.72f, .rot = (k - 0.5f) * 0.12f, .flip = 1 };
        p.glow = 0.6f;
        card_draw(c, CARD_L, &p);
    }
    text_neon(FONT_NEON_M, "POT  2,450", 640, 212, 36, UI_CYAN, 1, 1);
}

static void draw_legend(void)
{
    char buf[200];
    int p = snprintf(buf, sizeof buf, "RENDERTEST %s | bloom %s | particles %d", k_scene_name[R.scene],
                     post_quality_name(post_params()->quality), pfx_count());
    (void)p;
    gfx_rect(0, 0, PLAY_W, 26, gfx_cola(BLACK, 0.7f));
    text_draw(FONT_UI_S, buf, 10, 3, 20, UI_IVORY);
    text_draw(FONT_UI_S, "HOLD1-5 BET1 BETMAX CASHOUT: toggle fx | LEFT/RIGHT scene | UP/DOWN quality/count | DEAL skip",
              620, 3, 20, (Color){ 200, 190, 170, 255 });
}

static void rt_draw(const AppCtx *ctx)
{
    (void)ctx;
    float dx, dy, deg;
    shake_offset(&R.shake, &dx, &dy, &deg);
    float dim = celeb_dim(&R.cel);

    g_frame++;
    render_begin();
    if (!(g_skip & 1)) ui_background(R.time, dim);
    gfx_push_offset(dx, dy, deg, PLAY_W * 0.5f, PLAY_H * 0.5f);
    gfx_set_tint(dim, dim, dim);
    switch (g_skip & 2 ? -1 : R.scene) {
    case SC_SHOWCASE:
    case SC_JACKPOT:
        draw_paytable(dim);
        draw_hand(dim);
        draw_bottom_bar();
        break;
    case SC_DECK: draw_deck(); break;
    case SC_HOLDEM: draw_holdem(); break;
    default: break;
    }
    gfx_set_tint(1, 1, 1);
    if (!(g_skip & 4)) celeb_draw_back(&R.cel, R.time);
    if (!(g_skip & 8)) pfx_draw();
    if (!(g_skip & 16)) celeb_draw_front(&R.cel, R.time);
    gfx_pop_offset();
    if (!(g_skip & 32)) bulbs_draw(&R.bulbs);
    if (!(g_skip & 64)) marquee_draw(&R.marquee, PLAY_W * 0.5f, 66, 0.9f);
    if (R.scene == SC_PARTICLES) {
        char buf[64];
        snprintf(buf, sizeof buf, "%d PARTICLES", pfx_count());
        text_neon(FONT_NEON_M, buf, 640, 690, 32, UI_CYAN, 1, 1);
    }
    if (R.legend) draw_legend();
    render_end();
}

static void rt_shutdown(void)
{
#if defined(BPL_HAVE_AUDIO)
    if (R.audio) audio_shutdown();
#endif
    render_shutdown();
}

const AppMode mode_rendertest = {
    .name = "rendertest", .init = rt_init, .enter = rt_enter, .tick = rt_tick,
    .present_update = rt_update, .present_draw = rt_draw, .shutdown = rt_shutdown,
};
