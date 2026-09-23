/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* Strategy table generator: the optimal hold for every suit-canonical
   starting hand (134,459 classes), per variant, at 5 coins and at 1-4 coins,
   with the exact class-weighted return, written to
   assets/strategy/<variant>.bin (see games/draw/draw_strategy.h).

       draw_strategy_gen [--out DIR] [--threads N] [--variant job|bonus|deuces]
                         [--brute N | --brute-all] [--dry-run]

   Each class is solved by the exact hold-EV method of the live hint
   (draw_hint_compute). --brute N also solves N classes (spread evenly over
   the list) by brute force and requires identical outcome counts for all 32
   holds; --brute-all does every class (slow: about 40 ms per class).

   The return is exact: each hold's EV is num / C(47, k), so scaling by
   L = lcm C(47, 0..5) = 7,669,695 makes every EV an integer, and the sum of
   weight * EV * L over the classes, over 2,598,960 * L, is the return as a
   fraction with no rounding anywhere. */

#include "games/draw/draw_hint.h"
#include "games/draw/draw_rules.h"
#include "games/draw/draw_strategy.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static const int32_t C47[6] = { 1, 47, 1081, 16215, 178365, 1533939 };

typedef struct {
    int      variant;
    int      brute_every;           /* 0 = none, 1 = all, k = every k-th class */
    atomic_int next;
    DrawStrategyTable *table;
    /* per-thread results, merged after the join */
} Job;

typedef struct {
    Job     *job;
    int64_t  ret[DRAW_SECTS];                 /* sum of weight * EV * L (per coin) */
    int64_t  cat[DRAW_SECTS][DC_COUNT];        /* weight * ways * (L / den)        */
    long     brute_checked, brute_bad;
} Worker;

static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* Best hold for a per-coin pay vector; ties to the lower mask (as the hint). */
static unsigned best_hold(const DrawHint *h, const int pay[DC_COUNT], int64_t *num_out)
{
    int64_t num[32];
    unsigned m, best = 0;
    int c;
    for (m = 0; m < 32; m++) {
        num[m] = 0;
        for (c = 1; c < DC_COUNT; c++) num[m] += (int64_t)h->ways[m][c] * pay[c];
    }
    for (m = 1; m < 32; m++) {
        int64_t l = num[m] * C47[5 - __builtin_popcount(best)];
        int64_t r = num[best] * C47[5 - __builtin_popcount(m)];
        if (l > r) best = m;
    }
    *num_out = num[best];
    return best;
}

static void *worker_main(void *arg)
{
    Worker *w = arg;
    Job *job = w->job;
    int pay[DRAW_SECTS][DC_COUNT];
    draw_pay_vector(job->variant, 5, pay[DRAW_SECT_MAX]);
    draw_pay_vector(job->variant, 1, pay[DRAW_SECT_LOW]);
    for (;;) {
        int idx = atomic_fetch_add(&job->next, 1), s, c;
        Card hand[5];
        DrawHint h;
        if (idx >= DRAW_CLASSES) break;
        draw_class_hand(idx, hand);
        draw_hint_compute(job->variant, 5, hand, &h);
        for (s = 0; s < DRAW_SECTS; s++) {
            int64_t num;
            unsigned b = best_hold(&h, pay[s], &num);
            int64_t scale = DRAW_EV_LCM / C47[5 - __builtin_popcount(b)];
            int wt = draw_class_weight(idx);
            job->table->hold[s][idx] = (uint8_t)b;
            w->ret[s] += (int64_t)wt * num * scale;
            for (c = 0; c < DC_COUNT; c++) w->cat[s][c] += (int64_t)wt * h.ways[b][c] * scale;
        }
        if (job->brute_every && idx % job->brute_every == 0) {
            DrawHint bf;
            draw_hint_compute_bruteforce(job->variant, 5, hand, &bf);
            w->brute_checked++;
            if (memcmp(bf.ways, h.ways, sizeof h.ways) != 0) {
                char a[3];
                int i;
                w->brute_bad++;
                fprintf(stderr, "MISMATCH class %d:", idx);
                for (i = 0; i < 5; i++) fprintf(stderr, " %s", card_str(hand[i], a));
                fprintf(stderr, "\n");
            }
        }
    }
    return NULL;
}

/* num / den as a percentage with `digits` decimals, exactly (truncated). */
static void print_pct(int64_t num, int64_t den, int digits)
{
    __int128 n = (__int128)num * 100;
    int64_t ip = (int64_t)(n / den);
    __int128 rem = n % den;
    int i;
    printf("%" PRId64 ".", ip);
    for (i = 0; i < digits; i++) {
        rem *= 10;
        printf("%d", (int)(rem / den));
        rem %= den;
    }
}

static int parse_variant(const char *s)
{
    if (!strcmp(s, "job")) return DRAW_JOB;
    if (!strcmp(s, "bonus")) return DRAW_BONUS;
    if (!strcmp(s, "deuces")) return DRAW_DEUCES;
    return -1;
}

int main(int argc, char **argv)
{
    const char *outdir = "assets/strategy";
    int threads = (int)sysconf(_SC_NPROCESSORS_ONLN), only = -1, brute_n = 0, brute_all = 0, dry = 0;
    int v, i, fails = 0;
    double t_all = now();

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--out") && i + 1 < argc) outdir = argv[++i];
        else if (!strcmp(argv[i], "--threads") && i + 1 < argc) threads = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--variant") && i + 1 < argc) {
            only = parse_variant(argv[++i]);
            if (only < 0) { fprintf(stderr, "unknown variant %s\n", argv[i]); return 2; }
        }
        else if (!strcmp(argv[i], "--brute") && i + 1 < argc) brute_n = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--brute-all")) brute_all = 1;
        else if (!strcmp(argv[i], "--dry-run")) dry = 1;
        else {
            fprintf(stderr, "usage: %s [--out DIR] [--threads N] [--variant job|bonus|deuces] "
                            "[--brute N | --brute-all] [--dry-run]\n", argv[0]);
            return 2;
        }
    }
    if (threads < 1) threads = 1;
    if (threads > 256) threads = 256;
    draw_rules_init();
    draw_classes_init();
    if (!dry) mkdir(outdir, 0755);

    for (v = 0; v < DRAW_VARIANTS; v++) {
        static DrawStrategyTable table;
        Job job;
        Worker *ws = calloc((size_t)threads, sizeof *ws);
        pthread_t *th = calloc((size_t)threads, sizeof *th);
        const DrawPaytable *pt = draw_paytable(v);
        int64_t den = (int64_t)DRAW_HANDS * DRAW_EV_LCM;
        int s, c;
        long checked = 0, bad = 0;
        double t0 = now(), t1;
        if (only >= 0 && v != only) { free(ws); free(th); continue; }
        if (!ws || !th) return 1;
        memset(&table, 0, sizeof table);
        table.variant = v;
        job.variant = v;
        job.brute_every = brute_all ? 1 : brute_n > 0 ? (DRAW_CLASSES + brute_n - 1) / brute_n : 0;
        atomic_init(&job.next, 0);
        job.table = &table;
        for (i = 0; i < threads; i++) {
            ws[i].job = &job;
            pthread_create(&th[i], NULL, worker_main, &ws[i]);
        }
        for (i = 0; i < threads; i++) pthread_join(th[i], NULL);
        t1 = now();
        for (s = 0; s < DRAW_SECTS; s++) {
            table.ret_num[s] = 0;
            table.ret_den[s] = den;
        }
        {
            int64_t cat[DRAW_SECTS][DC_COUNT];
            memset(cat, 0, sizeof cat);
            for (i = 0; i < threads; i++) {
                for (s = 0; s < DRAW_SECTS; s++) {
                    table.ret_num[s] += ws[i].ret[s];
                    for (c = 0; c < DC_COUNT; c++) cat[s][c] += ws[i].cat[s][c];
                }
                checked += ws[i].brute_checked;
                bad += ws[i].brute_bad;
            }
            printf("%s (%s): %d classes, %d threads, %.2f s\n", pt->name, draw_strategy_file(v),
                   DRAW_CLASSES, threads, t1 - t0);
            printf("  return at 5 coins (royal 4000): ");
            print_pct(table.ret_num[DRAW_SECT_MAX], den, 10);
            printf(" %%  = %" PRId64 " / %" PRId64 "\n", table.ret_num[DRAW_SECT_MAX], den);
            printf("  return at 1-4 coins (royal 250/coin): ");
            print_pct(table.ret_num[DRAW_SECT_LOW], den, 10);
            printf(" %%\n");
            printf("  final-hand distribution under optimal play at 5 coins:\n");
            for (c = 0; c < DC_COUNT; c++) {
                int pay[DC_COUNT];
                double p = (double)cat[DRAW_SECT_MAX][c] / (double)den;
                draw_pay_vector(v, 5, pay);
                if (c != DC_NONE && pay[c] == 0) continue;
                printf("    %-18s pays %3d/coin  p = %.10f  (1 in %10.1f)  return %.6f\n",
                       c ? draw_cat_name(c) : "(nothing)", pay[c], p, p > 0 ? 1.0 / p : 0.0,
                       p * pay[c]);
            }
            {
                int diff = 0;
                for (i = 0; i < DRAW_CLASSES; i++)
                    diff += table.hold[DRAW_SECT_MAX][i] != table.hold[DRAW_SECT_LOW][i];
                printf("  classes whose best hold differs between 5 coins and 1-4 coins: %d\n", diff);
            }
        }
        if (job.brute_every) {
            printf("  brute-force cross-check: %ld classes, %ld mismatches\n", checked, bad);
            if (bad) fails++;
        }
        if (!dry) {
            char path[1024];
            snprintf(path, sizeof path, "%s/%s", outdir, draw_strategy_file(v));
            if (draw_strategy_save(&table, path) != 0) {
                fprintf(stderr, "cannot write %s\n", path);
                fails++;
            } else {
                printf("  wrote %s\n", path);
            }
        }
        free(ws);
        free(th);
    }
    printf("total %.2f s\n", now() - t_all);
    return fails ? 1 : 0;
}
