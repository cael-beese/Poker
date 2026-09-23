/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* The Draw Poker state machine.

   - A scripted hand: the cards are exactly what the documented RNG scheme
     deals (session Rng -> hand seed -> Fisher-Yates), every event fires on
     its documented tick, holds toggle, the draw replaces the right cards,
     the pay and the credits are right, and the hand log line parses.
   - Bets: BET ONE wraps, BET MAX sets 5 and deals, too few credits is
     refused with the wallet untouched, variant select and its lock.
   - Double-up, scripted both ways (predicted card from the hand seed),
     collect, auto-collect after the timeout.
   - Double-up fairness and limits over 1,000,000 rounds played through the
     game (BPL_DRAW_DOUBLE_ROUNDS): wins ~ 50 %, revealed cards uniform over
     52, never more than 5 rounds, never an offer over the cap, and the
     wallet balances to the event stream.
   - Replay determinism: the same seed and input frames give the same events,
     wallet and state bytes, also after a round trip through the engine's
     input log; a different seed gives a different game. */

#include "engine/replay.h"
#include "games/draw/draw_game.h"
#include "test_util.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

typedef struct { GameEvent e; long tick; } LogEv;
typedef struct { LogEv ev[4096]; int n; long tick; } Log;

static void tick(DrawGame *g, Wallet *w, uint32_t pressed, Log *log)
{
    InputFrame in;
    EventQueue q;
    int i;
    memset(&in, 0, sizeof in);
    in.pressed = in.down = pressed;
    q.n = 0;
    draw_tick(g, &in, w, &q);
    for (i = 0; i < q.n && log && log->n < 4096; i++) {
        log->ev[log->n].e = q.e[i];
        log->ev[log->n].tick = log->tick;
        log->n++;
    }
    if (log) log->tick++;
}

/* Index of the first event of `type` (with a == a, if a >= 0) from index `from`. */
static int find(const Log *l, int from, int type, int a)
{
    int i;
    for (i = from; i < l->n; i++)
        if (l->ev[i].e.type == type && (a < 0 || l->ev[i].e.a == a)) return i;
    return -1;
}

/* The documented deal: returns the shuffled deck of hand number `hand`
   (1-based) of a game seeded with `seed`, and the hand Rng after it. */
static uint64_t predict(uint64_t seed, int hand, Deck *d, Rng *hand_rng)
{
    Rng r;
    uint64_t hs = 0;
    int i;
    rng_seed(&r, seed);
    for (i = 0; i < hand; i++) hs = rng_next(&r);
    rng_seed(hand_rng, hs);
    deck_init(d);
    deck_shuffle(d, hand_rng);
    return hs;
}

static void scripted_hand(void)
{
    DrawConfig cfg;
    DrawGame g;
    Wallet w = { 100, 1 };
    static Log log;
    Deck d;
    Rng hr;
    uint64_t hs;
    int i, k, deal_tick, idx, pay;
    Card fin[5];

    draw_config_default(&cfg);
    cfg.double_up = 0;
    draw_init(&g, &cfg, 42);
    hs = predict(42, 1, &d, &hr);
    memset(&log, 0, sizeof log);

    tick(&g, &w, BTN_DEAL, &log);                          /* tick 0 */
    CHECK_EQ_INT(w.credits, 95);
    CHECK_EQ_INT(log.ev[0].e.type, EV_DRAW_CREDITS);
    CHECK_EQ_INT(log.ev[0].e.v, 95);
    CHECK_EQ_INT(log.ev[1].e.type, EV_DRAW_HAND_START);
    CHECK_EQ_INT(log.ev[1].e.b, 5);
    CHECK_EQ_INT(g.hand_seed, hs);
    for (i = 0; i < 5; i++) CHECK_EQ_INT(g.cards[i], d.c[i]);
    while (g.state == DS_DEALING && log.tick < 1000) tick(&g, &w, 0, &log);
    CHECK_EQ_INT(g.state, DS_HOLD);
    for (i = 0; i < 5; i++) {
        idx = find(&log, 0, EV_DRAW_DEAL_CARD, i);
        CHECK(idx >= 0 && log.ev[idx].tick == i * 6 && log.ev[idx].e.b == d.c[i]);
        idx = find(&log, 0, EV_DRAW_FLIP_CARD, i);
        CHECK(idx >= 0 && log.ev[idx].tick == i * 6 + 12 && log.ev[idx].e.b == d.c[i]);
    }
    idx = find(&log, 0, EV_DRAW_HOLD_PHASE, -1);
    CHECK(idx >= 0 && log.ev[idx].tick == 36);
    CHECK_EQ_INT(log.ev[idx].e.a, draw_classify(DRAW_JOB, d.c));
    CHECK_EQ_INT(g.face_up, 31);

    /* Hold cards 1 and 3; toggle 3 off and on again. */
    k = log.n;
    tick(&g, &w, BTN_HOLD1 | BTN_HOLD3, &log);
    tick(&g, &w, BTN_HOLD3, &log);
    tick(&g, &w, BTN_HOLD3, &log);
    CHECK_EQ_INT(log.n - k, 4);
    CHECK(log.ev[k].e.type == EV_DRAW_HOLD && log.ev[k].e.a == 0 && log.ev[k].e.b == 1);
    CHECK(log.ev[k + 2].e.type == EV_DRAW_HOLD && log.ev[k + 2].e.a == 2 && log.ev[k + 2].e.b == 0);
    CHECK_EQ_INT(g.held, 0x05);

    k = log.n;
    deal_tick = (int)log.tick;
    tick(&g, &w, BTN_DEAL, &log);
    while (g.state == DS_DRAWING && log.tick < 2000) tick(&g, &w, 0, &log);
    CHECK_EQ_INT(g.state, DS_IDLE);
    fin[0] = d.c[0]; fin[1] = d.c[5]; fin[2] = d.c[2]; fin[3] = d.c[6]; fin[4] = d.c[7];
    for (i = 0; i < 5; i++) CHECK_EQ_INT(g.cards[i], fin[i]);
    {
        static const int POS[3] = { 1, 3, 4 };
        for (i = 0; i < 3; i++) {
            idx = find(&log, k, EV_DRAW_DISCARD, POS[i]);
            CHECK(idx >= 0 && log.ev[idx].tick == deal_tick);
            idx = find(&log, k, EV_DRAW_DRAW_CARD, POS[i]);
            CHECK(idx >= 0 && log.ev[idx].tick == deal_tick + 6 * i && log.ev[idx].e.b == fin[POS[i]]);
            idx = find(&log, k, EV_DRAW_FLIP_CARD, POS[i]);
            CHECK(idx >= 0 && log.ev[idx].tick == deal_tick + 6 * i + 12);
        }
    }
    CHECK_EQ_INT(find(&log, k, EV_DRAW_DISCARD, 0), -1);
    pay = draw_pay(DRAW_JOB, draw_classify(DRAW_JOB, fin), 5);
    idx = find(&log, k, pay ? EV_DRAW_WIN : EV_DRAW_NO_WIN, -1);
    CHECK(idx >= 0 && log.ev[idx].tick == deal_tick + 12 + 12 + 8);
    CHECK_EQ_INT(w.credits, 95 + pay);
    idx = find(&log, k, EV_DRAW_HAND_END, -1);
    CHECK(idx >= 0 && log.ev[idx].e.v == pay);

    /* The hand log line. */
    {
        FILE *f = tmpfile();
        char line[512], mode[REPLAY_MODE_MAX + 1], res[256], want[256];
        int64_t t;
        uint64_t seed;
        CHECK(f != NULL);
        if (f) {
            CHECK(draw_write_hand_log(&g, f, 1790000000) == 0);
            rewind(f);
            CHECK(fgets(line, sizeof line, f) != NULL);
            CHECK(replay_parse_hand(line, &t, mode, &seed, res, sizeof res) == 0);
            CHECK(t == 1790000000 && strcmp(mode, "draw") == 0 && seed == hs);
            draw_hand_result(&g, want, sizeof want);
            CHECK(strcmp(res, want) == 0);
            CHECK(strncmp(res, "JOB bet=5 deal=", 15) == 0);
            printf("hand log: %s", line);
            fclose(f);
        }
    }
}

static void bets_and_denials(void)
{
    DrawConfig cfg;
    DrawGame g;
    Wallet w = { 3, 1 };
    static Log log;
    draw_config_default(&cfg);
    draw_init(&g, &cfg, 7);
    memset(&log, 0, sizeof log);

    tick(&g, &w, BTN_DEAL, &log);                 /* bet 5, 3 credits: refused */
    CHECK_EQ_INT(log.n, 1);
    CHECK(log.ev[0].e.type == EV_DRAW_DENIED && log.ev[0].e.a == DRAW_DENY_CREDITS);
    CHECK_EQ_INT(w.credits, 3);
    CHECK_EQ_INT(g.state, DS_IDLE);
    tick(&g, &w, BTN_BET_ONE, &log);              /* 5 wraps to 1 */
    CHECK(log.ev[1].e.type == EV_DRAW_BET && log.ev[1].e.a == 1 && log.ev[1].e.b == 0);
    tick(&g, &w, BTN_BET_ONE, &log);
    tick(&g, &w, BTN_BET_ONE, &log);
    CHECK_EQ_INT(g.bet, 3);
    tick(&g, &w, BTN_UP, &log);
    CHECK_EQ_INT(g.variant, DRAW_BONUS);
    tick(&g, &w, BTN_DOWN, &log);
    tick(&g, &w, BTN_DOWN, &log);
    CHECK_EQ_INT(g.variant, DRAW_DEUCES);
    CHECK_EQ_INT(log.ev[log.n - 1].e.type, EV_DRAW_VARIANT);
    tick(&g, &w, BTN_BET_MAX, &log);              /* bet 5 and deal: refused, bet stays 5 */
    CHECK_EQ_INT(g.bet, 5);
    CHECK_EQ_INT(g.state, DS_IDLE);
    CHECK(log.ev[log.n - 2].e.type == EV_DRAW_BET && log.ev[log.n - 2].e.b == 1);
    CHECK(log.ev[log.n - 1].e.type == EV_DRAW_DENIED);
    w.credits = 10;
    tick(&g, &w, BTN_BET_MAX, &log);              /* now it deals */
    CHECK_EQ_INT(g.state, DS_DEALING);
    CHECK_EQ_INT(w.credits, 5);
    CHECK_EQ_INT(g.variant, DRAW_DEUCES);
    /* Buttons that mean nothing mid-deal are ignored. */
    tick(&g, &w, BTN_BET_ONE | BTN_UP | BTN_HOLD1, &log);
    CHECK_EQ_INT(g.bet, 5);
    CHECK_EQ_INT(g.held, 0);

    cfg.allow_variant_select = 0;
    draw_init(&g, &cfg, 7);
    memset(&log, 0, sizeof log);
    tick(&g, &w, BTN_UP, &log);
    CHECK(log.n == 1 && log.ev[0].e.type == EV_DRAW_DENIED && log.ev[0].e.a == DRAW_DENY_VARIANT_LOCKED);
    CHECK_EQ_INT(g.variant, DRAW_JOB);
}

/* A seed whose first JoB hand, all five held, pays; and whose first
   double-up card is red (want_red) or black. */
static uint64_t find_seed(int want_red, int *pay_out, Card *dbl_out)
{
    uint64_t s;
    for (s = 1; s < 1000000; s++) {
        Deck d;
        Rng hr;
        int cat, red;
        predict(s, 1, &d, &hr);
        cat = draw_classify(DRAW_JOB, d.c);
        if (!draw_pay(DRAW_JOB, cat, 5)) continue;
        deck_init(&d);
        deck_shuffle(&d, &hr);                    /* the double-up shuffle */
        red = card_suit(d.c[0]) == SUIT_D || card_suit(d.c[0]) == SUIT_H;
        if (red != want_red) continue;
        *pay_out = draw_pay(DRAW_JOB, cat, 5);
        *dbl_out = d.c[0];
        return s;
    }
    return 0;
}

static void play_to_offer(DrawGame *g, Wallet *w, Log *log)
{
    tick(g, w, BTN_DEAL, log);
    while (g->state == DS_DEALING) tick(g, w, 0, log);
    tick(g, w, BTN_HOLD1 | BTN_HOLD2 | BTN_HOLD3 | BTN_HOLD4 | BTN_HOLD5, log);
    tick(g, w, BTN_DEAL, log);
    while (g->state == DS_DRAWING) tick(g, w, 0, log);
}

static void double_up_scripted(void)
{
    DrawConfig cfg;
    DrawGame g;
    Wallet w;
    static Log log;
    int want_red, pay, idx, k;
    Card dc;
    draw_config_default(&cfg);
    for (want_red = 0; want_red <= 1; want_red++) {
        uint64_t s = find_seed(want_red, &pay, &dc);
        CHECK(s != 0);
        draw_init(&g, &cfg, s);
        w.credits = 1000;
        w.denom = 1;
        memset(&log, 0, sizeof log);
        play_to_offer(&g, &w, &log);
        CHECK_EQ_INT(g.state, DS_OFFER);
        CHECK_EQ_INT(g.meter, pay);
        CHECK_EQ_INT(w.credits, 995);                 /* not credited until collect */
        idx = find(&log, 0, EV_DRAW_DOUBLE_OFFER, 1);
        CHECK(idx >= 0 && log.ev[idx].e.v == pay);
        tick(&g, &w, BTN_HOLD1, &log);
        CHECK_EQ_INT(g.state, DS_DOUBLE);
        k = log.n;
        tick(&g, &w, BTN_LEFT, &log);                 /* guess red */
        while (g.state == DS_DOUBLE_REVEAL) tick(&g, &w, 0, &log);
        idx = find(&log, k, EV_DRAW_DOUBLE_CARD, 1);
        CHECK(idx >= 0 && log.ev[idx].e.b == dc && log.ev[idx].tick == log.ev[k].tick + 30);
        if (want_red) {
            CHECK(find(&log, k, EV_DRAW_DOUBLE_WIN, 1) >= 0);
            CHECK_EQ_INT(g.state, DS_OFFER);
            CHECK_EQ_INT(g.meter, 2 * pay);
            k = log.n;
            tick(&g, &w, BTN_HOLD5, &log);            /* collect */
            idx = find(&log, k, EV_DRAW_COLLECT, -1);
            CHECK(idx >= 0 && log.ev[idx].e.v == 2 * pay);
            CHECK_EQ_INT(w.credits, 995 + 2 * pay);
            idx = find(&log, k, EV_DRAW_HAND_END, -1);
            CHECK(idx >= 0 && log.ev[idx].e.b == 1 && log.ev[idx].e.v == 2 * pay);
        } else {
            CHECK(find(&log, k, EV_DRAW_DOUBLE_LOSE, 1) >= 0);
            CHECK_EQ_INT(g.state, DS_IDLE);
            CHECK_EQ_INT(g.meter, 0);
            CHECK_EQ_INT(w.credits, 995);
            idx = find(&log, k, EV_DRAW_HAND_END, -1);
            CHECK(idx >= 0 && log.ev[idx].e.v == 0);
        }
    }

    /* Auto-collect: an offer left alone is collected after 1800 ticks. */
    {
        uint64_t s = find_seed(1, &pay, &dc);
        long t0;
        draw_init(&g, &cfg, s);
        w.credits = 1000;
        memset(&log, 0, sizeof log);
        play_to_offer(&g, &w, &log);
        t0 = log.tick - 1;
        while (g.state == DS_OFFER && log.tick < t0 + 5000) tick(&g, &w, 0, &log);
        idx = find(&log, 0, EV_DRAW_COLLECT, -1);
        CHECK(idx >= 0 && log.ev[idx].tick == t0 + 1800);
        CHECK_EQ_INT(w.credits, 995 + pay);
    }

    /* DEAL in the offer collects and starts the next hand at once. */
    {
        uint64_t s = find_seed(1, &pay, &dc);
        draw_init(&g, &cfg, s);
        w.credits = 1000;
        memset(&log, 0, sizeof log);
        uint64_t first_seed;
        char res[256], want[64];
        play_to_offer(&g, &w, &log);
        first_seed = g.hand_seed;
        k = log.n;
        tick(&g, &w, BTN_DEAL, &log);
        CHECK_EQ_INT(g.state, DS_DEALING);
        CHECK_EQ_INT(g.hand_no, 2);
        CHECK_EQ_INT(w.credits, 995 + pay - 5);
        /* Order on that one tick: collect, hand end, then the new hand. */
        CHECK(find(&log, k, EV_DRAW_COLLECT, -1) < find(&log, k, EV_DRAW_HAND_END, -1));
        CHECK(find(&log, k, EV_DRAW_HAND_END, -1) < find(&log, k, EV_DRAW_HAND_START, -1));
        /* The hand log still describes hand 1, not the one just dealt. */
        CHECK_EQ_INT(g.last.hand_no, 1);
        CHECK(g.last.seed == first_seed && g.last.seed != g.hand_seed);
        CHECK_EQ_INT(g.last.paid, pay);
        draw_hand_result(&g, res, sizeof res);
        snprintf(want, sizeof want, "paid=%d ", pay);
        CHECK(strstr(res, want) != NULL && strstr(res, "hold=11111") != NULL);
    }
}

static void double_up_fairness(void)
{
    const char *e = getenv("BPL_DRAW_DOUBLE_ROUNDS");
    long target = (e && *e) ? atol(e) : 1000000L;
    static const uint32_t RED_BTN[3] = { BTN_LEFT, BTN_HOLD1, BTN_HOLD2 };
    static const uint32_t BLACK_BTN[3] = { BTN_RIGHT, BTN_HOLD4, BTN_HOLD5 };
    DrawConfig cfg;
    DrawGame g;
    Wallet w;
    Rng r;
    long rounds = 0, wins = 0, reds = 0, card_n[52] = {0}, hands = 0, bad = 0, max_round = 0;
    int64_t start = 1000000000LL, bets = 0, collected = 0;
    double chi = 0, t0 = test_now();
    int i;

    draw_config_default(&cfg);
    cfg.bet = 1;
    cfg.deal_gap_ticks = cfg.flip_delay_ticks = cfg.result_delay_ticks = cfg.double_reveal_ticks = 0;
    cfg.double_cap_mult = 200;                      /* small, so the cap is exercised */
    draw_init(&g, &cfg, 0xFA1Aull);
    w.credits = start;
    w.denom = 1;
    rng_seed(&r, 0xC0FFEEull);

    while (rounds < target) {
        InputFrame in;
        EventQueue q;
        memset(&in, 0, sizeof in);
        switch (g.state) {
        case DS_IDLE:  in.pressed = BTN_DEAL; break;
        case DS_HOLD:  in.pressed = BTN_HOLD1 | BTN_HOLD2 | BTN_HOLD3 | BTN_HOLD4 | BTN_HOLD5 | BTN_DEAL; break;
        case DS_OFFER: in.pressed = rng_below(&r, 50) ? BTN_HOLD1 : BTN_OK; break;
        case DS_DOUBLE:
            in.pressed = rng_below(&r, 2) ? BLACK_BTN[rng_below(&r, 3)] : RED_BTN[rng_below(&r, 3)];
            break;
        default: break;
        }
        q.n = 0;
        draw_tick(&g, &in, &w, &q);
        for (i = 0; i < q.n; i++) {
            const GameEvent *ev = &q.e[i];
            switch (ev->type) {
            case EV_DRAW_CREDITS:
                if (ev->v != w.credits) bad++;
                if (ev->a == DRAW_CREDITS_BET) { bets += g.bet; hands++; }
                break;
            case EV_DRAW_COLLECT: collected += ev->v; break;
            case EV_DRAW_DOUBLE_OFFER:
                if (2LL * ev->v > 200LL * g.bet || ev->a > 5) bad++;
                break;
            case EV_DRAW_DOUBLE_CARD:
                rounds++;
                if (ev->a > max_round) max_round = ev->a;
                card_n[ev->b]++;
                if (card_suit((Card)ev->b) == SUIT_D || card_suit((Card)ev->b) == SUIT_H) reds++;
                break;
            case EV_DRAW_DOUBLE_WIN: wins++; break;
            default: break;
            }
        }
    }
    for (i = 0; i < 52; i++) {
        double ex = rounds / 52.0;
        chi += (card_n[i] - ex) * (card_n[i] - ex) / ex;
    }
    {
        double zw = (wins - rounds / 2.0) / sqrt(rounds / 4.0);
        double zr = (reds - rounds / 2.0) / sqrt(rounds / 4.0);
        double p = test_chi2_p(chi, 51);
        printf("double-up: %ld rounds in %ld hands: won %.4f %% (z %+.2f), red cards %.4f %% "
               "(z %+.2f), card chi-square %.1f on 51 df (p %.3f), max round %ld, %.1f s\n",
               rounds, hands, 100.0 * wins / rounds, zw, 100.0 * reds / rounds, zr, chi, p,
               max_round, test_now() - t0);
        CHECK(fabs(zw) < 4.5);
        CHECK(fabs(zr) < 4.5);
        CHECK(p > 1e-6);
    }
    CHECK(max_round <= 5);
    CHECK_EQ_INT(bad, 0);
    CHECK_EQ_INT(w.credits, start - bets + collected);   /* a pending meter is not credited */
}

/* ---- replay determinism -------------------------------------------------- */

static uint32_t random_buttons(Rng *r)
{
    static const uint32_t B[] = {
        BTN_HOLD1, BTN_HOLD2, BTN_HOLD3, BTN_HOLD4, BTN_HOLD5, BTN_DEAL, BTN_BET_ONE, BTN_BET_MAX,
        BTN_LEFT, BTN_RIGHT, BTN_UP, BTN_DOWN, BTN_OK, BTN_CASH_OUT, BTN_BACK
    };
    uint32_t p = 0;
    if (rng_below(r, 4) == 0) {
        p |= B[rng_below(r, sizeof B / sizeof B[0])];
        if (rng_below(r, 8) == 0) p |= B[rng_below(r, sizeof B / sizeof B[0])];
    }
    return p;
}

typedef struct { uint64_t hash; int64_t credits; long events; DrawGame g; } RunResult;

static void fnv(uint64_t *h, const void *p, size_t n)
{
    const uint8_t *b = p;
    size_t i;
    for (i = 0; i < n; i++) { *h ^= b[i]; *h *= 1099511628211ull; }
}

/* Plays `frames` from the array (or from a replay reader when rd != NULL). */
static void run_frames(uint64_t seed, const InputFrame *frames, long nframes, ReplayReader *rd, RunResult *out)
{
    DrawConfig cfg;
    Wallet w = { 100000, 1 };
    long i;
    draw_config_default(&cfg);
    draw_init(&out->g, &cfg, seed);
    out->hash = 1469598103934665603ull;
    out->events = 0;
    for (i = 0;; i++) {
        InputFrame in;
        EventQueue q;
        int k;
        if (rd) { if (replay_reader_frame(rd, &in) != 1) break; }
        else { if (i >= nframes) break; in = frames[i]; }
        q.n = 0;
        draw_tick(&out->g, &in, &w, &q);
        for (k = 0; k < q.n; k++) {
            fnv(&out->hash, &i, sizeof i);
            fnv(&out->hash, &q.e[k].type, sizeof q.e[k].type);
            fnv(&out->hash, &q.e[k].a, sizeof q.e[k].a);
            fnv(&out->hash, &q.e[k].b, sizeof q.e[k].b);
            fnv(&out->hash, &q.e[k].v, sizeof q.e[k].v);
        }
        out->events += q.n;
    }
    out->credits = w.credits;
}

static void replay_determinism(void)
{
    enum { N = 300000 };
    static InputFrame frames[N];
    static RunResult a, b, c, d;
    ReplayWriter wr;
    ReplayReader rd;
    Rng r;
    uint32_t prev = 0;
    long i;
    FILE *f;

    rng_seed(&r, 0xD1CE);
    for (i = 0; i < N; i++) {
        memset(&frames[i], 0, sizeof frames[i]);
        frames[i].down = random_buttons(&r);
        input_set_pressed(&frames[i], prev);
        prev = frames[i].down;
    }
    run_frames(0xABCDEFull, frames, N, NULL, &a);
    run_frames(0xABCDEFull, frames, N, NULL, &b);
    run_frames(0xABCDF0ull, frames, N, NULL, &c);
    CHECK(a.hash == b.hash);
    CHECK_EQ_INT(a.credits, b.credits);
    CHECK(memcmp(&a.g, &b.g, sizeof a.g) == 0);
    CHECK(a.hash != c.hash);
    CHECK(a.g.hand_no > 1000);

    /* Through the engine's input log and back. */
    f = tmpfile();
    CHECK(f != NULL);
    if (f) {
        CHECK(replay_writer_begin(&wr, f, 0xABCDEFull, "draw", "cfg=default", 11) == 0);
        for (i = 0; i < N; i++) replay_writer_frame(&wr, &frames[i]);
        CHECK(replay_writer_close(&wr) == 0);
        rewind(f);
        CHECK(replay_reader_begin(&rd, f) == 0);
        CHECK(rd.seed == 0xABCDEFull);
        run_frames(rd.seed, NULL, 0, &rd, &d);
        CHECK(d.hash == a.hash);
        CHECK_EQ_INT(d.credits, a.credits);
        CHECK(memcmp(&d.g, &a.g, sizeof a.g) == 0);
        fclose(f);
    }
    printf("replay: %d frames, %u hands, %ld events, credits %" PRId64 ", identical twice and "
           "via the input log\n", N, a.g.hand_no, a.events, a.credits);
}

int main(void)
{
    draw_rules_init();
    scripted_hand();
    bets_and_denials();
    double_up_scripted();
    replay_determinism();
    double_up_fairness();
    return test_finish("draw_game");
}
