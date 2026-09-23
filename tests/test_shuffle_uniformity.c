/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* Statistical checks on deck_shuffle and rng_below, with fixed seeds so the
   numbers are reproducible. Thresholds are for CLEAR failure only (p below
   1e-6, after allowing for the number of tests), so a correct implementation
   never flakes; a known-bad shuffle is run through the same statistic to show
   the test has the power to catch one. */

#include "engine/deck.h"
#include "engine/rng.h"
#include "test_util.h"

#include <stdlib.h>
#include <string.h>

#define SHUFFLES 1000000
#define P_FAIL 1e-6

static unsigned g_count[52][52];   /* [position][card] */

/* The classic broken shuffle: swap every position with ANY position. It has
   52^52 equally likely paths onto 52! permutations, so it is biased. */
static void bad_shuffle(Deck *d, Rng *r)
{
    int i;
    for (i = 0; i < d->n; i++) {
        int j = (int)rng_below(r, (uint32_t)d->n);
        Card t = d->c[i]; d->c[i] = d->c[j]; d->c[j] = t;
    }
    d->pos = 0;
}

/* Fills g_count and returns the overall statistic; reports per position. */
static double position_test(const char *name, void (*shuffle)(Deck *, Rng *), uint64_t seed,
                            double *worst_p, double *overall_p)
{
    Rng rng;
    Deck d;
    double expect = (double)SHUFFLES / 52.0, total = 0.0, minp = 1.0;
    int s, p, c;

    memset(g_count, 0, sizeof g_count);
    rng_seed(&rng, seed);
    for (s = 0; s < SHUFFLES; s++) {
        deck_init(&d);
        shuffle(&d, &rng);
        for (p = 0; p < 52; p++) g_count[p][d.c[p]]++;
    }
    for (p = 0; p < 52; p++) {
        double chi = 0.0, pv;
        for (c = 0; c < 52; c++) {
            double diff = (double)g_count[p][c] - expect;
            chi += diff * diff / expect;
        }
        pv = test_chi2_p(chi, 51.0);
        if (pv < minp) minp = pv;
        total += chi;
    }
    /* Rows and columns of the count matrix both sum to SHUFFLES, and the
       summed statistic is distributed as (52/51) * chi-square with 51*51
       degrees of freedom (the covariance of a random permutation matrix is
       1/(n-1) times the projection onto that subspace). */
    *worst_p = minp;
    *overall_p = test_chi2_p(total * 51.0 / 52.0, 2601.0);
    printf("%s: %d shuffles, 52x52 position counts (expected %.1f each)\n", name, SHUFFLES, expect);
    printf("  per position: chi2 dof 51, smallest p of 52 = %.3g (Bonferroni-adjusted %.3g)\n",
           minp, minp * 52 < 1 ? minp * 52 : 1.0);
    printf("  overall: sum chi2 = %.1f (mean under uniformity 2652), p = %.3g\n", total, *overall_p);
    return total;
}

static void below_test(Rng *rng, uint32_t n, long long samples)
{
    long long *cnt = calloc(n, sizeof *cnt), i;
    double expect = (double)samples / n, chi = 0.0, pv;
    uint32_t k;
    int out_of_range = 0;
    for (i = 0; i < samples; i++) {
        uint32_t v = rng_below(rng, n);
        if (v >= n) { out_of_range = 1; continue; }
        cnt[v]++;
    }
    for (k = 0; k < n; k++) {
        double diff = (double)cnt[k] - expect;
        chi += diff * diff / expect;
    }
    pv = test_chi2_p(chi, (double)(n - 1));
    printf("rng_below(%u): %lld samples, chi2 = %.2f, dof %u, p = %.3g\n", n, samples, chi, n - 1, pv);
    CHECK(!out_of_range);
    CHECK(pv > P_FAIL);
    free(cnt);
}

/* For n close to 2^32 a plain modulo is grossly biased: with n = 3 * 2^30,
   x % n puts half of all outputs in the lowest third. Bucket the output into
   thirds and demand an even split; the same buckets over x % n are shown for
   contrast. Also n = 2^31 + 1, where Lemire's method rejects almost half of
   all draws, so the rejection loop is exercised hard. */
static void below_big_test(Rng *rng)
{
    const uint32_t n = 3u << 30;
    const long long samples = 3000000;
    long long cnt[3] = {0}, naive[3] = {0}, i;
    double chi = 0.0, chin = 0.0, pv, pvn;
    int k, bad = 0;
    for (i = 0; i < samples; i++) {
        uint32_t v = rng_below(rng, n);
        uint32_t x = (uint32_t)(rng_next(rng) >> 32) % n;
        if (v >= n) bad = 1;
        cnt[v >> 30]++;
        naive[x >> 30]++;
    }
    for (k = 0; k < 3; k++) {
        double e = samples / 3.0;
        chi += (cnt[k] - e) * (cnt[k] - e) / e;
        chin += (naive[k] - e) * (naive[k] - e) / e;
    }
    pv = test_chi2_p(chi, 2.0);
    pvn = test_chi2_p(chin, 2.0);
    printf("rng_below(3*2^30) by thirds: %lld %lld %lld, chi2 = %.2f, p = %.3g "
           "(plain modulo: %lld %lld %lld, chi2 = %.0f, p = %.3g)\n",
           cnt[0], cnt[1], cnt[2], chi, pv, naive[0], naive[1], naive[2], chin, pvn);
    CHECK(!bad);
    CHECK(pv > P_FAIL);
    CHECK(pvn < P_FAIL);   /* the statistic does see a real bias */

    {
        const uint32_t m = (1u << 31) + 1u;
        long long half[2] = {0}, odd = 0;
        double e = 1000000 / 2.0, c2;
        bad = 0;
        for (i = 0; i < 1000000; i++) {
            uint32_t v = rng_below(rng, m);
            if (v >= m) bad = 1;
            half[v >= (m >> 1)]++;
            odd += v & 1;
        }
        c2 = (half[0] - e) * (half[0] - e) / e + (half[1] - e) * (half[1] - e) / e;
        printf("rng_below(2^31+1): halves %lld / %lld, chi2 = %.2f, p = %.3g; odd %lld\n",
               half[0], half[1], c2, test_chi2_p(c2, 1.0), odd);
        CHECK(!bad);
        CHECK(test_chi2_p(c2, 1.0) > P_FAIL);
    }
    CHECK_EQ_INT(rng_below(rng, 0), 0);
    CHECK_EQ_INT(rng_below(rng, 1), 0);
}

int main(void)
{
    double worst_p, overall_p, t0 = test_now();
    Rng rng;
    int i;

    position_test("deck_shuffle", deck_shuffle, 0x5AFE5EEDull, &worst_p, &overall_p);
    CHECK(worst_p * 52 > P_FAIL);
    CHECK(overall_p > P_FAIL);

    /* Sanity of the test itself: the broken shuffle must fail it clearly. */
    position_test("naive swap-with-any shuffle (known biased, must be caught)", bad_shuffle,
                  0x5AFE5EEDull, &worst_p, &overall_p);
    CHECK(overall_p < P_FAIL);

    rng_seed(&rng, 0xD1CE5ull);
    below_test(&rng, 2, 10000000);
    below_test(&rng, 3, 10000000);
    below_test(&rng, 5, 10000000);
    below_test(&rng, 52, 10000000);
    below_test(&rng, 1000, 10000000);
    below_big_test(&rng);

    /* rng_unit: in [0,1) and mean close to 1/2. */
    {
        double sum = 0.0, lo = 1.0, hi = 0.0;
        for (i = 0; i < 1000000; i++) {
            double u = rng_unit(&rng);
            sum += u;
            if (u < lo) lo = u;
            if (u > hi) hi = u;
        }
        printf("rng_unit: mean %.5f over 1e6, min %.3g, max %.9f\n", sum / 1e6, lo, hi);
        CHECK(lo >= 0.0 && hi < 1.0);
        CHECK(sum / 1e6 > 0.497 && sum / 1e6 < 0.503);
    }

    printf("total %.2f s\n", test_now() - t0);
    return test_finish("shuffle_uniformity");
}
