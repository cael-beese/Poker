/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
#ifndef BPL_ENGINE_EVAL_H
#define BPL_ENGINE_EVAL_H

#include <stddef.h>
#include <stdint.h>

#include "card.h"

/* engine/eval.h - Cactus Kev style poker hand evaluator, small tables built
   at start-up by enumeration (512 KB in all, built in about 10 ms on x86;
   no copied magic tables). See eval.c for the design.

   Ranks run 1 = royal flush .. 7462 = 7-5-4-3-2 unsuited; LOWER IS BETTER.
   Two hands tie exactly when their ranks are equal. The 7462 values are the
   standard equivalence classes:
       1..10     straight flush (1 is the royal)      10
       11..166   four of a kind                       156
       167..322  full house                           156
       323..1599 flush                               1277
       1600..1609 straight                             10
       1610..2467 three of a kind                     858
       2468..3325 two pair                            858
       3326..6185 one pair                           2860
       6186..7462 high card                          1277

   Call eval_init() once at start-up, before any other eval function (the
   evaluators do not check, to stay branch-free in hot loops; before init
   every evaluator returns 0). Cards passed to eval5/6/7 must be valid
   (0..51) and distinct; eval_best checks validity and returns 0 if not. */

void eval_init(void);                               /* idempotent, thread-safe after first call */
int  eval5(const Card c[5]);                        /* 1 = royal flush .. 7462 = 7-5-4-3-2 off  */
int  eval7(const Card c[7]);                        /* best five of seven, same scale           */
int  eval_best(const Card *c, int n, Card best[5]); /* n = 5..7; rank + the five used           */
enum HandCat { HC_STRAIGHT_FLUSH = 1, HC_QUADS, HC_FULL_HOUSE, HC_FLUSH, HC_STRAIGHT,
               HC_TRIPS, HC_TWO_PAIR, HC_PAIR, HC_HIGH_CARD };
int  eval_category(int rank);                       /* HandCat; 0 if rank is not 1..7462 */
int  eval_is_royal(int rank);                       /* rank == 1                */
const char *eval_category_name(int cat);            /* "FULL HOUSE" ...         */
/* hot-loop form for Monte Carlo: cards pre-converted once */
uint32_t eval_ck(Card c);                           /* Cactus Kev 32-bit card   */
int  eval5_ck(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e);
int  eval7_ck(const uint32_t ck[7]);

/* ---- additions --------------------------------------------------------- */

#define EVAL_WORST_RANK 7462

/* Six cards, best five, same scale (for turn equity and eval_best). */
int  eval6(const Card c[6]);

/* The five card ranks (0 = deuce .. 12 = ace) that define a hand rank, in
   order of significance: quads then kicker, trips then pair, both pairs
   high first then kicker, straights high card first (the wheel is
   5,4,3,2,A as 3,2,1,0,12). Returns 0, or -1 if rank is out of range. */
int  eval_rank_ranks(int rank, int out[5]);

/* A readable description: "Royal Flush", "Straight Flush, King High",
   "Four Aces", "Kings Full of Tens", "Flush, Ace High", "Straight, Five High",
   "Three Sevens", "Aces and Eights", "Pair of Jacks", "Ace High".
   Returns out ("" for a bad rank). */
const char *eval_describe(int rank, char *out, size_t cap);

/* Names for a single rank: "Ace"/"Aces", "Six"/"Sixes". */
const char *eval_rank_word(int rank, int plural);

/* Bytes of lookup tables eval_init() builds (for the debug overlay). */
size_t eval_table_bytes(void);

#endif
