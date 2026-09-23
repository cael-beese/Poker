/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* 10,000,000 random seven-card hands: eval7 must equal the minimum eval5
   over all 21 five-card subsets (enumerated here with plain nested loops,
   independently of the evaluator's own subset tables); eval_best must return
   that rank and five of the seven cards that evaluate to it; eval7_ck must
   agree with eval7. Then 2,000,000 six-card hands the same way for eval6. */

#include "engine/deck.h"
#include "engine/eval.h"
#include "engine/rng.h"
#include "test_util.h"

#include <stdlib.h>
#include <string.h>

static int brute_best(const Card *h, int n)
{
    int a, b, c, d, e, best = 1 << 30;
    for (a = 0; a < n; a++)
    for (b = a + 1; b < n; b++)
    for (c = b + 1; c < n; c++)
    for (d = c + 1; d < n; d++)
    for (e = d + 1; e < n; e++) {
        Card five[5] = { h[a], h[b], h[c], h[d], h[e] };
        int r = eval5(five);
        if (r < best) best = r;
    }
    return best;
}

/* The five cards must be distinct members of the hand. */
static int best_is_subset(const Card *h, int n, const Card best[5])
{
    int i, j, used[7] = {0};
    for (i = 0; i < 5; i++) {
        int found = 0;
        for (j = 0; j < n; j++)
            if (!used[j] && h[j] == best[i]) { used[j] = 1; found = 1; break; }
        if (!found) return 0;
    }
    return 1;
}

static void deal(Rng *r, Card *h, int n)
{
    /* Partial Fisher-Yates over a fresh 52-card array; independent of the
       engine's deck code so a deck bug cannot hide an evaluator bug. */
    Card pool[52];
    int i;
    for (i = 0; i < 52; i++) pool[i] = (Card)i;
    for (i = 0; i < n; i++) {
        int j = i + (int)rng_below(r, (uint32_t)(52 - i));
        Card t = pool[i]; pool[i] = pool[j]; pool[j] = t;
        h[i] = pool[i];
    }
}

int main(int argc, char **argv)
{
    long long hands = argc > 1 ? atoll(argv[1]) : 10000000LL;
    long long hands6 = hands / 5, i, bad7 = 0, badck = 0, badbest = 0, bad6 = 0;
    long long cat7[10] = {0};
    Rng rng;
    double t0, t1;
    int k;

    eval_init();
    rng_seed(&rng, 0xB0B5EED7ull);

    t0 = test_now();
    for (i = 0; i < hands; i++) {
        Card h[7], best[5];
        uint32_t ck[7];
        int want, got, rb;
        deal(&rng, h, 7);
        want = brute_best(h, 7);
        got = eval7(h);
        for (k = 0; k < 7; k++) ck[k] = eval_ck(h[k]);
        if (got != want) {
            if (bad7++ < 5) {
                char s[3];
                fprintf(stderr, "eval7 mismatch: got %d want %d:", got, want);
                for (k = 0; k < 7; k++) fprintf(stderr, " %s", card_str(h[k], s));
                fprintf(stderr, "\n");
            }
        }
        if (eval7_ck(ck) != got) badck++;
        rb = eval_best(h, 7, best);
        if (rb != want || eval5(best) != want || !best_is_subset(h, 7, best)) badbest++;
        cat7[eval_category(got)]++;
    }
    t1 = test_now();
    printf("7-card: %lld random hands in %.2f s; eval7 mismatches %lld, eval7_ck mismatches %lld, "
           "eval_best mismatches %lld\n", hands, t1 - t0, bad7, badck, badbest);
    printf("  category mix (per mille): SF %.3f  4K %.3f  FH %.2f  FL %.2f  ST %.2f  3K %.2f  2P %.1f  1P %.1f  HC %.1f\n",
           1e3 * cat7[1] / hands, 1e3 * cat7[2] / hands, 1e3 * cat7[3] / hands, 1e3 * cat7[4] / hands,
           1e3 * cat7[5] / hands, 1e3 * cat7[6] / hands, 1e3 * cat7[7] / hands, 1e3 * cat7[8] / hands,
           1e3 * cat7[9] / hands);
    CHECK_EQ_INT(bad7, 0);
    CHECK_EQ_INT(badck, 0);
    CHECK_EQ_INT(badbest, 0);

    t0 = test_now();
    for (i = 0; i < hands6; i++) {
        Card h[6], best[5];
        int want, rb;
        deal(&rng, h, 6);
        want = brute_best(h, 6);
        if (eval6(h) != want) bad6++;
        rb = eval_best(h, 6, best);
        if (rb != want || eval5(best) != want || !best_is_subset(h, 6, best)) badbest++;
    }
    t1 = test_now();
    printf("6-card: %lld random hands in %.2f s; eval6 mismatches %lld, eval_best mismatches %lld\n",
           hands6, t1 - t0, bad6, badbest);
    CHECK_EQ_INT(bad6, 0);
    CHECK_EQ_INT(badbest, 0);

    /* Bad n and bad cards are refused, not evaluated. */
    {
        Card h[7] = { 0, 1, 2, 3, 4, 5, 6 }, best[5];
        CHECK_EQ_INT(eval_best(h, 4, best), 0);
        CHECK_EQ_INT(eval_best(h, 8, best), 0);
        h[3] = CARD_NONE;
        CHECK_EQ_INT(eval_best(h, 7, best), 0);
    }

    return test_finish("eval7_vs_bruteforce");
}
