/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* mode_attract.c - APP_ATTRACT: the bar-machine attract loop.
 *
 *   TITLE (6 s): the neon title, a paytable, "PRESS DEAL"
 *   DRAW demo: five hands of Draw Poker played by the exact-EV strategy
 *              (draw_hint_compute on the five dealt cards, then the holds
 *              pressed one by one like a player would), the paytable on
 *              screen, the variant changing every loop
 *   HOLD'EM demo (45 s): a sit-and-go with all six seats played by the AI
 *              (HoldemConfig.seat0_ai)
 * then round again. Any button (or a touch) goes to the menu; a COIN is
 * credited as well (session.c).
 *
 * Honesty (CONTRACT section 5): the demos are the real games, run by
 * draw_tick / holdem_tick exactly as in play, on their own Rng streams
 * seeded from the session's seed stream. Every deal is a real Fisher-Yates
 * shuffle; nothing is chosen, filtered or replayed to look good. The demos
 * use a private wallet and never touch the player's credits. */
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "ai/ai.h"
#include "games/draw/draw_game.h"
#include "games/draw/draw_hint.h"
#include "games/holdem/holdem.h"
#include "platform/app.h"
#include "platform/placeholder_view.h"
#include "platform/session.h"

#define TITLE_TICKS   (6 * 60)
#define DRAW_HANDS    5
#define DRAW_MAX_TICKS (60 * 60)
#define HOLDEM_TICKS  (45 * 60)

static struct {
    int        phase, t;
    int        variant;
    Rng        rng;             /* attract's own stream: seeds for the demos */
    Wallet     w;               /* the demo's play money                     */
    DrawGame   dg;
    DrawHint   hint;
    int        hint_valid;
    uint8_t    target;          /* holds the demo player will press          */
    int        next_t, hands;
    HoldemGame hg;
    int        hg_running;
    char       names[HOLDEM_SEATS][32];
} A;

static void start_title(void)
{
    A.phase = ATTRACT_TITLE;
    A.t = 0;
}

static void start_draw(void)
{
    DrawConfig cfg;
    draw_config_default(&cfg);
    cfg.variant = A.variant;
    cfg.allow_variant_select = 0;
    cfg.bet = DRAW_MAX_BET;
    cfg.double_up = 0;
    draw_init(&A.dg, &cfg, rng_next(&A.rng));
    A.w.credits = 1000000;
    A.w.denom = 1;
    A.phase = ATTRACT_DRAW;
    A.t = 0;
    A.next_t = 45;
    A.hands = 0;
    A.hint_valid = 0;
}

static void start_holdem(void)
{
    HoldemConfig cfg;
    holdem_config_default(&cfg);
    cfg.seat0_ai = 1;
    int pers[5];
    ai_assign_personalities(AI_DIFF_NORMAL, &A.rng, pers);
    cfg.personality[0] = AI_SHARK;
    for (int i = 0; i < 5; i++) cfg.personality[i + 1] = pers[i];
    static const char *const nm[HOLDEM_SEATS] = { "BEE", "BUZZ", "HONEY", "STINGER", "DRONE", "QUEENIE" };
    for (int i = 0; i < HOLDEM_SEATS; i++) {
        char pn[16];
        snprintf(pn, sizeof pn, "%s", ai_personality_name(cfg.personality[i]));
        for (char *c = pn; *c; c++) *c = (char)toupper((unsigned char)*c);
        snprintf(A.names[i], sizeof A.names[i], "%s (%s)", nm[i], pn);
    }
    AiPool *pool = session_ai_pool();
    HoldemAiHooks hooks = pool ? ai_pool_hooks(pool) : (HoldemAiHooks){ 0 };
    holdem_init(&A.hg, &cfg, rng_next(&A.rng), pool ? &hooks : NULL);
    A.hg_running = 1;
    A.phase = ATTRACT_HOLDEM;
    A.t = 0;
}

static void stop_holdem(void)
{
    if (A.hg_running) holdem_release(&A.hg);
    A.hg_running = 0;
}

static void attract_enter(AppCtx *ctx, AppState from)
{
    (void)ctx;
    if (from == APP_SERVICE && A.phase != ATTRACT_TITLE) return;    /* resume the demo */
    rng_seed(&A.rng, session_next_seed());
    stop_holdem();
    start_title();
}

static void attract_leave(AppCtx *ctx, AppState to)
{
    (void)ctx;
    if (to != APP_SERVICE) {
        stop_holdem();
        A.phase = ATTRACT_TITLE;
        A.variant = (A.variant + 1) % DRAW_VARIANTS;
    }
}

/* The demo player: the input a person following the hint would give. */
static void draw_demo_tick(AppCtx *ctx)
{
    DrawGame *g = &A.dg;
    InputFrame f;
    memset(&f, 0, sizeof f);
    if (A.t >= A.next_t) {
        if (g->state == DS_IDLE) {
            if (A.hands >= DRAW_HANDS) {
                A.variant = (A.variant + 1) % DRAW_VARIANTS;
                start_holdem();
                return;
            }
            f.pressed = BTN_DEAL;
        } else if (g->state == DS_HOLD) {
            uint8_t todo = (uint8_t)(A.target & ~g->held);
            if (todo) {
                int i = 0;
                while (!(todo >> i & 1)) i++;
                f.pressed = BTN_HOLD1 << i;
                A.next_t = A.t + 16;
            } else {
                f.pressed = BTN_DEAL;
            }
        }
    }
    int n0 = ctx->events.n;
    draw_tick(g, &f, &A.w, &ctx->events);
    for (int i = n0; i < ctx->events.n; i++) {
        const GameEvent *e = &ctx->events.e[i];
        if (e->type == EV_DRAW_HAND_START) A.hint_valid = 0;
        if (e->type == EV_DRAW_HOLD_PHASE) {
            A.hint_valid = draw_hint_compute(g->variant, g->bet, g->cards, &A.hint) == 0;
            A.target = A.hint_valid ? A.hint.best : 0;
            A.next_t = A.t + 50;
        }
        if (e->type == EV_DRAW_HAND_END) {
            A.hands++;
            A.next_t = A.t + (g->last.paid > 0 ? 200 : 120);
        }
    }
    if (A.t > DRAW_MAX_TICKS && g->state == DS_IDLE) A.hands = DRAW_HANDS;
}

static void attract_tick(AppCtx *ctx, const InputFrame *in)
{
    if ((in->pressed & ~(uint32_t)BTN_DEBUG) || in->touch) {
        app_request(ctx, APP_MENU);
        return;
    }
    A.t++;
    switch (A.phase) {
    case ATTRACT_TITLE:
        if (A.t >= TITLE_TICKS) start_draw();
        break;
    case ATTRACT_DRAW:
        draw_demo_tick(ctx);
        break;
    case ATTRACT_HOLDEM: {
        InputFrame f;
        memset(&f, 0, sizeof f);
        holdem_tick(&A.hg, &f, NULL, &ctx->events);
        if (A.t >= HOLDEM_TICKS || A.hg.phase == HP_GAME_OVER) {
            stop_holdem();
            start_title();
        }
        break;
    }
    }
}

static void fill_draw(DrawViewInfo *v)
{
    memset(v, 0, sizeof *v);
    v->game = &A.dg;
    v->hint_on = 1;
    v->hint = A.hint_valid ? &A.hint : NULL;
    v->demo = 1;
}

static void fill_holdem(const AppCtx *ctx, HoldemViewInfo *v)
{
    memset(v, 0, sizeof *v);
    v->game = &A.hg;
    v->phase = HV_PLAYING;
    v->credits = ctx->wallet.credits;
    v->demo = 1;
    for (int i = 0; i < HOLDEM_SEATS; i++) v->seat_name[i] = A.names[i];
}

static void attract_present_update(const AppCtx *ctx, const GameEvent *ev, int nev, float dt)
{
    placeholder_common_update(ctx->state, ev, nev, dt);
    if (A.phase == ATTRACT_DRAW) {
        DrawViewInfo v;
        fill_draw(&v);
        draw_placeholder_update(&v, ev, nev, dt);
    } else if (A.phase == ATTRACT_HOLDEM) {
        HoldemViewInfo v;
        fill_holdem(ctx, &v);
        holdem_view_update(&v, ev, nev, dt);
    }
}

static void attract_present_draw(const AppCtx *ctx)
{
    AttractViewInfo av = { A.phase, A.phase == ATTRACT_DRAW ? A.dg.variant : A.variant, ctx->wallet.credits };
    if (A.phase == ATTRACT_DRAW) {
        DrawViewInfo v;
        fill_draw(&v);
        draw_placeholder_view(&v, ctx->time);
    } else if (A.phase == ATTRACT_HOLDEM) {
        HoldemViewInfo v;
        fill_holdem(ctx, &v);
        holdem_view_draw(&v, ctx->time);
    }
    /* The real Hold'em view draws its own PRESS DEAL overlay. */
    if (A.phase != ATTRACT_HOLDEM || !app_holdem_view) attract_placeholder_view(&av, ctx->time);
}

static void attract_shutdown(void)
{
    stop_holdem();
}

const AppMode mode_attract = {
    .name = "attract",
    .enter = attract_enter,
    .leave = attract_leave,
    .tick = attract_tick,
    .present_update = attract_present_update,
    .present_draw = attract_present_draw,
    .shutdown = attract_shutdown,
};
