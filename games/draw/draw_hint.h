/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
#ifndef BPL_DRAW_HINT_H
#define BPL_DRAW_HINT_H

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>

#include "draw_rules.h"
#include "engine/card.h"

/* games/draw/draw_hint.h - the strategy hint: the exact expected return of
   all 32 hold patterns for the player's own five cards, under the current
   variant and bet. It only reads the five cards; it never sees the deck and
   never changes the game.

   A hold pattern is a 5-bit mask, bit i = card i held (the same numbering as
   BTN_HOLD_N(i) and DrawGame.held).

   Exact: for hold mask m holding h cards, the k = 5 - h replacements are a
   uniform k-subset of the 47 cards not dealt, so
       EV(m) = sum over outcomes of pay / C(47, k)
   is a rational number. `num[m] / den[m]` is that value in credits, exactly;
   `ev[m]` is the same as a double. `ways[m][cat]` counts the replacement
   sets that finish in each pay category (they sum to den[m]).

   Method (draw_hint_compute): instead of evaluating all 2,598,960 final
   hands, it enumerates the rank multisets of the replacement cards (at most
   6,188 per hold, 20,814 over all 32), weighting each by the number of ways
   the 47-card stub can supply it, and separately counts the ways that end
   all of one suit. 50-115x faster than brute force; the tests check both
   give identical integer counts. draw_hint_compute_bruteforce is the
   reference: it deals every replacement set and classifies it. */

typedef struct {
    int      variant, bet;
    uint8_t  best;             /* the best hold mask                          */
    double   best_ev;          /* its EV in credits (bet included)            */
    double   ev[32];           /* EV of every hold mask, in credits           */
    int64_t  num[32];          /* exact EV = num / den credits                */
    int32_t  den[32];          /* C(47, 5 - popcount(mask))                   */
    uint32_t ways[32][DC_COUNT];
} DrawHint;

/* Ties (exactly equal EV) go to the lower mask value, so the answer is
   deterministic. Returns 0, or -1 for a bad variant, bet or hand (invalid or
   repeated cards). Calls draw_rules_init itself. */
int draw_hint_compute(int variant, int bet, const Card hand[5], DrawHint *out);
int draw_hint_compute_bruteforce(int variant, int bet, const Card hand[5], DrawHint *out);

/* Outcome counts of a single hold (fast method); ways[] sums to the return
   value, C(47, 5 - popcount(mask)), or -1 on bad input. */
int32_t draw_hold_ways(int variant, const Card hand[5], unsigned mask, uint32_t ways[DC_COUNT]);

/* Exact comparison of two holds of one hint: <0, 0, >0 like strcmp. */
int draw_hint_cmp(const DrawHint *h, unsigned a, unsigned b);

/* Running the hint off the main thread. The fast method takes well under a
   millisecond on x86, so the synchronous call is fine for the live game; the
   task exists so the app never has to reason about it.
       draw_hint_task_start(&t, variant, bet, cards);   when the hold phase begins
       if (draw_hint_task_ready(&t)) show(&t.result);   each frame, non-blocking
       draw_hint_task_wait(&t);                          before starting another / on exit
   A task must be waited for before it is started again or discarded.     */
typedef struct {
    pthread_t   thread;
    atomic_int  state;         /* 0 idle, 1 running, 2 done (not joined), 3 joined */
    int         variant, bet, rc;
    Card        hand[5];
    DrawHint    result;
} DrawHintTask;

void draw_hint_task_init(DrawHintTask *t);
int  draw_hint_task_start(DrawHintTask *t, int variant, int bet, const Card hand[5]);
int  draw_hint_task_ready(DrawHintTask *t);      /* 1 once result is valid */
const DrawHint *draw_hint_task_wait(DrawHintTask *t);   /* joins; NULL on failure */

#endif
