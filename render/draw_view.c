/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* draw_view.c - the Draw Poker screen. See draw_view.h.
 *
 * Layout (1280x720 play space):
 *   top left     the marquee sign, the variant, the strategy-hint panel
 *   top right    the paytable: every category x all five bet columns
 *   middle       five CARD_L cards (0.92 scale) dealt from a shoe off the
 *                right edge; the double-up panel takes their place
 *   under cards  one button per card (HOLD 1-5, which become HELD tags,
 *                DOUBLE / TAKE WIN, RED / BLACK)
 *   message      one neon line
 *   bottom bar   CREDITS / BET / WIN meters, BET ONE, BET MAX, DEAL-DRAW,
 *                CASH OUT - the arcade panel, lit when each is live
 *   border       the chasing bulbs
 *
 * Event -> reaction (all on the frame the event arrives):
 *   HAND_START      old cards sweep off, a running celebration is skipped
 *   DEAL/DRAW_CARD  the card flies an eased arc from the shoe; its snap
 *                   plays one frame before it lands
 *   FLIP_CARD       fake-3D turn with the specular sweep, flip sound
 *   HOLD_PHASE      the dealt hand's paytable row lights; hint shown
 *   HOLD            lift + glow + HELD tag at once, hold on/off sound
 *   DISCARD         the card turns down and sweeps away, slide sound
 *   WIN             winning row flashes, winning cards glow, the tier's
 *                   celebration (celebrate.h), WIN meter counts up
 *   DOUBLE_*        the double-up panel: face-down card, RED / BLACK, the
 *                   reveal (hit-pause), win burst or lose shake
 *   COLLECT/CREDITS WIN counts down into CREDITS, rising-pitch ticks
 *   BET / DENIED    bet sounds by level / error, meters bump or shake
 *   APP button      the on-screen button presses in the same frame; any
 *                   button skips a celebration (presentation only)
 */
#include "render/draw_view.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "games/draw/draw_game.h"
#include "games/draw/draw_hint.h"
#include "platform/app.h"
#include "platform/fx_settings.h"
#include "platform/screen.h"
#include "render/render.h"
#if defined(BPL_HAVE_AUDIO)
#include "audio.h"
#endif

#define PI_F 3.14159265f

/* ---- layout ------------------------------------------------------------- */

#define CARD_SC   0.92f
#define CARD_CY   392.0f
#define CARD_DX   214.0f
#define HOLD_CY   551.0f
#define HOLD_W    150.0f
#define HOLD_H    38.0f
#define MSG_CY    593.0f
#define BAR_Y     614.0f
#define BAR_H     76.0f
#define FLY_DUR   0.17f     /* DEAL_CARD -> landing; the flip event comes 0.2 s after */
#define FLIP_DUR  0.20f
#define OUT_DUR   0.24f

static const Rectangle k_pay = { 466, 24, 780, 214 };
static const Vec2f k_shoe = { 1380, 250 };

static float slot_x(int i) { return 640.0f + (float)(i - 2) * CARD_DX; }

/* ---- sound -------------------------------------------------------------- */

static int g_silent;          /* the attract demo: pictures only */
static int g_sounds;          /* sounds started this frame (button clicks fill gaps) */

void dv_sfx(int id, float vol, float pitch, float pan)
{
#if defined(BPL_HAVE_AUDIO)
    static int log = -1;
    if (g_silent) return;
    if (log < 0) log = getenv("BPL_SFX_LOG") != NULL;
    /* BPL_SFX_LOG: which sound fired, for checking the event -> sound map on
       a machine without an audio device. */
    if (log) fprintf(stderr, "SFX %s vol %.2f pitch %.2f pan %.2f\n", audio_sfx_name((SfxId)id), vol, pitch, pan);
    audio_play((SfxId)id, vol, pitch, pan);
    g_sounds++;
#else
    (void)id; (void)vol; (void)pitch; (void)pan;
#endif
}

static void sfx_stop(int id)
{
#if defined(BPL_HAVE_AUDIO)
    audio_stop((SfxId)id);
#else
    (void)id;
#endif
}

#if !defined(BPL_HAVE_AUDIO)
enum { SFX_CARD_DEAL, SFX_CARD_FLIP, SFX_CARD_SLIDE, SFX_BUTTON, SFX_HOLD_ON, SFX_HOLD_OFF, SFX_MENU_MOVE,
       SFX_ERROR, SFX_NEON_FLICKER, SFX_BET_ONE, SFX_BET_MAX, SFX_CREDIT_TICK, SFX_CREDIT_END, SFX_WIN_SMALL,
       SFX_WIN_MEDIUM, SFX_WIN_BIG, SFX_WIN_JACKPOT, SFX_DOUBLE_WIN, SFX_DOUBLE_LOSE, SFX_REVEAL, SFX_WHOOSH };
#endif

static float pan_x(float x) { return clampf((x - 640.0f) / 640.0f * 0.7f, -1, 1); }

/* ---- state -------------------------------------------------------------- */

typedef enum { SL_EMPTY, SL_WAIT, SL_FLY, SL_TABLE } SlotPhase;

typedef struct {
    SlotPhase  phase;
    Card       card;          /* shown only while flip >= 0.5                  */
    CardMotion mo;
    CardPose   pose;          /* x, y, rot, scale from the flight              */
    int        snapped;       /* the deal snap has played                      */
    float      flip, flip_t;  /* 0 back .. 1 face; flip_t < 0: not turning      */
    float      flip_lift;
    int        out_on;        /* the outgoing card, sweeping away              */
    Card       out_card;
    float      out_t, out_x, out_y, out_rot, out_flip;
    float      held;          /* shown 0..1                                    */
    int        held_on;
    float      win;           /* winning-card highlight 0..1                   */
    float      hint;          /* hint frame 0..1                               */
    UiButton   btn;
} Slot;

enum { B_BETONE, B_BETMAX, B_DEAL, B_CASH, B_COUNT };

static struct {
    int         inited;
    const DrawGame *game;     /* the game being followed (play or attract)     */
    uint32_t    hand_no;      /* its hand count at the last update             */
    FxClock     clock;
    Shake       shake;
    BulbRing    bulbs;
    Celebration cel;
    Marquee     mq;
    Slot        s[5];
    UiButton    btn[B_COUNT];
    Meter       credits, win, bet;
    long long   credits_to;   /* what the credits meter is heading for         */
    float       credits_shake;
    Spring      bet_col;      /* lit paytable column, sliding                  */
    int         pre_cat;      /* dealt category lit in the hold phase          */
    float       pre_k;
    int         win_cat;      /* flashing winning row                          */
    float       win_k;
    int         win_tier;
    uint8_t     win_mask;
    float       lose_dim;     /* cards dim after a losing hand                 */
    float       deny;         /* "not enough credits" line                     */
    const char *last_msg;
    int         last_hint_on;
    float       variant_flash;
    /* double-up */
    float       dbl_vis;      /* panel 0..1                                    */
    int         dbl_on;       /* the panel should be up                        */
    float       dbl_linger;   /* seconds to keep it after the round ended      */
    int         dbl_round;
    Card        dbl_card;
    CardMotion  dbl_mo;
    CardPose    dbl_pose;
    int         dbl_flying, dbl_snapped;
    float       dbl_flip, dbl_flip_t;
    int         dbl_guess;    /* -1 none, else DRAW_RED / DRAW_BLACK           */
    float       dbl_guess_t;
    int         dbl_result;   /* 0 pending, 1 won, -1 lost                     */
    float       dbl_result_t;
    float       dbl_shake;
    float       banner_t;     /* "DOUBLED!" banner, < 0 = none                  */
    long long   banner_amount;
    double      t;            /* presentation time (clock-scaled)              */
    double      rt;           /* real time                                     */
} V;

/* ---- helpers ------------------------------------------------------------ */

static void cel_cue(void *user, CelebCue cue, float a)
{
    (void)user;
#if defined(BPL_HAVE_AUDIO)
    switch (cue) {
    case CUE_CHIME: dv_sfx(SFX_WIN_SMALL, 1, 1, 0); break;
    case CUE_COINS: dv_sfx(SFX_WIN_MEDIUM, 1, 1, 0); break;
    case CUE_BIG: dv_sfx(SFX_WIN_BIG, 1, 1, 0); break;
    case CUE_JACKPOT: dv_sfx(SFX_WIN_JACKPOT, 1, 1, 0); break;
    case CUE_REVEAL: dv_sfx(SFX_REVEAL, 1, 1, 0); break;
    case CUE_TICK: dv_sfx(SFX_CREDIT_TICK, 0.45f, a, 0); break;
    case CUE_COUNT_END: dv_sfx(SFX_CREDIT_END, 0.9f, 1, 0); break;
    case CUE_SKIP: sfx_stop(SFX_WIN_JACKPOT); sfx_stop(SFX_WIN_BIG); break;
    default: break;
    }
#else
    (void)cue; (void)a;
#endif
}

/* Count-up ticks of the credit meter: the pitch rises 1 -> 2 over the count
 * (audio.h: the tick is a C7 made to be pitched). A big celebration counts
 * on its own meter, so the credit meter stays quiet under it. */
static void credit_tick(void *user, float pitch)
{
    (void)user;
    if (V.cel.active && V.cel.tier >= WIN_BIG) return;
    dv_sfx(SFX_CREDIT_TICK, 0.4f, pitch, -0.5f);
}
static void credit_end(void *user)
{
    (void)user;
    if (V.cel.active && V.cel.tier >= WIN_BIG) return;
    dv_sfx(SFX_CREDIT_END, 0.7f, 1, -0.5f);
}
static void win_tick(void *user, float pitch)
{
    (void)user;
    if (V.cel.active && V.cel.tier >= WIN_BIG) return;
    dv_sfx(SFX_CREDIT_TICK, 0.35f, pitch, 0.2f);
}

static float count_secs(long long amount)
{
    /* Long enough to feel, short enough not to stall play: 5 -> 0.8 s,
       100 -> 1.6 s, 4000 -> 3.2 s. */
    if (amount < 1) amount = 1;
    return clampf(0.35f + 0.24f * log2f((float)amount + 1.0f), 0.5f, 3.4f);
}

/* Which of the five cards make the category, for the win highlight. It reads
 * only the player's own five cards, as they are shown. */
static uint8_t win_cards(int variant, int cat, const Card c[5])
{
    int cnt[13] = { 0 }, deuces = variant == DRAW_DEUCES;
    for (int i = 0; i < 5; i++)
        if (c[i] != CARD_NONE && !(deuces && card_rank(c[i]) == 0)) cnt[card_rank(c[i])]++;
    uint8_t m = 0;
    switch (cat) {
    case DC_JACKS_OR_BETTER: case DC_TWO_PAIR: case DC_THREE_KIND:
    case DC_FOUR_KIND: case DC_FOUR_ACES: case DC_FOUR_2_4: case DC_FOUR_5_K: {
        int best = -1;
        for (int r = 12; r >= 0; r--)
            if (best < 0 || cnt[r] > cnt[best]) best = r;
        for (int i = 0; i < 5; i++) {
            if (c[i] == CARD_NONE) continue;
            int r = card_rank(c[i]);
            if (deuces && r == 0) m |= (uint8_t)(1u << i);
            else if (cat == DC_TWO_PAIR || cat == DC_JACKS_OR_BETTER ? cnt[r] >= 2 : r == best && cnt[r] >= 1)
                m |= (uint8_t)(1u << i);
        }
        break;
    }
    case DC_FOUR_DEUCES:
        for (int i = 0; i < 5; i++)
            if (c[i] != CARD_NONE && card_rank(c[i]) == 0) m |= (uint8_t)(1u << i);
        break;
    case DC_NONE: break;
    default: m = 0x1F; break;
    }
    return m ? m : 0x1F;
}

static Vector2 mask_centre(uint8_t m)
{
    float sx = 0;
    int n = 0;
    for (int i = 0; i < 5; i++)
        if (m >> i & 1) { sx += slot_x(i); n++; }
    return (Vector2){ n ? sx / (float)n : 640.0f, CARD_CY };
}

/* ---- init / reset / sync ------------------------------------------------- */

static void ensure_init(void)
{
    if (V.inited) return;
    V.inited = 1;
    clock_init(&V.clock);
    shake_init(&V.shake);
    bulbs_init(&V.bulbs, (Rectangle){ 13, 13, PLAY_W - 26, PLAY_H - 26 }, 36);
    bulbs_set(&V.bulbs, BULBS_IDLE, 8);
    celeb_init(&V.cel, &V.shake, &V.clock, &V.bulbs, cel_cue, NULL);
    marquee_init(&V.mq, 0);
    V.credits.on_tick = credit_tick;
    V.credits.on_end = credit_end;
    V.win.on_tick = win_tick;
    V.bet_col.x = 5;
    V.banner_t = -1;
    V.dbl_guess = -1;
}

/* Put every slot where the game says, with nothing moving: used when the
 * view starts following a game (entering Draw, attract's demo) mid-way. */
static void sync_to_game(const DrawViewInfo *v)
{
    const DrawGame *g = v->game;
    for (int i = 0; i < 5; i++) {
        Slot *s = &V.s[i];
        memset(&s->mo, 0, sizeof s->mo);
        s->out_on = 0;
        s->flip_t = -1;
        s->flip_lift = 0;
        s->snapped = 1;
        s->phase = SL_TABLE;
        s->card = g->cards[i];
        s->flip = (g->face_up >> i & 1) && g->cards[i] != CARD_NONE ? 1.0f : 0.0f;
        s->held_on = g->in_hand && (g->held >> i & 1);
        s->held = (float)s->held_on;
        s->win = 0;
        s->pose = (CardPose){ .x = slot_x(i), .y = CARD_CY, .scale = 1 };
    }
    V.pre_cat = g->state == DS_HOLD ? g->dealt_cat : DC_NONE;
    V.pre_k = V.pre_cat != DC_NONE;
    V.win_cat = DC_NONE;
    V.win_k = 0;
    V.win_mask = 0;
    V.lose_dim = 0;
    V.dbl_on = g->state == DS_DOUBLE || g->state == DS_DOUBLE_REVEAL;
    V.dbl_vis = (float)V.dbl_on;
    V.dbl_round = g->dbl_round;
    V.dbl_card = g->dbl_card;
    V.dbl_flip = g->state == DS_DOUBLE ? 0.0f : 1.0f;
    V.dbl_flip_t = -1;
    V.dbl_flying = 0;
    V.dbl_pose = (CardPose){ .x = 640, .y = CARD_CY, .scale = 1 };
    V.dbl_guess = -1;
    V.dbl_result = 0;
    V.banner_t = -1;
    V.bet_col.x = (float)g->bet;
    V.bet_col.v = 0;
    meter_set(&V.bet, g->bet);
    meter_set(&V.credits, (double)v->credits);
    V.credits_to = v->credits;
    long long w = g->state == DS_OFFER || g->state == DS_DOUBLE || g->state == DS_DOUBLE_REVEAL ? g->meter
                  : (g->state == DS_IDLE && g->hand_no > 0 ? g->last.paid : 0);
    meter_set(&V.win, (double)w);
    if (V.cel.active) celeb_skip(&V.cel);
    pfx_clear();
    clock_cancel(&V.clock);
    bulbs_set(&V.bulbs, BULBS_IDLE, 8);
}

void draw_view_reset(void)
{
    memset(&V, 0, sizeof V);
    ensure_init();
}

/* ---- event reactions ----------------------------------------------------- */

static void sweep_out(Slot *s, int i)
{
    if (s->phase == SL_EMPTY || (s->phase == SL_WAIT && !s->out_on)) return;
    if (s->phase == SL_FLY) { s->phase = SL_WAIT; return; }
    if (!g_effects.card_anim) { s->out_on = 0; return; }
    s->out_on = 1;
    s->out_card = s->card;
    s->out_t = 0;
    s->out_x = slot_x(i);
    s->out_y = CARD_CY - s->held * 18.0f;
    s->out_rot = s->pose.rot;
    s->out_flip = s->flip;
}

static void fly_in(Slot *s, int i, Card c)
{
    s->card = c;
    s->flip = 0;
    s->flip_t = -1;
    s->flip_lift = 0;
    s->held_on = 0;
    s->win = 0;
    if (!g_effects.card_anim) {
        s->phase = SL_TABLE;
        s->pose = (CardPose){ .x = slot_x(i), .y = CARD_CY, .scale = 1 };
        dv_sfx(SFX_CARD_DEAL, 0.8f, 1, pan_x(slot_x(i)));
        return;
    }
    s->phase = SL_FLY;
    s->snapped = 0;
    memset(&s->pose, 0, sizeof s->pose);
    card_motion_deal(&s->mo, k_shoe, (Vec2f){ slot_x(i), CARD_CY }, 0, FLY_DUR, 90, -0.75f,
                     fx_randf(-0.012f, 0.012f), 0, 0);
    card_motion_update(&s->mo, 0, &s->pose);
}

static void start_flip(Slot *s, Card c)
{
    s->card = c;
    if (s->phase != SL_TABLE) {
        /* Flipping before landing (a slow frame): land it now. */
        s->phase = SL_TABLE;
        s->pose.scale = 1;
        s->pose.rot = s->mo.rot_to;
        s->pose.x = s->mo.to.x;
        s->pose.y = s->mo.to.y;
    }
    if (!g_effects.card_anim) { s->flip = 1; s->flip_t = -1; return; }
    s->flip_t = 0;
}

static void begin_celebration(const DrawViewInfo *v, int tier, long long amount, int cat, uint8_t mask)
{
    WinTier wt = (WinTier)tier;
    if (wt == WIN_JACKPOT && !g_effects.takeover) wt = WIN_BIG;
    const char *title = NULL;
    if (wt >= WIN_MEDIUM) title = cat != DC_NONE ? draw_cat_name(cat) : "DOUBLE UP";
    if (wt == WIN_BIG && cat == DC_NONE) title = "BIG WIN";
    Vector2 focus = mask_centre(mask);
    /* The medium banner sits focus.y - 175 above its coin burst: burst from
       under the cards so the banner lands on them, not on the paytable. */
    if (wt == WIN_MEDIUM) focus.y = CARD_CY + 168;
    (void)v;
    celeb_start(&V.cel, wt, amount, title, focus);
}

static void on_button(const DrawViewInfo *v, int bit)
{
    const DrawGame *g = v->game;
    uint32_t b = 1u << bit;
    if (b & (BTN_HOLD1 | BTN_HOLD2 | BTN_HOLD3 | BTN_HOLD4 | BTN_HOLD5)) ui_button_press(&V.s[bit].btn);
    if (b & BTN_BET_ONE) ui_button_press(&V.btn[B_BETONE]);
    if (b & BTN_BET_MAX) ui_button_press(&V.btn[B_BETMAX]);
    if (b & BTN_DEAL) ui_button_press(&V.btn[B_DEAL]);
    if (b & BTN_CASH_OUT) ui_button_press(&V.btn[B_CASH]);
    /* Any button skips a celebration: presentation only, the game has
       already moved on (or will, from the same press). */
    if (V.cel.active && V.cel.t > 0.2f && (b & (BTN_HOLD1 | BTN_HOLD2 | BTN_HOLD3 | BTN_HOLD4 | BTN_HOLD5 |
                                                 BTN_DEAL | BTN_BET_ONE | BTN_BET_MAX | BTN_CASH_OUT | BTN_OK |
                                                 BTN_START | BTN_BACK)))
        celeb_skip(&V.cel);
    (void)g;
}

static void on_event(const DrawViewInfo *v, const GameEvent *e)
{
    const DrawGame *g = v->game;
    int a = e->a;
    Slot *s = (a >= 0 && a < 5) ? &V.s[a] : &V.s[0];
    switch (e->type) {
    case EV_DRAW_VARIANT:
        V.variant_flash = 1;
        dv_sfx(SFX_MENU_MOVE, 0.9f, 1, 0);
        break;
    case EV_DRAW_BET:
        V.bet.bump = 1;
        meter_set(&V.bet, a);
        V.bet.bump = 1;
        if (e->b) dv_sfx(SFX_BET_MAX, 1, 1, 0);
        else dv_sfx(SFX_BET_ONE, 1, exp2f((float)(a - 1) * 2.0f / 12.0f), 0);   /* a whole tone per level */
        break;
    case EV_DRAW_DENIED:
        V.deny = 2.4f;
        V.credits_shake = 1;
        dv_sfx(SFX_ERROR, 1, 1, 0);
        break;
    case EV_DRAW_CREDITS:
        if (a == DRAW_CREDITS_COLLECT) {
            meter_count(&V.credits, (double)e->v, count_secs(e->v - (long long)V.credits.shown));
        } else {
            meter_set(&V.credits, (double)e->v);
            V.credits.bump = 0.6f;
        }
        V.credits_to = e->v;
        break;
    case EV_DRAW_HAND_START:
        if (V.cel.active) celeb_skip(&V.cel);
        for (int i = 0; i < 5; i++) {
            sweep_out(&V.s[i], i);
            V.s[i].phase = SL_WAIT;
            V.s[i].held_on = 0;
            V.s[i].win = 0;
            V.s[i].flip_t = -1;
        }
        V.pre_cat = DC_NONE;
        V.win_cat = DC_NONE;
        V.win_mask = 0;
        V.lose_dim = 0;
        V.dbl_on = 0;
        V.dbl_linger = 0;
        V.dbl_round = 0;
        V.banner_t = -1;
        meter_set(&V.win, 0);
        bulbs_set(&V.bulbs, BULBS_IDLE, 8);
        break;
    case EV_DRAW_DEAL_CARD:
    case EV_DRAW_DRAW_CARD:
        fly_in(s, a, (Card)e->b);
        break;
    case EV_DRAW_FLIP_CARD:
        start_flip(s, (Card)e->b);
        dv_sfx(SFX_CARD_FLIP, 0.75f, 1, pan_x(slot_x(a)));
        break;
    case EV_DRAW_HOLD_PHASE:
        V.pre_cat = a;
        break;
    case EV_DRAW_HOLD:
        s->held_on = e->b;
        /* Visible on this very frame: the lift and glow jump part way and
           ease the rest. */
        if (e->b) s->held = fmaxf(s->held, 0.45f);
        else s->held = fminf(s->held, 0.55f);
        ui_button_press(&s->btn);
        dv_sfx(e->b ? SFX_HOLD_ON : SFX_HOLD_OFF, 1, 1, pan_x(slot_x(a)));
        break;
    case EV_DRAW_DISCARD: {
        int first = 1;
        for (int i = 0; i < a; i++)
            if (V.s[i].phase == SL_WAIT) first = 0;
        sweep_out(s, a);
        s->phase = SL_WAIT;
        if (first) dv_sfx(SFX_CARD_SLIDE, 0.7f, 1, 0);
        break;
    }
    case EV_DRAW_WIN: {
        V.pre_cat = DC_NONE;
        V.win_cat = a;
        for (int i = 0; i < 5; i++) V.s[i].held_on = 0;
        V.win_tier = e->b;
        V.win_mask = win_cards(g->variant, a, g->cards);   /* all five are face up by now */
        begin_celebration(v, e->b, e->v, a, V.win_mask);
        if (e->b >= DT_BIG) { meter_set(&V.win, 0); V.win.to = (double)e->v; }
        else meter_count(&V.win, (double)e->v, count_secs(e->v));
        break;
    }
    case EV_DRAW_NO_WIN:
        for (int i = 0; i < 5; i++) V.s[i].held_on = 0;
        V.pre_cat = DC_NONE;
        V.lose_dim = 0.001f;   /* starts easing in */
        break;
    case EV_DRAW_DOUBLE_OFFER:
        break;
    case EV_DRAW_DOUBLE_START:
        if (V.cel.active) celeb_skip(&V.cel);
        V.dbl_on = 1;
        V.dbl_linger = 0;
        V.dbl_round = a;
        V.dbl_card = CARD_NONE;
        V.dbl_flip = 0;
        V.dbl_flip_t = -1;
        V.dbl_guess = -1;
        V.dbl_result = 0;
        V.banner_t = -1;
        if (g_effects.card_anim) {
            V.dbl_flying = 1;
            V.dbl_snapped = 0;
            memset(&V.dbl_pose, 0, sizeof V.dbl_pose);
            card_motion_deal(&V.dbl_mo, k_shoe, (Vec2f){ 640, CARD_CY + 8 }, 0.08f, 0.26f, 120, -0.8f, 0, 0, 0);
            card_motion_update(&V.dbl_mo, 0, &V.dbl_pose);
        } else {
            V.dbl_flying = 0;
            V.dbl_pose = (CardPose){ .x = 640, .y = CARD_CY, .scale = 1 };
        }
        dv_sfx(SFX_WHOOSH, 0.5f, 1.1f, 0);
        break;
    case EV_DRAW_DOUBLE_GUESS:
        V.dbl_guess = e->b;
        V.dbl_guess_t = 0;
        dv_sfx(SFX_BUTTON, 1, 1, e->b == DRAW_RED ? -0.5f : 0.5f);
        break;
    case EV_DRAW_DOUBLE_CARD:
        V.dbl_card = (Card)e->b;
        V.dbl_flying = 0;
        V.dbl_pose = (CardPose){ .x = 640, .y = CARD_CY, .scale = 1 };
        V.dbl_flip_t = g_effects.card_anim ? 0 : -1;
        if (!g_effects.card_anim) V.dbl_flip = 1;
        clock_hitpause(&V.clock, 2);
        dv_sfx(SFX_CARD_FLIP, 1, 1, 0);
        break;
    case EV_DRAW_DOUBLE_WIN:
        V.dbl_result = 1;
        V.dbl_result_t = 0;
        if (e->b >= DT_BIG) {
            begin_celebration(v, e->b, e->v, DC_NONE, 0x04);
        } else {
            pfx_coin_burst(640, CARD_CY, 0.45f);
            pfx_sparkle(640, CARD_CY, 150, 18);
            dv_sfx(SFX_DOUBLE_WIN, 1, 1, 0);
            V.banner_t = 0;
            V.banner_amount = e->v;
            bulbs_set(&V.bulbs, BULBS_ALTERNATE, 7);
        }
        meter_count(&V.win, (double)e->v, count_secs(e->v / 2 + 1));
        break;
    case EV_DRAW_DOUBLE_LOSE:
        V.dbl_result = -1;
        V.dbl_result_t = 0;
        V.dbl_shake = 1;
        V.dbl_linger = 2.2f;
        shake_add(&V.shake, 0.25f);
        dv_sfx(SFX_DOUBLE_LOSE, 1, 1, 0);
        meter_count(&V.win, 0, 0.5f);
        break;
    case EV_DRAW_COLLECT:
        meter_count(&V.win, 0, count_secs(e->v));
        if (V.dbl_on) { V.dbl_on = 0; V.dbl_linger = 1.4f; }
        if (!V.cel.active) bulbs_set(&V.bulbs, BULBS_IDLE, 8);
        break;
    case EV_DRAW_HAND_END:
        if (V.dbl_on) { V.dbl_on = 0; if (V.dbl_linger <= 0) V.dbl_linger = 1.4f; }
        break;
    case APP_EV_BUTTON:
        on_button(v, a);
        break;
    default:
        break;
    }
}

/* ---- per-frame update ---------------------------------------------------- */

static void update_slot(Slot *s, int i, float dt, float real_dt)
{
    if (s->phase == SL_FLY) {
        int ev = card_motion_update(&s->mo, dt, &s->pose);
        float left = s->mo.dur - (s->mo.t - s->mo.delay);
        /* The snap sits ~15 ms into the sample: start it one frame early so
           it lands with the card. */
        if (!s->snapped && (left <= 1.0f / 60.0f + 0.002f || (ev & 2))) {
            s->snapped = 1;
            dv_sfx(SFX_CARD_DEAL, 0.85f, 1, pan_x(slot_x(i)));
        }
        if (ev & 2 || s->mo.state >= 3) {
            s->phase = SL_TABLE;
            s->pose.scale = 1;
        }
    }
    if (s->flip_t >= 0) {
        s->flip_t += dt;
        float k = clampf(s->flip_t / FLIP_DUR, 0, 1);
        s->flip = card_flip_curve(k);
        s->flip_lift = 12.0f * sinf(k * PI_F);
        if (k >= 1) { s->flip_t = -1; s->flip = 1; s->flip_lift = 0; }
    }
    if (s->out_on) {
        s->out_t += dt;
        if (s->out_t >= OUT_DUR) s->out_on = 0;
    }
    float target = s->held_on ? 1.0f : 0.0f;
    float rate = target > s->held ? 16.0f : 12.0f;
    s->held += (target - s->held) * clampf(real_dt * rate, 0, 1);
    if (fabsf(target - s->held) < 0.002f) s->held = target;
    ui_button_update(&s->btn, BTN_STATE_ON, real_dt);
}

void draw_view_update(const DrawViewInfo *v, const GameEvent *ev, int nev, float real_dt)
{
    ensure_init();
    const DrawGame *g = v->game;
    g_silent = v->demo;
    g_sounds = 0;
    /* A different game, or the same struct started afresh (the menu starts a
       new Draw game, attract a new demo): take the table as it is. */
    if (g != V.game || g->hand_no < V.hand_no) {
        V.game = g;
        sync_to_game(v);
    }
    V.hand_no = g->hand_no;

    int pressed_any = 0;
    for (int i = 0; i < nev; i++) {
        if (ev[i].type == APP_EV_BUTTON && ev[i].a <= 8) pressed_any = 1;
        on_event(v, &ev[i]);
    }
    /* A press that set nothing else off still clicks. */
    if (pressed_any && g_sounds == 0) dv_sfx(SFX_BUTTON, 0.55f, 1, 0);

    /* A message from the mode (e.g. "finish the hand") appears with the
       error sound, once. */
    if (v->message && v->message != V.last_msg) {
        dv_sfx(SFX_ERROR, 0.8f, 1, 0);
        V.deny = 0;
    }
    V.last_msg = v->message;
    if (v->hint_on != V.last_hint_on) {
        if (V.game && g->state == DS_HOLD) dv_sfx(v->hint_on ? SFX_HOLD_ON : SFX_HOLD_OFF, 0.7f, 1.3f, -0.6f);
        V.last_hint_on = v->hint_on;
    }

    float dt = clock_step(&V.clock, real_dt);
    V.t = V.clock.time;
    V.rt += real_dt;
    shake_update(&V.shake, real_dt);
    marquee_update(&V.mq, real_dt);
    if (marquee_take_cue(&V.mq)) dv_sfx(SFX_NEON_FLICKER, 0.25f, 1, -0.7f);
    bulbs_update(&V.bulbs, real_dt);

    for (int i = 0; i < 5; i++) update_slot(&V.s[i], i, dt, real_dt);

    /* Hint frames follow the hint for the cards on the table. */
    uint8_t hint = (v->hint_on && v->hint && g->state == DS_HOLD) ? v->hint->best : 0;
    for (int i = 0; i < 5; i++) {
        Slot *s = &V.s[i];
        float ht = (hint >> i & 1) ? 1.0f : 0.0f;
        s->hint += (ht - s->hint) * clampf(real_dt * 10, 0, 1);
        float wt = (V.win_mask >> i & 1) && V.win_cat != DC_NONE ? 1.0f : 0.0f;
        s->win += (wt - s->win) * clampf(real_dt * 8, 0, 1);
    }

    /* Paytable: the lit column slides (spring), rows ease in and out. */
    spring_update(&V.bet_col, (float)g->bet, 22, 0.75f, real_dt);
    float pt = V.pre_cat != DC_NONE && g->state == DS_HOLD ? 1.0f : 0.0f;
    V.pre_k += (pt - V.pre_k) * clampf(real_dt * 10, 0, 1);
    float wt = V.win_cat != DC_NONE ? 1.0f : 0.0f;
    V.win_k += (wt - V.win_k) * clampf(real_dt * 10, 0, 1);
    if (V.lose_dim > 0) V.lose_dim = fminf(1, V.lose_dim + real_dt * 3);
    V.variant_flash = fmaxf(0, V.variant_flash - real_dt * 1.5f);
    V.deny = fmaxf(0, V.deny - real_dt);
    V.credits_shake = fmaxf(0, V.credits_shake - real_dt * 2.5f);

    /* Meters: follow the wallet when something outside the hand changed it
       (a coin, the service menu). */
    if (!V.credits.running && (long long)llround(V.credits.to) != v->credits) {
        meter_count(&V.credits, (double)v->credits, 0.45f);
        V.credits_to = v->credits;
    }
    meter_update(&V.credits, real_dt);
    meter_update(&V.win, dt);
    meter_update(&V.bet, real_dt);
    if ((int)llround(V.bet.to) != g->bet) meter_set(&V.bet, g->bet);

    /* Double-up panel. */
    if (V.dbl_linger > 0) V.dbl_linger = fmaxf(0, V.dbl_linger - real_dt);
    float dv = V.dbl_on || V.dbl_linger > 0 ? 1.0f : 0.0f;
    V.dbl_vis += (dv - V.dbl_vis) * clampf(real_dt * 9, 0, 1);
    if (fabsf(dv - V.dbl_vis) < 0.003f) V.dbl_vis = dv;
    if (V.dbl_flying) {
        int e2 = card_motion_update(&V.dbl_mo, dt, &V.dbl_pose);
        float left = V.dbl_mo.dur - (V.dbl_mo.t - V.dbl_mo.delay);
        if (!V.dbl_snapped && V.dbl_mo.state >= 2 && (left <= 1.0f / 60.0f + 0.002f || (e2 & 2))) {
            V.dbl_snapped = 1;
            dv_sfx(SFX_CARD_DEAL, 0.9f, 1, 0);
        }
        if (V.dbl_mo.state >= 3) { V.dbl_flying = 0; V.dbl_pose.scale = 1; }
    }
    if (V.dbl_flip_t >= 0) {
        V.dbl_flip_t += dt;
        float k = clampf(V.dbl_flip_t / 0.3f, 0, 1);
        V.dbl_flip = card_flip_curve(k);
        if (k >= 1) V.dbl_flip_t = -1;
    }
    V.dbl_guess_t += real_dt;
    V.dbl_result_t += dt;
    V.dbl_shake = fmaxf(0, V.dbl_shake - real_dt * 2.2f);
    if (V.banner_t >= 0) {
        V.banner_t += dt;
        if (V.banner_t > 1.8f) { V.banner_t = -1; if (!V.cel.active) bulbs_set(&V.bulbs, BULBS_IDLE, 8); }
    }

    /* Ordinary play keeps the light chase for wins; the hold phase gets a
       slow wave, a finished hand a soft idle. */
    for (int b = 0; b < B_COUNT; b++) ui_button_update(&V.btn[b], BTN_STATE_ON, real_dt);
    celeb_update(&V.cel, dt, real_dt);
    /* Under a big celebration the WIN meter shows the celebration's own
       count-up, so the two never disagree; afterwards it holds the total. */
    if (V.cel.active && V.cel.tier >= WIN_BIG) {
        double to = V.win.to;
        V.win.shown = V.cel.meter.shown;
        V.win.running = 0;
        V.win.to = to;
    } else if (!V.win.running && fabs(V.win.shown - V.win.to) > 0.5) {
        V.win.shown = V.win.to;
    }
    pfx_update(dt);
}

/* ---- drawing: paytable --------------------------------------------------- */

void dv_paytable(Rectangle r, int variant, float bet_pos, int pre_cat, float pre_k, int win_cat, float win_k,
                 double time)
{
    const DrawPaytable *pt = draw_paytable(variant);
    if (!pt) return;
    ui_panel(r, UI_MAGENTA, 0.30f, 1.0f);
    const float pad = 10, head = 22;
    float name_w = r.width * 0.38f;
    float colw = (r.width - name_w - 2 * pad) / 5.0f;
    float x0 = r.x + pad + name_w;
    float rh = fminf(21.0f, (r.height - head - 2 * pad) / (float)pt->rows);
    float fs = fminf(22.0f, rh * 1.02f);
    float ytab = r.y + pad + head;

    /* The lit bet column: a honey glass band with a gold outline, sliding. */
    float bx = x0 + (clampf(bet_pos, 1, 5) - 1) * colw;
    gfx_nine(sprite_nine(NINE_RRECT), bx + 2, r.y + 6, colw - 4, r.height - 12, 12, gfx_add(UI_AMBER, 0.16f));
    gfx_nine(sprite_nine(NINE_RRECT_LINE), bx + 2, r.y + 6, colw - 4, r.height - 12, 12, gfx_cola(UI_GOLD, 0.85f));

    TextStyle ts;
    memset(&ts, 0, sizeof ts);
    ts.align = ALIGN_CENTER;
    int lit_col = (int)lroundf(clampf(bet_pos, 1, 5));
    for (int b = 1; b <= 5; b++) {
        char s[8];
        snprintf(s, sizeof s, "%d", b);
        ts.color = b == lit_col ? UI_GOLD : (Color){ 150, 120, 100, 255 };
        text_draw_ex(FONT_DISP_S, s, x0 + (b - 0.5f) * colw, r.y + pad - 1, 19, &ts);
    }
    memset(&ts, 0, sizeof ts);
    ts.color = (Color){ 190, 120, 200, 255 };
    ts.spacing = 2;
    text_draw_ex(FONT_UI_S, "BET", r.x + pad + 8, r.y + pad, 17, &ts);

    float flash = 0.5f + 0.5f * sinf((float)time * 9.0f);
    for (int i = 0; i < pt->rows; i++) {
        int cat = pt->row_cat[i];
        float y = ytab + (float)i * rh;
        int is_win = cat == win_cat && win_k > 0.01f, is_pre = cat == pre_cat && pre_k > 0.01f;
        if (is_pre) {
            gfx_rect(r.x + 6, y, r.width - 12, rh, gfx_add(UI_AMBER, 0.20f * pre_k));
            gfx_rect(r.x + 6, y + rh - 1.5f, r.width - 12, 1.5f, gfx_add(UI_GOLD, 0.7f * pre_k));
        }
        if (is_win) {
            float k = win_k * (0.35f + 0.65f * flash);
            gfx_rect(r.x + 6, y, r.width - 12, rh, gfx_add(UI_HONEY, 0.34f * k));
            gfx_spr_rot(sprite(SPR_GLOW), bx + colw * 0.5f, y + rh * 0.5f, colw * 1.6f, rh * 2.4f, 0,
                        gfx_add(UI_GOLD, 0.8f * k));
        }
        Color name_c = is_win ? (Color){ 255, 250, 235, 255 } : is_pre ? (Color){ 255, 244, 220, 255 }
                                                                       : (Color){ 255, 214, 130, 255 };
        text_draw(FONT_UI_M, draw_cat_name(cat), r.x + pad + 8, y + (rh - fs) * 0.5f - 1, fs, name_c);
        for (int b = 1; b <= 5; b++) {
            char s[16];
            snprintf(s, sizeof s, "%d", draw_pay(variant, cat, b));
            TextStyle ns;
            memset(&ns, 0, sizeof ns);
            ns.align = ALIGN_RIGHT;
            int on_col = b == lit_col;
            if (is_win && on_col) ns.color = WHITE;
            else if (on_col) ns.color = UI_GOLD;
            else if (is_win || is_pre) ns.color = (Color){ 240, 225, 200, 255 };
            else ns.color = (Color){ 176, 132, 88, 255 };
            if (cat == DC_ROYAL_FLUSH && b == 5) {
                ns.glow = UI_AMBER;
                ns.glow_k = on_col ? 0.6f : 0.0f;
            }
            text_draw_ex(FONT_UI_M, s, x0 + b * colw - 12, y + (rh - fs) * 0.5f - 1, fs, &ns);
        }
    }
}

/* ---- drawing: the table -------------------------------------------------- */

static void draw_left(const DrawViewInfo *v, double time)
{
    const DrawGame *g = v->game;
    const DrawPaytable *pt = draw_paytable(g->variant);
    /* The marquee is drawn last, outside the dimmed and shaken layer. */
    float vf = V.variant_flash;
    text_neon(FONT_NEON_M, draw_variant_name(g->variant), 240, 114, 36 * (1 + 0.08f * vf), UI_CYAN, 1, 0.9f + vf);
    static const char *const k_sub[DRAW_VARIANTS] = { "9/6 FULL PAY", "8/5 BONUS POKER", "FULL PAY DEUCES" };
    char s[96];
    snprintf(s, sizeof s, "%s  -  %.2f%% RETURN AT 5 COINS", g->variant >= 0 && g->variant < DRAW_VARIANTS ?
             k_sub[g->variant] : "", pt ? pt->optimal_return * 100.0 : 0.0);
    TextStyle ts;
    memset(&ts, 0, sizeof ts);
    ts.align = ALIGN_CENTER;
    ts.color = (Color){ 214, 190, 170, 255 };
    ts.spacing = 1;
    text_draw_ex(FONT_UI_S, s, 240, 136, 18, &ts);

    /* The strategy-hint panel. Cyan throughout: the hint's colour, used for
       nothing else on this screen. */
    Rectangle r = { 36, 162, 408, 76 };
    int live = v->hint_on && v->hint && g->state == DS_HOLD;
    ui_panel(r, v->hint_on ? UI_CYAN : (Color){ 90, 80, 100, 255 }, live ? 0.45f : 0.1f, 1);
    memset(&ts, 0, sizeof ts);
    ts.color = v->hint_on ? UI_CYAN : (Color){ 150, 140, 160, 255 };
    ts.spacing = 2;
    text_draw_ex(FONT_UI_S, v->hint_on ? "STRATEGY HINT" : "STRATEGY HINT  OFF", r.x + 14, r.y + 7, 17, &ts);
    Color body = (Color){ 205, 225, 235, 255 };
    if (live) {
        /* "HOLD" and the cards to keep as rank + neon suit pip, then the
           exact expected return of that hold. */
        const DrawHint *h = v->hint;
        static const char *const ranks[13] = { "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K", "A" };
        static const SpriteId pips[4] = { SPR_PIP_NEON_C, SPR_PIP_NEON_D, SPR_PIP_NEON_H, SPR_PIP_NEON_S };
        float x = r.x + 14, y = r.y + 27;
        text_draw(FONT_DISP_S, h->best ? "HOLD" : "DRAW FIVE NEW CARDS", x, y, 21, WHITE);
        x += text_width(FONT_DISP_S, "HOLD", 21, 0) + 12;
        for (int i = 0; i < 5 && h->best; i++) {
            if (!(h->best >> i & 1) || g->cards[i] == CARD_NONE) continue;
            int rk = card_rank(g->cards[i]), su = card_suit(g->cards[i]);
            text_draw(FONT_DISP_S, ranks[rk], x, y, 21, (Color){ 200, 250, 255, 255 });
            x += text_width(FONT_DISP_S, ranks[rk], 21, 0) + 1;
            Color pc = su == SUIT_H || su == SUIT_D ? (Color){ 255, 90, 110, 255 } : (Color){ 235, 240, 255, 255 };
            gfx_spr(sprite(pips[su]), x, y + 1, 22, 22, gfx_col(pc));
            x += 30;
        }
        snprintf(s, sizeof s, "EXPECTED RETURN  %.2f CREDITS", h->best_ev);
        text_draw(FONT_UI_M, s, r.x + 14, r.y + 50, 19, body);
    } else if (v->hint_on) {
        text_draw(FONT_UI_M, v->demo ? "THE DEMO PLAYS THE EXACT BEST HOLD" : "LIGHTS THE BEST HOLD WHEN YOU HOLD",
                  r.x + 14, r.y + 30, 20, body);
        text_draw(FONT_UI_S, "EXACT EXPECTED VALUE OF ALL 32 HOLDS", r.x + 14, r.y + 52, 16,
                  (Color){ 130, 170, 185, 255 });
    } else {
        text_draw(FONT_UI_M, "BET ONE WHILE HOLDING TURNS IT ON", r.x + 14, r.y + 34, 20,
                  (Color){ 150, 140, 160, 255 });
    }
    (void)time;
}

static void draw_card_slot(const DrawViewInfo *v, int i, float dim, double time)
{
    const DrawGame *g = v->game;
    Slot *s = &V.s[i];
    /* The outgoing card: turns down while it sinks and fades. */
    if (s->out_on) {
        float k = clampf(s->out_t / OUT_DUR, 0, 1), e = ease(EASE_IN_QUAD, k);
        CardPose p = { 0 };
        p.x = s->out_x + (float)(i - 2) * 14 * e;
        p.y = s->out_y + 110 * e;
        p.rot = s->out_rot + (i < 2 ? -0.18f : 0.18f) * e;
        p.flip = s->out_flip * (1 - ease(EASE_OUT_CUBIC, clampf(k * 2, 0, 1)));
        p.scale = CARD_SC * (1 - 0.08f * e);
        p.alpha = fmaxf(0.01f, 1 - ease(EASE_OUT_QUAD, k));   /* fades fast, sinks late */
        p.no_shadow = 1;
        card_draw(s->out_card, CARD_L, &p);
    }
    if (s->phase == SL_EMPTY || s->phase == SL_WAIT) return;
    CardPose p = s->pose;
    p.scale = (p.scale > 0 ? p.scale : 1) * CARD_SC;
    if (s->phase == SL_TABLE) {
        p.x = slot_x(i);
        p.y = CARD_CY;
        p.scale = CARD_SC;
    }
    p.flip = s->flip;
    p.lift = s->flip_lift + 18.0f * ease(EASE_OUT_BACK, s->held);
    p.glow = s->held;
    if (s->held > 0.5f && g->state != DS_DRAWING) card_shimmer(&p, time, i);
    if (s->hint > 0.01f && s->held < 0.5f) {
        p.glow = fmaxf(p.glow, s->hint * (0.55f + 0.25f * sinf((float)time * 5)));
        p.glow_color = UI_CYAN;
    }
    if (s->win > 0.01f) p.highlight = s->win * (0.55f + 0.45f * sinf((float)time * 6));
    else if (V.win_cat != DC_NONE) p.dim = 0.5f * V.win_k;
    if (V.lose_dim > 0) p.dim = fmaxf(p.dim, 0.35f * V.lose_dim);
    if (V.dbl_vis > 0) {
        p.dim = fmaxf(p.dim, 0.75f * V.dbl_vis);
        p.alpha = fmaxf(0.02f, 1 - 0.95f * V.dbl_vis);
        if (V.dbl_vis > 0.97f) return;
    }
    float t = (s->win > 0.5f) ? 1.0f : dim;
    gfx_set_tint(t, t, t);
    card_draw(s->card, CARD_L, &p);
    gfx_set_tint(dim, dim, dim);
    /* The hint chip, straddling the card's top edge. */
    if (s->hint > 0.02f && V.dbl_vis < 0.5f) {
        float cx = slot_x(i), cy = CARD_CY - CARD_L_H * CARD_SC * 0.5f - p.lift - 2;
        float a = s->hint;
        gfx_nine(sprite_nine(NINE_RRECT), cx - 30, cy - 11, 60, 22, 10, gfx_cola((Color){ 6, 40, 52, 255 }, 0.95f * a));
        gfx_nine(sprite_nine(NINE_RRECT_LINE), cx - 30, cy - 11, 60, 22, 10, gfx_cola(UI_CYAN, a));
        TextStyle ts;
        memset(&ts, 0, sizeof ts);
        ts.align = ALIGN_CENTER;
        ts.color = (Color){ 200, 250, 255, 255 };
        ts.opacity = a;
        ts.spacing = 1;
        text_draw_ex(FONT_DISP_S, "HINT", cx, cy - 8, 15, &ts);
    }
}

static void hold_button(int i, const char *label, Color neon, UiButtonState st, double time)
{
    Rectangle r = { slot_x(i) - HOLD_W / 2, HOLD_CY - HOLD_H / 2, HOLD_W, HOLD_H };
    ui_button_draw_key(&V.s[i].btn, r, label, neon, st, time, BTN_HOLD_N(i));
}

static void draw_hold_row(const DrawViewInfo *v, double time)
{
    const DrawGame *g = v->game;
    int st = g->state;
    static const Color k_red = { 255, 60, 80, 255 }, k_black = { 170, 190, 255, 255 };
    for (int i = 0; i < 5; i++) {
        char lab[16];
        if (st == DS_OFFER) {
            if (i == 0) hold_button(i, g->dbl_round ? "DOUBLE AGAIN" : "DOUBLE UP", UI_MAGENTA, BTN_STATE_LIT, time);
            else if (i == 4) hold_button(i, "TAKE WIN", UI_GOLD, BTN_STATE_LIT, time);
            else hold_button(i, "", (Color){ 80, 70, 90, 255 }, BTN_STATE_OFF, time);
            continue;
        }
        if (st == DS_DOUBLE || st == DS_DOUBLE_REVEAL) {
            int guessed = st == DS_DOUBLE_REVEAL;
            if (i < 2) hold_button(i, "RED", k_red, guessed && V.dbl_guess != DRAW_RED ? BTN_STATE_OFF : BTN_STATE_LIT, time);
            else if (i > 2) hold_button(i, "BLACK", k_black,
                                        guessed && V.dbl_guess != DRAW_BLACK ? BTN_STATE_OFF : BTN_STATE_LIT, time);
            else hold_button(i, "TAKE WIN", UI_GOLD, guessed ? BTN_STATE_OFF : BTN_STATE_ON, time);
            continue;
        }
        Slot *s = &V.s[i];
        if (s->held > 0.02f && g->in_hand) {
            ui_held_tag(slot_x(i), HOLD_CY, s->held, time);
            continue;
        }
        snprintf(lab, sizeof lab, "HOLD %d", i + 1);
        UiButtonState bs = st == DS_HOLD ? BTN_STATE_ON : BTN_STATE_OFF;
        if (v->demo && st != DS_HOLD) continue;
        hold_button(i, lab, UI_HONEY, bs, time);
    }
}

/* The double-up screen: it takes the hand's place (the hand fades out
 * behind it). A face-down card in the middle, RED on the left, BLACK on the
 * right, the round and stake along the top; the reveal turns the card, and
 * the chosen side lights up (won) or goes dark (lost). */
#define DBL_SC 0.78f
static void draw_double(const DrawViewInfo *v, double time)
{
    const DrawGame *g = v->game;
    float a = V.dbl_vis;
    if (a <= 0.01f) return;
    float sc = 0.94f + 0.06f * ease(EASE_OUT_BACK, a);
    float w = 780 * sc, h = 278 * sc;
    Rectangle r = { 640 - w / 2, CARD_CY - h / 2, w, h };
    Color edge = V.dbl_result < 0 ? (Color){ 255, 60, 80, 255 } : UI_MAGENTA;
    ui_panel(r, edge, 0.55f + 0.3f * (V.dbl_result != 0), a);

    TextStyle ts;
    memset(&ts, 0, sizeof ts);
    ts.color = (Color){ 255, 170, 235, 255 };
    ts.opacity = a;
    ts.spacing = 3;
    char s[64];
    snprintf(s, sizeof s, "DOUBLE UP  -  ROUND %d", V.dbl_round > 0 ? V.dbl_round : 1);
    text_draw_ex(FONT_UI_M, s, r.x + 22, r.y + 10, 21, &ts);
    long long stake = g->state == DS_DOUBLE || g->state == DS_DOUBLE_REVEAL ? g->meter : (long long)V.win.to;
    if (V.dbl_result > 0) snprintf(s, sizeof s, "WIN %lld", (long long)g->meter);
    else if (V.dbl_result < 0) snprintf(s, sizeof s, "STAKE LOST");
    else snprintf(s, sizeof s, "%lld  DOUBLES TO  %lld", stake, stake * 2);
    memset(&ts, 0, sizeof ts);
    ts.align = ALIGN_RIGHT;
    ts.color = V.dbl_result < 0 ? (Color){ 255, 120, 130, 255 } : UI_GOLD;
    ts.glow = UI_AMBER;
    ts.glow_k = 0.3f;
    ts.opacity = a;
    text_draw_ex(FONT_DISP_S, s, r.x + r.width - 22, r.y + 9, 21, &ts);

    /* RED on the left, BLACK on the right, each with its two suits. */
    float pulse = 0.72f + 0.28f * sinf((float)time * 6);
    for (int side = 0; side < 2; side++) {
        float cx = side ? 640 + 240 * sc : 640 - 240 * sc;
        int chosen = V.dbl_guess == side;
        float pw;
        if (V.dbl_guess >= 0) pw = chosen ? 1.0f : 0.22f;
        else pw = g->state == DS_DOUBLE ? pulse : 0.6f;
        if (chosen && V.dbl_result < 0) pw = 0.35f;
        Color tube = side ? (Color){ 160, 190, 255, 255 } : (Color){ 255, 50, 70, 255 };
        if (chosen && V.dbl_result >= 0)
            gfx_spr_rot(sprite(SPR_GLOW), cx, CARD_CY - 6, 320, 230, 0, gfx_add(tube, 0.4f * a));
        text_neon(FONT_NEON_L, side ? "BLACK" : "RED", cx, CARD_CY - 38, 62 * sc, tube, pw * a, 1.0f);
        static const SpriteId pips[2][2] = { { SPR_PIP_NEON_H, SPR_PIP_NEON_D }, { SPR_PIP_NEON_S, SPR_PIP_NEON_C } };
        for (int k = 0; k < 2; k++)
            gfx_spr_rot(sprite(pips[side][k]), cx + (k ? 30.0f : -30.0f), CARD_CY + 24, 48, 48, 0,
                        gfx_cola(tube, pw * a));
        memset(&ts, 0, sizeof ts);
        ts.align = ALIGN_CENTER;
        ts.opacity = a;
        if (chosen && V.dbl_result != 0) {
            ts.color = V.dbl_result > 0 ? UI_GOLD : (Color){ 255, 110, 120, 255 };
            text_draw_ex(FONT_DISP_S, V.dbl_result > 0 ? "RIGHT!" : "NOT THIS TIME", cx, CARD_CY + 58, 22, &ts);
        } else {
            ts.color = (Color){ 200, 190, 210, 255 };
            ts.opacity = a * (0.4f + 0.6f * pw);
            text_draw_ex(FONT_UI_M, side ? "HOLD 4 / 5" : "HOLD 1 / 2", cx, CARD_CY + 58, 20, &ts);
        }
    }

    /* Earlier cards of this double-up, small, along the bottom left. */
    for (int i = 0; i + 1 < V.dbl_round && i < 8; i++) {
        if (g->dbl_hist[i] == CARD_NONE) continue;
        CardPose p = { .x = r.x + 34 + i * 30.0f, .y = r.y + r.height - 40, .scale = 0.3f, .flip = 1,
                       .alpha = a, .no_shadow = 1 };
        card_draw(g->dbl_hist[i], CARD_M, &p);
    }

    /* The card. */
    CardPose p = V.dbl_pose;
    if (!V.dbl_flying) { p.x = 640; p.y = CARD_CY + 8; p.scale = 1; }
    p.scale *= DBL_SC * sc;
    p.flip = V.dbl_flip;
    p.alpha = a;
    p.back = 1;
    if (V.dbl_shake > 0) p.x += sinf((float)V.rt * 70) * 12 * V.dbl_shake;
    if (V.dbl_result > 0) p.highlight = 0.6f + 0.4f * sinf((float)time * 7);
    if (V.dbl_result < 0) p.dim = 0.35f;
    if (g->state == DS_DOUBLE && !V.dbl_flying) {
        p.glow = 0.5f + 0.3f * sinf((float)time * 4);
        p.glow_color = UI_MAGENTA;
        p.lift = 5 * sinf((float)time * 2.2f);
    }
    Card c = V.dbl_flip > 0.02f ? V.dbl_card : CARD_NONE;
    if (V.dbl_card != CARD_NONE || V.dbl_flying || g->state == DS_DOUBLE || g->state == DS_DOUBLE_REVEAL)
        card_draw(c, CARD_L, &p);

    /* "DOUBLED!" over the side that was called. */
    if (V.banner_t >= 0) {
        float ap = clampf(V.banner_t / 0.4f, 0, 1), out = clampf((V.banner_t - 1.4f) / 0.4f, 0, 1);
        float bx = V.dbl_guess == DRAW_BLACK ? 640 + 240 * sc : 640 - 240 * sc;
        ui_banner("DOUBLED!", bx, CARD_CY - 38, 50, ap, out, 0, time);
    }
}

static const char *state_message(const DrawViewInfo *v, char *buf, size_t cap, Color *col, float *power)
{
    const DrawGame *g = v->game;
    float blink = 0.55f + 0.45f * sinf((float)V.rt * 5);
    *power = 1;
    *col = UI_HONEY;
    if (v->message) { *col = UI_AMBER; return v->message; }
    if (V.deny > 0) { *col = (Color){ 255, 60, 80, 255 }; *power = blink; return "NOT ENOUGH CREDITS  -  INSERT COIN"; }
    switch (g->state) {
    case DS_IDLE:
        if (g->hand_no == 0) {
            *power = blink;
            return v->demo ? "" : "PRESS DEAL OR BET MAX";
        }
        if (g->last.paid > 0) {
            snprintf(buf, cap, "%s  -  PAID %d", g->last.dbl_won ? "DOUBLE UP" : draw_cat_name(g->last.cat), g->last.paid);
            return buf;
        }
        if (g->last.win > 0) { *col = (Color){ 255, 90, 110, 255 }; return "DOUBLE UP LOST"; }
        *col = (Color){ 200, 180, 190, 255 };
        return v->demo ? "" : "GAME OVER  -  PRESS DEAL";
    case DS_DEALING:
        *col = (Color){ 220, 200, 255, 255 };
        return "GOOD LUCK";
    case DS_HOLD:
        if (g->dealt_cat != DC_NONE) {
            snprintf(buf, cap, "%s!", draw_cat_name(g->dealt_cat));
            return buf;
        }
        *col = (Color){ 235, 225, 255, 255 };
        return "CHOOSE CARDS TO HOLD, THEN DRAW";
    case DS_DRAWING:
        return "";
    case DS_OFFER:
        *col = UI_GOLD;
        if (g->dbl_round > 0) snprintf(buf, cap, "DOUBLED!  WIN %d  -  DOUBLE AGAIN OR TAKE IT", g->meter);
        else snprintf(buf, cap, "%s  WINS %d  -  DOUBLE UP?", draw_cat_name(g->final_cat), g->meter);
        return buf;
    case DS_DOUBLE:
        *col = UI_MAGENTA;
        return "RED OR BLACK?";
    case DS_DOUBLE_REVEAL:
        *col = UI_MAGENTA;
        return g->dbl_guess == DRAW_RED ? "YOU CALLED RED ..." : "YOU CALLED BLACK ...";
    default:
        return "";
    }
}

static void draw_bar(const DrawViewInfo *v, double time)
{
    const DrawGame *g = v->game;
    float sx = V.credits_shake > 0 ? sinf((float)V.rt * 60) * 8 * V.credits_shake : 0;
    Rectangle rc = { 30 + sx, BAR_Y, 232, BAR_H };
    meter_draw(&V.credits, rc, "CREDITS", V.deny > 0 ? (Color){ 255, 60, 80, 255 } : UI_CYAN);
    meter_draw(&V.bet, (Rectangle){ 272, BAR_Y, 104, BAR_H }, "BET", UI_AMBER);
    meter_draw(&V.win, (Rectangle){ 386, BAR_Y, 196, BAR_H }, "WIN", UI_MAGENTA);
    if (v->denom_cents > 0) {
        long long cents = (long long)llround(V.credits.shown) * v->denom_cents;
        char s[32];
        snprintf(s, sizeof s, "$%lld.%02lld", cents / 100, cents % 100);
        TextStyle ts;
        memset(&ts, 0, sizeof ts);
        ts.align = ALIGN_RIGHT;
        ts.color = (Color){ 140, 200, 215, 255 };
        text_draw_ex(FONT_UI_S, s, rc.x + rc.width - 12, rc.y + 5, 15, &ts);
    }

    int st = g->state;
    int idle = st == DS_IDLE, hold = st == DS_HOLD, offer = st == DS_OFFER, dbl = st == DS_DOUBLE;
    int busy = st == DS_DEALING || st == DS_DRAWING || st == DS_DOUBLE_REVEAL;
    const float by = BAR_Y + 7, bh = BAR_H - 14;
    UiButtonState s1 = busy ? BTN_STATE_OFF : BTN_STATE_ON;
    const char *l1 = hold ? (v->hint_on ? "HINT OFF" : "HINT ON") : "BET ONE";
    ui_button_draw_key(&V.btn[B_BETONE], (Rectangle){ 598, by, 146, bh }, l1, hold ? UI_CYAN : UI_AMBER, s1, time, BTN_BET_ONE);
    ui_button_draw_key(&V.btn[B_BETMAX], (Rectangle){ 754, by, 146, bh }, "BET MAX", UI_MAGENTA,
                       idle || offer || dbl ? BTN_STATE_ON : BTN_STATE_OFF, time, BTN_BET_MAX);
    UiButtonState sd = busy ? BTN_STATE_OFF : (idle || hold) ? BTN_STATE_LIT : BTN_STATE_ON;
    ui_button_draw_key(&V.btn[B_DEAL], (Rectangle){ 910, by, 176, bh }, hold ? "DRAW" : "DEAL", UI_GOLD, sd, time, BTN_DEAL);
    ui_button_draw_key(&V.btn[B_CASH], (Rectangle){ 1096, by, 150, bh }, "CASH OUT", UI_CYAN,
                       busy ? BTN_STATE_OFF : BTN_STATE_ON, time, BTN_CASH_OUT);
}

/* Fill accounting (BPL_FILL_LOG=1): pixels drawn per frame by scene, the
 * desktop proxy for the Pi's fill-bound GPU cost (~2.2 ms per Mpx). */
void dv_fill_account(int scene)
{
    static int on = -1;
    static double sum[DV_FILL_SCENES], mx[DV_FILL_SCENES];
    static long n[DV_FILL_SCENES], frames;
    double f = gfx_fill_take();
    if (on < 0) {
        const char *e = getenv("BPL_FILL_LOG");
        on = e && *e && *e != '0';
    }
    if (!on || scene < 0 || scene >= DV_FILL_SCENES) return;
    sum[scene] += f;
    n[scene]++;
    if (f > mx[scene]) mx[scene] = f;
    if (++frames % 300 == 0) {
        static const char *const nm[DV_FILL_SCENES] = { "play", "celebration", "jackpot", "menu", "title" };
        for (int i = 0; i < DV_FILL_SCENES; i++)
            if (n[i])
                fprintf(stderr, "DRAWVIEW fill %-11s mean %.2f Mpx max %.2f Mpx (%ld frames)\n", nm[i],
                        sum[i] / (double)n[i] / 1e6, mx[i] / 1e6, n[i]);
    }
}

void draw_view_draw(const DrawViewInfo *v, double time)
{
    ensure_init();
    (void)time;
    double t = V.t;
    const DrawGame *g = v->game;
    float dx, dy, deg;
    shake_offset(&V.shake, &dx, &dy, &deg);
    float dim = celeb_dim(&V.cel);

    render_begin();
    ui_background(t, dim);
    gfx_push_offset(dx, dy, deg, PLAY_W * 0.5f, PLAY_H * 0.5f);
    gfx_set_tint(dim, dim, dim);
    dv_paytable(k_pay, g->variant, V.bet_col.x, V.pre_cat, V.pre_k, V.win_cat, V.win_k, t);
    draw_left(v, t);
    /* Cards: waiting slots show a faint outline of where the card goes. */
    for (int i = 0; i < 5; i++) {
        float w = CARD_L_W * CARD_SC, h = CARD_L_H * CARD_SC;
        if (V.s[i].phase == SL_WAIT || V.s[i].phase == SL_FLY)
            gfx_nine(sprite_nine(NINE_RRECT_LINE), slot_x(i) - w / 2, CARD_CY - h / 2, w, h, 16,
                     gfx_add(UI_HONEY, 0.12f));
    }
    for (int i = 0; i < 5; i++) draw_card_slot(v, i, dim, t);
    draw_double(v, t);
    draw_hold_row(v, t);
    char buf[96];
    Color mc;
    float pw;
    const char *msg = state_message(v, buf, sizeof buf, &mc, &pw);
    if (msg && *msg && !(V.cel.active && V.cel.tier >= WIN_BIG))
        text_neon(FONT_NEON_M, msg, 640, MSG_CY, 30, mc, pw, 0.8f);
    if (!v->demo) draw_bar(v, t);
    gfx_set_tint(1, 1, 1);
    celeb_draw_back(&V.cel, t);
    pfx_draw();
    celeb_draw_front(&V.cel, t);
    gfx_pop_offset();
    bulbs_draw(&V.bulbs);
    marquee_draw(&V.mq, 240, 60, 0.5f);
    render_end();
    dv_fill_account(V.cel.active ? (V.cel.tier >= WIN_JACKPOT ? DV_FILL_JACKPOT : DV_FILL_CELEB) : DV_FILL_PLAY);
}

void draw_view_probe(DrawViewProbe *out)
{
    memset(out, 0, sizeof *out);
    for (int i = 0; i < 5; i++) {
        const Slot *s = &V.s[i];
        if ((s->phase == SL_TABLE || s->phase == SL_FLY) && s->flip >= 0.5f) out->slots_face_up |= 1 << i;
        if (s->held > 0.5f) out->held_shown |= (uint8_t)(1u << i);
    }
    out->celebrating = V.cel.active ? (int)V.cel.tier : 0;
    out->double_panel = V.dbl_vis > 0.01f;
    out->credits_shown = (long long)llround(V.credits.shown);
    out->win_shown = (long long)llround(V.win.shown);
    out->particles = pfx_count();
}

/* ---- the view table ------------------------------------------------------ */

const DrawScreenViews draw_views_render = {
    .draw_update = draw_view_update,
    .draw_view = draw_view_draw,
    .menu_update = menu_view_update,
    .menu_view = menu_view_draw,
    .attract_update = attract_view_update,
    .attract_view = attract_view_draw,
};
