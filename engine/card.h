/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
#ifndef BPL_ENGINE_CARD_H
#define BPL_ENGINE_CARD_H

#include <stdint.h>

/* A card is one byte: rank * 4 + suit. Rank 0 is the deuce and 12 the ace;
   suit 0 clubs, 1 diamonds, 2 hearts, 3 spades. */
typedef uint8_t Card;                 /* 0..51 = rank*4 + suit              */
#define CARD_NONE 0xFF                /* rank 0 = deuce .. 12 = ace;         */
enum { SUIT_C, SUIT_D, SUIT_H, SUIT_S };/* suit 0 clubs 1 diamonds 2 hearts 3 spades */

enum { RANK_2 = 0, RANK_3, RANK_4, RANK_5, RANK_6, RANK_7, RANK_8, RANK_9,
       RANK_T, RANK_J, RANK_Q, RANK_K, RANK_A };

static inline int  card_rank(Card c){ return c >> 2; }
static inline int  card_suit(Card c){ return c & 3; }
static inline Card card_make(int rank, int suit){ return (Card)(rank*4 + suit); }
static inline int  card_valid(Card c){ return c < 52; }

/* "As", "Td", "2c"; "??" for anything that is not a card. Returns out. */
const char *card_str(Card c, char out[3]);

/* Parses exactly one card: a rank from "23456789TJQKA" (or "10"), then a
   suit from "cdhs", either case, and nothing after it. 0 ok, -1 bad. */
int         card_parse(const char *s, Card *out);

/* Parses a whitespace-separated list such as "As Kd 7h". Returns the number
   of cards written (at most max), or -1 on a bad card or too many cards.
   Duplicates are not rejected; that is the caller's business. */
int         cards_parse(const char *s, Card *out, int max);

/* Single characters for display: "23456789TJQKA"[rank], "cdhs"[suit]. */
extern const char CARD_RANK_CHARS[14];
extern const char CARD_SUIT_CHARS[5];

#endif
