/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
#ifndef BPL_DRAW_RULES_H
#define BPL_DRAW_RULES_H

#include <stdint.h>

#include "engine/card.h"

/* games/draw/draw_rules.h - video poker variants, pay categories, paytables
   and the per-variant hand classifiers.

   Pay is per coin; the bet is 1..5 coins. The royal flush pays 250 per coin
   at 1..4 coins and 4000 (800 per coin) at 5 coins, as on real machines.

   Optimal-play returns at 5 coins (exact fractions over 2,598,960 x
   7,669,695, from tools/draw_strategy_gen; tests/draw re-derives them):
       Jacks or Better 9/6   99.5439043695 %   (1-4 coins: 98.3734569484 %)
       Bonus Poker 8/5       99.1659731872 %   (1-4 coins: 97.9315158275 %)
       Deuces Wild full pay 100.7619612039 %   (1-4 coins: 99.5733690687 %)  */

enum DrawVariant { DRAW_JOB = 0, DRAW_BONUS, DRAW_DEUCES, DRAW_VARIANTS };

/* Pay categories, best first. The enum order is the paytable display order
   and the precedence order (a hand is the first category it qualifies for);
   each variant pays only some of them. */
enum DrawCat {
    DC_NONE = 0,
    DC_ROYAL_FLUSH,        /* natural royal flush (no wild card)            */
    DC_FOUR_DEUCES,        /* Deuces Wild                                   */
    DC_WILD_ROYAL,         /* Deuces Wild: royal flush using a deuce        */
    DC_FIVE_KIND,          /* Deuces Wild                                   */
    DC_STRAIGHT_FLUSH,
    DC_FOUR_ACES,          /* Bonus Poker                                   */
    DC_FOUR_2_4,           /* Bonus Poker: four 2s, 3s or 4s                */
    DC_FOUR_5_K,           /* Bonus Poker: four 5s through Ks               */
    DC_FOUR_KIND,          /* Jacks or Better, Deuces Wild                  */
    DC_FULL_HOUSE,
    DC_FLUSH,
    DC_STRAIGHT,
    DC_THREE_KIND,
    DC_TWO_PAIR,           /* not paid in Deuces Wild                       */
    DC_JACKS_OR_BETTER,    /* a pair of jacks, queens, kings or aces        */
    DC_COUNT
};

/* Win tiers for presentation (see draw_tier). */
enum DrawTier { DT_NONE = 0, DT_SMALL, DT_MEDIUM, DT_BIG, DT_JACKPOT };

#define DRAW_MAX_BET 5
#define DRAW_ROYAL_MAX_PAY 4000   /* credits, at 5 coins */

typedef struct {
    int         variant;
    const char *name;            /* "JACKS OR BETTER"                     */
    const char *short_name;      /* "JOB", "BONUS", "DEUCES" (hand log)    */
    int         pay[DC_COUNT];   /* per coin at 1..4 coins; 0 = not paid   */
    int         rows;            /* paid categories, in display order      */
    uint8_t     row_cat[DC_COUNT];
    double      optimal_return;  /* at 5 coins, e.g. 0.995439 (4 decimals of %) */
} DrawPaytable;

/* Must be called once before classifying (it calls eval_init). Idempotent
   and thread-safe. draw_init does it for the game. */
void draw_rules_init(void);

const DrawPaytable *draw_paytable(int variant);     /* NULL if out of range */
const char *draw_cat_name(int cat);                 /* "FULL HOUSE", "" for none */
const char *draw_variant_name(int variant);

/* Credits paid for category cat at a bet of 1..5 coins (0 for DC_NONE,
   unpaid categories and bad arguments). */
int  draw_pay(int variant, int cat, int bet);

/* Per-coin pay vector for a bet: the paytable with the royal at 800 per coin
   when bet == 5. Expected values in coins times bet are credits. */
void draw_pay_vector(int variant, int bet, int out[DC_COUNT]);

/* The pay category of five distinct valid cards under a variant. */
int  draw_classify(int variant, const Card h[5]);

/* Deuces Wild evaluator: counts the deuces and analyses the natural cards.
   Exact for all 2,598,960 hands (tests/draw checks it against substituting
   every possible card for each deuce). */
int  draw_classify_deuces(const Card h[5]);

/* Tier of a win of `amount` credits at `bet` coins:
     JACKPOT  natural royal, four deuces, or >= 200 x bet (double-up included)
     BIG      >= 25 x bet  (straight flush, quads in JoB/Bonus, wild royal)
     MEDIUM   >= 5 x bet   (full house, flush in JoB/Bonus, five of a kind)
     SMALL    any other win; NONE for 0.                                    */
int  draw_tier(int cat, int64_t amount, int bet);

/* ---- internals shared by the hint and the tests ---------------------- */

/* Category from final rank counts. c[13] are the counts per rank; mask is
   the set of ranks with a non-zero count, EXCLUDING rank 0 for Deuces Wild
   (deuces are wild there, and c[0] is their count). flush is 1 when the
   cards (the natural cards, in Deuces Wild) are all one suit; the caller
   guarantees that implies distinct ranks. */
int  draw_cat_from_counts(int variant, const uint8_t c[13], unsigned mask, int flush);

#endif
