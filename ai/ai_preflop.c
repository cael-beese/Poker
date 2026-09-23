/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* Starting-hand classes and the charts built from them.

   Nothing here is a hand-typed chart. Each of the 169 classes has a
   measured equity against one and against three random hands (generated
   by tools/ai_gen_preflop.c); the classes are ranked by those numbers at
   start-up and every chart is a cut of that ranking by a fraction of all
   1326 hands. Position, stack depth, personality and table size then only
   choose where to cut. */

#include "ai/ai_internal.h"

#include <math.h>
#include <string.h>

#include "ai/ai_preflop_data.h"

static float g_pct_play[AI_NCLASSES];
static float g_pct_allin[AI_NCLASSES];

int ai_hand_class(Card a, Card b)
{
  int ra = card_rank(a), rb = card_rank(b);
  int hi = ra > rb ? ra : rb, lo = ra > rb ? rb : ra;
  if (hi == lo) return hi * 13 + hi;
  if (card_suit(a) == card_suit(b)) return hi * 13 + lo;
  return lo * 13 + hi;
}

const char *ai_class_name(int cls, char out[4])
{
  int row, col;
  if (cls < 0 || cls >= AI_NCLASSES) { strcpy(out, "??"); return out; }
  row = cls / 13;
  col = cls % 13;
  if (row == col) {
    out[0] = out[1] = CARD_RANK_CHARS[row];
    out[2] = 0;
  } else {
    int hi = row > col ? row : col, lo = row > col ? col : row;
    out[0] = CARD_RANK_CHARS[hi];
    out[1] = CARD_RANK_CHARS[lo];
    out[2] = row > col ? 's' : 'o';
    out[3] = 0;
  }
  return out;
}

int ai_class_combos(int cls)
{
  int row = cls / 13, col = cls % 13;
  return row == col ? 6 : row > col ? 4 : 12;
}

float ai_class_eq1(int cls) { return cls >= 0 && cls < AI_NCLASSES ? AI_PF_EQ1[cls] / 10000.0f : 0.0f; }
float ai_class_eq3(int cls) { return cls >= 0 && cls < AI_NCLASSES ? AI_PF_EQ3[cls] / 10000.0f : 0.0f; }

float ai_class_pct(int cls)
{
  ai_init();
  return cls >= 0 && cls < AI_NCLASSES ? g_pct_play[cls] : 1.0f;
}

float ai_class_pct_allin(int cls)
{
  ai_init();
  return cls >= 0 && cls < AI_NCLASSES ? g_pct_allin[cls] : 1.0f;
}

/* Sorts classes by score (best first) and turns positions into the
   fraction of all hands at or above each class's midpoint. */
static void rank_classes(const float score[AI_NCLASSES], float pct[AI_NCLASSES])
{
  int order[AI_NCLASSES], i, j, cum = 0;
  for (i = 0; i < AI_NCLASSES; i++) order[i] = i;
  for (i = 1; i < AI_NCLASSES; i++) {       /* insertion sort, stable */
    int x = order[i];
    for (j = i; j > 0 && score[order[j - 1]] < score[x]; j--) order[j] = order[j - 1];
    order[j] = x;
  }
  for (i = 0; i < AI_NCLASSES; i++) {
    int n = ai_class_combos(order[i]);
    pct[order[i]] = ((float)cum + 0.5f * (float)n) / (float)AI_NCOMBOS;
    cum += n;
  }
}

void ai_preflop_build(void)
{
  float play[AI_NCLASSES], allin[AI_NCLASSES];
  int c;
  for (c = 0; c < AI_NCLASSES; c++) {
    /* Equity relative to a fair share: heads-up equity alone overrates
       offsuit high cards, three-way equity alone overrates suited
       connectors; a pot the AI plays is somewhere between. */
    float r1 = 2.0f * AI_PF_EQ1[c] / 10000.0f;
    float r3 = 4.0f * AI_PF_EQ3[c] / 10000.0f;
    play[c] = 0.55f * r1 + 0.45f * r3;
    allin[c] = r1;
  }
  rank_classes(play, g_pct_play);
  rank_classes(allin, g_pct_allin);
}

float ai_push_range(float bb, int n_behind)
{
  /* Fitted to published no-ante Nash push charts for the first player in:
     at 10 BB roughly SB 58%, BTN 36%, CO 27%, HJ 21%, UTG 17%, widening as
     the stack shrinks and narrowing as it grows. */
  static const float base[6] = { 0.58f, 0.58f, 0.36f, 0.27f, 0.21f, 0.17f };
  float p;
  if (n_behind < 0) n_behind = 0;
  if (n_behind > 5) n_behind = 5;
  if (!(bb > 1.0f)) bb = 1.0f;
  p = base[n_behind] * powf(10.0f / bb, 0.7f);
  if (p < 0.03f) p = 0.03f;
  if (p > 1.0f) p = 1.0f;
  return p;
}

float ai_open_range(int n_behind, int ndealt)
{
  /* Standard 6-max opening ranges by position; a shorter table simply
     starts later in the order, because n_behind counts who is left. */
  static const float open[6] = { 0.40f, 0.40f, 0.45f, 0.27f, 0.19f, 0.15f };
  if (ndealt <= 2) return 0.80f;            /* heads-up button: raise wide */
  if (n_behind < 0) n_behind = 0;
  if (n_behind > 5) n_behind = 5;
  return open[n_behind];
}
