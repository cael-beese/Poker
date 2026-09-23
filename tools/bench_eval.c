/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* Evaluator benchmark: ns per hand for each evaluation path, on random hands
   pre-dealt into memory so dealing is not timed. Run it on the Pi:
       ./build/bench_eval [hands]        (default 2,000,000)
   The checksum line keeps the compiler from discarding the work and doubles
   as a cross-check: paths that must agree print the same sum. */

#include "engine/eval.h"
#include "engine/eval_internal.h"
#include "engine/rng.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void deal(Rng *r, Card *h, int n)
{
    Card pool[52];
    int i;
    for (i = 0; i < 52; i++) pool[i] = (Card)i;
    for (i = 0; i < n; i++) {
        int j = i + (int)rng_below(r, (uint32_t)(52 - i));
        Card t = pool[i]; pool[i] = pool[j]; pool[j] = t;
        h[i] = pool[i];
    }
}

static void report(const char *name, double secs, long n, unsigned long long sum)
{
    printf("  %-34s %8.2f ns/hand  %7.2f M hands/s   (sum %llu)\n",
           name, secs * 1e9 / (double)n, (double)n / secs / 1e6, sum);
}

int main(int argc, char **argv)
{
    long n = argc > 1 ? atol(argv[1]) : 2000000L, i;
    Card *h7 = malloc((size_t)n * 7);
    uint32_t *ck = malloc((size_t)n * 7 * sizeof *ck);
    Card best[5];
    Rng r;
    double t0, t1;
    unsigned long long sum;
    unsigned slots, keys, maxp;
    double avgp;
    int round;

    if (!h7 || !ck) return 1;
    t0 = now();
    eval_init();
    t1 = now();
    eval_hash_stats(&slots, &keys, &avgp, &maxp);
    printf("eval_init %.2f ms; tables %zu bytes; paired-hand hash %u keys in %u slots, "
           "avg probe %.3f, max %u\n", (t1 - t0) * 1e3, eval_table_bytes(), keys, slots, avgp, maxp);

    rng_seed(&r, 12345);
    for (i = 0; i < n; i++) {
        int k;
        deal(&r, h7 + i * 7, 7);
        for (k = 0; k < 7; k++) ck[i * 7 + k] = eval_ck(h7[i * 7 + k]);
    }
    printf("%ld random hands (first five of each for the 5-card paths)\n", n);

    /* Two rounds: the first warms caches and the CPU clock. */
    for (round = 0; round < 2; round++) {
        printf("round %d\n", round + 1);

        sum = 0; t0 = now();
        for (i = 0; i < n; i++) sum += (unsigned)eval5(h7 + i * 7);
        report("eval5 (Card)", now() - t0, n, sum);

        sum = 0; t0 = now();
        for (i = 0; i < n; i++) {
            const uint32_t *c = ck + i * 7;
            sum += (unsigned)eval5_ck(c[0], c[1], c[2], c[3], c[4]);
        }
        report("eval5_ck (hash)", now() - t0, n, sum);

        sum = 0; t0 = now();
        for (i = 0; i < n; i++) {
            const uint32_t *c = ck + i * 7;
            sum += (unsigned)eval5_ck_bsearch(c[0], c[1], c[2], c[3], c[4]);
        }
        report("eval5_ck (binary search)", now() - t0, n, sum);

        sum = 0; t0 = now();
        for (i = 0; i < n; i++) sum += (unsigned)eval6(h7 + i * 7);
        report("eval6 (Card)", now() - t0, n, sum);

        sum = 0; t0 = now();
        for (i = 0; i < n; i++) sum += (unsigned)eval7(h7 + i * 7);
        report("eval7 (Card, direct)", now() - t0, n, sum);

        sum = 0; t0 = now();
        for (i = 0; i < n; i++) sum += (unsigned)eval7_ck(ck + i * 7);
        report("eval7_ck (direct)", now() - t0, n, sum);

        sum = 0; t0 = now();
        for (i = 0; i < n; i++) sum += (unsigned)eval7_combo(h7 + i * 7);
        report("eval7 via 21 x eval5_ck", now() - t0, n, sum);

        sum = 0; t0 = now();
        for (i = 0; i < n; i++) sum += (unsigned)eval_best(h7 + i * 7, 7, best);
        report("eval_best(7)", now() - t0, n, sum);
    }

    /* Sequential enumeration, the pattern of an exact EV calculation. */
    {
        int a, b, c, d, e;
        long cnt = 0;
        sum = 0; t0 = now();
        for (a = 0; a < 52; a++) for (b = a + 1; b < 52; b++) for (c = b + 1; c < 52; c++)
        for (d = c + 1; d < 52; d++) for (e = d + 1; e < 52; e++) {
            Card hh[5] = { (Card)a, (Card)b, (Card)c, (Card)d, (Card)e };
            sum += (unsigned)eval5(hh);
            cnt++;
        }
        report("all 2,598,960 five-card hands", now() - t0, cnt, sum);
    }

    free(h7);
    free(ck);
    return 0;
}
