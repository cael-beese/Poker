/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* Every one of the 133,784,560 seven-card hands through eval7, checked
   against the published seven-card category frequencies and the known
   count of 4,824 distinct best-hand ranks reachable with seven cards. This
   covers the whole direct 7-card path (flush table and rank-multiset index),
   not a random sample of it. */

#include "engine/eval.h"
#include "test_util.h"

#include <string.h>

static unsigned char g_seen[EVAL_WORST_RANK + 1];

int main(void)
{
    static const long long expect[10] = {
        0, 41584, 224848, 3473184, 4047644, 6180020, 6461620, 31433400, 58627800, 23294460
    };
    static const char *const label[10] = {
        "", "Straight flush", "Quads", "Full house", "Flush", "Straight",
        "Trips", "Two pair", "Pair", "High card"
    };
    long long count[10] = {0}, total = 0, royal = 0;
    int a, b, c, d, e, f, g, i, distinct = 0;
    double t0, t1;

    eval_init();
    memset(g_seen, 0, sizeof g_seen);
    t0 = test_now();
    for (a = 0; a < 52; a++)
    for (b = a + 1; b < 52; b++)
    for (c = b + 1; c < 52; c++)
    for (d = c + 1; d < 52; d++)
    for (e = d + 1; e < 52; e++)
    for (f = e + 1; f < 52; f++)
    for (g = f + 1; g < 52; g++) {
        Card h[7] = { (Card)a, (Card)b, (Card)c, (Card)d, (Card)e, (Card)f, (Card)g };
        int r = eval7(h);
        count[eval_category(r)]++;
        royal += (r == 1);
        g_seen[r] = 1;
        total++;
    }
    t1 = test_now();
    for (i = 1; i <= EVAL_WORST_RANK; i++) distinct += g_seen[i];

    printf("7-card exhaustive: %lld hands in %.2f s (%.2f ns/hand incl. loop)\n",
           total, t1 - t0, (t1 - t0) * 1e9 / (double)total);
    CHECK_EQ_INT(total, 133784560LL);
    CHECK_EQ_INT(count[0], 0);
    for (i = 1; i < 10; i++) {
        printf("  %-15s %10lld  (expected %lld)%s\n", label[i], count[i], expect[i],
               count[i] == expect[i] ? "" : "  <-- MISMATCH");
        CHECK_EQ_INT(count[i], expect[i]);
    }
    printf("  royal flushes   %10lld  (expected 4324)\n", royal);
    CHECK_EQ_INT(royal, 4324);
    printf("distinct ranks: %d (expected 4824)\n", distinct);
    CHECK_EQ_INT(distinct, 4824);
    return test_finish("eval7_exhaustive");
}
