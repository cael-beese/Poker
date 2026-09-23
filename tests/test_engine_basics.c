/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* Cards, deck, events, wallet, fixed step and concurrent eval_init. */

#include "engine/card.h"
#include "engine/deck.h"
#include "engine/eval.h"
#include "engine/event.h"
#include "engine/input.h"
#include "engine/step.h"
#include "engine/wallet.h"
#include "test_util.h"

#include <pthread.h>
#include <string.h>

static void cards(void)
{
    char s[3];
    Card c, list[8];
    int i;
    for (i = 0; i < 52; i++) {
        CHECK(card_parse(card_str((Card)i, s), &c) == 0);
        CHECK_EQ_INT(c, i);
        CHECK_EQ_INT(card_make(card_rank((Card)i), card_suit((Card)i)), i);
    }
    CHECK(strcmp(card_str(card_make(RANK_A, SUIT_S), s), "As") == 0);
    CHECK(strcmp(card_str(card_make(RANK_T, SUIT_D), s), "Td") == 0);
    CHECK(strcmp(card_str(card_make(RANK_2, SUIT_C), s), "2c") == 0);
    CHECK(strcmp(card_str(CARD_NONE, s), "??") == 0);
    CHECK(card_parse("10h", &c) == 0 && c == card_make(RANK_T, SUIT_H));
    CHECK(card_parse("qS", &c) == 0 && c == card_make(RANK_Q, SUIT_S));
    CHECK(card_parse("", &c) == -1);
    CHECK(card_parse("A", &c) == -1);
    CHECK(card_parse("Ax", &c) == -1);
    CHECK(card_parse("1s", &c) == -1);
    CHECK(card_parse("Asx", &c) == -1);
    CHECK(card_parse(NULL, &c) == -1);
    CHECK_EQ_INT(cards_parse("  As Kd\t7h ", list, 8), 3);
    CHECK(list[0] == card_make(RANK_A, SUIT_S) && list[2] == card_make(RANK_7, SUIT_H));
    CHECK_EQ_INT(cards_parse("As Kd", list, 1), -1);
    CHECK_EQ_INT(cards_parse("AsKd", list, 8), -1);
    CHECK_EQ_INT(cards_parse("", list, 8), 0);
}

static void decks(void)
{
    Deck d;
    Rng r;
    int i, seen[52] = {0};
    Card known[3] = { 0, 17, 51 };

    deck_init(&d);
    CHECK_EQ_INT(d.n, 52);
    CHECK_EQ_INT(deck_left(&d), 52);
    for (i = 0; i < 52; i++) CHECK_EQ_INT(deck_draw(&d), i);
    CHECK_EQ_INT(deck_draw(&d), CARD_NONE);
    CHECK_EQ_INT(deck_left(&d), 0);

    rng_seed(&r, 5);
    deck_init(&d);
    d.pos = 10;
    deck_shuffle(&d, &r);
    CHECK_EQ_INT(d.pos, 0);
    for (i = 0; i < 52; i++) seen[deck_draw(&d)]++;
    for (i = 0; i < 52; i++) CHECK_EQ_INT(seen[i], 1);

    deck_init_without(&d, known, 3);
    CHECK_EQ_INT(d.n, 49);
    for (i = 0; i < d.n; i++) CHECK(d.c[i] != 0 && d.c[i] != 17 && d.c[i] != 51);
    memset(seen, 0, sizeof seen);
    for (i = 0; i < 49; i++) seen[deck_draw_random(&d, &r)]++;
    CHECK_EQ_INT(deck_draw_random(&d, &r), CARD_NONE);
    CHECK_EQ_INT(seen[0] + seen[17] + seen[51], 0);
    for (i = 1; i < 51; i++) if (i != 17) CHECK_EQ_INT(seen[i], 1);
    deck_reset(&d);
    CHECK_EQ_INT(deck_left(&d), 49);
}

static void events(void)
{
    static EventQueue q;
    int i;
    ev_clear(&q);
    ev_push(&q, 101, -3, 7, 123456789);
    CHECK_EQ_INT(q.n, 1);
    CHECK(q.e[0].type == 101 && q.e[0].a == -3 && q.e[0].b == 7 && q.e[0].v == 123456789);
    for (i = 0; i < 300; i++) ev_push(&q, 1, i, 0, 0);
    CHECK_EQ_INT(q.n, 256);          /* full: extra events are dropped */
    CHECK_EQ_INT(q.e[255].a, 254);
    ev_clear(&q);
    CHECK_EQ_INT(q.n, 0);
}

static void wallets(void)
{
    Wallet w = { 100, 1 };
    CHECK(wallet_debit(&w, 30) == 0 && w.credits == 70);
    CHECK(wallet_debit(&w, 71) == -1 && w.credits == 70);
    CHECK(wallet_debit(&w, -5) == -1 && w.credits == 70);
    wallet_credit(&w, 5);
    wallet_credit(&w, -50);
    CHECK_EQ_INT(w.credits, 75);
    CHECK(wallet_can_afford(&w, 75) && !wallet_can_afford(&w, 76));
}

static void inputs(void)
{
    InputFrame f;
    memset(&f, 0, sizeof f);
    f.down = BTN_DEAL | BTN_HOLD_N(2);
    input_set_pressed(&f, BTN_DEAL);
    CHECK(f.pressed == BTN_HOLD3);
    CHECK(BTN_HOLD_N(4) == BTN_HOLD5);
    CHECK(BTN_ALL == 0x7FFFFu);
}

static void steps(void)
{
    FixedStep s;
    int i, total = 0;
    long long ticks;

    /* Exactly 60 Hz frames: one tick each, every frame. */
    step_init(&s, 60, 4);
    for (i = 0; i < 600; i++) CHECK_EQ_INT(step_advance(&s, 1.0 / 60.0), 1);

    /* Jittery vsync (16.5 / 16.8 ms alternating) snaps to one tick a frame. */
    step_init(&s, 60, 4);
    for (i = 0; i < 600; i++) CHECK_EQ_INT(step_advance_ns(&s, (i & 1) ? 16800000 : 16533333), 1);

    /* 144 Hz display: 60 ticks per 144 frames on average, never more than 1. */
    step_init(&s, 60, 4);
    for (i = 0; i < 144 * 10; i++) {
        int n = step_advance(&s, 1.0 / 144.0);
        CHECK(n == 0 || n == 1);
        total += n;
    }
    CHECK(total >= 599 && total <= 600);

    /* 30 Hz display: two ticks per frame. */
    step_init(&s, 60, 4);
    for (i = 0; i < 100; i++) CHECK_EQ_INT(step_advance(&s, 1.0 / 30.0), 2);

    /* A 1 s stall runs at most max_ticks and drops the rest. */
    step_init(&s, 60, 4);
    CHECK_EQ_INT(step_advance(&s, 1.0), 4);
    CHECK_EQ_INT((long long)s.dropped, 56);
    CHECK_EQ_INT(step_advance(&s, -1.0), 0);

    /* No drift: frames of 16666667, 16666666, 16666667 ns (1/60 s as a ns
       clock reports it; each triple is exactly 50 ms) give exactly one tick
       per frame for 600,000 frames without snapping, because time is kept
       as integers, not accumulated in floating point. */
    step_init(&s, 60, 4);
    step_set_snap(&s, 0);
    ticks = 0;
    for (i = 0; i < 600000; i++) ticks += step_advance_ns(&s, (i % 3 == 1) ? 16666666 : 16666667);
    CHECK_EQ_INT(ticks, 600000);
    CHECK_EQ_INT((long long)s.ticks, 600000);
    CHECK(step_alpha(&s) >= 0.0f && step_alpha(&s) < 1.0f);
}

static void *init_thread(void *arg)
{
    Card h[7] = { 48, 44, 40, 36, 32, 0, 5 };   /* A K Q J T of clubs + junk */
    eval_init();
    *(int *)arg = eval7(h);
    return NULL;
}

static void eval_threads(void)
{
    pthread_t t[8];
    int r[8], i;
    for (i = 0; i < 8; i++) pthread_create(&t[i], NULL, init_thread, &r[i]);
    for (i = 0; i < 8; i++) pthread_join(t[i], NULL);
    for (i = 0; i < 8; i++) CHECK_EQ_INT(r[i], 1);
}

int main(void)
{
    eval_threads();   /* first: eval_init races from 8 threads at once */
    cards();
    decks();
    events();
    wallets();
    inputs();
    steps();
    return test_finish("engine_basics");
}
