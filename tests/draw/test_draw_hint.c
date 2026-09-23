/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* The live strategy hint.

   1. Fast method == brute force, as exact integer outcome counts for all 32
      holds, on hand-picked edge cases and on random hands per variant
      (BPL_DRAW_BRUTE_HANDS, default 60 per variant).
   2. Hint agrees with the precomputed tables on 20,000 random hands per
      variant (BPL_DRAW_HINT_HANDS), at 5 coins and at 1 coin: the table's
      hold has exactly the hint's best EV. Mask-identical agreement is
      reported too (exact ties between different holds are legitimate).
   3. The worker-thread API returns the same result as the synchronous one.
   4. Known values: a dealt royal holds all five with EV 4000 at 5 coins;
      four deuces hold the deuces; discarding everything in JoB is
      1,533,939 ways.
   5. The hint only reads the five cards: computing it leaves a game's
      state byte-for-byte unchanged. */

#include "games/draw/draw_game.h"
#include "games/draw/draw_hint.h"
#include "games/draw/draw_strategy.h"
#include "test_util.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef BPL_SOURCE_DIR
#define BPL_SOURCE_DIR "."
#endif

static DrawStrategyTable g_tab[DRAW_VARIANTS];

static long env_long(const char *name, long def)
{
    const char *s = getenv(name);
    return (s && *s) ? atol(s) : def;
}

static void random_hand(Rng *r, Card h[5])
{
    Deck d;
    int i;
    deck_init(&d);
    for (i = 0; i < 5; i++) h[i] = deck_draw_random(&d, r);
}

static int parse5(const char *s, Card h[5])
{
    return cards_parse(s, h, 5) == 5 ? 0 : -1;
}

static void brute_vs_fast(void)
{
    static const char *const EDGE[] = {
        "As Ks Qs Js Ts", "2c 2d 2h 2s 9c", "2c 2d 2h 5s 9s", "2c 3c 4c 5c 6c",
        "Ah Kh Qh Jh 9h", "Tc Jc Qc Kc 2d", "2s 2h Ac Kc Qc", "3d 4d 5d 6d 8d",
        "7c 7d 7h 7s 2c", "Ac Ad Ah As Kc", "Jc Jd 3h 8s 9c", "As 2s 3s 4s 9h",
        "Ah 2d 3c 4s 5h", "9c Tc Jc Qd Kd", "2c 2d Ah Ad Kc", "Td Jd Qd Kd Ad"
    };
    long nrand = env_long("BPL_DRAW_BRUTE_HANDS", 60);
    Rng r;
    Card h[5];
    DrawHint a, b;
    int v, i, bad = 0, n = 0;
    double t0 = test_now();
    rng_seed(&r, 0xB0B0);
    for (v = 0; v < DRAW_VARIANTS; v++) {
        for (i = 0; i < (int)(sizeof EDGE / sizeof EDGE[0]) + nrand; i++) {
            if (i < (int)(sizeof EDGE / sizeof EDGE[0])) CHECK(parse5(EDGE[i], h) == 0);
            else random_hand(&r, h);
            CHECK(draw_hint_compute(v, 5, h, &a) == 0);
            CHECK(draw_hint_compute_bruteforce(v, 5, h, &b) == 0);
            n++;
            if (memcmp(a.ways, b.ways, sizeof a.ways) != 0 || a.best != b.best) {
                char s[3];
                int k;
                bad++;
                fprintf(stderr, "%s fast/brute mismatch:", draw_variant_name(v));
                for (k = 0; k < 5; k++) fprintf(stderr, " %s", card_str(h[k], s));
                fprintf(stderr, "\n");
            }
        }
    }
    CHECK_EQ_INT(bad, 0);
    printf("fast == brute force on %d hands (%.1f s)\n", n, test_now() - t0);
}

static void known_values(void)
{
    Card h[5];
    DrawHint d;
    int m, s;
    CHECK(parse5("Ts Js Qs Ks As", h) == 0);
    draw_hint_compute(DRAW_JOB, 5, h, &d);
    CHECK_EQ_INT(d.best, 31);
    CHECK(d.best_ev == 4000.0);
    draw_hint_compute(DRAW_JOB, 1, h, &d);
    CHECK(d.best_ev == 250.0);
    for (m = 0, s = 0; m < DC_COUNT; m++) s += (int)d.ways[0][m];
    CHECK_EQ_INT(s, 1533939);
    CHECK_EQ_INT(d.den[0], 1533939);

    CHECK(parse5("2c 9h 2d 2h 2s", h) == 0);
    draw_hint_compute(DRAW_DEUCES, 5, h, &d);
    CHECK(d.best == 0x1D || d.best == 0x1F);          /* the deuces, with or without the 9 */
    CHECK(d.best_ev == 1000.0);
    CHECK(draw_hint_compute(DRAW_JOB, 5, (const Card[5]){ 1, 1, 2, 3, 4 }, &d) == -1);
    CHECK(draw_hint_compute(DRAW_JOB, 6, h, &d) == -1);
}

typedef struct {
    int variant;
    long n;
    atomic_long next, ev_equal, mask_equal, done;
    uint64_t seed;
} AgreeJob;

static void *agree_worker(void *arg)
{
    AgreeJob *j = arg;
    for (;;) {
        long i = atomic_fetch_add(&j->next, 1);
        Rng r;
        Card h[5];
        DrawHint hint;
        int bet, ok = 1, same = 1;
        if (i >= j->n) break;
        rng_seed(&r, j->seed + (uint64_t)i);
        random_hand(&r, h);
        for (bet = 1; bet <= 5; bet += 4) {
            uint8_t m = draw_strategy_hold(&g_tab[j->variant], bet, h);
            draw_hint_compute(j->variant, bet, h, &hint);
            if (draw_hint_cmp(&hint, m, hint.best) != 0) ok = 0;
            if (m != hint.best) same = 0;
        }
        if (ok) atomic_fetch_add(&j->ev_equal, 1);
        if (same) atomic_fetch_add(&j->mask_equal, 1);
        atomic_fetch_add(&j->done, 1);
    }
    return NULL;
}

static void hint_vs_table(void)
{
    long n = env_long("BPL_DRAW_HINT_HANDS", 20000);
    int nthreads = (int)sysconf(_SC_NPROCESSORS_ONLN), v, i;
    if (nthreads < 1) nthreads = 1;
    if (nthreads > 64) nthreads = 64;
    for (v = 0; v < DRAW_VARIANTS; v++) {
        char path[1024];
        AgreeJob j;
        pthread_t th[64];
        double t0 = test_now();
        snprintf(path, sizeof path, "%s/assets/strategy/%s", BPL_SOURCE_DIR, draw_strategy_file(v));
        if (draw_strategy_load(&g_tab[v], v, path) != 0) {
            fprintf(stderr, "cannot load %s\n", path);
            CHECK(0);
            continue;
        }
        memset(&j, 0, sizeof j);
        j.variant = v;
        j.n = n;
        j.seed = 0x5EED0000ull + (uint64_t)v * 1000003ull;
        for (i = 0; i < nthreads; i++) pthread_create(&th[i], NULL, agree_worker, &j);
        for (i = 0; i < nthreads; i++) pthread_join(th[i], NULL);
        CHECK_EQ_INT(atomic_load(&j.done), n);
        CHECK_EQ_INT(atomic_load(&j.ev_equal), n);
        printf("%s: hint vs table on %ld hands at 5 and 1 coins: %ld EV-equal, %ld same mask (%.1f s)\n",
               draw_variant_name(v), n, atomic_load(&j.ev_equal), atomic_load(&j.mask_equal),
               test_now() - t0);
    }
}

static void task_api(void)
{
    DrawHintTask t;
    DrawHint sync;
    const DrawHint *res;
    Card h[5];
    int spins = 0;
    CHECK(parse5("Jh Qh 4c 9h Kd", h) == 0);
    draw_hint_compute(DRAW_BONUS, 3, h, &sync);
    draw_hint_task_init(&t);
    CHECK(draw_hint_task_wait(&t) == NULL);           /* never started */
    CHECK(draw_hint_task_start(&t, DRAW_BONUS, 3, h) == 0);
    while (!draw_hint_task_ready(&t) && spins < 100000000) spins++;
    res = draw_hint_task_wait(&t);
    CHECK(res != NULL);
    if (res) CHECK(memcmp(res, &sync, sizeof sync) == 0);
    /* Restarting before waiting is allowed: start joins the old thread. */
    CHECK(draw_hint_task_start(&t, DRAW_DEUCES, 5, h) == 0);
    CHECK(draw_hint_task_start(&t, DRAW_JOB, 5, h) == 0);
    res = draw_hint_task_wait(&t);
    draw_hint_compute(DRAW_JOB, 5, h, &sync);
    CHECK(res && memcmp(res, &sync, sizeof sync) == 0);
}

static void read_only(void)
{
    DrawConfig cfg;
    DrawGame g, before;
    Wallet w = { 100, 1 };
    InputFrame in;
    EventQueue q;
    DrawHint h;
    int i;
    draw_config_default(&cfg);
    draw_init(&g, &cfg, 99);
    memset(&in, 0, sizeof in);
    in.pressed = BTN_DEAL;
    for (i = 0; i < 200 && g.state != DS_HOLD; i++) {
        q.n = 0;
        draw_tick(&g, &in, &w, &q);
        in.pressed = 0;
    }
    CHECK_EQ_INT(g.state, DS_HOLD);
    before = g;
    draw_hint_compute(g.variant, g.bet, g.cards, &h);
    CHECK(memcmp(&before, &g, sizeof g) == 0);
}

int main(void)
{
    draw_rules_init();
    known_values();
    task_api();
    read_only();
    brute_vs_fast();
    hint_vs_table();
    return test_finish("draw_hint");
}
