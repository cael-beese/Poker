/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* Showdown order, mucking (AI and the human's prompt), uncalled bets and
   the all-in run-out, through the real table. Button 0, blinds 10/20. */

#include "holdem_harness.h"

static TH t;

static const int64_t k_full[6] = { 1000, 1000, 1000, 1000, 1000, 1000 };

static void start(int human)
{
    HoldemConfig c;
    th_config(&c, 0, 10, 20);
    memcpy(c.seat_stack, k_full, sizeof c.seat_stack);
    c.seat0_ai = !human;
    th_init(&t, &c, 99);
}

/* Event order helpers: index of the n-th SHOW or MUCK, whichever. */
typedef struct { int type, seat, b; } Reveal;

static int reveals(Reveal *r, int max)
{
    int i, n = 0;
    for (i = 0; i < t.nev && n < max; i++)
        if (t.ev[i].type == EV_HOLDEM_SHOW || t.ev[i].type == EV_HOLDEM_MUCK) {
            r[n].type = t.ev[i].type;
            r[n].seat = t.ev[i].a;
            r[n].b = t.ev[i].b;
            n++;
        }
    return n;
}

static void river_aggressor_first(void)
{
    static const Step sc[] = {
        { 3, ACT_CALL, 0, 0 },  { 4, ACT_FOLD, 0, 0 }, { 5, ACT_FOLD, 0, 0 },
        { 0, ACT_FOLD, 0, 0 },  { 1, ACT_CALL, 0, 0 }, { 2, ACT_CHECK, 0, 0 },
        { 1, ACT_CHECK, 0, 0 }, { 2, ACT_CHECK, 0, 0 }, { 3, ACT_CHECK, 0, 0 },
        { 1, ACT_CHECK, 0, 0 }, { 2, ACT_CHECK, 0, 0 }, { 3, ACT_CHECK, 0, 0 },
        { 1, ACT_CHECK, 0, 0 }, { 2, ACT_BET, 40, 0 },  { 3, ACT_CALL, 0, 0 }, { 1, ACT_CALL, 0, 0 },
    };
    const char *holes[6] = { NULL, "Kh Kd", "3d 5h", "Ah Ad", NULL, NULL };
    Reveal r[8];
    int n;
    start(0);
    th_script(&t, sc, 16);
    th_play_hand(&t, holes, "2c 7d 9h Js 4c");
    CHECK_EQ_INT(t.sc.unscripted, 0);
    n = reveals(r, 8);
    CHECK_EQ_INT(n, 3);
    /* The river bettor (seat 2, worst hand) shows first, seat 3 beats it
       and must show, seat 1 cannot win and the AI mucks. */
    CHECK(r[0].type == EV_HOLDEM_SHOW && r[0].seat == 2);
    CHECK(r[1].type == EV_HOLDEM_SHOW && r[1].seat == 3);
    CHECK(r[2].type == EV_HOLDEM_MUCK && r[2].seat == 1 && r[2].b == 0);
    CHECK_EQ_INT(th_awarded(&t, 3), 60 + 120);
    CHECK_EQ_INT(th_count(&t, EV_HOLDEM_AWARD), 1);
}

static void no_river_bet_left_of_button(void)
{
    static const Step sc[] = {
        { 3, ACT_CALL, 0, 0 },  { 4, ACT_FOLD, 0, 0 }, { 5, ACT_FOLD, 0, 0 },
        { 0, ACT_FOLD, 0, 0 },  { 1, ACT_CALL, 0, 0 }, { 2, ACT_CHECK, 0, 0 },
        { 1, ACT_CHECK, 0, 0 }, { 2, ACT_CHECK, 0, 0 }, { 3, ACT_CHECK, 0, 0 },
        { 1, ACT_CHECK, 0, 0 }, { 2, ACT_CHECK, 0, 0 }, { 3, ACT_CHECK, 0, 0 },
        { 1, ACT_CHECK, 0, 0 }, { 2, ACT_CHECK, 0, 0 }, { 3, ACT_CHECK, 0, 0 },
    };
    const char *holes[6] = { NULL, "Kh Kd", "3d 5h", "Ah Ad", NULL, NULL };
    Reveal r[8];
    int n;
    start(0);
    th_script(&t, sc, 15);
    th_play_hand(&t, holes, "2c 7d 9h Js 4c");
    n = reveals(r, 8);
    CHECK_EQ_INT(n, 3);
    CHECK(r[0].type == EV_HOLDEM_SHOW && r[0].seat == 1);
    CHECK(r[1].type == EV_HOLDEM_MUCK && r[1].seat == 2);
    CHECK(r[2].type == EV_HOLDEM_SHOW && r[2].seat == 3);
    /* A SHOW carries both cards and the final rank. */
    {
        const GameEvent *e = th_nth(&t, EV_HOLDEM_SHOW, 0);
        Card a, b;
        card_parse("Kh", &a);
        card_parse("Kd", &b);
        CHECK_EQ_INT(e->v & 0xff, a);
        CHECK_EQ_INT((e->v >> 8) & 0xff, b);
        CHECK_EQ_INT(e->v >> 16, t.g.seat[1].rank);
        CHECK(eval_category(e->v >> 16) == HC_PAIR);
    }
}

/* Plays the human (seat 0): HOLD2 on every turn, `answer` at the show/muck
   prompt (0 = let it time out). Returns ticks the prompt stayed open. */
static long human_hand(const char *holes[6], const char *board, uint32_t human_btn, uint32_t answer)
{
    long k, prompt_ticks = 0;
    th_run_until_event(&t, EV_HOLDEM_HAND_START, 100000);
    th_stack(&t, holes, board);
    for (k = 0; k < 100000; k++) {
        InputFrame in;
        int n0 = t.nev, i, done = 0;
        memset(&in, 0, sizeof in);
        if (holdem_is_human_turn(&t.g)) in.pressed = in.down = human_btn;
        if (t.g.prompt) { prompt_ticks++; in.pressed = in.down = answer; }
        th_tick(&t, &in);
        for (i = n0; i < t.nev; i++) done |= t.ev[i].type == EV_HOLDEM_HAND_END;
        if (done) break;
    }
    return prompt_ticks;
}

static void human_showdown(uint32_t answer, int expect_type, int human_wins)
{
    /* Human is the button and calls, seat 3 limps, the blinds check. */
    static const Step sc[] = {
        { 3, ACT_CALL, 0, 0 }, { 4, ACT_FOLD, 0, 0 }, { 5, ACT_FOLD, 0, 0 },
        { 1, ACT_FOLD, 0, 0 }, { 2, ACT_CHECK, 0, 0 },
        { 2, ACT_CHECK, 0, 0 }, { 3, ACT_CHECK, 0, 0 },
        { 2, ACT_CHECK, 0, 0 }, { 3, ACT_CHECK, 0, 0 },
        { 2, ACT_CHECK, 0, 0 }, { 3, ACT_CHECK, 0, 0 },
    };
    const char *lose[6] = { "Kh Kd", NULL, "Ah Ad", "3d 5h", NULL, NULL };
    const char *win[6]  = { "Ah Ad", NULL, "Kh Kd", "3d 5h", NULL, NULL };
    Reveal r[8];
    long open;
    int n;
    start(1);
    th_script(&t, sc, 11);
    open = human_hand(human_wins ? win : lose, "2c 7d 9h Js 4c", BTN_HOLD2, answer);
    CHECK_EQ_INT(t.sc.unscripted, 0);
    n = reveals(r, 8);
    CHECK_EQ_INT(n, 3);
    /* Order: seat 2 (first left of the button), seat 3, then the human. */
    CHECK(r[0].seat == 2 && r[1].seat == 3 && r[2].seat == 0);
    CHECK(r[1].type == EV_HOLDEM_MUCK);
    CHECK_EQ_INT(r[2].type, expect_type);
    if (human_wins) {
        CHECK(r[0].type == EV_HOLDEM_SHOW);
        CHECK_EQ_INT(th_count(&t, EV_HOLDEM_SHOW_PROMPT), 0);    /* a winner must show */
        CHECK_EQ_INT(open, 0);
        CHECK_EQ_INT(th_awarded(&t, 0), 70);
    } else {
        CHECK_EQ_INT(th_count(&t, EV_HOLDEM_SHOW_PROMPT), 1);
        CHECK(th_nth(&t, EV_HOLDEM_SHOW_PROMPT, 0)->b == 0);
        if (!answer) CHECK_EQ_INT(open, t.g.cfg.human_show_ticks);
        CHECK_EQ_INT(th_awarded(&t, 2), 70);
    }
}

static void human_uncontested(uint32_t answer, int expect_type)
{
    /* The human raises and everyone folds. */
    static const Step sc[] = {
        { 3, ACT_FOLD, 0, 0 }, { 4, ACT_FOLD, 0, 0 }, { 5, ACT_FOLD, 0, 0 },
        { 1, ACT_FOLD, 0, 0 }, { 2, ACT_FOLD, 0, 0 },
    };
    const GameEvent *e;
    start(1);
    th_script(&t, sc, 5);
    human_hand(NULL, NULL, BTN_DEAL, answer);
    CHECK_EQ_INT(t.sc.unscripted, 0);
    e = th_nth(&t, EV_HOLDEM_UNCALLED, 0);
    CHECK(e && e->a == 0 && e->v == 20);                 /* raised to 40, 20 unmatched */
    e = th_nth(&t, EV_HOLDEM_SHOW_PROMPT, 0);
    CHECK(e && e->a == 0 && e->b == 1);
    e = th_nth(&t, expect_type, 0);
    CHECK(e && e->a == 0);
    if (expect_type == EV_HOLDEM_SHOW) CHECK_EQ_INT(e->b, HOLDEM_SHOW_VOLUNTARY);
    else CHECK_EQ_INT(e->b, 1);
    CHECK_EQ_INT(t.g.seat[0].stack, 1030);
}

static void allin_runout(void)
{
    static const Step sc[] = {
        { 3, ACT_ALLIN, 0, 0 }, { 4, ACT_FOLD, 0, 0 }, { 5, ACT_FOLD, 0, 0 },
        { 0, ACT_FOLD, 0, 0 },  { 1, ACT_FOLD, 0, 0 }, { 2, ACT_CALL, 0, 0 },
    };
    const char *holes[6] = { NULL, NULL, "Qh Qd", "Ah Kh", NULL, NULL };
    const GameEvent *ro;
    int iro, i, boards_after = 0, turns_after = 0;
    Reveal r[8];
    start(0);
    th_script(&t, sc, 6);
    th_play_hand(&t, holes, "2h 7h 9c Jd 3h");            /* seat 3 makes a flush */
    ro = th_nth(&t, EV_HOLDEM_RUNOUT, 0);
    CHECK(ro != NULL);
    iro = th_index(&t, ro);
    for (i = iro; i < t.nev; i++) {
        boards_after += t.ev[i].type == EV_HOLDEM_BOARD;
        turns_after += t.ev[i].type == EV_HOLDEM_TURN;
    }
    CHECK_EQ_INT(boards_after, 5);
    CHECK_EQ_INT(turns_after, 0);
    CHECK_EQ_INT(reveals(r, 8), 2);
    /* Both tabled before the board, first left of the button first. */
    CHECK(r[0].type == EV_HOLDEM_SHOW && r[0].seat == 2 && r[0].b == HOLDEM_SHOW_ALLIN);
    CHECK(r[1].type == EV_HOLDEM_SHOW && r[1].seat == 3 && r[1].b == HOLDEM_SHOW_ALLIN);
    CHECK(th_index(&t, th_nth(&t, EV_HOLDEM_SHOW, 1)) < th_index(&t, th_nth(&t, EV_HOLDEM_BOARD, 0)));
    CHECK_EQ_INT(th_count(&t, EV_HOLDEM_HAND_RANK), 2);
    CHECK_EQ_INT(th_awarded(&t, 3), 2010);
    CHECK_EQ_INT(t.g.seat[3].stack, 2010);
    CHECK_EQ_INT(t.g.seat[2].stack, 0);
}

static void uncalled_river_bet(void)
{
    static const Step sc[] = {
        { 3, ACT_CALL, 0, 0 },  { 4, ACT_FOLD, 0, 0 }, { 5, ACT_FOLD, 0, 0 },
        { 0, ACT_FOLD, 0, 0 },  { 1, ACT_FOLD, 0, 0 }, { 2, ACT_CHECK, 0, 0 },
        { 2, ACT_CHECK, 0, 0 }, { 3, ACT_CHECK, 0, 0 },
        { 2, ACT_CHECK, 0, 0 }, { 3, ACT_CHECK, 0, 0 },
        { 2, ACT_BET, 300, 0 }, { 3, ACT_FOLD, 0, 0 },
    };
    const GameEvent *e;
    start(0);
    th_script(&t, sc, 12);
    th_play_hand(&t, NULL, NULL);
    e = th_nth(&t, EV_HOLDEM_UNCALLED, 0);
    CHECK(e && e->a == 2 && e->v == 300);
    CHECK_EQ_INT(th_count(&t, EV_HOLDEM_SHOW), 0);
    CHECK_EQ_INT(th_awarded(&t, 2), 50);
    CHECK_EQ_INT(t.g.seat[2].stack, 1030);
    CHECK_EQ_INT(t.g.seat[3].stack, 980);
}

int main(void)
{
    eval_init();
    river_aggressor_first();
    no_river_bet_left_of_button();
    human_showdown(BTN_BACK, EV_HOLDEM_MUCK, 0);
    human_showdown(BTN_DEAL, EV_HOLDEM_SHOW, 0);
    human_showdown(0, EV_HOLDEM_MUCK, 0);            /* prompt times out: muck */
    human_showdown(0, EV_HOLDEM_SHOW, 1);            /* winner shows, no prompt */
    human_uncontested(BTN_OK, EV_HOLDEM_SHOW);
    human_uncontested(0, EV_HOLDEM_MUCK);
    allin_runout();
    uncalled_river_bet();
    return test_finish("holdem_showdown");
}
