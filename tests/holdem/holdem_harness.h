/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
#ifndef BPL_TEST_HOLDEM_HARNESS_H
#define BPL_TEST_HOLDEM_HARNESS_H

/* Drives a real HoldemGame through holdem_tick, the way the app does, with
   scripted AI decisions arriving through the real HoldemAiHooks. Every tick
   it checks the table's invariants and that any action taken was legal in
   the state before that tick. Scenarios fix the cards by rewriting the
   freshly shuffled deck on the tick after the hand's shuffle and before
   the first card is dealt - from outside the library, which has no such
   back door. */

#include "engine/eval.h"
#include "games/holdem/holdem.h"
#include "games/holdem/holdem_bots.h"
#include "test_util.h"

#include <stdio.h>
#include <string.h>

typedef struct { int seat, action; int64_t amount; int think; } Step;

typedef struct {
    Step    steps[96];
    int     n, pos;
    int     wrong_seat, unscripted;
    AiView  views[128];
    int     nviews;
} Script;

typedef struct { int seat; HoldemLegal legal; int64_t stack, bet; } TurnRec;

typedef struct {
    HoldemGame g;
    Script     sc;
    GameEvent  ev[8192];
    int        nev;
    TurnRec    turns[256];
    int        nturns;
    long       ticks, inv_checks, action_checks;
    int        failures_reported;
} TH;

/* ---- scripted hooks ---- */

static inline void sc_begin(void *ctx, int seat, const AiView *v, uint64_t seed)
{
    Script *sc = ctx;
    (void)seed;
    if (sc->nviews < 128) sc->views[sc->nviews++] = *v;
    (void)seat;
}

static inline void sc_collect(void *ctx, int seat, AiDecision *out)
{
    Script *sc = ctx;
    memset(out, 0, sizeof *out);
    if (sc->pos < sc->n) {
        const Step *st = &sc->steps[sc->pos];
        if (st->seat != seat) {
            sc->wrong_seat++;
            fprintf(stderr, "  script step %d expected seat %d, table asked seat %d\n", sc->pos, st->seat, seat);
        }
        sc->pos++;
        out->action = st->action;
        out->amount = st->amount;
        out->think_ticks = st->think;
        return;
    }
    /* Past the script everyone checks or calls, and it is counted. */
    sc->unscripted++;
    out->action = ACT_CALL;
}

static inline void th_script(TH *t, const Step *steps, int n)
{
    memcpy(t->sc.steps, steps, sizeof(Step) * (size_t)n);
    t->sc.n = n;
    t->sc.pos = 0;
}

/* ---- legality of an action, judged from the state before it ---- */

static inline int th_action_legal(const TurnRec *r, int code, int64_t v)
{
    const HoldemLegal *L = &r->legal;
    int64_t all = r->bet + r->stack;
    switch (code) {
    case ACT_FOLD:  return L->can_fold;
    case ACT_CHECK: return L->can_check && v == r->bet;
    case ACT_CALL:  return L->can_call && v == L->call_to && v <= all;   /* all-in calls too */
    case ACT_BET:   return L->can_bet && v >= L->min_to && v <= L->max_to && v < all;
    case ACT_RAISE: return L->can_raise && v >= L->min_to && v <= L->max_to && v < all;
    case ACT_ALLIN:
        return v == all && (L->can_bet || L->can_raise) && v == L->max_to;   /* bets, raises */
    default: return 0;
    }
}

static inline void th_fail(TH *t, const char *what)
{
    g_test_failures++;
    if (t->failures_reported++ < 10)
        fprintf(stderr, "FAIL tick %ld hand %d: %s\n", t->ticks, t->g.hand_no, what);
}

static inline void th_init(TH *t, const HoldemConfig *cfg, uint64_t seed)
{
    HoldemAiHooks h;
    memset(t, 0, sizeof *t);
    h.ctx = &t->sc;
    h.begin = sc_begin;
    h.collect = sc_collect;
    holdem_init(&t->g, cfg, seed, &h);
}

/* One tick with checks. Returns the number of events it produced. */
static inline int th_tick(TH *t, const InputFrame *in)
{
    EventQueue q;
    TurnRec before;
    char why[160];
    int i, had_turn = t->g.phase == HP_TURN;

    memset(&before, 0, sizeof before);
    if (had_turn) {
        holdem_legal(&t->g, &before.legal);
        before.seat = t->g.to_act;
        before.stack = t->g.seat[before.seat].stack;
        before.bet = t->g.seat[before.seat].bet;
    }
    q.n = 0;
    holdem_tick(&t->g, in, NULL, &q);
    t->ticks++;
    t->inv_checks++;
    if (holdem_check_invariants(&t->g, why, sizeof why) != 0) th_fail(t, why);
    for (i = 0; i < q.n; i++) {
        const GameEvent *e = &q.e[i];
        if (t->nev < (int)(sizeof t->ev / sizeof t->ev[0])) t->ev[t->nev++] = *e;
        if (e->type == EV_HOLDEM_TURN && t->nturns < 256) {
            TurnRec *r = &t->turns[t->nturns++];
            holdem_legal(&t->g, &r->legal);
            r->seat = e->a;
            r->stack = t->g.seat[e->a].stack;
            r->bet = t->g.seat[e->a].bet;
        }
        if (e->type == EV_HOLDEM_ACTION) {
            t->action_checks++;
            if (!had_turn || e->a != before.seat) th_fail(t, "action by a seat that was not to act");
            else if (!th_action_legal(&before, e->b, e->v)) {
                char m[160];
                snprintf(m, sizeof m, "illegal action seat %d code %d to %d (min %lld max %lld call %lld)",
                         e->a, e->b, e->v, (long long)before.legal.min_to, (long long)before.legal.max_to,
                         (long long)before.legal.call_to);
                th_fail(t, m);
            }
        }
    }
    return q.n;
}

static inline void th_run_until_event(TH *t, int type, long max_ticks)
{
    long k;
    for (k = 0; k < max_ticks; k++) {
        int n0 = t->nev, i;
        th_tick(t, NULL);
        for (i = n0; i < t->nev; i++)
            if (t->ev[i].type == type) return;
        if (t->g.phase == HP_GAME_OVER) return;
    }
    th_fail(t, "event never came");
}

static inline Card th_free_card(uint8_t used[52], int *next)
{
    while (*next < 52 && used[*next]) (*next)++;
    if (*next >= 52) return CARD_NONE;
    used[*next] = 1;
    return (Card)*next;
}

/* Called right after EV_HOLDEM_HAND_START: sets the hole cards ("As Kd",
   NULL = any) and the board ("2c 7d 9h 3s 4c", NULL = any) by rewriting
   the deck in dealing order (burn cards before flop, turn and river). */
static inline void th_stack(TH *t, const char *holes[HOLDEM_SEATS], const char *board)
{
    HoldemGame *g = &t->g;
    uint8_t used[52] = { 0 };
    Card hc[HOLDEM_SEATS][2], bd[5], order[52];
    int n = 0, i, s, nd = g->ndeal, next = 0;

    if (g->deal_idx != 0 || g->deck.pos != 0) { th_fail(t, "th_stack after dealing started"); return; }
    for (s = 0; s < HOLDEM_SEATS; s++) {
        hc[s][0] = hc[s][1] = CARD_NONE;
        if (holes && holes[s]) {
            Card c2[2];
            if (cards_parse(holes[s], c2, 2) != 2) { th_fail(t, "bad hole cards"); return; }
            hc[s][0] = c2[0]; hc[s][1] = c2[1];
            used[c2[0]] = used[c2[1]] = 1;
        }
    }
    for (i = 0; i < 5; i++) bd[i] = CARD_NONE;
    if (board) {
        if (cards_parse(board, bd, 5) != 5) { th_fail(t, "bad board"); return; }
        for (i = 0; i < 5; i++) used[bd[i]] = 1;
    }

    for (i = 0; i < 2 * nd; i++) {
        s = g->deal_order[i % nd];
        order[n++] = hc[s][i / nd] != CARD_NONE ? hc[s][i / nd] : th_free_card(used, &next);
    }
    order[n++] = th_free_card(used, &next);                                   /* burn */
    for (i = 0; i < 3; i++) order[n++] = bd[i] != CARD_NONE ? bd[i] : th_free_card(used, &next);
    order[n++] = th_free_card(used, &next);                                   /* burn */
    order[n++] = bd[3] != CARD_NONE ? bd[3] : th_free_card(used, &next);
    order[n++] = th_free_card(used, &next);                                   /* burn */
    order[n++] = bd[4] != CARD_NONE ? bd[4] : th_free_card(used, &next);
    while (n < 52) order[n++] = th_free_card(used, &next);

    memcpy(g->deck.c, order, 52);
    g->deck.n = 52;
    g->deck.pos = 0;
}

/* Plays one hand from wherever the game is: waits for the shuffle, stacks
   the deck, then runs to the end of the hand. */
static inline void th_play_hand(TH *t, const char *holes[HOLDEM_SEATS], const char *board)
{
    th_run_until_event(t, EV_HOLDEM_HAND_START, 100000);
    th_stack(t, holes, board);
    th_run_until_event(t, EV_HOLDEM_HAND_END, 100000);
}

/* ---- reading the event log ---- */

static inline int th_count(const TH *t, int type)
{
    int i, n = 0;
    for (i = 0; i < t->nev; i++) n += t->ev[i].type == type;
    return n;
}

/* The k-th event of a type (0-based), or NULL. */
static inline const GameEvent *th_nth(const TH *t, int type, int k)
{
    int i;
    for (i = 0; i < t->nev; i++)
        if (t->ev[i].type == type && k-- == 0) return &t->ev[i];
    return NULL;
}

static inline int th_index(const TH *t, const GameEvent *e) { return e ? (int)(e - t->ev) : -1; }

/* Sum of AWARD amounts to a seat. */
static inline int64_t th_awarded(const TH *t, int seat)
{
    int i;
    int64_t s = 0;
    for (i = 0; i < t->nev; i++)
        if (t->ev[i].type == EV_HOLDEM_AWARD && t->ev[i].a == seat) s += t->ev[i].v;
    return s;
}

/* A fast config, antes off, 10/20 forever, button on seat `button`. */
static inline void th_config(HoldemConfig *c, int button, int sb, int bb)
{
    holdem_config_fast(c);
    c->antes = 0;
    c->nlevels = 1;
    c->levels[0].sb = sb;
    c->levels[0].bb = bb;
    c->levels[0].ante = 0;
    c->hands_per_level = 1000;
    c->first_button = button;
    c->seat0_ai = 1;
}

#endif
