/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
#ifndef BPL_DRAW_INTERNAL_H
#define BPL_DRAW_INTERNAL_H

/* Classification from rank counts, inline so the hint's enumeration (tens of
   thousands of leaves per hand) does not pay a call per leaf. Only for
   games/draw sources; the public entry point is draw_cat_from_counts.

   The Deuces Wild rules, each step being the best category still possible
   so the first match pays most (the exhaustive test in tests/draw proves
   it against substituting every card for every deuce):
     - d == 4 is four deuces, whatever the fifth card.
     - If the naturals are all one suit (so distinct ranks): a natural royal
       (d == 0, T-A), a wild royal (d > 0, naturals within T-A), a straight
       flush (naturals fit one five-rank window, the wheel included), four
       of a kind (d == 3, which beats a plain flush), else a flush.
     - Otherwise with m the largest natural count: five of a kind if
       m + d >= 5, four if m + d >= 4, a full house from 3+2 naturals or two
       pairs plus a deuce, a straight if the distinct naturals fit a window,
       three of a kind if m + d >= 3.                                       */

#include <stdint.h>

#include "draw_rules.h"

#define DRAW_ROYAL_MASK 0x1F00u   /* T J Q K A */

extern uint8_t draw_straight5_tab[8192];   /* mask is exactly a five-rank straight */
extern uint8_t draw_window_tab[8192];      /* mask fits inside a straight window   */

static inline int draw_quad_cat(int variant, int r)
{
    if (variant != DRAW_BONUS) return DC_FOUR_KIND;
    if (r == RANK_A) return DC_FOUR_ACES;
    if (r <= RANK_4) return DC_FOUR_2_4;
    return DC_FOUR_5_K;
}

static inline int draw_jb_from_counts(int variant, const uint8_t c[13], unsigned mask, int flush)
{
    int nd = __builtin_popcount(mask);
    unsigned m;
    if (nd == 5) {
        int st = draw_straight5_tab[mask];
        if (flush) return st ? (mask == DRAW_ROYAL_MASK ? DC_ROYAL_FLUSH : DC_STRAIGHT_FLUSH) : DC_FLUSH;
        return st ? DC_STRAIGHT : DC_NONE;
    }
    if (nd == 2) {                           /* 4+1 or 3+2 */
        for (m = mask; m; m &= m - 1) {
            int r = __builtin_ctz(m);
            if (c[r] == 4) return draw_quad_cat(variant, r);
        }
        return DC_FULL_HOUSE;
    }
    if (nd == 3) {                           /* 3+1+1 or 2+2+1 */
        for (m = mask; m; m &= m - 1)
            if (c[__builtin_ctz(m)] == 3) return DC_THREE_KIND;
        return DC_TWO_PAIR;
    }
    /* nd == 4: one pair */
    for (m = mask & 0x1E00u; m; m &= m - 1)  /* J Q K A */
        if (c[__builtin_ctz(m)] == 2) return DC_JACKS_OR_BETTER;
    return DC_NONE;
}

static inline int draw_dw_from_counts(const uint8_t c[13], unsigned mask, int flush)
{
    int d = c[0], maxc = 0, nd;
    unsigned m;
    if (d == 4) return DC_FOUR_DEUCES;
    if (flush) {
        if (d == 0 && mask == DRAW_ROYAL_MASK) return DC_ROYAL_FLUSH;
        if (d > 0 && (mask & ~DRAW_ROYAL_MASK) == 0) return DC_WILD_ROYAL;
        if (draw_window_tab[mask]) return DC_STRAIGHT_FLUSH;
        return d == 3 ? DC_FOUR_KIND : DC_FLUSH;
    }
    for (m = mask; m; m &= m - 1) {
        int k = c[__builtin_ctz(m)];
        if (k > maxc) maxc = k;
    }
    if (maxc + d >= 5) return DC_FIVE_KIND;
    if (maxc + d >= 4) return DC_FOUR_KIND;
    nd = __builtin_popcount(mask);
    if (nd == 2 && ((d == 0 && maxc == 3) || (d == 1 && maxc == 2))) return DC_FULL_HOUSE;
    if (nd == 5 - d && draw_window_tab[mask]) return DC_STRAIGHT;
    if (maxc + d >= 3) return DC_THREE_KIND;
    return DC_NONE;
}

#endif
