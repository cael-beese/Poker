/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* The paytable returns, exactly.

   1. Classes: every one of the 2,598,960 hands maps to a class, each class
      is hit exactly weight(i) times, and a class's representative maps to
      itself.
   2. The shipped tables (assets/strategy/<variant>.bin) load, and their stored
      exact returns are 99.5439 % / 99.1660 % / 100.7620 % to 4 decimals of
      a percent at 5 coins.
   3. The return is recomputed here from the stored holds alone: for every
      class, the exact EV of the stored hold (draw_hold_ways), weighted,
      summed as integers. It must equal the stored numerator exactly, for
      both sections (5 coins and 1-4 coins).
   4. The stored holds are optimal: for a spread sample of classes (every
      class with BPL_DRAW_FULL=1) the full 32-hold hint is computed and the
      stored hold's EV must equal the best EV exactly. */

#include "games/draw/draw_hint.h"
#include "games/draw/draw_rules.h"
#include "games/draw/draw_strategy.h"
#include "test_util.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef BPL_SOURCE_DIR
#define BPL_SOURCE_DIR "."
#endif


static DrawStrategyTable g_tab[DRAW_VARIANTS];

typedef struct {
    int         variant, stride;
    atomic_int  next;
    atomic_long bad_opt, checked_opt;
    int64_t     ret[DRAW_SECTS];
    pthread_mutex_t lock;
} Job;

static void *recompute(void *arg)
{
    Job *j = arg;
    const DrawStrategyTable *t = &g_tab[j->variant];
    int pay[DRAW_SECTS][DC_COUNT], s, c;
    int64_t ret[DRAW_SECTS] = { 0, 0 };
    draw_pay_vector(j->variant, 5, pay[DRAW_SECT_MAX]);
    draw_pay_vector(j->variant, 1, pay[DRAW_SECT_LOW]);
    for (;;) {
        int idx = atomic_fetch_add(&j->next, 1);
        Card hand[5];
        if (idx >= DRAW_CLASSES) break;
        draw_class_hand(idx, hand);
        for (s = 0; s < DRAW_SECTS; s++) {
            uint32_t ways[DC_COUNT];
            unsigned m = t->hold[s][idx];
            int32_t den = draw_hold_ways(j->variant, hand, m, ways);
            int64_t num = 0;
            for (c = 1; c < DC_COUNT; c++) num += (int64_t)ways[c] * pay[s][c];
            ret[s] += (int64_t)draw_class_weight(idx) * num * (DRAW_EV_LCM / den);
        }
        if (j->stride && idx % j->stride == 0) {
            /* Optimality: the stored hold's EV equals the best of all 32. */
            DrawHint h1, h5;
            draw_hint_compute(j->variant, 5, hand, &h5);
            draw_hint_compute(j->variant, 1, hand, &h1);
            atomic_fetch_add(&j->checked_opt, 1);
            if (draw_hint_cmp(&h5, t->hold[DRAW_SECT_MAX][idx], h5.best) != 0
                || draw_hint_cmp(&h1, t->hold[DRAW_SECT_LOW][idx], h1.best) != 0)
                atomic_fetch_add(&j->bad_opt, 1);
        }
    }
    pthread_mutex_lock(&j->lock);
    for (s = 0; s < DRAW_SECTS; s++) j->ret[s] += ret[s];
    pthread_mutex_unlock(&j->lock);
    return NULL;
}

static void check_classes(void)
{
    static uint8_t hits[DRAW_CLASSES];
    Card h[5];
    uint8_t pos[5];
    int a, b, c, d, e, i, bad = 0;
    long total = 0;
    memset(hits, 0, sizeof hits);
    for (a = 0; a < 52; a++)
    for (b = a + 1; b < 52; b++)
    for (c = b + 1; c < 52; c++)
    for (d = c + 1; d < 52; d++)
    for (e = d + 1; e < 52; e++) {
        int idx;
        h[0] = (Card)a; h[1] = (Card)b; h[2] = (Card)c; h[3] = (Card)d; h[4] = (Card)e;
        idx = draw_class_of(h, pos);
        if (idx < 0) { bad++; continue; }
        hits[idx]++;
        total++;
    }
    CHECK_EQ_INT(bad, 0);
    CHECK_EQ_INT(total, DRAW_HANDS);
    for (i = 0; i < DRAW_CLASSES; i++) {
        Card rep[5];
        int k;
        if (hits[i] != draw_class_weight(i)) bad++;
        draw_class_hand(i, rep);
        if (draw_class_of(rep, pos) != i) bad++;
        for (k = 0; k < 5; k++) if (pos[k] != k) bad++;   /* representative is canonical order */
    }
    CHECK_EQ_INT(bad, 0);
}

static void print_pct(const char *label, int64_t num, int64_t den)
{
    __int128 n = (__int128)num * 100;
    int64_t ip = (int64_t)(n / den);
    __int128 rem = n % den;
    int i;
    printf("%s%" PRId64 ".", label, ip);
    for (i = 0; i < 10; i++) { rem *= 10; printf("%d", (int)(rem / den)); rem %= den; }
    printf(" %%\n");
}

int main(void)
{
    /* The published figures, and the exact numerators this code derives
       (over 2,598,960 * 7,669,695 = 19,933,230,517,200). */
    static const double EXPECT[DRAW_VARIANTS] = { 99.5439, 99.1660, 100.7620 };
    const char *full = getenv("BPL_DRAW_FULL");
    int nthreads = (int)sysconf(_SC_NPROCESSORS_ONLN), v, i;
    int stride = (full && *full && *full != '0') ? 1 : 67;   /* ~2,000 classes per variant */
    double t0 = test_now();

    if (nthreads < 1) nthreads = 1;
    if (nthreads > 64) nthreads = 64;
    draw_rules_init();
    draw_classes_init();
    check_classes();
    printf("classes: %d, every hand mapped (%.1f s)\n", DRAW_CLASSES, test_now() - t0);

    for (v = 0; v < DRAW_VARIANTS; v++) {
        char path[1024];
        DrawStrategyTable *t = &g_tab[v];
        Job job;
        pthread_t th[64];
        double pct, t1 = test_now();
        int s;
        snprintf(path, sizeof path, "%s/assets/strategy/%s", BPL_SOURCE_DIR, draw_strategy_file(v));
        if (draw_strategy_load(t, v, path) != 0) {
            fprintf(stderr, "cannot load %s\n", path);
            CHECK(0);
            continue;
        }
        for (s = 0; s < DRAW_SECTS; s++)
            CHECK_EQ_INT(t->ret_den[s], (int64_t)DRAW_HANDS * DRAW_EV_LCM);
        pct = 100.0 * (double)t->ret_num[DRAW_SECT_MAX] / (double)t->ret_den[DRAW_SECT_MAX];
        CHECK(fabs(pct - EXPECT[v]) < 0.00005);
        CHECK(fabs(draw_paytable(v)->optimal_return * 100.0 - EXPECT[v]) < 1e-9);

        memset(&job, 0, sizeof job);
        job.variant = v;
        job.stride = stride;
        atomic_init(&job.next, 0);
        atomic_init(&job.bad_opt, 0);
        atomic_init(&job.checked_opt, 0);
        pthread_mutex_init(&job.lock, NULL);
        for (i = 0; i < nthreads; i++) pthread_create(&th[i], NULL, recompute, &job);
        for (i = 0; i < nthreads; i++) pthread_join(th[i], NULL);
        pthread_mutex_destroy(&job.lock);

        for (s = 0; s < DRAW_SECTS; s++) CHECK_EQ_INT(job.ret[s], t->ret_num[s]);
        CHECK_EQ_INT(atomic_load(&job.bad_opt), 0);
        printf("%s: ", draw_variant_name(v));
        print_pct("return at 5 coins ", job.ret[DRAW_SECT_MAX], t->ret_den[DRAW_SECT_MAX]);
        printf("    ");
        print_pct("return at 1-4 coins ", job.ret[DRAW_SECT_LOW], t->ret_den[DRAW_SECT_LOW]);
        printf("    recomputed from stored holds = stored numerators: %s; optimality checked on "
               "%ld classes, %ld not optimal (%.1f s)\n",
               job.ret[0] == t->ret_num[0] && job.ret[1] == t->ret_num[1] ? "yes" : "NO",
               atomic_load(&job.checked_opt), atomic_load(&job.bad_opt), test_now() - t1);
    }
    return test_finish("draw_return");
}
