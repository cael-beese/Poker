/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* selftest.c - see selftest.h. */
#include "platform/selftest.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "ai/ai.h"
#include "engine/card.h"
#include "engine/deck.h"
#include "engine/eval.h"
#include "engine/rng.h"
#include "games/draw/draw_hint.h"
#include "games/draw/draw_rules.h"
#include "games/holdem/holdem.h"

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

void selftest_add(SelfTestReport *r, const char *name, int pass, double ms, const char *fmt, ...)
{
    if (!pass) r->failures++;
    if (r->n >= SELFTEST_MAX) return;
    SelfTestItem *it = &r->item[r->n++];
    snprintf(it->name, sizeof it->name, "%s", name);
    it->pass = pass;
    it->ms = ms;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(it->detail, sizeof it->detail, fmt, ap);
    va_end(ap);
}

/* "AsKsQsJsTs" -> five cards; 0 ok. */
static int hand(const char *s, Card *out, int n)
{
    for (int i = 0; i < n; i++) {
        char two[3] = { s[2 * i], s[2 * i + 1], 0 };
        if (card_parse(two, &out[i]) != 0) return -1;
    }
    return 0;
}

static void check_eval(SelfTestReport *r)
{
    double t0 = now_ms();
    eval_init();
    static const struct { const char *h; int cat; } cases[] = {
        { "AsKsQsJsTs", HC_STRAIGHT_FLUSH }, { "AsAhAdAc2s", HC_QUADS }, { "KsKhKd2c2s", HC_FULL_HOUSE },
        { "2h7h9hJhKh", HC_FLUSH },          { "Ac2d3h4s5c", HC_STRAIGHT }, { "9s9h9d4c2s", HC_TRIPS },
        { "9s9h4d4c2s", HC_TWO_PAIR },       { "JsJd2c5h9s", HC_PAIR },  { "7s5d4c3h2s", HC_HIGH_CARD },
    };
    int bad = 0;
    Card c[7];
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++)
        if (hand(cases[i].h, c, 5) != 0 || eval_category(eval5(c)) != cases[i].cat) bad++;
    hand("AsKsQsJsTs", c, 5);
    int royal = eval5(c);
    hand("7s5d4c3h2s", c, 5);
    int worst = eval5(c);
    hand("2c3dAsKsQsJsTs", c, 7);
    int best7 = eval7(c);
    int ok = bad == 0 && royal == 1 && worst == 7462 && best7 == 1;
    selftest_add(r, "hand evaluator", ok, now_ms() - t0, "%d/9 categories, royal=%d, worst=%d, best-of-7=%d",
                 9 - bad, royal, worst, best7);
}

static void check_classify(SelfTestReport *r)
{
    double t0 = now_ms();
    draw_rules_init();
    static const struct { int v; const char *h; int cat; } cases[] = {
        { DRAW_JOB, "JsJd2c5h9s", DC_JACKS_OR_BETTER }, { DRAW_JOB, "TsTd2c5h9s", DC_NONE },
        { DRAW_JOB, "AsKsQsJsTs", DC_ROYAL_FLUSH },     { DRAW_BONUS, "AsAhAdAc9s", DC_FOUR_ACES },
        { DRAW_BONUS, "3s3h3d3c9s", DC_FOUR_2_4 },      { DRAW_BONUS, "8s8h8d8c9s", DC_FOUR_5_K },
        { DRAW_DEUCES, "2c2d2h2s9s", DC_FOUR_DEUCES },  { DRAW_DEUCES, "2cKsQsJsTs", DC_WILD_ROYAL },
        { DRAW_DEUCES, "2c9s9h9d9c", DC_FIVE_KIND },    { DRAW_DEUCES, "9s9h4d4c2s", DC_FULL_HOUSE },
    };
    int bad = 0, n = (int)(sizeof cases / sizeof cases[0]);
    Card c[5];
    for (int i = 0; i < n; i++)
        if (hand(cases[i].h, c, 5) != 0 || draw_classify(cases[i].v, c) != cases[i].cat) bad++;
    int pay_ok = draw_pay(DRAW_JOB, DC_ROYAL_FLUSH, 5) == 4000 && draw_pay(DRAW_JOB, DC_ROYAL_FLUSH, 4) == 1000 &&
                 draw_pay(DRAW_JOB, DC_FULL_HOUSE, 1) == 9 && draw_pay(DRAW_JOB, DC_FLUSH, 1) == 6;
    selftest_add(r, "pay categories", bad == 0 && pay_ok, now_ms() - t0, "%d/%d hands, 9/6 paytable %s", n - bad, n,
                 pay_ok ? "ok" : "WRONG");
}

static void check_returns(SelfTestReport *r)
{
    static const double expect[DRAW_VARIANTS] = { 0.995439043695, 0.991659731872, 1.007619612039 };
    int bad = 0;
    char buf[100] = "";
    size_t n = 0;
    for (int v = 0; v < DRAW_VARIANTS; v++) {
        const DrawPaytable *pt = draw_paytable(v);
        double got = pt ? pt->optimal_return : 0.0;
        if (fabs(got - expect[v]) > 5e-6) bad++;
        int w = snprintf(buf + n, sizeof buf - n, "%s%s %.4f%%", v ? ", " : "", pt ? pt->short_name : "?", got * 100.0);
        if (w > 0 && (size_t)w < sizeof buf - n) n += (size_t)w;
    }
    selftest_add(r, "paytable returns", bad == 0, 0.0, "%s", buf);
}

/* The exact-EV hint: a dealt royal must be held for exactly 4000, and the
 * fast method must agree with dealing out every replacement set. */
static void check_ev(SelfTestReport *r)
{
    static DrawHint a, b;       /* large; keep them off the worker's stack */
    Card c[5];
    double t0 = now_ms();
    hand("AsKsQsJsTs", c, 5);
    int ok = draw_hint_compute(DRAW_JOB, 5, c, &a) == 0 && a.best == 31 && a.best_ev == 4000.0;
    double t1 = now_ms();
    selftest_add(r, "hint: dealt royal", ok, t1 - t0, "best hold %02x, EV %.1f (want 1f, 4000)", a.best, a.best_ev);

    static const struct { int v; const char *h; } hands[] = {
        { DRAW_JOB, "AsKsQsJs9d" }, { DRAW_DEUCES, "2c7h8h9dKs" },
    };
    int agree = 1;
    double fast = 0, brute = 0;
    for (size_t i = 0; i < sizeof hands / sizeof hands[0]; i++) {
        hand(hands[i].h, c, 5);
        double s0 = now_ms();
        int ra = draw_hint_compute(hands[i].v, 5, c, &a);
        double s1 = now_ms();
        int rb = draw_hint_compute_bruteforce(hands[i].v, 5, c, &b);
        double s2 = now_ms();
        fast += s1 - s0;
        brute += s2 - s1;
        if (ra != 0 || rb != 0 || a.best != b.best) agree = 0;
        for (int m = 0; m < 32 && agree; m++)
            if (a.num[m] != b.num[m] || a.den[m] != b.den[m]) agree = 0;
    }
    selftest_add(r, "hint: exact EV", agree, fast + brute, "fast = brute force on 2 hands x 32 holds (%.1f / %.0f ms)",
                 fast, brute);
}

static void check_shuffle(SelfTestReport *r)
{
    double t0 = now_ms();
    Rng a, b;
    rng_seed(&a, 12345);
    rng_seed(&b, 12345);
    int same = 1;
    for (int i = 0; i < 1000; i++) if (rng_next(&a) != rng_next(&b)) same = 0;
    Deck d;
    deck_init(&d);
    deck_shuffle(&d, &a);
    uint64_t seen = 0;
    for (int i = 0; i < 52; i++) seen |= UINT64_C(1) << d.c[i];
    int perm = seen == (UINT64_C(1) << 52) - 1;
    /* Position counts over many shuffles: every card should land in every
       position about equally (a coarse chi-square, 52x52 cells). */
    static uint32_t cnt[52][52];
    memset(cnt, 0, sizeof cnt);
    const int N = 20000;
    for (int k = 0; k < N; k++) {
        deck_init(&d);
        deck_shuffle(&d, &a);
        for (int i = 0; i < 52; i++) cnt[d.c[i]][i]++;
    }
    double e = N / 52.0, chi = 0;
    for (int i = 0; i < 52; i++)
        for (int j = 0; j < 52; j++) chi += (cnt[i][j] - e) * (cnt[i][j] - e) / e;
    /* 2601 degrees of freedom: mean 2601, sd ~72; 3000 is > 5 sd. */
    uint64_t s1 = rng_os_seed(), s2 = rng_os_seed();
    int ok = same && perm && chi < 3000 && s1 != s2;
    selftest_add(r, "shuffle and RNG", ok, now_ms() - t0, "replay %s, permutation %s, chi2 %.0f (df 2601), OS seed %s",
                 same ? "ok" : "BAD", perm ? "ok" : "BAD", chi, s1 != s2 ? "ok" : "REPEATS");
}

static void check_pots(SelfTestReport *r)
{
    double t0 = now_ms();
    int64_t committed[HOLDEM_SEATS] = { 100, 200, 300, 300, 50, 0 };
    uint8_t live[HOLDEM_SEATS] = { 1, 1, 1, 1, 0, 0 };
    HoldemPot pots[HOLDEM_MAX_POTS];
    int n = holdem_build_pots(committed, live, pots);
    int64_t total = 0;
    for (int i = 0; i < n; i++) total += pots[i].amount;
    int64_t share[HOLDEM_SEATS] = { 0 };
    holdem_split_pot(5, 0x05, 3, share);     /* seats 0 and 2 split 5, button 3 */
    int ok = n == 3 && total == 950 && pots[0].amount == 450 && share[0] + share[2] == 5 &&
             (share[0] == 3 || share[2] == 3);
    selftest_add(r, "hold'em side pots", ok, now_ms() - t0, "%d pots, %lld chips (want 3, 950), odd chip split %lld/%lld",
                 n, (long long)total, (long long)share[0], (long long)share[2]);
}

static void check_ai(SelfTestReport *r)
{
    double t0 = now_ms();
    Card aa[2];
    hand("AsAh", aa, 2);
    Rng g;
    rng_seed(&g, 7);
    double eq = ai_equity(aa, NULL, 0, 1, NULL, 20000, &g);
    int ok = eq > 0.83 && eq < 0.88;
    selftest_add(r, "AI equity", ok, now_ms() - t0, "AA vs one random hand %.3f (exact 0.852)", eq);
}

void selftest_run_logic(SelfTestReport *r)
{
    check_eval(r);
    check_classify(r);
    check_returns(r);
    check_ev(r);
    check_shuffle(r);
    check_pots(r);
    check_ai(r);
}
