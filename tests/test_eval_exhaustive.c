/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* All 2,598,960 five-card hands: exact category counts, exactly 7462
   distinct ranks, and the full ordering of all 7462 classes checked against
   an independent, deliberately naive scorer written here. */

#include "engine/eval.h"
#include "test_util.h"

#include <stdint.h>
#include <string.h>

/* Naive scorer: category (8 = straight flush .. 0 = high card) then the ranks
   ordered by (count desc, rank desc); higher score = better hand. Shares no
   code or tables with the evaluator. */
static uint32_t naive_score(const Card *c)
{
    int cnt[13] = {0}, i, j, flush = 1, distinct = 0, straight_hi = -1, cat;
    int order[5], n = 0;
    for (i = 0; i < 5; i++) {
        cnt[card_rank(c[i])]++;
        if (card_suit(c[i]) != card_suit(c[0])) flush = 0;
    }
    for (i = 0; i < 13; i++) if (cnt[i]) distinct++;
    if (distinct == 5) {
        int hi = -1, lo = 13;
        for (i = 0; i < 13; i++) if (cnt[i]) { if (i > hi) hi = i; if (i < lo) lo = i; }
        if (hi - lo == 4) straight_hi = hi;
        else if (cnt[12] && cnt[0] && cnt[1] && cnt[2] && cnt[3]) straight_hi = 3;
    }
    /* ranks by count desc, then rank desc */
    for (j = 4; j >= 1; j--)
        for (i = 12; i >= 0; i--)
            if (cnt[i] == j) { int k; for (k = 0; k < j; k++) order[n++] = i; }

    if (straight_hi >= 0 && flush) cat = 8;
    else if (cnt[order[0]] == 4) cat = 7;
    else if (cnt[order[0]] == 3 && cnt[order[3]] == 2) cat = 6;
    else if (flush) cat = 5;
    else if (straight_hi >= 0) cat = 4;
    else if (cnt[order[0]] == 3) cat = 3;
    else if (cnt[order[0]] == 2 && cnt[order[2]] == 2) cat = 2;
    else if (cnt[order[0]] == 2) cat = 1;
    else cat = 0;

    if (cat == 8 || cat == 4)
        return (uint32_t)cat << 20 | (uint32_t)straight_hi << 16;
    return (uint32_t)cat << 20 | (uint32_t)order[0] << 16 | (uint32_t)order[1] << 12
         | (uint32_t)order[2] << 8 | (uint32_t)order[3] << 4 | (uint32_t)order[4];
}

static int rank_of(const char *s)
{
    Card c[5];
    if (cards_parse(s, c, 5) != 5) { fprintf(stderr, "bad hand %s\n", s); return -1; }
    return eval5(c);
}

/* a must beat b (strictly lower rank). */
static void better(const char *a, const char *b)
{
    int ra = rank_of(a), rb = rank_of(b);
    if (!(ra > 0 && rb > 0 && ra < rb)) {
        g_test_failures++;
        fprintf(stderr, "FAIL order: [%s]=%d should beat [%s]=%d\n", a, ra, b, rb);
    }
}

static void tie(const char *a, const char *b)
{
    int ra = rank_of(a), rb = rank_of(b);
    if (!(ra > 0 && ra == rb)) {
        g_test_failures++;
        fprintf(stderr, "FAIL tie: [%s]=%d vs [%s]=%d\n", a, ra, b, rb);
    }
}

static uint32_t g_score[EVAL_WORST_RANK + 1];

int main(void)
{
    static const long long expect[10] = {
        4, 36, 624, 3744, 5108, 10200, 54912, 123552, 1098240, 1302540
    };
    static const char *const label[10] = {
        "Royal flush", "Straight flush", "Quads", "Full house", "Flush", "Straight",
        "Trips", "Two pair", "Pair", "High card"
    };
    long long count[10] = {0}, total = 0;
    int distinct = 0, class_count[10] = {0};
    int a, b, c, d, e, i;
    double t0, t1, t2;
    char desc[64];

    t0 = test_now();
    eval_init();
    t1 = test_now();
    eval_init();   /* second call must be a no-op */
    t2 = test_now();
    printf("eval_init: first call %.2f ms, second %.4f ms, tables %zu bytes\n",
           (t1 - t0) * 1e3, (t2 - t1) * 1e3, eval_table_bytes());
    /* The budget is 50 ms on x86; the Pi is several times slower per core,
       so it gets a looser bound that still catches an accidental blow-up. */
#if defined(__x86_64__)
    CHECK_TIMING((t1 - t0) < 0.050);
#else
    CHECK_TIMING((t1 - t0) < 0.250);
#endif

    memset(g_score, 0, sizeof g_score);
    t0 = test_now();
    for (a = 0; a < 52; a++)
    for (b = a + 1; b < 52; b++)
    for (c = b + 1; c < 52; c++)
    for (d = c + 1; d < 52; d++)
    for (e = d + 1; e < 52; e++) {
        Card h[5] = { (Card)a, (Card)b, (Card)c, (Card)d, (Card)e };
        Card best[5];
        int r = eval5(h), cat;
        uint32_t sc;
        if (r < 1 || r > EVAL_WORST_RANK) { CHECK(0); continue; }
        cat = eval_category(r);
        if (eval_is_royal(r)) count[0]++;
        else count[cat]++;
        total++;
        CHECK_EQ_INT(eval5_ck(eval_ck(h[0]), eval_ck(h[1]), eval_ck(h[2]), eval_ck(h[3]), eval_ck(h[4])), r);
        if ((total & 15) == 0) {   /* eval_best is slower; a 1-in-16 sample is plenty */
            CHECK_EQ_INT(eval_best(h, 5, best), r);
        }
        sc = naive_score(h);
        if (g_score[r] == 0) { g_score[r] = sc; distinct++; }
        else if (g_score[r] != sc) {
            CHECK(g_score[r] == sc);   /* two different naive classes share a rank */
        }
    }
    t1 = test_now();

    printf("hands: %lld (%.1f ms, eval5 + checks)\n", total, (t1 - t0) * 1e3);
    CHECK_EQ_INT(total, 2598960);
    for (i = 0; i < 10; i++) {
        printf("  %-15s %9lld  (expected %lld)%s\n", label[i], count[i], expect[i],
               count[i] == expect[i] ? "" : "  <-- MISMATCH");
        CHECK_EQ_INT(count[i], expect[i]);
    }
    printf("distinct ranks: %d (expected 7462)\n", distinct);
    CHECK_EQ_INT(distinct, 7462);

    /* Full ordering: rank 1 must be the best naive class and every next rank
       strictly worse, and the category ranges must hold 10/156/156/1277/10/
       858/858/2860/1277 classes. */
    for (i = 1; i <= EVAL_WORST_RANK; i++) {
        if (i > 1 && !(g_score[i] < g_score[i - 1])) {
            CHECK(g_score[i] < g_score[i - 1]);
            fprintf(stderr, "  order breaks between rank %d and %d\n", i - 1, i);
        }
        class_count[eval_category(i)]++;
    }
    {
        static const int want[10] = { 0, 10, 156, 156, 1277, 10, 858, 858, 2860, 1277 };
        for (i = 1; i <= 9; i++) CHECK_EQ_INT(class_count[i], want[i]);
    }
    CHECK_EQ_INT(eval_category(0), 0);
    CHECK_EQ_INT(eval_category(7463), 0);

    /* Hand-picked comparisons. */
    better("As Ks Qs Js Ts", "Ks Qs Js Ts 9s");        /* royal over K-high SF */
    better("6h 5h 4h 3h 2h", "5d 4d 3d 2d Ad");        /* steel wheel is the lowest SF */
    better("5d 4d 3d 2d Ad", "Ac Ad Ah As Kc");        /* any SF over quads */
    better("Ac Ad Ah As 2c", "Kc Kd Kh Ks Ac");        /* quad rank before kicker */
    better("2c 2d 2h 2s Ac", "2c 2d 2h 2s Kc");        /* quads kicker */
    better("2c 2d 2h 3s 3c", "As Ks Qs Js 9s");        /* worst boat over best flush */
    better("Ac Ad Ah 2s 2c", "Kc Kd Kh As Ac");        /* boat: trips rank first */
    better("As Ks Qs Js 9s", "As Kd Qh Jc Tc");        /* flush over Broadway */
    better("7h 5h 4h 3h 2h", "As Kd Qh Jc Tc");        /* worst flush over best straight */
    better("6c 5d 4h 3s 2c", "5c 4d 3h 2s Ac");        /* 6-high straight over the wheel */
    better("5c 4d 3h 2s Ac", "Ac Ad Ah Ks Qc");        /* wheel over any trips */
    better("Ac Ad Ah 3s 2c", "Kc Kd Kh As Qc");        /* trips rank first */
    better("7c 7d 7h As 2c", "7c 7d 7h Ks Qc");        /* trips kicker */
    better("7c 7d 7h As 3c", "7c 7d 7h As 2d");        /* trips second kicker */
    better("2c 2d 2h 4s 3c", "Ac Ad Kh Ks Qc");        /* worst trips over best two pair */
    better("Ac Ad 3h 3s 2c", "Kc Kd Qh Qs Jc");        /* two pair: high pair first */
    better("Kc Kd Qh Qs 2c", "Kc Kd Jh Js Ac");        /* then low pair */
    better("Kc Kd Qh Qs 3c", "Kh Ks Qc Qd 2h");        /* then kicker */
    better("3c 3d 2h 2s 4c", "Ac Ad Kh Qs Jc");        /* worst two pair over best pair */
    better("Ac Ad 4h 3s 2c", "Kc Kd Ah Qs Jc");        /* pair rank first */
    better("Jc Jd Ah 3s 2c", "Jh Js Kh Qs Tc");        /* pair kickers */
    better("Jc Jd Ah Ks 3c", "Jh Js Ac Kd 2c");        /* third kicker */
    better("2c 2d 5h 4s 3c", "Ac Kd Qh Js 9c");        /* worst pair over best high card */
    better("Ac Kd Qh Js 9c", "Ac Kd Qh Js 8c");        /* high card last kicker */
    better("Ac 6d 5h 4s 3c", "Kc Qd Jh Ts 8c");        /* ace high beats king high */
    better("8c 6d 5h 4s 3c", "7c 5d 4h 3s 2c");        /* second worst over worst */
    tie("As Kd Qh Jc 9s", "Ac Kh Qd Js 9d");           /* suits never matter off-flush */
    tie("As Ks Qs Js 9s", "Ah Kh Qh Jh 9h");
    CHECK_EQ_INT(rank_of("As Ks Qs Js Ts"), 1);
    CHECK_EQ_INT(rank_of("7c 5d 4h 3s 2c"), 7462);
    CHECK_EQ_INT(rank_of("5d 4d 3d 2d Ad"), 10);
    CHECK_EQ_INT(rank_of("Ac Ad Ah As Kc"), 11);
    CHECK_EQ_INT(rank_of("Ac Ad Ah Ks Kc"), 167);
    CHECK_EQ_INT(rank_of("As Ks Qs Js 9s"), 323);
    CHECK_EQ_INT(rank_of("As Kd Qh Jc Tc"), 1600);
    CHECK_EQ_INT(rank_of("5c 4d 3h 2s Ac"), 1609);
    CHECK_EQ_INT(rank_of("Ac Ad Ah Ks Qc"), 1610);
    CHECK_EQ_INT(rank_of("Ac Ad Kh Ks Qc"), 2468);
    CHECK_EQ_INT(rank_of("Ac Ad Kh Qs Jc"), 3326);
    CHECK_EQ_INT(rank_of("Ac Kd Qh Js 9c"), 6186);

    /* Descriptions and names. */
    {
        static const struct { const char *hand, *text; } D[] = {
            { "As Ks Qs Js Ts", "Royal Flush" },
            { "9h 8h 7h 6h 5h", "Straight Flush, Nine High" },
            { "5d 4d 3d 2d Ad", "Straight Flush, Five High" },
            { "Ac Ad Ah As Kc", "Four Aces" },
            { "Kc Kd Kh Ts Tc", "Kings Full of Tens" },
            { "Ah 9h 7h 4h 2h", "Flush, Ace High" },
            { "5c 4d 3h 2s Ac", "Straight, Five High" },
            { "7c 7d 7h As 2c", "Three Sevens" },
            { "Ac Ad 8h 8s 2c", "Aces and Eights" },
            { "Jc Jd Ah 3s 2c", "Pair of Jacks" },
            { "Ac 6d 5h 4s 3c", "Ace High" },
        };
        for (i = 0; i < (int)(sizeof D / sizeof D[0]); i++) {
            eval_describe(rank_of(D[i].hand), desc, sizeof desc);
            if (strcmp(desc, D[i].text) != 0) {
                g_test_failures++;
                fprintf(stderr, "FAIL describe [%s]: \"%s\" expected \"%s\"\n", D[i].hand, desc, D[i].text);
            }
        }
        CHECK(strcmp(eval_category_name(HC_FULL_HOUSE), "FULL HOUSE") == 0);
        CHECK(strcmp(eval_category_name(0), "") == 0);
        CHECK(strcmp(eval_describe(0, desc, sizeof desc), "") == 0);
    }

    return test_finish("eval_exhaustive");
}
