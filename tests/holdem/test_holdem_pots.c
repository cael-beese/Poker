/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* Pots: the pure side-pot builder and splitter against a naive reference
   on random inputs, then full hands through the real table with 3, 4 and
   5 all-ins of different sizes (one of them smaller than the big blind),
   split pots where the board plays, two- and three-way ties, and odd chips
   in the main and in side pots. */

#include "holdem_harness.h"

#include "engine/rng.h"

static TH t;

/* ---- pure functions ------------------------------------------------------ */

/* Reference: walk every chip level one chip at a time. Slow and obvious. */
static void ref_winnings(const int64_t com[6], const uint8_t live[6], const int rank[6], int button,
                         int64_t won[6])
{
    int64_t level, maxc = 0;
    int s;
    for (s = 0; s < 6; s++) { won[s] = 0; if (com[s] > maxc) maxc = com[s]; }
    /* Chips of layer (level-1, level] go to the best live hand that put in
       at least `level`; with nobody live that high they join the layer
       below (the table's dead-money rule). Collect per-eligible-set pots. */
    {
        int64_t pot_amt[64] = { 0 };
        uint8_t pot_set[64];
        int np = 0, i;
        uint8_t last_set = 0;
        for (level = 1; level <= maxc; level++) {
            uint8_t set = 0;
            int64_t amt = 0;
            for (s = 0; s < 6; s++) {
                if (com[s] >= level) amt++;
                if (com[s] >= level && live[s]) set |= (uint8_t)(1u << s);
            }
            if (!set) set = last_set;
            if (np == 0 || pot_set[np - 1] != set) { pot_set[np] = set; pot_amt[np] = 0; np++; }
            pot_amt[np - 1] += amt;
            last_set = set;
        }
        for (i = 0; i < np; i++) {
            int best = 1 << 30;
            uint8_t win = 0;
            for (s = 0; s < 6; s++)
                if ((pot_set[i] & (1u << s)) && rank[s] < best) best = rank[s];
            for (s = 0; s < 6; s++)
                if ((pot_set[i] & (1u << s)) && rank[s] == best) win |= (uint8_t)(1u << s);
            holdem_split_pot(pot_amt[i], win, button, won);
        }
    }
}

static void pure_random(void)
{
    Rng r;
    int iter;
    rng_seed(&r, 12345);
    for (iter = 0; iter < 200000; iter++) {
        int64_t com[6], won_a[6] = { 0 }, won_b[6], sum = 0, psum = 0;
        uint8_t live[6];
        int rank[6], s, n, i, nlive = 0, button = (int)rng_below(&r, 6);
        HoldemPot pots[HOLDEM_MAX_POTS];
        for (s = 0; s < 6; s++) {
            com[s] = rng_below(&r, 4) == 0 ? 0 : (int64_t)rng_below(&r, 60);
            if (rng_below(&r, 3) == 0) com[s] = 40;      /* many equal levels */
            live[s] = com[s] > 0 && rng_below(&r, 4) != 0;
            rank[s] = 1 + (int)rng_below(&r, 4);         /* plenty of ties   */
            nlive += live[s];
            sum += com[s];
        }
        if (nlive == 0) continue;
        /* The table returns uncalled chips first, so dead money above every
           live seat never reaches the builder; the builder must still keep
           it if it did. */
        n = holdem_build_pots(com, live, pots);
        CHECK(n >= 1 && n <= HOLDEM_MAX_POTS);
        for (i = 0; i < n; i++) {
            psum += pots[i].amount;
            CHECK(pots[i].amount > 0);
            CHECK(pots[i].eligible != 0);
            for (s = 0; s < 6; s++)
                if (pots[i].eligible & (1u << s)) CHECK(live[s]);
            if (i > 0) CHECK((pots[i].eligible & ~pots[i - 1].eligible) == 0);   /* nested */
        }
        CHECK_EQ_INT(psum, sum);
        for (i = 0; i < n; i++) {
            int best = 1 << 30;
            uint8_t win = 0;
            for (s = 0; s < 6; s++)
                if ((pots[i].eligible & (1u << s)) && rank[s] < best) best = rank[s];
            for (s = 0; s < 6; s++)
                if ((pots[i].eligible & (1u << s)) && rank[s] == best) win |= (uint8_t)(1u << s);
            holdem_split_pot(pots[i].amount, win, button, won_a);
        }
        ref_winnings(com, live, rank, button, won_b);
        for (s = 0; s < 6; s++) CHECK_EQ_INT(won_a[s], won_b[s]);
    }
}

static void split_odd_chips(void)
{
    int64_t w[6];
    /* 100 three ways, button 4: winners 5, 1, 3 in that order from the
       button; the one odd chip goes to seat 5. */
    memset(w, 0, sizeof w);
    holdem_split_pot(100, (1u << 5) | (1u << 1) | (1u << 3), 4, w);
    CHECK_EQ_INT(w[5], 34); CHECK_EQ_INT(w[1], 33); CHECK_EQ_INT(w[3], 33);
    /* 101: two odd chips, to 5 and then 1. */
    memset(w, 0, sizeof w);
    holdem_split_pot(101, (1u << 5) | (1u << 1) | (1u << 3), 4, w);
    CHECK_EQ_INT(w[5], 34); CHECK_EQ_INT(w[1], 34); CHECK_EQ_INT(w[3], 33);
    /* Button 0, winners 0 and 1: seat 1 is first left of the button. */
    memset(w, 0, sizeof w);
    holdem_split_pot(7, 0x03, 0, w);
    CHECK_EQ_INT(w[1], 4); CHECK_EQ_INT(w[0], 3);
    /* The button itself is last in the order. */
    memset(w, 0, sizeof w);
    holdem_split_pot(5, (1u << 2) | (1u << 4), 2, w);
    CHECK_EQ_INT(w[4], 3); CHECK_EQ_INT(w[2], 2);
}

static void build_known(void)
{
    int64_t com[6] = { 700, 80, 200, 350, 450, 700 };
    uint8_t live[6] = { 1, 1, 1, 1, 1, 1 };
    HoldemPot p[HOLDEM_MAX_POTS];
    int n = holdem_build_pots(com, live, p);
    CHECK_EQ_INT(n, 5);
    CHECK_EQ_INT(p[0].amount, 480); CHECK_EQ_INT(p[0].eligible, 0x3f);
    CHECK_EQ_INT(p[1].amount, 600); CHECK_EQ_INT(p[1].eligible, 0x3d);
    CHECK_EQ_INT(p[2].amount, 600); CHECK_EQ_INT(p[2].eligible, 0x39);
    CHECK_EQ_INT(p[3].amount, 300); CHECK_EQ_INT(p[3].eligible, 0x31);
    CHECK_EQ_INT(p[4].amount, 500); CHECK_EQ_INT(p[4].eligible, 0x21);
}

/* ---- hands through the table -------------------------------------------- */

static void start(int button, int sb, int bb, const int64_t stacks[6])
{
    HoldemConfig c;
    th_config(&c, button, sb, bb);
    memcpy(c.seat_stack, stacks, sizeof c.seat_stack);
    th_init(&t, &c, 777);
}

static int64_t total_chips(void)
{
    int64_t s = 0;
    int i;
    for (i = 0; i < 6; i++) s += t.g.seat[i].stack;
    return s;
}

static void check_pot_event(int k, int64_t amount, int elig)
{
    /* The k-th POT event of the last collection. */
    int i, last = -1, n = 0;
    for (i = 0; i < t.nev; i++)
        if (t.ev[i].type == EV_HOLDEM_BETS_TO_POT) last = i;
    for (i = last; i >= 0 && i < t.nev; i++) {
        if (t.ev[i].type != EV_HOLDEM_POT) continue;
        if (n++ == k) {
            CHECK_EQ_INT(t.ev[i].v, amount);
            CHECK_EQ_INT(t.ev[i].b, elig);
            return;
        }
    }
    CHECK(!"pot event missing");
}

static void three_allins(void)
{
    static const int64_t st[6] = { 1000, 100, 250, 500, 1000, 1000 };
    static const Step sc[] = {
        { 3, ACT_ALLIN, 0, 0 },  { 4, ACT_CALL, 0, 0 }, { 5, ACT_FOLD, 0, 0 },
        { 0, ACT_FOLD, 0, 0 },   { 1, ACT_CALL, 0, 0 }, { 2, ACT_CALL, 0, 0 },
    };
    const char *holes[6] = { NULL, "As Ah", "Ks Kh", "Qs Qh", "Js Jh", NULL };
    int64_t before;
    start(0, 10, 20, st);
    before = total_chips();
    th_script(&t, sc, 6);
    th_play_hand(&t, holes, "2c 7d 9h 3s 4c");
    CHECK_EQ_INT(t.sc.pos, 6);
    CHECK_EQ_INT(t.sc.unscripted, 0);
    CHECK_EQ_INT(t.sc.wrong_seat, 0);
    check_pot_event(0, 400, 0x1e);
    check_pot_event(1, 450, 0x1c);
    check_pot_event(2, 500, 0x18);
    CHECK_EQ_INT(th_count(&t, EV_HOLDEM_RUNOUT), 1);
    /* No betting after the run-out began, although seat 4 has chips. */
    CHECK(th_index(&t, th_nth(&t, EV_HOLDEM_TURN, 6)) < 0);
    CHECK_EQ_INT(t.g.seat[1].stack, 400);
    CHECK_EQ_INT(t.g.seat[2].stack, 450);
    CHECK_EQ_INT(t.g.seat[3].stack, 500);
    CHECK_EQ_INT(t.g.seat[4].stack, 500);
    CHECK_EQ_INT(t.g.seat[5].stack, 1000);
    CHECK_EQ_INT(t.g.seat[0].stack, 1000);
    CHECK_EQ_INT(total_chips(), before);
}

static void four_allins_short_blind(void)
{
    /* The big blind has 15 of the 20: all-in posting the blind. */
    static const int64_t st[6] = { 1000, 300, 15, 120, 600, 1000 };
    static const Step sc[] = {
        { 3, ACT_ALLIN, 0, 0 }, { 4, ACT_ALLIN, 0, 0 }, { 5, ACT_CALL, 0, 0 },
        { 0, ACT_FOLD, 0, 0 },  { 1, ACT_CALL, 0, 0 },
    };
    const char *holes[6] = { NULL, "Qs Qh", "As Ah", "Ks Kh", "Js Jh", "Ts Th" };
    const GameEvent *bb;
    int64_t before;
    start(0, 10, 20, st);
    before = total_chips();
    th_script(&t, sc, 5);
    th_play_hand(&t, holes, "2c 7d 9h 3s 4c");
    CHECK_EQ_INT(t.sc.pos, 5);
    CHECK_EQ_INT(t.sc.unscripted, 0);
    bb = th_nth(&t, EV_HOLDEM_POST_BB, 0);
    CHECK(bb && bb->a == 2 && bb->b == 1 && bb->v == 15);
    check_pot_event(0, 75, 0x3e);
    check_pot_event(1, 420, 0x3a);
    check_pot_event(2, 540, 0x32);
    check_pot_event(3, 600, 0x30);
    CHECK_EQ_INT(t.g.seat[1].stack, 540);
    CHECK_EQ_INT(t.g.seat[2].stack, 75);
    CHECK_EQ_INT(t.g.seat[3].stack, 420);
    CHECK_EQ_INT(t.g.seat[4].stack, 600);
    CHECK_EQ_INT(t.g.seat[5].stack, 400);
    CHECK_EQ_INT(t.g.seat[0].stack, 1000);
    CHECK_EQ_INT(total_chips(), before);
}

static void five_allins(void)
{
    static const int64_t st[6] = { 1000, 80, 200, 350, 450, 700 };
    static const Step sc[] = {
        { 3, ACT_ALLIN, 0, 0 }, { 4, ACT_ALLIN, 0, 0 }, { 5, ACT_ALLIN, 0, 0 },
        { 0, ACT_CALL, 0, 0 },  { 1, ACT_CALL, 0, 0 },  { 2, ACT_CALL, 0, 0 },
    };
    /* Seat 1 (smallest) has the best hand, seat 0 the second best. */
    const char *holes[6] = { "Ks Kh", "As Ah", "5s 6h", "8s 8h", "Js Td", "Qs Jh" };
    int64_t before;
    int i;
    start(0, 10, 20, st);
    before = total_chips();
    th_script(&t, sc, 6);
    th_play_hand(&t, holes, "2c 7d 9h 3s Ac");
    CHECK_EQ_INT(t.sc.unscripted, 0);
    check_pot_event(0, 480, 0x3f);
    check_pot_event(1, 600, 0x3d);
    check_pot_event(2, 600, 0x39);
    check_pot_event(3, 300, 0x31);
    check_pot_event(4, 500, 0x21);
    CHECK_EQ_INT(t.g.seat[1].stack, 480);
    CHECK_EQ_INT(t.g.seat[0].stack, 300 + 2000);
    for (i = 2; i <= 5; i++) CHECK_EQ_INT(t.g.seat[i].stack, 0);
    CHECK_EQ_INT(total_chips(), before);
    /* Four busted in one hand: bigger starting stack, better place. */
    CHECK_EQ_INT(t.g.seat[5].place, 3);
    CHECK_EQ_INT(t.g.seat[4].place, 4);
    CHECK_EQ_INT(t.g.seat[3].place, 5);
    CHECK_EQ_INT(t.g.seat[2].place, 6);
    CHECK_EQ_INT(t.g.players_left, 2);
    CHECK_EQ_INT(th_count(&t, EV_HOLDEM_ELIMINATED), 4);
}

static void board_plays_two_way(void)
{
    /* 5/10. Seat 3 raises to 45, seat 4 calls, everyone else folds, then
       checked down. Pot 5 + 10 + 45 + 45 = 105 split between 3 and 4 on a
       broadway board: seat 3 is first left of button 0 and gets the odd chip. */
    static const int64_t st[6] = { 1000, 1000, 1000, 1000, 1000, 1000 };
    static const Step sc[] = {
        { 3, ACT_RAISE, 45, 0 }, { 4, ACT_CALL, 0, 0 }, { 5, ACT_FOLD, 0, 0 },
        { 0, ACT_FOLD, 0, 0 },   { 1, ACT_FOLD, 0, 0 }, { 2, ACT_FOLD, 0, 0 },
        { 3, ACT_CHECK, 0, 0 },  { 4, ACT_CHECK, 0, 0 },
        { 3, ACT_CHECK, 0, 0 },  { 4, ACT_CHECK, 0, 0 },
        { 3, ACT_CHECK, 0, 0 },  { 4, ACT_CHECK, 0, 0 },
    };
    const char *holes[6] = { NULL, NULL, NULL, "2c 3d", "2d 3c", NULL };
    start(0, 5, 10, st);
    th_script(&t, sc, 12);
    th_play_hand(&t, holes, "As Kd Qh Jc Ts");
    CHECK_EQ_INT(t.sc.unscripted, 0);
    CHECK_EQ_INT(th_awarded(&t, 3), 53);
    CHECK_EQ_INT(th_awarded(&t, 4), 52);
    CHECK_EQ_INT(t.g.seat[3].stack, 1008);
    CHECK_EQ_INT(t.g.seat[4].stack, 1007);
}

static void three_way_tie(void)
{
    /* Button 4, SB seat 5, BB seat 0, 10/20. Seat 1 raises to 110, seats 3
       and 5 call, the big blind folds: pot 3 * 110 + 20 = 350, a three-way
       tie. 116 each and two odd chips, which go to the winners in order
       from the button: seat 5, then seat 1 (seat 0 folded). */
    static const int64_t st[6] = { 1000, 1000, 1000, 1000, 1000, 1000 };
    static const Step sc[] = {
        { 1, ACT_RAISE, 110, 0 }, { 2, ACT_FOLD, 0, 0 }, { 3, ACT_CALL, 0, 0 },
        { 4, ACT_FOLD, 0, 0 },    { 5, ACT_CALL, 0, 0 }, { 0, ACT_FOLD, 0, 0 },
        { 5, ACT_CHECK, 0, 0 }, { 1, ACT_CHECK, 0, 0 }, { 3, ACT_CHECK, 0, 0 },
        { 5, ACT_CHECK, 0, 0 }, { 1, ACT_CHECK, 0, 0 }, { 3, ACT_CHECK, 0, 0 },
        { 5, ACT_CHECK, 0, 0 }, { 1, ACT_CHECK, 0, 0 }, { 3, ACT_CHECK, 0, 0 },
    };
    const char *holes[6] = { NULL, "Ac 2d", NULL, "Ad 3c", NULL, "Ah 4c" };
    start(4, 10, 20, st);
    th_script(&t, sc, 15);
    th_play_hand(&t, holes, "Ks Kd Qh Qc 9s");   /* all: KKQQA */
    CHECK_EQ_INT(t.sc.unscripted, 0);
    CHECK_EQ_INT(t.sc.wrong_seat, 0);
    CHECK_EQ_INT(th_awarded(&t, 5), 117);
    CHECK_EQ_INT(th_awarded(&t, 1), 117);
    CHECK_EQ_INT(th_awarded(&t, 3), 116);
    CHECK_EQ_INT(th_count(&t, EV_HOLDEM_AWARD), 3);
}

static void odd_chip_in_side_pot(void)
{
    /* 5/10, button 0. Seat 3 all-in for 17 (an incomplete raise), seat 4
       raises to 40, seat 5 calls, 0 and 1 fold, 2 calls 40. Flop: 2 checks,
       4 bets 25, 5 folds, 2 calls. Checked down; 2 and 4 tie with the
       same straight, 3 has less.
       Main (17 each from 1..5 capped): 5 + 17*4 = 73 for {2,3,4}:
         36 each, odd chip to seat 2 (first left of the button).
       Side: 48 + 48 + 23 dead = 119 for {2,4}: 59 each, odd chip to 2. */
    static const int64_t st[6] = { 1000, 1000, 1000, 17, 1000, 1000 };
    static const Step sc[] = {
        { 3, ACT_ALLIN, 0, 0 }, { 4, ACT_RAISE, 40, 0 }, { 5, ACT_CALL, 0, 0 },
        { 0, ACT_FOLD, 0, 0 },  { 1, ACT_FOLD, 0, 0 },   { 2, ACT_CALL, 0, 0 },
        { 2, ACT_CHECK, 0, 0 }, { 4, ACT_BET, 25, 0 },   { 5, ACT_FOLD, 0, 0 }, { 2, ACT_CALL, 0, 0 },
        { 2, ACT_CHECK, 0, 0 }, { 4, ACT_CHECK, 0, 0 },
        { 2, ACT_CHECK, 0, 0 }, { 4, ACT_CHECK, 0, 0 },
    };
    const char *holes[6] = { NULL, NULL, "Tc 2d", "4c 5d", "Th 2s", NULL };
    const GameEvent *e;
    start(0, 5, 10, st);
    th_script(&t, sc, 14);
    th_play_hand(&t, holes, "As Ks Qd Jc 3h");
    CHECK_EQ_INT(t.sc.unscripted, 0);
    CHECK_EQ_INT(t.sc.wrong_seat, 0);
    check_pot_event(0, 73, 0x1c);
    check_pot_event(1, 119, 0x14);
    CHECK_EQ_INT(th_awarded(&t, 2), 37 + 60);
    CHECK_EQ_INT(th_awarded(&t, 4), 36 + 59);
    CHECK_EQ_INT(th_awarded(&t, 3), 0);
    CHECK_EQ_INT(t.g.seat[2].stack, 1032);
    CHECK_EQ_INT(t.g.seat[4].stack, 1030);
    CHECK_EQ_INT(t.g.seat[3].stack, 0);
    CHECK_EQ_INT(t.g.seat[3].place, 6);
    /* No river bet: seat 2 (first live left of the button) shows first,
       seat 3 cannot beat it and the AI mucks, seat 4 ties and must show. */
    e = th_nth(&t, EV_HOLDEM_SHOW, 0);
    CHECK(e && e->a == 2 && e->b == HOLDEM_SHOW_SHOWDOWN);
    e = th_nth(&t, EV_HOLDEM_MUCK, 0);
    CHECK(e && e->a == 3);
    e = th_nth(&t, EV_HOLDEM_SHOW, 1);
    CHECK(e && e->a == 4);
    CHECK(th_index(&t, th_nth(&t, EV_HOLDEM_MUCK, 0)) < th_index(&t, th_nth(&t, EV_HOLDEM_SHOW, 1)));
}

int main(void)
{
    eval_init();
    split_odd_chips();
    build_known();
    pure_random();
    three_allins();
    four_allins_short_blind();
    five_allins();
    board_plays_two_way();
    three_way_tie();
    odd_chip_in_side_pot();
    return test_finish("holdem_pots");
}
