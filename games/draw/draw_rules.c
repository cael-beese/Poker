/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* Variants, paytables and classifiers.

   Jacks or Better and Bonus Poker classify through the engine's eval5: a
   table built at start-up maps each of the 7462 hand ranks to a pay category
   (the pair rank for "jacks or better", the quad rank for Bonus Poker).

   Deuces Wild has its own evaluator (draw_classify_deuces): it counts the
   deuces and analyses the natural cards, with no substitution search. The
   rules are in draw_internal.h, shared with the hint's enumeration. */

#include "draw_rules.h"
#include "draw_internal.h"

#include "engine/eval.h"

#include <pthread.h>
#include <string.h>

static const DrawPaytable PAYTABLES[DRAW_VARIANTS] = {
    { DRAW_JOB, "JACKS OR BETTER", "JOB",
      { [DC_ROYAL_FLUSH] = 250, [DC_STRAIGHT_FLUSH] = 50, [DC_FOUR_KIND] = 25,
        [DC_FULL_HOUSE] = 9, [DC_FLUSH] = 6, [DC_STRAIGHT] = 4, [DC_THREE_KIND] = 3,
        [DC_TWO_PAIR] = 2, [DC_JACKS_OR_BETTER] = 1 },
      0, {0}, 0.995439 },
    { DRAW_BONUS, "BONUS POKER", "BONUS",
      { [DC_ROYAL_FLUSH] = 250, [DC_STRAIGHT_FLUSH] = 50, [DC_FOUR_ACES] = 80,
        [DC_FOUR_2_4] = 40, [DC_FOUR_5_K] = 25, [DC_FULL_HOUSE] = 8, [DC_FLUSH] = 5,
        [DC_STRAIGHT] = 4, [DC_THREE_KIND] = 3, [DC_TWO_PAIR] = 2, [DC_JACKS_OR_BETTER] = 1 },
      0, {0}, 0.991660 },
    { DRAW_DEUCES, "DEUCES WILD", "DEUCES",
      { [DC_ROYAL_FLUSH] = 250, [DC_FOUR_DEUCES] = 200, [DC_WILD_ROYAL] = 25,
        [DC_FIVE_KIND] = 15, [DC_STRAIGHT_FLUSH] = 9, [DC_FOUR_KIND] = 5,
        [DC_FULL_HOUSE] = 3, [DC_FLUSH] = 2, [DC_STRAIGHT] = 2, [DC_THREE_KIND] = 1 },
      0, {0}, 1.007620 },
};

/* The display rows are derived from the pay arrays at init, so the two can
   never disagree. */
static DrawPaytable g_tables[DRAW_VARIANTS];

static const char *const CAT_NAMES[DC_COUNT] = {
    "", "ROYAL FLUSH", "FOUR DEUCES", "WILD ROYAL FLUSH", "FIVE OF A KIND",
    "STRAIGHT FLUSH", "FOUR ACES", "FOUR 2S 3S 4S", "FOUR 5S THRU KS", "FOUR OF A KIND",
    "FULL HOUSE", "FLUSH", "STRAIGHT", "THREE OF A KIND", "TWO PAIR", "JACKS OR BETTER"
};

static uint8_t g_rank_cat[DRAW_VARIANTS][EVAL_WORST_RANK + 1];   /* JoB, Bonus */
uint8_t draw_straight5_tab[8192];     /* see draw_internal.h */
uint8_t draw_window_tab[8192];
static pthread_once_t g_once = PTHREAD_ONCE_INIT;

static const uint16_t WINDOWS[10] = {
    0x1F00, 0x0F80, 0x07C0, 0x03E0, 0x01F0, 0x00F8, 0x007C, 0x003E, 0x001F, 0x100F
};

static void build(void)
{
    int v, r, i;
    unsigned m;

    eval_init();
    for (v = 0; v < DRAW_VARIANTS; v++) {
        g_tables[v] = PAYTABLES[v];
        g_tables[v].rows = 0;
        for (i = 1; i < DC_COUNT; i++)
            if (g_tables[v].pay[i] > 0) g_tables[v].row_cat[g_tables[v].rows++] = (uint8_t)i;
    }
    for (m = 0; m < 8192; m++) {
        for (i = 0; i < 10; i++) {
            if (m == WINDOWS[i]) draw_straight5_tab[m] = 1;
            if ((m & ~(unsigned)WINDOWS[i]) == 0) draw_window_tab[m] = 1;
        }
    }
    for (v = DRAW_JOB; v <= DRAW_BONUS; v++) {
        for (r = 1; r <= EVAL_WORST_RANK; r++) {
            int rr[5], cat = DC_NONE;
            eval_rank_ranks(r, rr);
            switch (eval_category(r)) {
            case HC_STRAIGHT_FLUSH: cat = eval_is_royal(r) ? DC_ROYAL_FLUSH : DC_STRAIGHT_FLUSH; break;
            case HC_QUADS:          cat = draw_quad_cat(v, rr[0]); break;
            case HC_FULL_HOUSE:     cat = DC_FULL_HOUSE; break;
            case HC_FLUSH:          cat = DC_FLUSH; break;
            case HC_STRAIGHT:       cat = DC_STRAIGHT; break;
            case HC_TRIPS:          cat = DC_THREE_KIND; break;
            case HC_TWO_PAIR:       cat = DC_TWO_PAIR; break;
            case HC_PAIR:           cat = rr[0] >= RANK_J ? DC_JACKS_OR_BETTER : DC_NONE; break;
            default:                cat = DC_NONE; break;
            }
            g_rank_cat[v][r] = (uint8_t)cat;
        }
    }
}

void draw_rules_init(void)
{
    pthread_once(&g_once, build);
}

const DrawPaytable *draw_paytable(int variant)
{
    if (variant < 0 || variant >= DRAW_VARIANTS) return NULL;
    draw_rules_init();
    return &g_tables[variant];
}

const char *draw_cat_name(int cat)
{
    return (cat > 0 && cat < DC_COUNT) ? CAT_NAMES[cat] : "";
}

const char *draw_variant_name(int variant)
{
    return (variant >= 0 && variant < DRAW_VARIANTS) ? PAYTABLES[variant].name : "";
}

int draw_pay(int variant, int cat, int bet)
{
    if (variant < 0 || variant >= DRAW_VARIANTS || cat <= 0 || cat >= DC_COUNT) return 0;
    if (bet < 1 || bet > DRAW_MAX_BET) return 0;
    if (cat == DC_ROYAL_FLUSH && bet == DRAW_MAX_BET) return DRAW_ROYAL_MAX_PAY;
    return PAYTABLES[variant].pay[cat] * bet;
}

void draw_pay_vector(int variant, int bet, int out[DC_COUNT])
{
    int i;
    for (i = 0; i < DC_COUNT; i++) out[i] = 0;
    if (variant < 0 || variant >= DRAW_VARIANTS) return;
    for (i = 0; i < DC_COUNT; i++) out[i] = PAYTABLES[variant].pay[i];
    if (bet == DRAW_MAX_BET) out[DC_ROYAL_FLUSH] = DRAW_ROYAL_MAX_PAY / DRAW_MAX_BET;
}

/* ---- classification from rank counts ---------------------------------- */

int draw_cat_from_counts(int variant, const uint8_t c[13], unsigned mask, int flush)
{
    if (variant == DRAW_DEUCES) return draw_dw_from_counts(c, mask, flush);
    return draw_jb_from_counts(variant, c, mask, flush);
}

int draw_classify_deuces(const Card h[5])
{
    uint8_t c[13] = {0};
    unsigned mask = 0, suits = 0;
    int i;
    for (i = 0; i < 5; i++) {
        int r = card_rank(h[i]);
        c[r]++;
        if (r != RANK_2) {
            mask |= 1u << r;
            suits |= 1u << card_suit(h[i]);
        }
    }
    /* Naturals all of one suit are necessarily distinct ranks. */
    return draw_dw_from_counts(c, mask, __builtin_popcount(suits) == 1);
}

int draw_classify(int variant, const Card h[5])
{
    if (variant == DRAW_DEUCES) return draw_classify_deuces(h);
    if (variant < 0 || variant >= DRAW_VARIANTS) return DC_NONE;
    return g_rank_cat[variant][eval5(h)];
}

int draw_tier(int cat, int64_t amount, int bet)
{
    if (amount <= 0) return DT_NONE;
    if (bet < 1) bet = 1;
    if (cat == DC_ROYAL_FLUSH || cat == DC_FOUR_DEUCES || amount >= 200LL * bet) return DT_JACKPOT;
    if (amount >= 25LL * bet) return DT_BIG;
    if (amount >= 5LL * bet) return DT_MEDIUM;
    return DT_SMALL;
}
