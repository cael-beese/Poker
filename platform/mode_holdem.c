/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* mode_holdem.c - APP_HOLDEM: a No-Limit Hold'em sit-and-go against five AI
 * players (games/holdem + ai/ through ai_pool_hooks).
 *
 * The money (docs/SYSTEMS.md): the buy-in (service menu, default 100
 * credits) is debited when the player sits down. Six buy-ins make the prize
 * pool; 1st / 2nd / 3rd are paid 3.0x / 1.8x / 1.2x the buy-in
 * (sng_prize), credited the moment the player's place is decided: when they
 * bust, or when they win. Table chips are tournament chips, never credits.
 *
 * Flow and controls (arcade buttons only):
 *   LOBBY    DEAL / BET MAX / OK: buy in and sit down.  CASH OUT / BACK: menu.
 *   PLAYING  the table's controls (games/holdem/holdem.h): HOLD1 fold,
 *            HOLD2 check/call, HOLD3/4/5 1/2, 3/4, 1x pot, BET MAX all-in,
 *            BET ONE +1 big blind, DEAL bet/raise to the amount shown.
 *            CASH OUT while still in: asks "leave the table?"; CASH OUT again
 *            leaves, finishing in the place of the players still in (with
 *            three left you finish 3rd and are paid 3rd) - i.e. exactly as if
 *            you had been blinded out first; any other button stays.
 *            After busting: DEAL watches the AI play on, CASH OUT leaves.
 *   RESULT   DEAL: play again (new buy-in).  CASH OUT / BACK: menu.
 *   60 s idle in the lobby or the result screen: attract mode.
 * Leaving the program mid-game (ESC, COIN+START) applies the same leave rule;
 * a power cut mid-game voids it and the buy-in goes back (session.h). */
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "ai/ai.h"
#include "games/holdem/holdem.h"
#include "platform/app.h"
#include "platform/placeholder_view.h"
#include "platform/session.h"

static const char *const k_diff_name[AI_NDIFF] = { "EASY", "NORMAL", "HARD", "EXPERT" };
static const char *const k_ai_names[HOLDEM_SEATS] = { "YOU", "BUZZ", "HONEY", "STINGER", "DRONE", "QUEENIE" };

static struct {
    AppCtx    *ctx;
    HoldemGame g;
    int        phase;           /* HV_*                                     */
    int        running;         /* g is live (hooks may hold a decision)    */
    int64_t    buyin;           /* of the game being played                 */
    int        paid;            /* the player's place has been settled      */
    int        place;
    int64_t    won;
    int        seated;          /* the player is still in (not out / left)  */
    int        confirm, confirm_ticks;
    char       names[HOLDEM_SEATS][32];
    const char *msg;
    int        msg_ticks;
} H;

static void say(const char *m)
{
    H.msg = m;
    H.msg_ticks = 180;
}

static void settle(int place)
{
    if (H.paid) return;
    Stats *st = session_stats();
    H.paid = 1;
    H.seated = 0;
    H.place = place;
    H.won = sng_prize(H.buyin, place);
    wallet_credit(&H.ctx->wallet, H.won);
    st->holdem_games++;
    if (place == 1) st->holdem_wins++;
    if (place >= 1 && place <= SAVE_SEATS) st->holdem_places[place - 1]++;
    st->holdem_prizes += (uint64_t)H.won;
    session_stats_changed();
}

static void stop_game(void)
{
    if (H.running) holdem_release(&H.g);
    H.running = 0;
}

static void start_game(AppCtx *ctx)
{
    const Settings *s = session_settings();
    int64_t buyin = s->holdem_buyin;
    if (wallet_debit(&ctx->wallet, buyin) != 0) {
        say("NOT ENOUGH CREDITS FOR THE BUY-IN - INSERT COIN");
        return;
    }
    session_stats()->holdem_buyins += (uint64_t)buyin;
    session_stats_changed();

    HoldemConfig cfg;
    holdem_config_default(&cfg);
    Rng r;
    rng_seed(&r, session_next_seed());
    int pers[5];
    ai_assign_personalities(s->difficulty, &r, pers);
    cfg.personality[0] = AI_SHARK;          /* unused: seat 0 is the player */
    for (int i = 0; i < 5; i++) cfg.personality[i + 1] = pers[i];
    snprintf(H.names[0], sizeof H.names[0], "YOU");
    for (int i = 1; i < HOLDEM_SEATS; i++) {
        char pn[16];
        snprintf(pn, sizeof pn, "%s", ai_personality_name(cfg.personality[i]));
        for (char *c = pn; *c; c++) *c = (char)toupper((unsigned char)*c);
        snprintf(H.names[i], sizeof H.names[i], "%s (%s)", k_ai_names[i], pn);
    }
    AiPool *pool = session_ai_pool();
    HoldemAiHooks hooks = pool ? ai_pool_hooks(pool) : (HoldemAiHooks){ 0 };
    holdem_init(&H.g, &cfg, session_next_seed(), pool ? &hooks : NULL);
    H.running = 1;
    H.buyin = buyin;
    H.paid = 0;
    H.place = 0;
    H.won = 0;
    H.seated = 1;
    H.confirm = 0;
    H.phase = HV_PLAYING;
}

static void holdem_mode_init(AppCtx *ctx)
{
    H.ctx = ctx;
    H.phase = HV_LOBBY;
}

static void holdem_enter(AppCtx *ctx, AppState from)
{
    (void)ctx;
    if (from == APP_SERVICE) return;        /* carry on where we were */
    if (!H.running) H.phase = HV_LOBBY;
    H.msg = NULL;
}

static void leave_table(AppCtx *ctx)
{
    /* Finishing in the place of everyone still in: as if blinded out first. */
    if (H.seated) settle(H.g.players_left);
    stop_game();
    H.phase = HV_LOBBY;
    app_request(ctx, APP_MENU);
}

static void playing_tick(AppCtx *ctx, const InputFrame *in)
{
    HoldemGame *g = &H.g;
    InputFrame f = *in;
    uint32_t p = in->pressed;

    if (H.confirm) {
        f.pressed = 0;
        f.down = 0;
        f.slider = 0;
        if (p & BTN_CASH_OUT) { leave_table(ctx); return; }
        if ((p & ~(uint32_t)(BTN_COIN | BTN_DEBUG | BTN_SERVICE)) || ++H.confirm_ticks > 600) H.confirm = 0;
    } else if (H.seated && (p & BTN_CASH_OUT)) {
        H.confirm = 1;
        H.confirm_ticks = 0;
        f.pressed &= ~BTN_CASH_OUT;
    }

    int n0 = ctx->events.n;
    holdem_tick(g, &f, NULL, &ctx->events);
    for (int i = n0; i < ctx->events.n; i++) {
        const GameEvent *e = &ctx->events.e[i];
        switch (e->type) {
        case EV_HOLDEM_HAND_START:
            if (H.seated) { session_stats()->holdem_hands++; session_stats_changed(); }
            break;
        case EV_HOLDEM_HAND_LOG: {
            FILE *lf = session_hand_log();
            char line[512];
            if (lf && holdem_hand_log_line(g, (int64_t)time(NULL), line, sizeof line) > 0) {
                fputs(line, lf);
                fflush(lf);
            }
            break;
        }
        case EV_HOLDEM_HUMAN_OUT:
            settle(e->a);
            break;
        case EV_HOLDEM_GAME_OVER:
            if (e->a == 0) settle(1);
            stop_game();
            if (e->b == HOLDEM_OVER_HUMAN_LEFT) {
                H.phase = HV_LOBBY;
                app_request(ctx, APP_MENU);
            } else {
                H.phase = HV_RESULT;
            }
            break;
        default: break;
        }
    }
}

static void holdem_mode_tick(AppCtx *ctx, const InputFrame *in)
{
    uint32_t p = in->pressed;
    if (H.msg_ticks > 0 && --H.msg_ticks == 0) H.msg = NULL;

    switch (H.phase) {
    case HV_LOBBY:
    case HV_RESULT:
        if (p & (BTN_DEAL | BTN_BET_MAX | BTN_OK | BTN_START)) start_game(ctx);
        else if (p & (BTN_CASH_OUT | BTN_BACK)) { H.phase = HV_LOBBY; app_request(ctx, APP_MENU); }
        else if (session_idle_ticks() >= SESSION_IDLE_ATTRACT_TICKS) { H.phase = HV_LOBBY; app_request(ctx, APP_ATTRACT); }
        break;
    default:
        playing_tick(ctx, in);
        break;
    }
    /* A power cut while the player is still in voids the game: the buy-in
       is what the file keeps for them (session.h). */
    session_set_pending(PENDING_HOLDEM, H.running && H.seated ? H.buyin : 0);
}

static void holdem_mode_shutdown(void)
{
    /* Leaving the program mid-game: the same rule as leaving the table. */
    if (H.running && H.seated) settle(H.g.players_left);
    stop_game();
    session_set_pending(PENDING_HOLDEM, 0);
}

static void fill(const AppCtx *ctx, HoldemViewInfo *v)
{
    const Settings *s = session_settings();
    int64_t b = H.phase == HV_PLAYING ? H.buyin : (int64_t)s->holdem_buyin;
    memset(v, 0, sizeof *v);
    v->game = (H.phase == HV_LOBBY) ? NULL : &H.g;
    v->phase = H.phase;
    v->credits = ctx->wallet.credits;
    v->denom_cents = s->denom_cents;
    v->buyin = b;
    for (int p = 1; p <= 3; p++) v->prize[p] = sng_prize(b, p);
    v->difficulty = k_diff_name[s->difficulty < AI_NDIFF ? s->difficulty : 1];
    for (int i = 0; i < HOLDEM_SEATS; i++) v->seat_name[i] = H.names[i];
    v->place = H.place;
    v->won = H.won;
    v->confirm_leave = H.confirm;
    v->message = H.msg;
}

static void holdem_mode_present_update(const AppCtx *ctx, const GameEvent *ev, int nev, float dt)
{
    HoldemViewInfo v;
    fill(ctx, &v);
    placeholder_common_update(ctx->state, ev, nev, dt);
    holdem_placeholder_update(&v, ev, nev, dt);
}

static void holdem_mode_present_draw(const AppCtx *ctx)
{
    HoldemViewInfo v;
    fill(ctx, &v);
    holdem_placeholder_view(&v, ctx->time);
}

const AppMode mode_holdem = {
    .name = "holdem",
    .init = holdem_mode_init,
    .enter = holdem_enter,
    .tick = holdem_mode_tick,
    .present_update = holdem_mode_present_update,
    .present_draw = holdem_mode_present_draw,
    .shutdown = holdem_mode_shutdown,
};
