/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* The No-Limit Hold'em table: a tick-driven state machine over one flat
   struct. Each step of a hand (a post, a card, an action, a reveal, an
   award) happens on its own tick and is announced as an event; the betting
   rules live in legal_for() and apply_action(), the pot arithmetic in
   holdem_pots.c. See holdem.h for the rules decisions. */

#include "games/holdem/holdem.h"

#include "engine/eval.h"
#include "engine/replay.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* ---- configuration ------------------------------------------------------- */

static const HoldemLevel k_levels[] = {
    { 10, 20, 0 },      { 15, 30, 0 },      { 25, 50, 0 },      { 50, 100, 0 },
    { 75, 150, 0 },     { 100, 200, 25 },   { 150, 300, 40 },   { 200, 400, 50 },
    { 300, 600, 75 },   { 400, 800, 100 },  { 600, 1200, 150 }, { 800, 1600, 200 },
    { 1000, 2000, 250 },{ 1500, 3000, 400 },{ 2000, 4000, 500 },{ 3000, 6000, 750 },
};

void holdem_config_default(HoldemConfig *c)
{
    int i;
    memset(c, 0, sizeof *c);
    c->start_stack = 1500;
    c->hands_per_level = 10;
    c->nlevels = (int)(sizeof k_levels / sizeof k_levels[0]);
    for (i = 0; i < c->nlevels; i++) c->levels[i] = k_levels[i];
    c->antes = 1;
    c->first_button = -1;
    for (i = 0; i < HOLDEM_SEATS; i++) c->personality[i] = i % 4;
    c->t_hand_start = 90;
    c->t_post = 18;
    c->t_deal = 8;
    c->t_action = 30;
    c->t_collect = 24;
    c->t_board = 20;
    c->t_street = 60;
    c->t_show = 45;
    c->t_award = 60;
    c->human_show_ticks = 300;
    c->human_turn_ticks = 0;
}

void holdem_config_fast(HoldemConfig *c)
{
    holdem_config_default(c);
    c->t_hand_start = c->t_post = c->t_deal = c->t_action = c->t_collect = 1;
    c->t_board = c->t_street = c->t_show = c->t_award = 1;
    c->human_show_ticks = 30;
}

/* ---- small helpers ------------------------------------------------------- */

static void emit(EventQueue *q, int type, int a, int b, int64_t v)
{
    if (!q) return;
    if (v > INT32_MAX) v = INT32_MAX;
    if (v < INT32_MIN) v = INT32_MIN;
    ev_push(q, (uint16_t)type, a, b, (int)v);
}

static void wait_ticks(HoldemGame *g, int d) { g->timer = d < 1 ? 1 : d; }

static int live(const HoldemGame *g, int s) { return g->seat[s].in_hand && !g->seat[s].folded; }
static int can_act(const HoldemGame *g, int s) { return live(g, s) && !g->seat[s].allin; }
static int is_ai(const HoldemGame *g, int s) { return s != 0 || g->cfg.seat0_ai; }

static int count_live(const HoldemGame *g)
{
    int s, n = 0;
    for (s = 0; s < HOLDEM_SEATS; s++) n += live(g, s);
    return n;
}

static int count_can_act(const HoldemGame *g)
{
    int s, n = 0;
    for (s = 0; s < HOLDEM_SEATS; s++) n += can_act(g, s);
    return n;
}

static int others_can_act(const HoldemGame *g, int me)
{
    int s;
    for (s = 0; s < HOLDEM_SEATS; s++)
        if (s != me && can_act(g, s)) return 1;
    return 0;
}

static int next_in_game(const HoldemGame *g, int from)
{
    int i;
    for (i = 1; i <= HOLDEM_SEATS; i++) {
        int s = (from + i) % HOLDEM_SEATS;
        if (g->seat[s].in_game) return s;
    }
    return -1;
}

static int next_live(const HoldemGame *g, int from)
{
    int i;
    for (i = 1; i <= HOLDEM_SEATS; i++) {
        int s = (from + i) % HOLDEM_SEATS;
        if (live(g, s)) return s;
    }
    return -1;
}

HoldemLevel holdem_level(const HoldemGame *g)
{
    HoldemLevel l = g->cfg.levels[g->level];
    if (!g->cfg.antes) l.ante = 0;
    return l;
}

int64_t holdem_pot_total(const HoldemGame *g)
{
    int64_t t = g->pot;
    int s;
    for (s = 0; s < HOLDEM_SEATS; s++) t += g->seat[s].bet;
    return t;
}

int holdem_seat_live(const HoldemGame *g, int seat)
{
    return seat >= 0 && seat < HOLDEM_SEATS && live(g, seat);
}

int holdem_is_human_turn(const HoldemGame *g)
{
    return g->phase == HP_TURN && !is_ai(g, g->to_act);
}

static void hist_push(HoldemGame *g, int seat, int act)
{
    /* 64 entries hold any realistic hand; past that the history stops
       growing rather than wrapping, so it never lies about the order. */
    if (g->nhist < HOLDEM_HIST_MAX) g->history[g->nhist++] = AI_HIST(g->street, seat, act);
}

/* Moves chips from the stack into the seat's bet so the bet totals `to`
   (or as much as the stack allows). */
static void put_to(HoldemGame *g, int s, int64_t to)
{
    HoldemSeat *p = &g->seat[s];
    int64_t add = to - p->bet;
    if (add > p->stack) add = p->stack;
    if (add < 0) add = 0;
    p->stack -= add;
    p->bet += add;
    p->committed += add;
    if (p->stack == 0) p->allin = 1;
}

/* ---- the betting rules --------------------------------------------------- */

/* The bet seat s has to match. Normally the street's bet (which pre-flop is
   the full big blind even if the big blind is short). When nobody else in
   the hand could still act, calling more than anyone actually put in would
   only be handed back, so the level is what the others really bet. */
static int64_t level_for(const HoldemGame *g, int s)
{
    int64_t m = 0;
    int t;
    if (others_can_act(g, s)) return g->cur_bet;
    for (t = 0; t < HOLDEM_SEATS; t++)
        if (t != s && g->seat[t].bet > m) m = g->seat[t].bet;
    return m;
}

static int needs_to_act(const HoldemGame *g, int s)
{
    const HoldemSeat *p = &g->seat[s];
    if (!can_act(g, s)) return 0;
    if (!others_can_act(g, s)) return p->bet < level_for(g, s);
    return !p->acted || p->bet < g->cur_bet;
}

static int next_needing(const HoldemGame *g, int from)
{
    int i;
    for (i = 1; i <= HOLDEM_SEATS; i++) {
        int s = (from + i) % HOLDEM_SEATS;
        if (needs_to_act(g, s)) return s;
    }
    return -1;
}

static void legal_for(const HoldemGame *g, int s, HoldemLegal *L)
{
    const HoldemSeat *p;
    int64_t max_to, lvl, call_to;

    memset(L, 0, sizeof *L);
    L->seat = -1;
    if (s < 0 || s >= HOLDEM_SEATS || !can_act(g, s)) return;
    p = &g->seat[s];
    L->seat = s;
    max_to = p->bet + p->stack;
    lvl = level_for(g, s);
    call_to = lvl < max_to ? lvl : max_to;
    if (call_to < p->bet) call_to = p->bet;
    L->call_to = call_to;
    L->to_call = call_to - p->bet;
    L->can_check = L->to_call == 0;
    L->can_call = L->to_call > 0;
    L->can_fold = L->to_call > 0;

    /* Raising: someone must be able to answer, the seat must have chips
       beyond a call, and the action must be open to it - it has not acted
       since the last full raise, or what it now faces adds up to one. */
    if (others_can_act(g, s) && max_to > g->cur_bet &&
        (!p->acted || g->cur_bet - p->bet >= g->last_raise)) {
        int64_t min_to = g->cur_bet + g->last_raise;
        if (min_to > max_to) min_to = max_to;      /* only all-in is left */
        L->min_to = min_to;
        L->max_to = max_to;
        if (g->cur_bet == 0) L->can_bet = 1;
        else L->can_raise = 1;
    }
}

void holdem_legal(const HoldemGame *g, HoldemLegal *out)
{
    if (g->phase != HP_TURN) {
        memset(out, 0, sizeof *out);
        out->seat = -1;
        return;
    }
    legal_for(g, g->to_act, out);
}

static int64_t snap_amount(const HoldemGame *g, const HoldemLegal *L, int64_t x)
{
    int64_t unit = holdem_level(g).sb;
    if (unit < 1) unit = 1;
    if (x < 0) x = 0;
    x = (x + unit / 2) / unit * unit;
    if (x < L->min_to) x = L->min_to;
    if (x > L->max_to || L->max_to - x < unit) x = L->max_to;
    return x;
}

int64_t holdem_quick_bet(const HoldemGame *g, int frac_pct)
{
    HoldemLegal L;
    int64_t owed, to;
    holdem_legal(g, &L);
    if (!(L.can_bet || L.can_raise)) return 0;
    owed = g->cur_bet - g->seat[L.seat].bet;
    if (owed < 0) owed = 0;
    /* A fraction of the pot as it would be after calling, on top of the
       call: the usual meaning of a "pot-sized raise". */
    to = g->cur_bet + (holdem_pot_total(g) + owed) * frac_pct / 100;
    return snap_amount(g, &L, to);
}

/* Validates and applies an action for the seat to act. Anything illegal is
   turned into the nearest legal action and never trusted: a fold or check
   when checking is free is a check, a check facing a bet is a fold, a call
   with nothing to call is a check, a raise when raising is closed is a
   call, a raise amount is clamped to [min, all-in], and an all-in when
   raising is closed is a call. */
static void apply_action(HoldemGame *g, int s, int act, int64_t amount, EventQueue *out)
{
    HoldemLegal L;
    HoldemSeat *p = &g->seat[s];
    int kind, code;
    int64_t to = 0;
    int raise_ok;

    legal_for(g, s, &L);
    raise_ok = L.can_bet || L.can_raise;
    switch (act) {
    case ACT_FOLD:  kind = L.can_check ? ACT_CHECK : ACT_FOLD; break;
    case ACT_CHECK: kind = L.can_check ? ACT_CHECK : ACT_FOLD; break;
    case ACT_CALL:  kind = L.can_check ? ACT_CHECK : ACT_CALL; break;
    case ACT_BET:
    case ACT_RAISE:
        if (!raise_ok || amount <= L.call_to) {
            kind = L.can_check ? ACT_CHECK : ACT_CALL;
        } else {
            kind = ACT_RAISE;
            to = amount < L.min_to ? L.min_to : amount > L.max_to ? L.max_to : amount;
        }
        break;
    case ACT_ALLIN:
        if (raise_ok) { kind = ACT_RAISE; to = L.max_to; }
        else kind = L.can_check ? ACT_CHECK : ACT_CALL;
        break;
    default:
        kind = L.can_check ? ACT_CHECK : ACT_FOLD;
        break;
    }

    switch (kind) {
    case ACT_FOLD:
        p->folded = 1;
        p->acted = 1;
        code = ACT_FOLD;
        break;
    case ACT_CHECK:
        p->acted = 1;
        code = ACT_CHECK;
        break;
    case ACT_CALL:
        put_to(g, s, L.call_to);
        p->acted = 1;
        /* An all-in that only calls is a call: the AI reads ACT_ALLIN as
           aggression (CONTRACT section 5). allin[] still shows it. */
        code = ACT_CALL;
        break;
    default: {
        int64_t prev = g->cur_bet, inc = to - prev;
        int t;
        put_to(g, s, to);
        if (inc >= g->last_raise) {
            /* A full raise reopens the betting for everyone else. */
            g->last_raise = inc;
            for (t = 0; t < HOLDEM_SEATS; t++)
                if (t != s) g->seat[t].acted = 0;
        }
        g->cur_bet = to;
        p->acted = 1;
        g->last_aggr = s;
        if (g->street == 3) g->river_aggr = s;
        code = p->allin ? ACT_ALLIN : (prev == 0 ? ACT_BET : ACT_RAISE);
        break;
    }
    }

    hist_push(g, s, code);
    g->nactions++;
    emit(out, EV_HOLDEM_ACTION, s, code, p->bet);
    g->phase = HP_AFTER_ACTION;
    wait_ticks(g, g->cfg.t_action);
}

/* ---- the AI seats -------------------------------------------------------- */

void holdem_build_view(const HoldemGame *g, int me, AiView *v)
{
    HoldemLegal L;
    int64_t maxbet = 0;
    int s, i;

    memset(v, 0, sizeof *v);
    v->seats = HOLDEM_SEATS;
    v->me = me;
    v->button = g->button;
    v->street = g->street;
    v->hole[0] = v->hole[1] = CARD_NONE;
    for (i = 0; i < 5; i++) v->board[i] = CARD_NONE;
    if (me < 0 || me >= HOLDEM_SEATS) return;
    /* The seat's own cards, and nothing hidden from anyone else. */
    v->hole[0] = g->seat[me].hole[0];
    v->hole[1] = g->seat[me].hole[1];
    for (i = 0; i < g->nboard && i < 5; i++) v->board[i] = g->board[i];
    v->nboard = g->nboard;
    /* Field meanings are fixed by CONTRACT section 5: stack behind, bet this
       street, pot from completed streets only (antes included), to_call =
       max(bet) - bet[me] uncapped, min_raise a raise-to total (0 when this
       seat may not raise), active = in the hand and able to act, allin =
       in the hand and all-in. */
    for (s = 0; s < HOLDEM_SEATS; s++) {
        v->stack[s] = g->seat[s].stack;
        v->bet[s] = g->seat[s].bet;
        v->active[s] = (uint8_t)can_act(g, s);
        v->allin[s] = (uint8_t)(live(g, s) && g->seat[s].allin);
        if (v->bet[s] > maxbet) maxbet = v->bet[s];
    }
    v->pot = g->pot;
    v->to_call = maxbet - v->bet[me];
    legal_for(g, me, &L);
    if (L.seat == me) v->min_raise = (L.can_bet || L.can_raise) ? L.min_to : 0;
    v->big_blind = holdem_level(g).bb;
    memcpy(v->history, g->history, sizeof v->history);
    v->nhist = g->nhist;
    v->personality = g->cfg.personality[me];
}

static uint64_t ai_seed(const HoldemGame *g)
{
    uint64_t st = g->hand_seed ^ (0x9E3779B97F4A7C15ull * (uint64_t)(g->nactions + 1));
    return rng_splitmix64(&st);
}

static int hooks_ok(const HoldemGame *g) { return g->hooks.begin && g->hooks.collect; }

static void ai_begin(HoldemGame *g, int s)
{
    AiView v;
    holdem_build_view(g, s, &v);
    if (hooks_ok(g)) g->hooks.begin(g->hooks.ctx, s, &v, ai_seed(g));
    g->ai_begun = 1;
}

static void ai_collect(HoldemGame *g, int s, AiDecision *d)
{
    memset(d, 0, sizeof *d);
    if (hooks_ok(g)) {
        g->hooks.collect(g->hooks.ctx, s, d);
    } else {
        /* No AI wired: a passive check/call player keeps the table going. */
        d->action = ACT_CALL;
        d->think_ticks = HOLDEM_AI_MIN_THINK;
    }
}

void holdem_set_hooks(HoldemGame *g, const HoldemAiHooks *hooks)
{
    if (hooks) g->hooks = *hooks;
    else memset(&g->hooks, 0, sizeof g->hooks);
    /* A restored game may be mid-think with a decision the new hooks never
       started; begin again (same view, same seed, same decision). */
    if (g->phase == HP_TURN && g->ai_begun && !g->ai_collected) g->ai_begun = 0;
}

void holdem_release(HoldemGame *g)
{
    if (g->phase == HP_TURN && g->ai_begun && !g->ai_collected && hooks_ok(g)) {
        AiDecision d;
        g->hooks.collect(g->hooks.ctx, g->to_act, &d);
        g->ai_dec = d;
        g->ai_collected = 1;
        g->ai_think = HOLDEM_AI_MIN_THINK;
    }
}

/* ---- hand flow ----------------------------------------------------------- */

static void begin_turn(HoldemGame *g, int s, EventQueue *out)
{
    HoldemLegal L;
    g->phase = HP_TURN;
    g->to_act = s;
    g->turn_ticks = 0;
    g->ai_begun = g->ai_collected = 0;
    g->ai_think = 0;
    memset(&g->ai_dec, 0, sizeof g->ai_dec);
    legal_for(g, s, &L);
    emit(out, EV_HOLDEM_TURN, s, !is_ai(g, s), L.to_call);
    if (!is_ai(g, s)) {
        g->sel_amount = (L.can_bet || L.can_raise) ? L.min_to : 0;
        if (g->sel_amount) emit(out, EV_HOLDEM_AMOUNT, s, 0, g->sel_amount);
    } else {
        ai_begin(g, s);
    }
}

static void end_round(HoldemGame *g)
{
    g->to_act = -1;
    g->phase = HP_COLLECT;
    wait_ticks(g, g->cfg.t_collect);
}

static void start_round(HoldemGame *g, EventQueue *out)
{
    int s, first;
    if (g->street > 0) {
        g->cur_bet = 0;
        g->last_raise = holdem_level(g).bb;
        g->last_aggr = -1;
        for (s = 0; s < HOLDEM_SEATS; s++) g->seat[s].acted = 0;
    }
    first = next_needing(g, g->street == 0 ? g->bb_seat : g->button);
    if (first < 0) { end_round(g); return; }
    emit(out, EV_HOLDEM_STREET, g->street, 0, 0);
    begin_turn(g, first, out);
}

static void start_hand(HoldemGame *g, EventQueue *out)
{
    const HoldemConfig *c = &g->cfg;
    HoldemLevel lv;
    Rng hr;
    int s, lvl, i, n;

    g->hand_no++;
    lvl = (g->hand_no - 1) / c->hands_per_level;
    if (lvl >= c->nlevels) lvl = c->nlevels - 1;
    if (lvl != g->level) {
        g->level = lvl;
        emit(out, EV_HOLDEM_LEVEL_UP, lvl, 0, holdem_level(g).bb);
    }
    lv = holdem_level(g);

    /* One seed per hand from the session stream; the deck comes only from
       Fisher-Yates on an Rng seeded with it, so the logged seed replays
       the hand's cards exactly. */
    g->hand_seed = rng_next(&g->rng);
    rng_seed(&hr, g->hand_seed);
    deck_init(&g->deck);
    deck_shuffle(&g->deck, &hr);

    if (g->hand_no > 1 || !g->seat[g->button].in_game) g->button = next_in_game(g, g->button);

    for (s = 0; s < HOLDEM_SEATS; s++) {
        HoldemSeat *p = &g->seat[s];
        p->bet = p->committed = 0;
        p->hole[0] = p->hole[1] = CARD_NONE;
        p->rank = 0;
        p->folded = p->allin = p->acted = p->shown = p->mucked = 0;
        p->in_hand = p->in_game;
        p->hand_start = p->stack;
        g->won[s] = 0;
    }
    for (i = 0; i < 5; i++) g->board[i] = CARD_NONE;
    g->nboard = 0;
    g->street = 0;
    g->pot = 0;
    g->cur_bet = 0;
    g->last_raise = lv.bb;
    g->last_aggr = g->river_aggr = -1;
    g->nactions = 0;
    g->nhist = 0;
    g->runout = 0;
    g->npots = 0;
    g->to_act = -1;

    if (g->players_left == 2) {
        g->sb_seat = g->button;
        g->bb_seat = next_in_game(g, g->button);
    } else {
        g->sb_seat = next_in_game(g, g->button);
        g->bb_seat = next_in_game(g, g->sb_seat);
    }
    n = 0;
    for (i = 1; i <= HOLDEM_SEATS; i++) {
        s = (g->button + i) % HOLDEM_SEATS;
        if (g->seat[s].in_hand) g->deal_order[n++] = s;
    }
    g->ndeal = n;
    g->deal_idx = 0;

    emit(out, EV_HOLDEM_HAND_START, g->hand_no, g->button, g->level);
    g->phase = lv.ante > 0 ? HP_ANTES : HP_POST_SB;
    wait_ticks(g, c->t_post);
}

static void post_antes(HoldemGame *g, EventQueue *out)
{
    int64_t ante = holdem_level(g).ante;
    int i;
    for (i = 0; i < g->ndeal; i++) {
        int s = g->deal_order[i];
        HoldemSeat *p = &g->seat[s];
        int64_t a = ante < p->stack ? ante : p->stack;
        /* Antes are dead money: straight into the middle, not a bet. */
        p->stack -= a;
        p->committed += a;
        g->pot += a;
        if (p->stack == 0) p->allin = 1;
        emit(out, EV_HOLDEM_ANTE, s, p->allin, a);
    }
    g->phase = HP_POST_SB;
    wait_ticks(g, g->cfg.t_post);
}

static void post_blind(HoldemGame *g, int s, int64_t blind, int act, int evtype, EventQueue *out)
{
    HoldemSeat *p = &g->seat[s];
    int64_t before = p->bet;
    if (p->stack == 0) return;              /* all-in from the ante already */
    put_to(g, s, blind);
    if (p->bet > g->cur_bet) g->cur_bet = p->bet;
    hist_push(g, s, act);
    emit(out, evtype, s, p->allin, p->bet - before);
}

static void deal_one(HoldemGame *g, EventQueue *out)
{
    int s = g->deal_order[g->deal_idx % g->ndeal];
    int slot = g->deal_idx / g->ndeal;
    Card c = deck_draw(&g->deck);
    g->seat[s].hole[slot] = c;
    /* Face down for everyone but seat 0, so the obvious way to draw the
       deal never shows an opponent's card. */
    emit(out, EV_HOLDEM_DEAL_HOLE, s, slot, s == 0 ? (int)c : -1);
    g->deal_idx++;
    if (g->deal_idx >= 2 * g->ndeal) start_round(g, out);
    else wait_ticks(g, g->cfg.t_deal);
}

static void begin_board(HoldemGame *g, int delay)
{
    g->board_target = g->street == 0 ? 3 : g->nboard + 1;
    g->phase = HP_BOARD;
    wait_ticks(g, delay);
}

static void set_show_order(HoldemGame *g, int first)
{
    int i, n = 0;
    for (i = 0; i < HOLDEM_SEATS; i++) {
        int s = (first + i) % HOLDEM_SEATS;
        if (live(g, s)) g->show_order[n++] = s;
    }
    g->nshow = n;
    g->show_idx = 0;
}

static void rebuild_pots(HoldemGame *g)
{
    int64_t com[HOLDEM_SEATS];
    uint8_t lv[HOLDEM_SEATS];
    int s;
    for (s = 0; s < HOLDEM_SEATS; s++) {
        com[s] = g->seat[s].committed;
        lv[s] = (uint8_t)live(g, s);
    }
    g->npots = holdem_build_pots(com, lv, g->pots);
}

static void show_seat(HoldemGame *g, int s, int reason, EventQueue *out)
{
    HoldemSeat *p = &g->seat[s];
    int64_t v = (int64_t)p->hole[0] | ((int64_t)p->hole[1] << 8) | ((int64_t)p->rank << 16);
    p->shown = 1;
    emit(out, EV_HOLDEM_SHOW, s, reason, v);
}

static void muck_seat(HoldemGame *g, int s, int uncontested, EventQueue *out)
{
    g->seat[s].mucked = 1;
    emit(out, EV_HOLDEM_MUCK, s, uncontested, 0);
}

static void begin_showdown(HoldemGame *g, EventQueue *out)
{
    int s, first;
    for (s = 0; s < HOLDEM_SEATS; s++) {
        HoldemSeat *p = &g->seat[s];
        if (!live(g, s)) continue;
        {
            Card c7[7];
            c7[0] = p->hole[0];
            c7[1] = p->hole[1];
            memcpy(c7 + 2, g->board, 5);
            p->rank = eval7(c7);
        }
        if (p->shown) emit(out, EV_HOLDEM_HAND_RANK, s, 0, p->rank);
    }
    rebuild_pots(g);
    /* The last river aggressor shows first; with no river bet the first
       live seat left of the button does. */
    if (g->river_aggr >= 0 && live(g, g->river_aggr)) first = g->river_aggr;
    else first = next_live(g, g->button);
    set_show_order(g, first);
    g->phase = HP_SHOWDOWN;
    wait_ticks(g, g->cfg.t_show);
}

static void begin_award(HoldemGame *g)
{
    rebuild_pots(g);
    g->award_idx = 0;
    g->phase = HP_AWARD;
    wait_ticks(g, g->cfg.t_award);
}

static void collect_bets(HoldemGame *g, EventQueue *out)
{
    int s, top = -1, i;
    int64_t b1 = 0, b2 = 0;

    for (s = 0; s < HOLDEM_SEATS; s++) {
        int64_t b = g->seat[s].bet;
        if (b > b1) { b2 = b1; b1 = b; top = s; }
        else if (b > b2) b2 = b;
    }
    if (top >= 0 && b1 > b2) {
        /* Nobody matched the top bet: the unmatched part goes back. */
        HoldemSeat *p = &g->seat[top];
        int64_t r = b1 - b2;
        p->bet -= r;
        p->committed -= r;
        p->stack += r;
        if (p->stack > 0) p->allin = 0;
        emit(out, EV_HOLDEM_UNCALLED, top, 0, r);
    }
    for (s = 0; s < HOLDEM_SEATS; s++) {
        g->pot += g->seat[s].bet;
        g->seat[s].bet = 0;
    }
    g->cur_bet = 0;
    emit(out, EV_HOLDEM_BETS_TO_POT, g->street, 0, g->pot);
    rebuild_pots(g);
    for (i = 0; i < g->npots; i++) emit(out, EV_HOLDEM_POT, i, g->pots[i].eligible, g->pots[i].amount);
}

static void step_collect(HoldemGame *g, EventQueue *out)
{
    collect_bets(g, out);
    if (count_live(g) <= 1) {
        g->phase = HP_UNCONTESTED;
        wait_ticks(g, g->cfg.t_award);
    } else if (g->street == 3) {
        begin_showdown(g, out);
    } else if (count_can_act(g) <= 1) {
        /* Nobody can bet any more: table the hands and run the board out. */
        g->runout = 1;
        emit(out, EV_HOLDEM_RUNOUT, g->street, 0, 0);
        set_show_order(g, next_live(g, g->button));
        g->phase = HP_TABLE;
        wait_ticks(g, g->cfg.t_show);
    } else {
        begin_board(g, g->cfg.t_board);
    }
}

static void step_table(HoldemGame *g, EventQueue *out)
{
    show_seat(g, g->show_order[g->show_idx++], HOLDEM_SHOW_ALLIN, out);
    if (g->show_idx >= g->nshow) begin_board(g, g->cfg.t_street);
    else wait_ticks(g, g->cfg.t_show);
}

static void step_board(HoldemGame *g, EventQueue *out)
{
    Card c;
    if (g->nboard == 0 || g->nboard == 3 || g->nboard == 4) (void)deck_draw(&g->deck);   /* burn */
    c = deck_draw(&g->deck);
    g->board[g->nboard++] = c;
    emit(out, EV_HOLDEM_BOARD, g->nboard - 1, g->street + 1, c);
    if (g->nboard < g->board_target) { wait_ticks(g, g->cfg.t_board); return; }
    g->street++;
    if (g->runout) {
        if (g->street == 3) begin_showdown(g, out);
        else begin_board(g, g->cfg.t_street);
        return;
    }
    start_round(g, out);
}

/* Must seat s show? Yes if its hand wins or ties some pot it is in against
   every hand shown so far in that pot (a pot nobody has shown for counts).
   Pots it alone can win need no showing. */
static int must_show(const HoldemGame *g, int s)
{
    int i, t;
    for (i = 0; i < g->npots; i++) {
        uint8_t e = g->pots[i].eligible;
        int best = 0;
        if (!(e & (1u << s))) continue;
        if ((e & (uint8_t)~(1u << s)) == 0) continue;
        for (t = 0; t < HOLDEM_SEATS; t++)
            if ((e & (1u << t)) && g->seat[t].shown && (best == 0 || g->seat[t].rank < best))
                best = g->seat[t].rank;
        if (best == 0 || g->seat[s].rank <= best) return 1;
    }
    return 0;
}

static void open_prompt(HoldemGame *g, int s, int uncontested, EventQueue *out)
{
    g->prompt = 1;
    g->prompt_ticks = 0;
    emit(out, EV_HOLDEM_SHOW_PROMPT, s, uncontested, 0);
}

static void step_showdown(HoldemGame *g, EventQueue *out)
{
    int s;
    while (g->show_idx < g->nshow && g->seat[g->show_order[g->show_idx]].shown) g->show_idx++;
    if (g->show_idx >= g->nshow) { begin_award(g); return; }
    s = g->show_order[g->show_idx];
    if (must_show(g, s)) {
        show_seat(g, s, HOLDEM_SHOW_SHOWDOWN, out);
    } else if (is_ai(g, s)) {
        muck_seat(g, s, 0, out);
    } else {
        open_prompt(g, s, 0, out);
        return;
    }
    g->show_idx++;
    wait_ticks(g, g->cfg.t_show);
}

static void step_uncontested(HoldemGame *g, EventQueue *out)
{
    int s = next_live(g, g->button);
    if (s >= 0 && !is_ai(g, s)) { open_prompt(g, s, 1, out); return; }
    if (s >= 0) muck_seat(g, s, 1, out);
    begin_award(g);
}

static void prompt_tick(HoldemGame *g, const InputFrame *in, EventQueue *out)
{
    int show = -1;
    int s = g->phase == HP_SHOWDOWN ? g->show_order[g->show_idx] : next_live(g, g->button);
    g->prompt_ticks++;
    if (in->pressed & (BTN_DEAL | BTN_OK | BTN_HOLD2)) show = 1;
    else if (in->pressed & (BTN_HOLD1 | BTN_BACK)) show = 0;
    else if (g->prompt_ticks >= g->cfg.human_show_ticks) show = 0;
    if (show < 0) return;
    g->prompt = 0;
    if (g->phase == HP_SHOWDOWN) {
        if (show) show_seat(g, s, HOLDEM_SHOW_SHOWDOWN, out);
        else muck_seat(g, s, 0, out);
        g->show_idx++;
        wait_ticks(g, g->cfg.t_show);
    } else {
        if (show) show_seat(g, s, HOLDEM_SHOW_VOLUNTARY, out);
        else muck_seat(g, s, 1, out);
        begin_award(g);
    }
}

static void step_award(HoldemGame *g, EventQueue *out)
{
    const HoldemPot *p = &g->pots[g->award_idx];
    int64_t share[HOLDEM_SEATS] = { 0 };
    uint8_t win = 0, e = p->eligible;
    int s, best = 0;

    if ((e & (e - 1)) == 0) {
        win = e;                                   /* one contender */
    } else {
        for (s = 0; s < HOLDEM_SEATS; s++)
            if ((e & (1u << s)) && !g->seat[s].mucked && g->seat[s].rank > 0 &&
                (best == 0 || g->seat[s].rank < best))
                best = g->seat[s].rank;
        for (s = 0; s < HOLDEM_SEATS; s++)
            if ((e & (1u << s)) && !g->seat[s].mucked && g->seat[s].rank == best)
                win |= (uint8_t)(1u << s);
        /* The show rules guarantee a shown hand in every contested pot;
           this keeps the chips if that were ever broken. */
        if (!win) win = e;
    }
    holdem_split_pot(p->amount, win, g->button, share);
    for (s = 0; s < HOLDEM_SEATS; s++) {
        if (!share[s]) continue;
        g->seat[s].stack += share[s];
        g->won[s] += share[s];
        g->pot -= share[s];
        emit(out, EV_HOLDEM_AWARD, s, g->award_idx, share[s]);
    }
    g->award_idx++;
    if (g->award_idx >= g->npots) g->phase = HP_HAND_END;
    wait_ticks(g, g->cfg.t_award);
}

static void write_log(HoldemGame *g, const int *busted, int nb)
{
    HoldemLevel lv = holdem_level(g);
    char *o = g->log_result;
    size_t cap = sizeof g->log_result, n = 0;
    int64_t total = 0;
    int s, i;

#define LOGF(...) do { \
        if (n < cap) { int k_ = snprintf(o + n, cap - n, __VA_ARGS__); if (k_ > 0) n += (size_t)k_; } \
    } while (0)

    for (s = 0; s < HOLDEM_SEATS; s++) total += g->seat[s].committed;
    LOGF("hand=%d lvl=%d %d/%d/%d btn=%d board=", g->hand_no, g->level, (int)lv.sb, (int)lv.bb,
         (int)lv.ante, g->button);
    if (g->nboard == 0) LOGF("-");
    for (i = 0; i < g->nboard; i++) {
        char cs[3];
        LOGF("%s", card_str(g->board[i], cs));
    }
    LOGF(" pot=%" PRId64 " net", total);
    for (s = 0; s < HOLDEM_SEATS; s++) {
        int64_t d = g->seat[s].stack - g->seat[s].hand_start;
        if (g->seat[s].in_hand && d) LOGF(" s%d%+" PRId64, s, d);
    }
    for (i = 0; i < nb; i++) LOGF(" out=s%d#%d", busted[i], g->seat[busted[i]].place);
#undef LOGF
    g->log_seed = g->hand_seed;
}

static void step_hand_end(HoldemGame *g, EventQueue *out)
{
    int busted[HOLDEM_SEATS], nb = 0, i, j, s;
    int human_out = 0;

    for (i = 1; i <= HOLDEM_SEATS; i++) {
        s = (g->button + i) % HOLDEM_SEATS;
        if (g->seat[s].in_game && g->seat[s].stack == 0) busted[nb++] = s;
    }
    /* Best place first: a bigger stack at the start of the hand finishes
       higher; equal stacks keep seat order from the button (stable sort). */
    for (i = 1; i < nb; i++) {
        int x = busted[i];
        for (j = i; j > 0 && g->seat[busted[j - 1]].hand_start < g->seat[x].hand_start; j--)
            busted[j] = busted[j - 1];
        busted[j] = x;
    }
    for (i = 0; i < nb; i++) {
        s = busted[i];
        g->seat[s].place = g->players_left - nb + 1 + i;
        g->seat[s].in_game = 0;
        if (s == 0 && !g->cfg.seat0_ai) human_out = 1;
    }
    for (i = nb - 1; i >= 0; i--) emit(out, EV_HOLDEM_ELIMINATED, busted[i], g->seat[busted[i]].place, 0);
    g->players_left -= nb;

    write_log(g, busted, nb);
    emit(out, EV_HOLDEM_HAND_END, g->hand_no, 0, 0);
    emit(out, EV_HOLDEM_HAND_LOG, g->hand_no, 0, 0);

    if (g->players_left <= 1) {
        g->winner = next_in_game(g, HOLDEM_SEATS - 1);
        if (g->winner >= 0) g->seat[g->winner].place = 1;
        g->over_reason = HOLDEM_OVER_WINNER;
        g->phase = HP_GAME_OVER;
        emit(out, EV_HOLDEM_GAME_OVER, g->winner, HOLDEM_OVER_WINNER, 0);
        return;
    }
    if (human_out && !g->human_watching) {
        g->phase = HP_BUSTED;
        emit(out, EV_HOLDEM_HUMAN_OUT, g->seat[0].place, 0, 0);
        return;
    }
    g->phase = HP_HAND_START;
    wait_ticks(g, g->cfg.t_hand_start);
}

static void step_after_action(HoldemGame *g, EventQueue *out)
{
    int next;
    if (count_live(g) <= 1) { end_round(g); return; }
    next = next_needing(g, g->to_act);
    if (next < 0) end_round(g);
    else begin_turn(g, next, out);
}

/* ---- per-tick input handling --------------------------------------------- */

static void human_turn(HoldemGame *g, const InputFrame *in, EventQueue *out)
{
    HoldemLegal L;
    uint32_t pr = in->pressed;
    int s = g->to_act;
    int raise_ok, nact = 0;

    legal_for(g, s, &L);
    raise_ok = L.can_bet || L.can_raise;
    if (raise_ok) {
        int64_t sel = g->sel_amount, step = holdem_level(g).bb;
        if (pr & BTN_HOLD3) sel = holdem_quick_bet(g, 50);
        if (pr & BTN_HOLD4) sel = holdem_quick_bet(g, 75);
        if (pr & BTN_HOLD5) sel = holdem_quick_bet(g, 100);
        if (pr & BTN_BET_MAX) sel = L.max_to;
        if (pr & (BTN_BET_ONE | BTN_UP | BTN_RIGHT)) sel += step;
        if (pr & (BTN_DOWN | BTN_LEFT)) sel -= step;
        if (in->slider != g->last_slider)
            sel = L.min_to + ((int64_t)in->slider + 32767) * (L.max_to - L.min_to) / 65534;
        sel = snap_amount(g, &L, sel);
        if (sel != g->sel_amount) {
            g->sel_amount = sel;
            emit(out, EV_HOLDEM_AMOUNT, s, 0, sel);
        }
    }

    /* Two action buttons on the same tick are ambiguous; do neither. */
    if (pr & BTN_HOLD1) nact++;
    if (pr & BTN_HOLD2) nact++;
    if (pr & (BTN_DEAL | BTN_OK)) nact++;
    if (nact > 1) return;
    if ((pr & BTN_HOLD1) && L.can_fold) apply_action(g, s, ACT_FOLD, 0, out);
    else if (pr & BTN_HOLD2) apply_action(g, s, ACT_CALL, 0, out);
    else if ((pr & (BTN_DEAL | BTN_OK)) && raise_ok) apply_action(g, s, ACT_RAISE, g->sel_amount, out);
    else if (g->cfg.human_turn_ticks > 0 && g->turn_ticks >= g->cfg.human_turn_ticks)
        apply_action(g, s, ACT_CHECK, 0, out);   /* check, or fold facing a bet */
}

static void turn_tick(HoldemGame *g, const InputFrame *in, EventQueue *out)
{
    int s = g->to_act;
    g->turn_ticks++;
    if (!is_ai(g, s)) { human_turn(g, in, out); return; }
    if (!g->ai_begun) ai_begin(g, s);
    if (!g->ai_collected && g->turn_ticks >= HOLDEM_AI_MIN_THINK) {
        AiDecision d;
        float t;
        ai_collect(g, s, &d);
        g->ai_dec = d;
        g->ai_collected = 1;
        g->ai_think = d.think_ticks < HOLDEM_AI_MIN_THINK ? HOLDEM_AI_MIN_THINK
                    : d.think_ticks > HOLDEM_AI_MAX_THINK ? HOLDEM_AI_MAX_THINK : d.think_ticks;
        t = d.tell;
        if (!(t == t)) t = 0.0f;                   /* NaN */
        if (t > 1000.0f) t = 1000.0f;
        if (t < -1000.0f) t = -1000.0f;
        emit(out, EV_HOLDEM_TELL, s, 0, (int64_t)(t * 1000.0f));
    }
    if (g->ai_collected && g->turn_ticks >= g->ai_think)
        apply_action(g, s, g->ai_dec.action, g->ai_dec.amount, out);
}

static void busted_tick(HoldemGame *g, const InputFrame *in, EventQueue *out)
{
    if (in->pressed & (BTN_DEAL | BTN_OK)) {
        g->human_watching = 1;
        g->phase = HP_HAND_START;
        wait_ticks(g, g->cfg.t_hand_start);
    } else if (in->pressed & (BTN_BACK | BTN_CASH_OUT)) {
        g->winner = -1;
        g->over_reason = HOLDEM_OVER_HUMAN_LEFT;
        g->phase = HP_GAME_OVER;
        emit(out, EV_HOLDEM_GAME_OVER, -1, HOLDEM_OVER_HUMAN_LEFT, 0);
    }
}

static void step(HoldemGame *g, EventQueue *out)
{
    switch (g->phase) {
    case HP_HAND_START:   start_hand(g, out); break;
    case HP_ANTES:        post_antes(g, out); break;
    case HP_POST_SB:
        post_blind(g, g->sb_seat, holdem_level(g).sb, ACT_POST_SB, EV_HOLDEM_POST_SB, out);
        g->phase = HP_POST_BB;
        wait_ticks(g, g->cfg.t_post);
        break;
    case HP_POST_BB: {
        int s;
        post_blind(g, g->bb_seat, holdem_level(g).bb, ACT_POST_BB, EV_HOLDEM_POST_BB, out);
        /* Everyone must call the full big blind, even when it is short. */
        g->cur_bet = holdem_level(g).bb;
        for (s = 0; s < HOLDEM_SEATS; s++)
            if (g->seat[s].bet > g->cur_bet) g->cur_bet = g->seat[s].bet;
        g->last_raise = holdem_level(g).bb;
        g->phase = HP_DEAL;
        wait_ticks(g, g->cfg.t_deal);
        break;
    }
    case HP_DEAL:         deal_one(g, out); break;
    case HP_AFTER_ACTION: step_after_action(g, out); break;
    case HP_COLLECT:      step_collect(g, out); break;
    case HP_BOARD:        step_board(g, out); break;
    case HP_TABLE:        step_table(g, out); break;
    case HP_SHOWDOWN:     step_showdown(g, out); break;
    case HP_UNCONTESTED:  step_uncontested(g, out); break;
    case HP_AWARD:        step_award(g, out); break;
    case HP_HAND_END:     step_hand_end(g, out); break;
    default: break;
    }
}

void holdem_tick(HoldemGame *g, const InputFrame *in, Wallet *w, EventQueue *out)
{
    static const InputFrame none;
    (void)w;
    if (!in) in = &none;
    g->tick++;
    if (g->phase == HP_GAME_OVER) {
        /* nothing more happens */
    } else if (g->prompt) {
        prompt_tick(g, in, out);
    } else if (g->phase == HP_TURN) {
        turn_tick(g, in, out);
    } else if (g->phase == HP_BUSTED) {
        busted_tick(g, in, out);
    } else if (g->timer > 1) {
        g->timer--;
    } else {
        g->timer = 0;
        step(g, out);
    }
    g->last_slider = in->slider;
}

void holdem_init(HoldemGame *g, const HoldemConfig *cfg, uint64_t seed, const HoldemAiHooks *hooks)
{
    HoldemConfig c;
    int s;

    memset(g, 0, sizeof *g);
    eval_init();
    if (cfg) c = *cfg;
    else holdem_config_default(&c);
    if (c.nlevels <= 0 || c.nlevels > HOLDEM_MAX_LEVELS) {
        HoldemConfig d;
        holdem_config_default(&d);
        c.nlevels = d.nlevels;
        memcpy(c.levels, d.levels, sizeof c.levels);
    }
    if (c.hands_per_level < 1) c.hands_per_level = 1;
    if (c.start_stack < 1) c.start_stack = 1;
    g->cfg = c;
    holdem_set_hooks(g, hooks);
    rng_seed(&g->rng, seed);

    for (s = 0; s < HOLDEM_SEATS; s++) {
        HoldemSeat *p = &g->seat[s];
        int64_t st = c.seat_stack[s];
        p->empty = st < 0;
        p->stack = st < 0 ? 0 : st > 0 ? st : c.start_stack;
        p->in_game = !p->empty && p->stack > 0;
        p->hole[0] = p->hole[1] = CARD_NONE;
        g->players_left += p->in_game;
        g->chips_total += p->stack;
    }
    for (s = 0; s < 5; s++) g->board[s] = CARD_NONE;

    /* The first button: as configured, else drawn from the session stream.
       If that seat is empty the first hand moves it on to the next player. */
    if (c.first_button >= 0 && c.first_button < HOLDEM_SEATS) g->button = c.first_button;
    else g->button = (int)rng_below(&g->rng, HOLDEM_SEATS);

    g->to_act = -1;
    g->winner = -1;
    g->last_aggr = g->river_aggr = -1;
    g->phase = HP_HAND_START;
    g->timer = 1;
    if (g->players_left < 2) {
        g->winner = next_in_game(g, HOLDEM_SEATS - 1);
        g->phase = HP_GAME_OVER;
    }
}

/* ---- checks and logs ----------------------------------------------------- */

int holdem_check_invariants(const HoldemGame *g, char *why, size_t cap)
{
    int64_t sum = g->pot, com = 0, bets = 0;
    int s, n_in = 0, in_hand_phase;
    uint8_t places = 0;

#define FAIL(...) do { if (why && cap) snprintf(why, cap, __VA_ARGS__); return -1; } while (0)

    in_hand_phase = g->phase >= HP_ANTES && g->phase <= HP_UNCONTESTED;
    for (s = 0; s < HOLDEM_SEATS; s++) {
        const HoldemSeat *p = &g->seat[s];
        sum += p->stack + p->bet;
        com += p->committed;
        bets += p->bet;
        if (p->stack < 0 || p->bet < 0 || p->committed < 0) FAIL("seat %d negative chips", s);
        if (p->bet > p->committed) FAIL("seat %d bet above committed", s);
        if (p->bet > g->cur_bet) FAIL("seat %d bet %" PRId64 " above cur_bet %" PRId64, s, p->bet, g->cur_bet);
        if (p->in_game) n_in++;
        if (!p->in_game && !p->empty) {
            if (p->stack != 0) FAIL("eliminated seat %d has chips", s);
            if (p->place < 1 || p->place > HOLDEM_SEATS) FAIL("eliminated seat %d has no place", s);
            if (places & (1u << p->place)) FAIL("place %d given twice", p->place);
            places |= (uint8_t)(1u << p->place);
        }
        if (in_hand_phase && live(g, s) && (p->allin != 0) != (p->stack == 0))
            FAIL("seat %d all-in flag %d with stack %" PRId64, s, p->allin, p->stack);
        if (g->phase == HP_HAND_START && p->in_game && p->stack <= 0) FAIL("seat %d in game with no chips", s);
    }
    if (sum != g->chips_total) FAIL("chips %" PRId64 " != %" PRId64, sum, g->chips_total);
    if (g->phase != HP_GAME_OVER && n_in != g->players_left) FAIL("players_left %d != %d", g->players_left, n_in);
    if (in_hand_phase && com != g->pot + bets) FAIL("committed %" PRId64 " != pot+bets %" PRId64, com, g->pot + bets);
    if (g->phase == HP_AWARD) {
        int64_t left = 0;
        int i;
        for (i = g->award_idx; i < g->npots; i++) left += g->pots[i].amount;
        if (left != g->pot) FAIL("pots left %" PRId64 " != pot %" PRId64, left, g->pot);
    }
    if ((g->phase == HP_HAND_END || g->phase == HP_HAND_START || g->phase == HP_BUSTED ||
         g->phase == HP_GAME_OVER) && (g->pot != 0 || bets != 0))
        FAIL("chips left in the middle between hands");
    if (g->phase == HP_TURN && (g->to_act < 0 || !can_act(g, g->to_act)))
        FAIL("to_act %d cannot act", g->to_act);
    if (g->last_raise < 0 || g->cur_bet < 0) FAIL("negative bet level");
#undef FAIL
    if (why && cap) why[0] = '\0';
    return 0;
}

int holdem_hand_log_line(const HoldemGame *g, int64_t unix_time, char *out, size_t cap)
{
    return replay_format_hand(out, cap, unix_time, "holdem", g->log_seed, g->log_result);
}
