/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* Betting rules through the real table: minimum raises, incomplete all-ins
   that do or do not reopen the action, a raise to exactly the minimum, a
   bet after a check, the big blind's option, heads-up order, short blinds,
   the table clamping illegal AI decisions, and the human's input mapping
   (quick bets, steps, slider, snapping). Default seats: button 0, small
   blind 1, big blind 2, first to act pre-flop 3; blinds 10/20. */

#include "holdem_harness.h"

static TH t;

static const int64_t k_full[6] = { 1000, 1000, 1000, 1000, 1000, 1000 };

static void start(const int64_t stacks[6], int button, int human)
{
    HoldemConfig c;
    th_config(&c, button, 10, 20);
    memcpy(c.seat_stack, stacks, sizeof c.seat_stack);
    c.seat0_ai = !human;
    th_init(&t, &c, 4242);
}

static void run(const Step *sc, int n)
{
    th_script(&t, sc, n);
    th_play_hand(&t, NULL, NULL);
    CHECK_EQ_INT(t.sc.wrong_seat, 0);
    CHECK_EQ_INT(t.sc.unscripted, 0);
    CHECK_EQ_INT(t.sc.pos, n);
}

static const GameEvent *action(int k) { return th_nth(&t, EV_HOLDEM_ACTION, k); }

static void check_action(int k, int seat, int code, int64_t to)
{
    const GameEvent *e = action(k);
    CHECK(e != NULL);
    if (!e) return;
    if (e->a != seat || e->b != code || e->v != to)
        fprintf(stderr, "  action %d: seat %d code %d to %d, expected seat %d code %d to %lld\n",
                k, e->a, e->b, e->v, seat, code, (long long)to);
    CHECK_EQ_INT(e->a, seat);
    CHECK_EQ_INT(e->b, code);
    CHECK_EQ_INT(e->v, to);
}

static void min_raise_after_raise(void)
{
    static const Step sc[] = {
        { 3, ACT_RAISE, 60, 0 },  { 4, ACT_RAISE, 100, 0 }, { 5, ACT_RAISE, 120, 0 },
        { 0, ACT_FOLD, 0, 0 },    { 1, ACT_FOLD, 0, 0 },    { 2, ACT_FOLD, 0, 0 },
        { 3, ACT_FOLD, 0, 0 },    { 4, ACT_FOLD, 0, 0 },
    };
    const GameEvent *u;
    start(k_full, 0, 0);
    run(sc, 8);
    /* 20 -> 60 is a raise of 40, so the next must be to at least 100. */
    CHECK_EQ_INT(t.turns[0].legal.min_to, 40);
    CHECK_EQ_INT(t.turns[1].legal.min_to, 100);
    check_action(1, 4, ACT_RAISE, 100);                  /* exactly the minimum */
    CHECK_EQ_INT(t.turns[2].legal.min_to, 140);
    check_action(2, 5, ACT_RAISE, 140);                  /* 120 clamped up */
    u = th_nth(&t, EV_HOLDEM_UNCALLED, 0);
    CHECK(u && u->a == 5 && u->v == 40);
    CHECK_EQ_INT(t.g.seat[5].stack, 1190);
    CHECK(th_nth(&t, EV_HOLDEM_MUCK, 0) && th_nth(&t, EV_HOLDEM_MUCK, 0)->b == 1);
    CHECK_EQ_INT(th_count(&t, EV_HOLDEM_SHOW), 0);
}

static void incomplete_allin_does_not_reopen(void)
{
    static const int64_t st[6] = { 1000, 1000, 1000, 1000, 150, 1000 };
    static const Step sc[] = {
        { 3, ACT_RAISE, 100, 0 }, { 4, ACT_ALLIN, 0, 0 }, { 5, ACT_FOLD, 0, 0 },
        { 0, ACT_FOLD, 0, 0 },    { 1, ACT_FOLD, 0, 0 },  { 2, ACT_CALL, 0, 0 },
        { 3, ACT_RAISE, 400, 0 },                          /* not allowed: becomes a call */
        { 2, ACT_CHECK, 0, 0 }, { 3, ACT_CHECK, 0, 0 },
        { 2, ACT_CHECK, 0, 0 }, { 3, ACT_CHECK, 0, 0 },
        { 2, ACT_CHECK, 0, 0 }, { 3, ACT_CHECK, 0, 0 },
    };
    start(st, 0, 0);
    run(sc, 13);
    check_action(1, 4, ACT_ALLIN, 150);
    /* The big blind has not acted since the last full raise: may raise. */
    CHECK_EQ_INT(t.turns[5].seat, 2);
    CHECK(t.turns[5].legal.can_raise);
    CHECK_EQ_INT(t.turns[5].legal.min_to, 230);
    /* The original raiser faces only 50 more (< 80): call or fold. */
    CHECK_EQ_INT(t.turns[6].seat, 3);
    CHECK(!t.turns[6].legal.can_raise && !t.turns[6].legal.can_bet);
    CHECK(t.turns[6].legal.can_call && t.turns[6].legal.can_fold);
    CHECK_EQ_INT(t.turns[6].legal.call_to, 150);
    check_action(6, 3, ACT_CALL, 150);
}

static void full_raise_reopens(void)
{
    static const int64_t st[6] = { 1000, 1000, 1000, 1000, 150, 1000 };
    static const Step sc[] = {
        { 3, ACT_RAISE, 100, 0 }, { 4, ACT_ALLIN, 0, 0 }, { 5, ACT_RAISE, 300, 0 },
        { 0, ACT_FOLD, 0, 0 },    { 1, ACT_FOLD, 0, 0 },  { 2, ACT_FOLD, 0, 0 },
        { 3, ACT_RAISE, 450, 0 }, { 5, ACT_FOLD, 0, 0 },
    };
    const GameEvent *u;
    start(st, 0, 0);
    run(sc, 8);
    /* 150 -> 300 is a full raise (150 >= 80): seat 3 may raise again. */
    CHECK_EQ_INT(t.turns[6].seat, 3);
    CHECK(t.turns[6].legal.can_raise);
    CHECK_EQ_INT(t.turns[6].legal.min_to, 450);
    check_action(6, 3, ACT_RAISE, 450);
    CHECK_EQ_INT(t.turns[7].seat, 5);
    CHECK(t.turns[7].legal.can_raise);
    CHECK_EQ_INT(t.turns[7].legal.min_to, 600);
    /* Seat 3's 450 was only matched up to seat 5's folded 300. */
    u = th_nth(&t, EV_HOLDEM_UNCALLED, 0);
    CHECK(u && u->a == 3 && u->v == 150);
    CHECK_EQ_INT(th_count(&t, EV_HOLDEM_RUNOUT), 1);
    CHECK_EQ_INT(t.nturns, 8);
}

static void short_allins_adding_up(int64_t s5, int reopen)
{
    int64_t st[6] = { 1000, 1000, 1000, 1000, 150, 0 };
    /* The big blind calls too, so someone could still answer a raise. */
    static const Step sc[] = {
        { 3, ACT_RAISE, 100, 0 }, { 4, ACT_ALLIN, 0, 0 }, { 5, ACT_ALLIN, 0, 0 },
        { 0, ACT_FOLD, 0, 0 },    { 1, ACT_FOLD, 0, 0 },  { 2, ACT_CALL, 0, 0 },
        { 3, ACT_CALL, 0, 0 },
        { 2, ACT_CHECK, 0, 0 }, { 3, ACT_CHECK, 0, 0 },
        { 2, ACT_CHECK, 0, 0 }, { 3, ACT_CHECK, 0, 0 },
        { 2, ACT_CHECK, 0, 0 }, { 3, ACT_CHECK, 0, 0 },
    };
    st[5] = s5;
    start(st, 0, 0);
    run(sc, 13);
    CHECK(t.turns[5].legal.can_raise);                   /* the big blind had not acted */
    check_action(2, 5, ACT_ALLIN, s5);
    CHECK_EQ_INT(t.turns[6].seat, 3);
    CHECK_EQ_INT(t.turns[6].legal.can_raise, reopen);
    if (reopen) CHECK_EQ_INT(t.turns[6].legal.min_to, s5 + 80);
    check_action(6, 3, ACT_CALL, s5);
}

static void bet_after_check(void)
{
    static const Step sc[] = {
        { 3, ACT_CALL, 0, 0 }, { 4, ACT_FOLD, 0, 0 }, { 5, ACT_FOLD, 0, 0 },
        { 0, ACT_FOLD, 0, 0 }, { 1, ACT_FOLD, 0, 0 }, { 2, ACT_CHECK, 0, 0 },
        { 2, ACT_CHECK, 0, 0 }, { 3, ACT_BET, 5, 0 }, { 2, ACT_RAISE, 40, 0 }, { 3, ACT_CALL, 0, 0 },
        { 2, ACT_BET, 30, 0 },  { 3, ACT_RAISE, 70, 0 }, { 2, ACT_CALL, 0, 0 },
        { 2, ACT_CHECK, 0, 0 }, { 3, ACT_CHECK, 0, 0 },
    };
    start(k_full, 0, 0);
    run(sc, 15);
    CHECK_EQ_INT(t.turns[6].seat, 2);
    CHECK(t.turns[6].legal.can_bet && t.turns[6].legal.can_check);
    CHECK_EQ_INT(t.turns[6].legal.min_to, 20);
    check_action(6, 2, ACT_CHECK, 0);
    CHECK(t.turns[7].legal.can_bet);
    CHECK_EQ_INT(t.turns[7].legal.min_to, 20);
    check_action(7, 3, ACT_BET, 20);                     /* 5 is below the minimum bet */
    CHECK_EQ_INT(t.turns[8].legal.min_to, 40);
    check_action(8, 2, ACT_RAISE, 40);                   /* check-raise to exactly the minimum */
    CHECK_EQ_INT(t.turns[9].legal.min_to, 60);
    check_action(9, 3, ACT_CALL, 40);
    check_action(10, 2, ACT_BET, 30);
    CHECK_EQ_INT(t.turns[11].legal.min_to, 60);
    check_action(11, 3, ACT_RAISE, 70);
    check_action(12, 2, ACT_CALL, 70);
}

static void big_blind_option(void)
{
    static const Step raise_sc[] = {
        { 3, ACT_CALL, 0, 0 }, { 4, ACT_CALL, 0, 0 }, { 5, ACT_CALL, 0, 0 },
        { 0, ACT_CALL, 0, 0 }, { 1, ACT_CALL, 0, 0 }, { 2, ACT_RAISE, 60, 0 },
        { 3, ACT_CALL, 0, 0 }, { 4, ACT_FOLD, 0, 0 }, { 5, ACT_FOLD, 0, 0 },
        { 0, ACT_FOLD, 0, 0 }, { 1, ACT_FOLD, 0, 0 },
        { 2, ACT_CHECK, 0, 0 }, { 3, ACT_CHECK, 0, 0 },
        { 2, ACT_CHECK, 0, 0 }, { 3, ACT_CHECK, 0, 0 },
        { 2, ACT_CHECK, 0, 0 }, { 3, ACT_CHECK, 0, 0 },
    };
    static const Step check_sc[] = {
        { 3, ACT_CALL, 0, 0 }, { 4, ACT_FOLD, 0, 0 }, { 5, ACT_FOLD, 0, 0 },
        { 0, ACT_FOLD, 0, 0 }, { 1, ACT_CALL, 0, 0 }, { 2, ACT_FOLD, 0, 0 },   /* fold = check */
        { 1, ACT_CHECK, 0, 0 }, { 2, ACT_CHECK, 0, 0 }, { 3, ACT_CHECK, 0, 0 },
        { 1, ACT_CHECK, 0, 0 }, { 2, ACT_CHECK, 0, 0 }, { 3, ACT_CHECK, 0, 0 },
        { 1, ACT_CHECK, 0, 0 }, { 2, ACT_CHECK, 0, 0 }, { 3, ACT_CHECK, 0, 0 },
    };
    start(k_full, 0, 0);
    run(raise_sc, 17);
    CHECK_EQ_INT(t.turns[5].seat, 2);
    CHECK(t.turns[5].legal.can_check && t.turns[5].legal.can_raise && !t.turns[5].legal.can_fold);
    CHECK_EQ_INT(t.turns[5].legal.min_to, 40);
    check_action(5, 2, ACT_RAISE, 60);
    CHECK_EQ_INT(t.turns[6].seat, 3);                    /* the limpers act again */

    start(k_full, 0, 0);
    run(check_sc, 15);
    check_action(4, 1, ACT_CALL, 20);                    /* small blind completes */
    check_action(5, 2, ACT_CHECK, 20);                   /* a free fold is a check */
    CHECK_EQ_INT(t.turns[6].seat, 1);                    /* flop: small blind first */
}

static void heads_up_order(void)
{
    static const int64_t st[6] = { 1000, -1, -1, 1000, -1, -1 };
    static const Step hand1[] = {
        { 0, ACT_CALL, 0, 0 },  { 3, ACT_CHECK, 0, 0 },
        { 3, ACT_CHECK, 0, 0 }, { 0, ACT_CHECK, 0, 0 },
        { 3, ACT_CHECK, 0, 0 }, { 0, ACT_CHECK, 0, 0 },
        { 3, ACT_BET, 40, 0 },  { 0, ACT_CALL, 0, 0 },
    };
    static const Step hand2[] = { { 3, ACT_FOLD, 0, 0 } };
    const GameEvent *e;
    start(st, 0, 0);
    run(hand1, 8);
    e = th_nth(&t, EV_HOLDEM_POST_SB, 0);
    CHECK(e && e->a == 0);                               /* the button posts the small blind */
    e = th_nth(&t, EV_HOLDEM_POST_BB, 0);
    CHECK(e && e->a == 3);
    CHECK_EQ_INT(t.turns[0].seat, 0);                    /* and acts first pre-flop */
    CHECK_EQ_INT(t.turns[2].seat, 3);                    /* and last after the flop */
    CHECK_EQ_INT(th_nth(&t, EV_HOLDEM_DEAL_HOLE, 0)->a, 3);   /* dealing starts left of the button */
    e = th_nth(&t, EV_HOLDEM_SHOW, 0);
    CHECK(e && e->a == 3);                               /* river bettor shows first */

    t.nev = 0;
    t.nturns = 0;
    run(hand2, 1);
    CHECK_EQ_INT(t.g.button, 3);
    e = th_nth(&t, EV_HOLDEM_POST_SB, 0);
    CHECK(e && e->a == 3);
    CHECK_EQ_INT(t.turns[0].seat, 3);
}

static void incomplete_bet_below_big_blind(void)
{
    static const int64_t st[6] = { 1000, 1000, 1000, 25, 1000, 1000 };
    static const Step sc[] = {
        { 3, ACT_CALL, 0, 0 },  { 4, ACT_CALL, 0, 0 }, { 5, ACT_FOLD, 0, 0 },
        { 0, ACT_FOLD, 0, 0 },  { 1, ACT_FOLD, 0, 0 }, { 2, ACT_CHECK, 0, 0 },
        { 2, ACT_CHECK, 0, 0 }, { 3, ACT_ALLIN, 0, 0 }, { 4, ACT_CALL, 0, 0 }, { 2, ACT_RAISE, 500, 0 },
        { 2, ACT_CHECK, 0, 0 }, { 4, ACT_CHECK, 0, 0 },
        { 2, ACT_CHECK, 0, 0 }, { 4, ACT_CHECK, 0, 0 },
    };
    start(st, 0, 0);
    run(sc, 14);
    check_action(7, 3, ACT_ALLIN, 5);
    /* Seat 4 has not acted on the flop: may raise, to at least 5 + 20. */
    CHECK_EQ_INT(t.turns[8].seat, 4);
    CHECK(t.turns[8].legal.can_raise);
    CHECK_EQ_INT(t.turns[8].legal.min_to, 25);
    /* Seat 2 checked; a 5-chip bet is not a full bet, so call or fold. */
    CHECK_EQ_INT(t.turns[9].seat, 2);
    CHECK(!t.turns[9].legal.can_raise);
    CHECK_EQ_INT(t.turns[9].legal.call_to, 5);
    check_action(9, 2, ACT_CALL, 5);
}

static void short_small_blind(void)
{
    /* The small blind has 7: all-in posting. Everyone folds to the big
       blind, who has nobody left to act against: no turn, 13 returned,
       and the two hands run out. */
    static const int64_t st[6] = { 1000, 7, 1000, 1000, 1000, 1000 };
    static const Step sc[] = {
        { 3, ACT_FOLD, 0, 0 }, { 4, ACT_FOLD, 0, 0 }, { 5, ACT_FOLD, 0, 0 }, { 0, ACT_FOLD, 0, 0 },
    };
    const GameEvent *e;
    start(st, 0, 0);
    run(sc, 4);
    e = th_nth(&t, EV_HOLDEM_POST_SB, 0);
    CHECK(e && e->a == 1 && e->v == 7 && e->b == 1);
    CHECK_EQ_INT(t.nturns, 4);
    e = th_nth(&t, EV_HOLDEM_UNCALLED, 0);
    CHECK(e && e->a == 2 && e->v == 13);
    CHECK_EQ_INT(th_count(&t, EV_HOLDEM_RUNOUT), 1);
    CHECK_EQ_INT(th_count(&t, EV_HOLDEM_BOARD), 5);
    CHECK_EQ_INT(t.g.seat[1].stack + t.g.seat[2].stack, 1007);
}

static void short_big_blind(void)
{
    /* The big blind has 15 of the 20: the others must still call 20, and
       the minimum raise is still to 40. The short blind can only win the
       main pot of 15 from each caller. */
    static const int64_t st[6] = { 1000, 1000, 15, 1000, 1000, 1000 };
    static const Step sc[] = {
        { 3, ACT_CALL, 0, 0 },  { 4, ACT_FOLD, 0, 0 }, { 5, ACT_FOLD, 0, 0 },
        { 0, ACT_FOLD, 0, 0 },  { 1, ACT_CALL, 0, 0 },
        { 1, ACT_CHECK, 0, 0 }, { 3, ACT_CHECK, 0, 0 },
        { 1, ACT_CHECK, 0, 0 }, { 3, ACT_CHECK, 0, 0 },
        { 1, ACT_CHECK, 0, 0 }, { 3, ACT_CHECK, 0, 0 },
    };
    start(st, 0, 0);
    run(sc, 11);
    CHECK_EQ_INT(t.turns[0].legal.call_to, 20);
    CHECK_EQ_INT(t.turns[0].legal.min_to, 40);
    check_action(0, 3, ACT_CALL, 20);
    check_action(4, 1, ACT_CALL, 20);
    CHECK_EQ_INT(t.turns[5].seat, 1);                    /* the all-in big blind never acts */
    CHECK_EQ_INT(th_nth(&t, EV_HOLDEM_POT, 0)->v, 45);  /* 15 x 3 */
    CHECK_EQ_INT(th_nth(&t, EV_HOLDEM_POT, 1)->v, 10);  /* 5 x 2  */
}

static void clamps_illegal_decisions(void)
{
    static const Step sc[] = {
        { 3, ACT_CHECK, 0, 0 },    /* facing 20: a fold                      */
        { 4, 9, 77, 0 },           /* no such action: fold                   */
        { 5, ACT_CALL, 0, 999 },   /* think 999 is clamped                   */
        { 0, ACT_RAISE, 5000, -3 },/* above the stack: all-in                */
        { 1, ACT_POST_SB, 0, 0 },  /* not a decision: fold                   */
        { 2, ACT_BET, 20, 0 },     /* "raise to" below the bet: a call (all-in) */
        { 5, ACT_ALLIN, 0, 0 },    /* raising closed (nobody left): a call   */
    };
    start(k_full, 0, 0);
    run(sc, 7);
    check_action(0, 3, ACT_FOLD, 0);
    check_action(1, 4, ACT_FOLD, 0);
    check_action(2, 5, ACT_CALL, 20);
    check_action(3, 0, ACT_ALLIN, 1000);
    check_action(4, 1, ACT_FOLD, 10);
    check_action(5, 2, ACT_ALLIN, 1000);
    CHECK_EQ_INT(t.turns[6].seat, 5);
    CHECK(!t.turns[6].legal.can_raise);
    check_action(6, 5, ACT_ALLIN, 1000);
    CHECK_EQ_INT(th_count(&t, EV_HOLDEM_RUNOUT), 1);
}

static void think_time(void)
{
    /* The table applies an AI action at max(24, think) ticks after the
       turn starts, clamped to HOLDEM_AI_MAX_THINK. */
    static const Step sc[] = { { 3, ACT_FOLD, 0, 60 }, { 4, ACT_FOLD, 0, 0 }, { 5, ACT_FOLD, 0, 9999 },
                               { 0, ACT_FOLD, 0, 0 }, { 1, ACT_FOLD, 0, 0 } };
    long turn_tick[8], act_tick[8];
    int nt = 0, na = 0, i;
    start(k_full, 0, 0);
    th_script(&t, sc, 5);
    th_run_until_event(&t, EV_HOLDEM_HAND_START, 1000);
    while (t.g.phase != HP_HAND_END && t.g.phase != HP_HAND_START && t.ticks < 5000) {
        int n0 = t.nev;
        th_tick(&t, NULL);
        for (i = n0; i < t.nev; i++) {
            if (t.ev[i].type == EV_HOLDEM_TURN && nt < 8) turn_tick[nt++] = t.ticks;
            if (t.ev[i].type == EV_HOLDEM_ACTION && na < 8) act_tick[na++] = t.ticks;
        }
        if (na == 5) break;
    }
    CHECK_EQ_INT(na, 5);
    CHECK_EQ_INT(act_tick[0] - turn_tick[0], 60);
    CHECK_EQ_INT(act_tick[1] - turn_tick[1], HOLDEM_AI_MIN_THINK);
    CHECK_EQ_INT(act_tick[2] - turn_tick[2], HOLDEM_AI_MAX_THINK);
}

/* ---- the human's input ---- */

static int16_t g_slider;

static void press(uint32_t btn)
{
    InputFrame in;
    memset(&in, 0, sizeof in);
    in.down = in.pressed = btn;
    in.slider = g_slider;
    th_tick(&t, &in);
}

static void run_to_human_turn(void)
{
    long k;
    for (k = 0; k < 20000 && !holdem_is_human_turn(&t.g); k++) {
        InputFrame in;
        memset(&in, 0, sizeof in);
        in.slider = g_slider;
        th_tick(&t, &in);
    }
    CHECK(holdem_is_human_turn(&t.g));
}

static void human_input(void)
{
    static const Step sc[] = {
        { 3, ACT_RAISE, 60, 0 }, { 4, ACT_FOLD, 0, 0 }, { 5, ACT_FOLD, 0, 0 },
        { 1, ACT_FOLD, 0, 0 },   { 2, ACT_FOLD, 0, 0 }, { 3, ACT_CALL, 0, 0 },
        { 3, ACT_CHECK, 0, 0 },
    };
    int n_act;
    start(k_full, 0, 1);
    th_script(&t, sc, 7);
    g_slider = 0;
    run_to_human_turn();
    CHECK_EQ_INT(t.g.sel_amount, 100);                   /* starts at the min raise */
    press(BTN_HOLD5);                                    /* pot: 60 + (90 + 60) */
    CHECK_EQ_INT(t.g.sel_amount, 210);
    CHECK_EQ_INT(t.ev[t.nev - 1].type, EV_HOLDEM_AMOUNT);
    CHECK_EQ_INT(t.ev[t.nev - 1].v, 210);
    press(BTN_UP);
    CHECK_EQ_INT(t.g.sel_amount, 230);
    press(BTN_DOWN);
    press(BTN_LEFT);
    CHECK_EQ_INT(t.g.sel_amount, 190);
    press(BTN_BET_ONE);
    CHECK_EQ_INT(t.g.sel_amount, 210);
    g_slider = 32767;
    press(0);
    CHECK_EQ_INT(t.g.sel_amount, 1000);                  /* slider right: all-in */
    g_slider = -32767;
    press(0);
    CHECK_EQ_INT(t.g.sel_amount, 100);                   /* slider left: min raise */
    g_slider = 0;
    press(0);
    CHECK_EQ_INT(t.g.sel_amount, 550);                   /* middle, snapped to 10s */
    press(BTN_BET_MAX);
    CHECK_EQ_INT(t.g.sel_amount, 1000);
    press(BTN_HOLD4);                                    /* 3/4 pot: 60 + 112.5 -> 170 */
    CHECK_EQ_INT(t.g.sel_amount, 170);
    press(BTN_HOLD3);                                    /* 1/2 pot: 60 + 75 = 135 -> 140 */
    CHECK_EQ_INT(t.g.sel_amount, 140);
    n_act = th_count(&t, EV_HOLDEM_ACTION);
    press(BTN_HOLD2 | BTN_DEAL);                         /* ambiguous: nothing */
    CHECK_EQ_INT(th_count(&t, EV_HOLDEM_ACTION), n_act);
    CHECK(holdem_is_human_turn(&t.g));
    press(BTN_DEAL);
    check_action(3, 0, ACT_RAISE, 140);

    run_to_human_turn();                                 /* flop, after seat 3 checks */
    n_act = th_count(&t, EV_HOLDEM_ACTION);
    press(BTN_HOLD1);                                    /* fold while checking is free: ignored */
    CHECK_EQ_INT(th_count(&t, EV_HOLDEM_ACTION), n_act);
    press(BTN_DOWN);                                     /* below the min bet stays at the min */
    CHECK_EQ_INT(t.g.sel_amount, 20);
    press(BTN_HOLD2);
    check_action(n_act, 0, ACT_CHECK, 0);
}

int main(void)
{
    eval_init();
    min_raise_after_raise();
    incomplete_allin_does_not_reopen();
    full_raise_reopens();
    short_allins_adding_up(190, 1);   /* 150 then 190 over a 100 raise: 90 >= 80 */
    short_allins_adding_up(170, 0);   /* 150 then 170: only 70 more, stays closed */
    bet_after_check();
    big_blind_option();
    heads_up_order();
    incomplete_bet_below_big_blind();
    short_small_blind();
    short_big_blind();
    clamps_illegal_decisions();
    think_time();
    human_input();
    return test_finish("holdem_betting");
}
