/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* Every one of the 2,598,960 five-card hands, classified three ways:

   1. Deuces Wild: draw_classify_deuces (count the deuces, analyse the rest)
      against a brute-force substitution evaluator that replaces each deuce
      by every one of the 52 cards (duplicates allowed, which is how five of
      a kind happens) and keeps the best natural reading. Written here from
      the rules, sharing no code with the library.
   2. Jacks or Better and Bonus Poker: draw_classify (engine eval5 plus a
      rank table) against draw_cat_from_counts, the classifier the hint's
      enumeration uses.
   3. The same counts classifier for Deuces Wild, fed as the hint feeds it.
   Category totals for JoB and Bonus are checked against the standard
   five-card counts (SPEC section 7). */

#include "games/draw/draw_rules.h"
#include "test_util.h"

#include <string.h>

/* ---- the independent substitution evaluator ------------------------------ */

/* Deuces Wild category of five cards read naturally (no wild cards), with
   duplicates allowed. Pairs and two pair pay nothing in Deuces Wild. */
static int natural_cat(const int rk[5], const int st[5])
{
    int cnt[13] = {0}, i, maxc = 0, pairs = 0, trips = 0, distinct = 0, lo = 13, hi = -1;
    int flush = 1, straight = 0, royal = 0;
    for (i = 0; i < 5; i++) {
        cnt[rk[i]]++;
        if (st[i] != st[0]) flush = 0;
    }
    for (i = 0; i < 13; i++) {
        if (!cnt[i]) continue;
        distinct++;
        if (i < lo) lo = i;
        if (i > hi) hi = i;
        if (cnt[i] > maxc) maxc = cnt[i];
        if (cnt[i] == 2) pairs++;
        if (cnt[i] == 3) trips++;
    }
    if (distinct == 5) {
        if (hi - lo == 4) straight = 1;
        if (cnt[12] && cnt[0] && cnt[1] && cnt[2] && cnt[3]) straight = 1;   /* A2345 */
        if (lo == RANK_T) royal = 1;
    }
    if (maxc == 5) return DC_FIVE_KIND;
    if (flush && straight) return royal ? DC_ROYAL_FLUSH : DC_STRAIGHT_FLUSH;
    if (maxc == 4) return DC_FOUR_KIND;
    if (trips && pairs) return DC_FULL_HOUSE;
    if (flush) return DC_FLUSH;
    if (straight) return DC_STRAIGHT;
    if (trips) return DC_THREE_KIND;
    return DC_NONE;
}

/* Lower enum value = better category; DC_NONE (0) is the worst. */
static int better(int a, int b)
{
    if (a == DC_NONE) return b;
    if (b == DC_NONE) return a;
    return a < b ? a : b;
}

static int brute_deuces(const Card h[5])
{
    int rk[5], st[5], wild[5], nw = 0, n = 0, i, best = DC_NONE, sub[4];
    for (i = 0; i < 5; i++) {
        if (card_rank(h[i]) == RANK_2) wild[nw++] = i;
        else { rk[n] = card_rank(h[i]); st[n] = card_suit(h[i]); n++; }
    }
    (void)wild;
    if (nw == 4) return DC_FOUR_DEUCES;
    if (nw == 0) return natural_cat(rk, st);          /* a royal here is natural */
    /* Every multiset of nw substitute cards (order does not matter). */
    for (sub[0] = 0; sub[0] < 52; sub[0]++)
    for (sub[1] = nw > 1 ? sub[0] : 0; sub[1] < (nw > 1 ? 52 : 1); sub[1]++)
    for (sub[2] = nw > 2 ? sub[1] : 0; sub[2] < (nw > 2 ? 52 : 1); sub[2]++) {
        int j, c;
        for (j = 0; j < nw; j++) { rk[n + j] = sub[j] >> 2; st[n + j] = sub[j] & 3; }
        c = natural_cat(rk, st);
        if (c == DC_ROYAL_FLUSH) c = DC_WILD_ROYAL;   /* a deuce was used */
        best = better(best, c);
    }
    return best;
}

/* ---- the counts path, as the hint feeds it ------------------------------ */

static int counts_cat(int variant, const Card h[5])
{
    uint8_t c[13] = {0};
    unsigned mask = 0, suits = 0;
    int i;
    for (i = 0; i < 5; i++) {
        int r = card_rank(h[i]);
        c[r]++;
        if (variant == DRAW_DEUCES && r == RANK_2) continue;
        mask |= 1u << r;
        suits |= 1u << card_suit(h[i]);
    }
    return draw_cat_from_counts(variant, c, mask, __builtin_popcount(suits) == 1);
}

int main(void)
{
    static const long JOB_EXPECT[DC_COUNT] = {
        [DC_ROYAL_FLUSH] = 4, [DC_STRAIGHT_FLUSH] = 36, [DC_FOUR_KIND] = 624,
        [DC_FULL_HOUSE] = 3744, [DC_FLUSH] = 5108, [DC_STRAIGHT] = 10200,
        [DC_THREE_KIND] = 54912, [DC_TWO_PAIR] = 123552, [DC_JACKS_OR_BETTER] = 4 * 84480,
        [DC_NONE] = 1302540 + 9 * 84480
    };
    long cnt[DRAW_VARIANTS][DC_COUNT];
    long hands = 0, dw_bad = 0;
    Card h[5];
    int a, b, c, d, e, v, i;
    double t0 = test_now();

    draw_rules_init();
    memset(cnt, 0, sizeof cnt);
    for (a = 0; a < 52; a++)
    for (b = a + 1; b < 52; b++)
    for (c = b + 1; c < 52; c++)
    for (d = c + 1; d < 52; d++)
    for (e = d + 1; e < 52; e++) {
        h[0] = (Card)a; h[1] = (Card)b; h[2] = (Card)c; h[3] = (Card)d; h[4] = (Card)e;
        hands++;
        for (v = 0; v < DRAW_VARIANTS; v++) {
            int k = draw_classify(v, h);
            cnt[v][k]++;
            CHECK_EQ_INT(counts_cat(v, h), k);
        }
        {
            int fast = draw_classify_deuces(h), slow = brute_deuces(h);
            if (fast != slow) {
                char s[3];
                dw_bad++;
                if (dw_bad <= 10) {
                    fprintf(stderr, "deuces mismatch:");
                    for (i = 0; i < 5; i++) fprintf(stderr, " %s", card_str(h[i], s));
                    fprintf(stderr, "  fast %s, substitution %s\n", draw_cat_name(fast), draw_cat_name(slow));
                }
            }
        }
    }
    CHECK_EQ_INT(hands, 2598960);
    CHECK_EQ_INT(dw_bad, 0);

    for (i = 0; i < DC_COUNT; i++) {
        if (i == DC_FOUR_ACES || i == DC_FOUR_2_4 || i == DC_FOUR_5_K) {
            CHECK_EQ_INT(cnt[DRAW_JOB][i], 0);
            continue;
        }
        CHECK_EQ_INT(cnt[DRAW_JOB][i], JOB_EXPECT[i]);
        if (i != DC_FOUR_KIND) CHECK_EQ_INT(cnt[DRAW_BONUS][i], JOB_EXPECT[i]);
    }
    /* Bonus Poker splits the 624 quads by rank: 4 x 12 kickers per rank. */
    CHECK_EQ_INT(cnt[DRAW_BONUS][DC_FOUR_ACES], 48);
    CHECK_EQ_INT(cnt[DRAW_BONUS][DC_FOUR_2_4], 3 * 48);
    CHECK_EQ_INT(cnt[DRAW_BONUS][DC_FOUR_5_K], 9 * 48);
    CHECK_EQ_INT(cnt[DRAW_BONUS][DC_FOUR_KIND], 0);
    /* Deuces Wild: the two categories with obvious counts. */
    CHECK_EQ_INT(cnt[DRAW_DEUCES][DC_ROYAL_FLUSH], 4 * 1 * 1);
    CHECK_EQ_INT(cnt[DRAW_DEUCES][DC_FOUR_DEUCES], 48);

    printf("deuces wild dealt-hand distribution (2,598,960 hands):\n");
    for (i = 0; i < DC_COUNT; i++)
        if (cnt[DRAW_DEUCES][i])
            printf("  %-18s %8ld\n", i ? draw_cat_name(i) : "(nothing)", cnt[DRAW_DEUCES][i]);
    printf("deuces evaluator vs substitution: %ld hands, %ld mismatches; %.1f s\n",
           hands, dw_bad, test_now() - t0);
    return test_finish("draw_classify_exhaustive");
}
