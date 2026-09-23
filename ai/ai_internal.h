/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
#ifndef BPL_AI_INTERNAL_H
#define BPL_AI_INTERNAL_H

/* Shared between the AI's own source files; not part of its API. */

#include <stdint.h>

#include "ai/ai.h"

/* Tables built once by ai_init(). */
extern uint32_t g_ai_ck[52];                 /* Cactus Kev form of every card  */
extern uint8_t  g_ai_combo[AI_NCOMBOS][2];   /* the two cards of each combo    */
extern uint8_t  g_ai_combo_class[AI_NCOMBOS];

void ai_preflop_build(void);                 /* called once from ai_init()     */

static inline uint64_t ai_card_bit(Card c) { return (uint64_t)1 << c; }

/* 13-bit rank mask has a five-card straight (the wheel counts). */
static inline int ai_mask_has_straight(unsigned m)
{
  unsigned s = m & (m << 1) & (m << 2) & (m << 3) & (m << 4);
  return s != 0 || (m & 0x100Fu) == 0x100Fu;
}

/* Draw information for two hole cards on a flop or turn: whether they make
   a four-card flush draw, and how many ranks would complete a straight that
   the board alone would not (2 = open-ended or double gutter, 1 = gutshot).
   Both are 0 on the river or with no board. */
void ai_draw_info(const Card hole[2], const Card *board, int nboard,
                  int *flush_draw, int *straight_outs);

/* Rank (lower is better) of hole + board for 3..5 board cards. */
int ai_eval_with_board(const Card hole[2], const Card *board, int nboard);

#endif
