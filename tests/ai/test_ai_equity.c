/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* Equity accuracy: Monte Carlo at the AI's default trial count against exact
   enumeration of every opponent hand and every runout, for known spots -
   pre-flop hand against hand and against a range, flop / turn / river
   against a random hand, against the AI's own weighted range model, and
   against two opponents (which exercises the importance-weighted sampler).
   Each result must be within 4 standard errors of the exact value. */

#include "ai/ai.h"
#include "engine/eval.h"
#include "test_util.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static uint32_t CK[52];

/* Exact equity of hole against one or two opponents with the given combo
   weights (NULL = every combo), over every runout of the board. */
typedef struct { double num, den; } Acc;

static void runouts(const uint32_t *fixed7, int nfixed, const Card *rest, int nrest,
                    int need, int start, uint32_t *h, const uint32_t opp[][2], int nopp,
                    Acc *acc, double w)
{
  int i;
  if (need == 0) {
    uint32_t c[7];
    int mine, o, best = 1, ties = 0;
    memcpy(c, h, sizeof c);
    mine = eval7_ck(c);
    for (o = 0; o < nopp && best; o++) {
      int r;
      c[0] = opp[o][0];
      c[1] = opp[o][1];
      r = eval7_ck(c);
      if (r < mine) best = 0;
      else if (r == mine) ties++;
    }
    acc->num += best ? w / (double)(ties + 1) : 0.0;
    acc->den += w;
    return;
  }
  (void)fixed7;
  for (i = start; i <= nrest - need; i++) {
    h[7 - need] = CK[rest[i]];
    runouts(fixed7, nfixed, rest, nrest, need - 1, i + 1, h, opp, nopp, acc, w);
  }
}

static double exact(const Card hole[2], const Card *board, int nboard,
                    const float *w1, int two, const float *w2)
{
  Acc acc = {0, 0};
  uint64_t known = 0;
  uint32_t h[7];
  int i, a, b;
  for (i = 0; i < 2; i++) known |= 1ull << hole[i];
  for (i = 0; i < nboard; i++) known |= 1ull << board[i];
  h[0] = CK[hole[0]];
  h[1] = CK[hole[1]];
  for (i = 0; i < nboard; i++) h[2 + i] = CK[board[i]];
  for (a = 0; a < AI_NCOMBOS; a++) {
    Card ca[2];
    double wa;
    ai_combo_cards(a, ca);
    if (known & ((1ull << ca[0]) | (1ull << ca[1]))) continue;
    wa = w1 ? w1[a] : 1.0;
    if (wa <= 0) continue;
    for (b = two ? 0 : -1; b < (two ? AI_NCOMBOS : 0); b++) {
      Card cb[2], rest[52];
      uint64_t used = known | (1ull << ca[0]) | (1ull << ca[1]);
      uint32_t opp[2][2];
      double wb = 1.0;
      int nrest = 0, c;
      if (b >= 0) {
        ai_combo_cards(b, cb);
        if (used & ((1ull << cb[0]) | (1ull << cb[1]))) continue;
        wb = w2 ? w2[b] : 1.0;
        if (wb <= 0) continue;
        used |= (1ull << cb[0]) | (1ull << cb[1]);
        opp[1][0] = CK[cb[0]];
        opp[1][1] = CK[cb[1]];
      }
      opp[0][0] = CK[ca[0]];
      opp[0][1] = CK[ca[1]];
      for (c = 0; c < 52; c++) if (!(used & (1ull << c))) rest[nrest++] = (Card)c;
      {
        Acc one = {0, 0};
        runouts(h, 2 + nboard, rest, nrest, 5 - nboard, 0, h, (const uint32_t(*)[2])opp,
                b >= 0 ? 2 : 1, &one, 1.0);
        acc.num += wa * wb * one.num / one.den;
        acc.den += wa * wb;
      }
    }
  }
  return acc.num / acc.den;
}

static int g_spots;

static void spot(const char *name, const char *hole_s, const char *board_s,
                 const float *w1, int two, const float *w2, int use_model,
                 const AiRange *model, double known_exact)
{
  Card hole[2], board[5];
  int nb = board_s ? cards_parse(board_s, board, 5) : 0;
  int nopp = two ? 2 : 1, trials = ai_default_trials(nopp);
  double ex, mc, se;
  const float *ws[2];
  Rng r;
  cards_parse(hole_s, hole, 2);
  rng_seed(&r, 0xE0u + (uint64_t)g_spots++);
  ex = known_exact >= 0 ? known_exact : exact(hole, board, nb, w1, two, w2);
  if (use_model) {
    mc = ai_equity(hole, board, nb, nopp, model, trials, &r);
  } else {
    ws[0] = w1;
    ws[1] = w2;
    mc = ai_equity_w(hole, board, nb, nopp, (w1 || w2) ? ws : NULL, trials, &r);
  }
  se = sqrt(ex * (1.0 - ex) / trials);
  if (se < 0.002) se = 0.002;
  printf("  %-40s exact %6.2f%%  MC %6.2f%%  err %+5.2f pts (%4.1f se)  %d trials\n",
         name, 100 * ex, 100 * mc, 100 * (mc - ex), fabs(mc - ex) / se, trials);
  CHECK(fabs(mc - ex) <= 4.0 * se);
}

static float W_A[AI_NCOMBOS], W_B[AI_NCOMBOS];

static void weights_of(const char *list, float *w)
{
  /* "QdQc QdQh ..." -> weight 1 on those combos, 0 elsewhere */
  Card c[40];
  int n = cards_parse(list, c, 40), i;
  memset(w, 0, sizeof(float) * AI_NCOMBOS);
  for (i = 0; i + 1 < n; i += 2) w[ai_combo_index(c[i], c[i + 1])] = 1.0f;
}

int main(void)
{
  int i;
  double t0 = test_now();
  ai_init();
  for (i = 0; i < 52; i++) CK[i] = eval_ck((Card)i);

  printf("equity: Monte Carlo at the default trial count vs exact enumeration\n");
  weights_of("Kd Kc", W_A);
  spot("AhAs vs KdKc, pre-flop", "Ah As", NULL, W_A, 0, NULL, 0, NULL, -1);
  weights_of("Qc Qd Qc Qh Qc Qs Qd Qh Qd Qs Qh Qs", W_A);
  spot("AhKh vs QQ (all 6 combos), pre-flop", "Ah Kh", NULL, W_A, 0, NULL, 0, NULL, -1);
  /* AA against one random hand: 1225 hands x 1.7M boards is too slow for a
     test; the enumerated value is the published one (85.20%). */
  spot("AA vs random hand, pre-flop (published)", "Ah As", NULL, NULL, 0, NULL, 0, NULL, 0.8520);
  spot("AhKh on Qh7h2c vs random", "Ah Kh", "Qh 7h 2c", NULL, 0, NULL, 0, NULL, -1);
  spot("9s8s on Ts7d2h vs random", "9s 8s", "Ts 7d 2h", NULL, 0, NULL, 0, NULL, -1);
  spot("JcTc on 9c8d2h3s vs random", "Jc Tc", "9c 8d 2h 3s", NULL, 0, NULL, 0, NULL, -1);
  spot("AdQs on Qc9h4d8s2c vs random", "Ad Qs", "Qc 9h 4d 8s 2c", NULL, 0, NULL, 0, NULL, -1);
  {
    /* The AI's own range model: a pre-flop raiser who bet the flop. */
    AiRange m = { 0.15f, 1, 0 };
    Card hole[2], board[3], known[5];
    cards_parse("Jh Js", hole, 2);
    cards_parse("Kd 8c 3h", board, 3);
    memcpy(known, hole, 2);
    memcpy(known + 2, board, 3);
    ai_range_weights(&m, known, 5, board, 3, W_A);
    spot("JJ on Kd8c3h vs model(top 15%, bet flop)", "Jh Js", "Kd 8c 3h", W_A, 0, NULL, 1, &m, -1);
  }
  {
    /* Two opponents on the river: a model range and a random hand; the
       weighted multi-opponent path with importance weights. */
    AiRange m[2] = { { 0.10f, 1, 0 }, { 1.0f, 0, 0 } };
    Card hole[2], board[5], known[7];
    cards_parse("Ah Qh", hole, 2);
    cards_parse("Qd Tc 6s 5h 2d", board, 5);
    memcpy(known, hole, 2);
    memcpy(known + 2, board, 5);
    ai_range_weights(&m[0], known, 7, board, 5, W_A);
    spot("AQ on QdTc6s5h2d vs model + random", "Ah Qh", "Qd Tc 6s 5h 2d", W_A, 1, NULL, 1, m, -1);
    /* Two tight ranges that fight over the same cards. */
    weights_of("As Ks Ac Kc Ad Kd Ks Kc Kd Kh Kc Kh", W_A);
    weights_of("As Ad Ac Ad As Ac Ks Kc Ac Kd Js Jc", W_B);
    spot("TT on Jh9s4c3d2h vs two tight lists", "Th Td", "Jh 9s 4c 3d 2h", W_A, 1, W_B, 0, NULL, -1);
  }
  spot("7c7d on Ks8h2c5d vs 2 random, turn", "7c 7d", "Ks 8h 2c 5d", NULL, 1, NULL, 0, NULL, -1);

  /* The generated starting-hand table agrees with fresh Monte Carlo. */
  {
    static const char *const hands[6] = { "As Ad", "Ah Kh", "7s 6s", "Kc 9d", "7h 2c", "2s 2d" };
    Rng r;
    rng_seed(&r, 424242);
    for (i = 0; i < 6; i++) {
      Card h[2];
      int cls;
      double e1, e3;
      cards_parse(hands[i], h, 2);
      cls = ai_hand_class(h[0], h[1]);
      e1 = ai_equity(h, NULL, 0, 1, NULL, 60000, &r);
      e3 = ai_equity(h, NULL, 0, 3, NULL, 60000, &r);
      CHECK(fabs(e1 - ai_class_eq1(cls)) < 4 * sqrt(0.25 / 60000) + 0.001);
      CHECK(fabs(e3 - ai_class_eq3(cls)) < 4 * sqrt(0.25 / 60000) + 0.001);
    }
    CHECK(ai_class_pct(ai_hand_class(card_make(RANK_A, 0), card_make(RANK_A, 1))) < 0.01f);
    CHECK(ai_class_pct(ai_hand_class(card_make(RANK_7, 0), card_make(RANK_2, 1))) > 0.9f);
    printf("  generated class table matches fresh Monte Carlo (6 classes, 1 and 3 opponents)\n");
  }
  printf("  %.1f s\n", test_now() - t0);
  return test_finish("ai_equity_accuracy");
}
