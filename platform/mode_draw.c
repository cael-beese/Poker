/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* mode_draw.c - APP_DRAW: the real Draw Poker game (games/draw) at 60 Hz
 * with the app's Wallet, the hand log, stats, the strategy hint and cash-out.
 *
 * Controls are games/draw/draw_game.h's, plus:
 *   CASH OUT   between hands: back to the menu. With a win on the meter
 *              (double-up offer or a double-up guess): the win is collected
 *              first, then back to the menu. During a deal or draw it is
 *              refused ("FINISH THE HAND") - the bet is already in play.
 *   BET ONE    while holding (where the game has no use for it): strategy
 *              hint on / off. The hint is the exact-EV best hold over all 32
 *              patterns, computed from the player's own five cards only.
 *   60 s idle between hands (nothing on the meter): attract mode.
 *
 * Presentation is placeholder_draw.c, through draw_placeholder_update/view. */
#include <string.h>
#include <time.h>

#include "games/draw/draw_game.h"
#include "games/draw/draw_hint.h"
#include "platform/app.h"
#include "platform/placeholder_view.h"
#include "platform/session.h"

static struct {
    DrawGame g;
    int      active;
    int      bet;               /* carried across games                         */
    DrawHint hint;
    int      hint_valid;
    int      leave;             /* CASH OUT: collect, then back to the menu      */
    const char *msg;
    int      msg_ticks;
} D;

static void new_game(int variant)
{
    DrawConfig cfg;
    draw_config_default(&cfg);
    cfg.variant = variant;
    cfg.bet = D.bet >= 1 && D.bet <= DRAW_MAX_BET ? D.bet : DRAW_MAX_BET;
    cfg.double_up = session_settings()->double_up;
    draw_init(&D.g, &cfg, session_next_seed());
    D.active = 1;
    D.hint_valid = 0;
    D.leave = 0;
}

static void draw_enter(AppCtx *ctx, AppState from)
{
    (void)ctx;
    int variant = session_menu_game();
    if (variant < 0 || variant >= DRAW_VARIANTS) variant = DRAW_JOB;
    if (from == APP_SERVICE && D.active) {
        /* Back from the service menu: carry on, but pick up a changed
           double-up setting if nothing is in play. */
        if (D.g.state == DS_IDLE && D.g.meter == 0 && D.g.cfg.double_up != session_settings()->double_up) {
            D.bet = D.g.bet;
            new_game(D.g.variant);
        }
        return;
    }
    if (D.active) D.bet = D.g.bet;
    new_game(variant);
}

static void say(const char *m)
{
    D.msg = m;
    D.msg_ticks = 150;
}

static void on_hand_end(const DrawGame *g)
{
    const DrawHandRecord *h = &g->last;
    Stats *st = session_stats();
    int v = h->variant >= 0 && h->variant < SAVE_VARIANTS ? h->variant : 0;
    st->draw_hands[v]++;
    st->draw_bet[v] += (uint64_t)h->bet;
    st->draw_paid[v] += (uint64_t)(h->paid > 0 ? h->paid : 0);
    if (h->cat == DC_ROYAL_FLUSH) st->royals++;
    if (h->cat == DC_FOUR_DEUCES) st->four_deuces++;
    st->dbl_played += (uint64_t)h->dbl_round;
    st->dbl_won += (uint64_t)h->dbl_won;
    if (h->paid > st->biggest_win) st->biggest_win = h->paid;
    session_stats_changed();
    FILE *f = session_hand_log();
    if (f) draw_write_hand_log(g, f, (int64_t)time(NULL));
}

static void draw_mode_tick(AppCtx *ctx, const InputFrame *in)
{
    DrawGame *g = &D.g;
    InputFrame f = *in;
    uint32_t p = in->pressed;
    if (D.msg_ticks > 0 && --D.msg_ticks == 0) D.msg = NULL;

    if (g->state == DS_HOLD && (p & BTN_BET_ONE)) session_set_hint_on(!session_hint_on());

    if (p & BTN_CASH_OUT) {
        switch (g->state) {
        case DS_IDLE:
            app_request(ctx, APP_MENU);
            f.pressed &= ~BTN_CASH_OUT;
            break;
        case DS_OFFER:
        case DS_DOUBLE:
        case DS_DOUBLE_REVEAL:
            D.leave = 1;            /* the game collects on CASH OUT itself */
            break;
        default:
            say("FINISH THE HAND FIRST - THE BET IS IN PLAY");
            f.pressed &= ~BTN_CASH_OUT;
            break;
        }
    }
    if (D.leave && (g->state == DS_OFFER || g->state == DS_DOUBLE)) f.pressed |= BTN_CASH_OUT;

    int n0 = ctx->events.n;
    draw_tick(g, &f, &ctx->wallet, &ctx->events);
    for (int i = n0; i < ctx->events.n; i++) {
        const GameEvent *e = &ctx->events.e[i];
        if (e->type == EV_DRAW_HAND_START) D.hint_valid = 0;
        else if (e->type == EV_DRAW_HOLD_PHASE)
            D.hint_valid = draw_hint_compute(g->variant, g->bet, g->cards, &D.hint) == 0;
        else if (e->type == EV_DRAW_HAND_END) on_hand_end(g);
    }

    if (D.leave && g->state == DS_IDLE) {
        D.leave = 0;
        app_request(ctx, APP_MENU);
    }

    /* What the file must say if the power goes now (session.h): a hand in
       progress with nothing won yet is void (the bet goes back); a win on the
       meter is the player's. */
    int in_play = g->in_hand && g->meter == 0 &&
                  (g->state == DS_DEALING || g->state == DS_HOLD || g->state == DS_DRAWING);
    session_set_pending(PENDING_DRAW, in_play ? g->bet : g->meter);

    if (g->state == DS_IDLE && g->meter == 0 && session_idle_ticks() >= SESSION_IDLE_ATTRACT_TICKS)
        app_request(ctx, APP_ATTRACT);
}

static void fill(const AppCtx *ctx, DrawViewInfo *v)
{
    v->game = &D.g;
    v->credits = ctx->wallet.credits;
    v->denom_cents = session_settings()->denom_cents;
    v->hint_on = session_hint_on();
    v->hint = D.hint_valid ? &D.hint : NULL;
    v->demo = 0;
    v->message = D.msg;
}

static void draw_mode_present_update(const AppCtx *ctx, const GameEvent *ev, int nev, float dt)
{
    DrawViewInfo v;
    fill(ctx, &v);
    placeholder_common_update(ctx->state, ev, nev, dt);
    draw_placeholder_update(&v, ev, nev, dt);
}

static void draw_mode_present_draw(const AppCtx *ctx)
{
    DrawViewInfo v;
    fill(ctx, &v);
    draw_placeholder_view(&v, ctx->time);
}

const AppMode mode_draw = {
    .name = "draw",
    .enter = draw_enter,
    .tick = draw_mode_tick,
    .present_update = draw_mode_present_update,
    .present_draw = draw_mode_present_draw,
};
