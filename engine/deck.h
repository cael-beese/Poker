/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
#ifndef BPL_ENGINE_DECK_H
#define BPL_ENGINE_DECK_H

#include "card.h"
#include "rng.h"

/* c[0..n) are the cards in the deck; c[pos..n) are the ones not yet dealt.
   A flat struct with no pointers, so it copies and saves with the game. */
typedef struct { Card c[52]; int n, pos; } Deck;

void deck_init(Deck *d);                            /* 52 cards in order        */
void deck_shuffle(Deck *d, Rng *r);                 /* Fisher-Yates, all n cards;
                                                       also resets pos to 0      */
Card deck_draw(Deck *d);                            /* CARD_NONE when empty     */
void deck_init_without(Deck *d, const Card *known, int n);  /* for simulations  */

/* Cards not yet dealt. */
static inline int deck_left(const Deck *d) { return d->n - d->pos; }

/* Puts every card back (pos = 0) without changing the order. */
static inline void deck_reset(Deck *d) { d->pos = 0; }

/* Deals one uniformly random card from the undealt part: an incremental
   Fisher-Yates step (swap a random undealt card into position pos, deal it).
   Dealing k cards this way is exactly as uniform as a full shuffle followed by
   k draws, but costs k RNG calls instead of n, which matters in Monte Carlo
   loops. For SIMULATIONS ONLY (AI equity, strategy tools): the honesty rules
   require game cards to come from deck_shuffle + deck_draw. */
Card deck_draw_random(Deck *d, Rng *r);

#endif
