/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
#ifndef BPL_DRAW_STRATEGY_H
#define BPL_DRAW_STRATEGY_H

#include <stddef.h>
#include <stdint.h>

#include "engine/card.h"

/* games/draw/draw_strategy.h - suit-canonical hand classes and the
   precomputed optimal-hold tables (assets/strategy/<variant>.bin, written by
   tools/draw_strategy_gen). The live game does not need these: the hint is
   computed exactly per hand. They serve the fast simulations and the
   cross-check of the live hint.

   Classes. A hand is four 13-bit rank masks, one per suit. Relabelling the
   suits permutes the masks and changes nothing about the play (no variant
   treats a suit specially), so a hand's class is its masks sorted in
   descending order, packed into a 52-bit key:
       key = m0 << 39 | m1 << 26 | m2 << 13 | m3,   m0 >= m1 >= m2 >= m3.
   There are 134,459 classes; class i is the i-th key in ascending order and
   stands for weight(i) hands (24 over the symmetries of its masks), which
   sum to 2,598,960.

   Canonical card order: the class's representative hand gives slot j the
   suit j (clubs for slot 0 ...), and its cards are sorted by slot then rank
   ascending. Table hold masks refer to that order; draw_strategy_hold maps
   them back onto the caller's card order. */

#define DRAW_CLASSES   134459
#define DRAW_HANDS     2598960

/* Builds the class list once (a few ms); idempotent and thread-safe. The
   other class functions call it. */
void     draw_classes_init(void);
uint64_t draw_class_key(int idx);
int      draw_class_weight(int idx);
void     draw_class_hand(int idx, Card out[5]);     /* canonical card order */

/* The class of a hand (-1 for an invalid hand). If pos is not NULL,
   pos[i] is the canonical position (0..4) of hand card i. */
int      draw_class_of(const Card hand[5], uint8_t pos[5]);

/* Table file sections: the optimal holds at 5 coins (royal 4000) and at 1-4
   coins (royal 250 per coin), which differ for a few classes. */
enum { DRAW_SECT_MAX = 0, DRAW_SECT_LOW = 1, DRAW_SECTS = 2 };

typedef struct {
    int      variant;
    /* Exact class-weighted return of the table's holds per coin:
       ret_num / ret_den (ret_den = 2,598,960 * 7,669,695, the LCM of
       C(47, 0..5) times the hands). */
    int64_t  ret_num[DRAW_SECTS], ret_den[DRAW_SECTS];
    uint8_t  hold[DRAW_SECTS][DRAW_CLASSES];        /* canonical-order masks */
} DrawStrategyTable;

#define DRAW_EV_LCM 7669695LL                        /* lcm C(47, 0..5)     */

static inline int draw_strategy_sect(int bet) { return bet >= 5 ? DRAW_SECT_MAX : DRAW_SECT_LOW; }

/* Returns the canonical-order mask as a mask over hand's own card order. */
uint8_t  draw_hold_from_canonical(uint8_t canon_mask, const uint8_t pos[5]);

/* The table's hold for a hand (bit i = hand card i); 0 for an invalid hand. */
uint8_t  draw_strategy_hold(const DrawStrategyTable *t, int bet, const Card hand[5]);

/* File I/O. The format (little-endian):
       "BPLSTRAT" u32 version(1) u32 variant u32 classes u32 sections
       per section: i64 ret_num, i64 ret_den, u32 FNV-1a of the masks,
                    then `classes` mask bytes
   Load returns 0, or -1 (missing, short, wrong variant or checksum). */
int      draw_strategy_save(const DrawStrategyTable *t, const char *path);
int      draw_strategy_load(DrawStrategyTable *t, int variant, const char *path);

/* Asset file name for a variant: "job.bin", "bonus.bin", "deuces.bin". */
const char *draw_strategy_file(int variant);

#endif
