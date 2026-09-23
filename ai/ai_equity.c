/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* Monte Carlo equity against opponents whose cards the AI cannot see.

   The only cards this file ever looks at are the AI's own two hole cards
   and the public board, passed in by the caller. Opponent hands and the
   rest of the board are drawn at random from the cards the AI has not
   seen - every unseen card is equally possible, because to the AI it is.
   A range model (from public actions only) reweights which unseen
   two-card combos an opponent is likely to hold; it never excludes a
   combo outright, since humans play anything.

   Determinism: a fixed trial count, all randomness from the caller's Rng,
   integer accumulation of pot shares. Same inputs + same Rng state = the
   same result, bit for bit, on any thread. */

#include "ai/ai_internal.h"

#include "engine/eval.h"

#include <math.h>
#include <pthread.h>
#include <string.h>

uint32_t g_ai_ck[52];
uint8_t  g_ai_combo[AI_NCOMBOS][2];
uint8_t  g_ai_combo_class[AI_NCOMBOS];
static uint64_t g_combo_mask[AI_NCOMBOS];

static pthread_once_t g_ai_once = PTHREAD_ONCE_INIT;

static void ai_build(void)
{
  int a, b;
  eval_init();
  for (a = 0; a < 52; a++) g_ai_ck[a] = eval_ck((Card)a);
  for (b = 1; b < 52; b++)
    for (a = 0; a < b; a++) {
      int i = ai_combo_index((Card)a, (Card)b);
      g_ai_combo[i][0] = (uint8_t)a;
      g_ai_combo[i][1] = (uint8_t)b;
      g_combo_mask[i] = ai_card_bit((Card)a) | ai_card_bit((Card)b);
      g_ai_combo_class[i] = (uint8_t)ai_hand_class((Card)a, (Card)b);
    }
  ai_preflop_build();
}

void ai_init(void)
{
  pthread_once(&g_ai_once, ai_build);
}

void ai_combo_cards(int idx, Card out[2])
{
  ai_init();
  if (idx < 0 || idx >= AI_NCOMBOS) { out[0] = out[1] = CARD_NONE; return; }
  out[0] = g_ai_combo[idx][0];
  out[1] = g_ai_combo[idx][1];
}

int ai_eval_with_board(const Card hole[2], const Card *board, int nboard)
{
  Card c[7];
  int i;
  c[0] = hole[0];
  c[1] = hole[1];
  for (i = 0; i < nboard && i < 5; i++) c[2 + i] = board[i];
  if (nboard >= 5) return eval7(c);
  if (nboard == 4) return eval6(c);
  return eval5(c);
}

void ai_draw_info(const Card hole[2], const Card *board, int nboard,
                  int *flush_draw, int *straight_outs)
{
  int suit_all[4] = {0, 0, 0, 0}, suit_hole[4] = {0, 0, 0, 0};
  unsigned mall = 0, mboard = 0;
  int i, s, outs = 0;
  *flush_draw = 0;
  *straight_outs = 0;
  if (nboard < 3 || nboard > 4) return;
  for (i = 0; i < 2; i++) {
    suit_all[card_suit(hole[i])]++;
    suit_hole[card_suit(hole[i])]++;
    mall |= 1u << card_rank(hole[i]);
  }
  for (i = 0; i < nboard; i++) {
    suit_all[card_suit(board[i])]++;
    mall |= 1u << card_rank(board[i]);
    mboard |= 1u << card_rank(board[i]);
  }
  for (s = 0; s < 4; s++)
    if (suit_all[s] == 4 && suit_hole[s] > 0) *flush_draw = 1;
  if (!ai_mask_has_straight(mall))
    for (i = 0; i < 13; i++) {
      unsigned bit = 1u << i;
      if (mall & bit) continue;
      /* Counts only straights that need a hole card: a straight on the
         board plus one card is everyone's, not a draw of mine. */
      if (ai_mask_has_straight(mall | bit) && !ai_mask_has_straight(mboard | bit)) outs++;
    }
  *straight_outs = outs;
}

/* ---- range weights ------------------------------------------------------ */

static float pre_weight(float pct, float top)
{
  float w;
  if (top >= 1.0f || pct <= top) return 1.0f;
  /* A soft tail rather than a cliff: the model is a guess, and a human (or
     a Maniac) turns up with hands outside any chart. */
  w = expf(-(pct - top) / 0.06f);
  return w < 0.02f ? 0.02f : w;
}

static float lerp_clamp(float x, float x0, float x1, float y0, float y1)
{
  if (x <= x0) return y0;
  if (x >= x1) return y1;
  return y0 + (y1 - y0) * (x - x0) / (x1 - x0);
}

static float powi(float x, int n)
{
  float r = 1.0f;
  if (n > 4) n = 4;
  while (n-- > 0) r *= x;
  return r;
}

void ai_range_weights(const AiRange *r, const Card *known, int nknown,
                      const Card *board, int nboard, float w[AI_NCOMBOS])
{
  uint64_t kmask = 0;
  float top = r ? r->top : 1.0f;
  int nb = r ? r->post_bets : 0, nc = r ? r->post_calls : 0;
  int i;

  ai_init();
  if (!(top > 0.02f)) top = 0.02f;        /* also catches NaN */
  for (i = 0; i < nknown; i++)
    if (card_valid(known[i])) kmask |= ai_card_bit(known[i]);
  for (i = 0; i < AI_NCOMBOS; i++)
    w[i] = (g_combo_mask[i] & kmask) ? 0.0f
         : pre_weight(ai_class_pct(g_ai_combo_class[i]), top);

  for (i = 0; i < nboard && i < 5; i++)
    if (!card_valid(board[i]) || !(kmask & ai_card_bit(board[i]))) nb = nc = 0;  /* not a real board */
  if (nboard >= 3 && nboard <= 5 && (nb > 0 || nc > 0)) {
    /* Post-flop: how strong each combo is on this board, as the fraction of
       other possible holdings it beats. Players who bet or raise hold the
       top of that order (plus draws and a few bluffs); players who call
       have shed the bottom of it. Stack buffers only - no allocation. */
    int rank[AI_NCOMBOS];
    uint16_t hist[EVAL_WORST_RANK + 2];
    int total = 0, below = 0;
    memset(hist, 0, sizeof hist);
    for (i = 0; i < AI_NCOMBOS; i++) {
      Card h[2];
      if (w[i] == 0.0f) { rank[i] = 0; continue; }
      h[0] = g_ai_combo[i][0];
      h[1] = g_ai_combo[i][1];
      rank[i] = ai_eval_with_board(h, board, nboard);
      hist[rank[i]]++;
      total++;
    }
    /* hist[r] becomes the number of combos strictly worse than rank r. */
    for (i = EVAL_WORST_RANK + 1; i >= 1; i--) {
      int here = hist[i];
      hist[i] = (uint16_t)below;
      below += here;
    }
    for (i = 0; i < AI_NCOMBOS; i++) {
      Card h[2];
      int fd, so;
      float s, wb, wc;
      if (w[i] == 0.0f) continue;
      h[0] = g_ai_combo[i][0];
      h[1] = g_ai_combo[i][1];
      s = total > 1 ? (float)hist[rank[i]] / (float)(total - 1) : 0.5f;
      ai_draw_info(h, board, nboard, &fd, &so);
      wb = lerp_clamp(s, 0.40f, 0.85f, 0.07f, 1.0f);
      wc = lerp_clamp(s, 0.20f, 0.70f, 0.15f, 1.0f);
      if (fd || so >= 2) { if (wb < 0.6f) wb = 0.6f; if (wc < 0.9f) wc = 0.9f; }
      else if (so == 1)  { if (wb < 0.25f) wb = 0.25f; if (wc < 0.4f) wc = 0.4f; }
      w[i] *= powi(wb, nb) * powi(wc, nc);
    }
  }
}

/* ---- Monte Carlo -------------------------------------------------------- */

/* One opponent's weighted combos: a cumulative table for sampling, the
   integer weight of every combo, and the total weight of the combos that
   contain each card (to know how much weight a set of used cards blocks). */
typedef struct {
  uint16_t idx[AI_NCOMBOS];
  uint32_t cum[AI_NCOMBOS];
  uint32_t wq[AI_NCOMBOS];
  uint32_t wcard[52];
  int      n;
  uint32_t total;
} ComboDist;

static void dist_build(ComboDist *d, const float w[AI_NCOMBOS])
{
  uint32_t acc = 0;
  int i;
  d->n = 0;
  memset(d->wcard, 0, sizeof d->wcard);
  for (i = 0; i < AI_NCOMBOS; i++) {
    uint32_t q;
    d->wq[i] = 0;
    if (!(w[i] > 0.0f)) continue;
    q = (uint32_t)(w[i] * 65536.0f + 0.5f);
    if (q == 0) q = 1;                    /* possible, however unlikely */
    d->wq[i] = q;
    d->wcard[g_ai_combo[i][0]] += q;
    d->wcard[g_ai_combo[i][1]] += q;
    acc += q;
    d->idx[d->n] = (uint16_t)i;
    d->cum[d->n] = acc;
    d->n++;
  }
  d->total = acc;
}

/* Weight still available once the cards in used[0..nused) are gone. */
static uint64_t dist_free_weight(const ComboDist *d, const Card *used, int nused)
{
  uint64_t blocked = 0;
  int a, b;
  for (a = 0; a < nused; a++) {
    blocked += d->wcard[used[a]];
    for (b = 0; b < a; b++) blocked -= d->wq[ai_combo_index(used[a], used[b])];
  }
  return (uint64_t)d->total - blocked;
}

static int dist_sample(const ComboDist *d, Rng *rng)
{
  uint32_t r = rng_below(rng, d->total);
  int lo = 0, hi = d->n - 1;
  while (lo < hi) {                       /* first cum > r */
    int mid = (lo + hi) >> 1;
    if (d->cum[mid] > r) hi = mid; else lo = mid + 1;
  }
  return d->idx[lo];
}

static int range_is_uniform(const AiRange *r)
{
  return r == NULL || (r->top >= 1.0f && r->post_bets == 0 && r->post_calls == 0);
}

/* The one Monte Carlo loop behind ai_equity and ai_equity_w: opponent o's
   hand comes from ranges[o] (AiRange model) or wexp[o] (explicit weights);
   NULL arrays, or NULL / uniform entries, mean any two unseen cards. */
static double equity_core(const Card hole[2], const Card *board, int nboard, int nopp,
                          const AiRange *ranges, const float *const *wexp,
                          int trials, Rng *rng)
{
  uint32_t mine_ck[2], h[7];
  Card avail[52], known[7];
  uint64_t kmask = 0, share60 = 0;
  int navail = 0, nknown = 0, need_board, i, t, o, uniform = 1;

  ai_init();
  if (nopp < 1) return 1.0;
  if (nopp > 5) nopp = 5;
  if (nboard < 0) nboard = 0;
  if (nboard > 5) nboard = 5;
  if (trials < 1) trials = 1;

  known[nknown++] = hole[0];
  known[nknown++] = hole[1];
  for (i = 0; i < nboard; i++) known[nknown++] = board[i];
  for (i = 0; i < nknown; i++) {
    /* Not a real position (a bad or repeated card): no equity to speak of. */
    if (!card_valid(known[i]) || (kmask & ai_card_bit(known[i]))) return 0.0;
    kmask |= ai_card_bit(known[i]);
  }
  for (i = 0; i < 52; i++)
    if (!(kmask & ai_card_bit((Card)i))) avail[navail++] = (Card)i;
  need_board = 5 - nboard;
  mine_ck[0] = g_ai_ck[hole[0]];
  mine_ck[1] = g_ai_ck[hole[1]];
  for (i = 0; i < nboard; i++) h[2 + i] = g_ai_ck[board[i]];

  if (ranges)
    for (o = 0; o < nopp; o++)
      if (!range_is_uniform(&ranges[o])) uniform = 0;
  if (wexp)
    for (o = 0; o < nopp; o++)
      if (wexp[o]) uniform = 0;

  if (uniform) {
    /* Every opponent holds any two unseen cards: a partial Fisher-Yates
       over the unseen cards deals the whole trial. The array is left in
       its shuffled state between trials; picking k cards this way from any
       arrangement is still a uniform draw. */
    int need = need_board + 2 * nopp;
    for (t = 0; t < trials; t++) {
      int mine, lost = 0, ties = 0;
      for (i = 0; i < need; i++) {
        int j = i + (int)rng_below(rng, (uint32_t)(navail - i));
        Card c = avail[i]; avail[i] = avail[j]; avail[j] = c;
      }
      for (i = 0; i < need_board; i++) h[2 + nboard + i] = g_ai_ck[avail[i]];
      h[0] = mine_ck[0]; h[1] = mine_ck[1];
      mine = eval7_ck(h);
      for (o = 0; o < nopp; o++) {
        int r;
        h[0] = g_ai_ck[avail[need_board + 2 * o]];
        h[1] = g_ai_ck[avail[need_board + 2 * o + 1]];
        r = eval7_ck(h);
        if (r < mine) { lost = 1; break; }
        if (r == mine) ties++;
      }
      if (!lost) share60 += (uint64_t)(60 / (ties + 1));
    }
  } else {
    /* Weighted ranges. The target is the product of the opponents' weights
       over hands that do not overlap. Each opponent in turn draws from its
       own weights among the combos the earlier ones left free; that
       under-represents hands that leave the later opponents little room,
       so each trial is weighted by the fraction of every later opponent's
       weight that was still free when it drew (importance sampling, exact
       in expectation). Rejecting whole joint draws would also be exact but
       costs tens of redraws when several tight ranges all want the same
       aces. Opponents with the same range share one table. */
    ComboDist dist[5];
    int which[5], ndist = 0, combo[5];
    float w[AI_NCOMBOS];
    double acc = 0.0, accw = 0.0;
    AiRange any = { 1.0f, 0, 0 }, rr[5];
    const float *wx[5];
    for (o = 0; o < nopp; o++) {
      rr[o] = ranges && !range_is_uniform(&ranges[o]) ? ranges[o] : any;
      wx[o] = wexp ? wexp[o] : NULL;
    }
    for (o = 0; o < nopp; o++) {
      int k;
      which[o] = -1;
      for (k = 0; k < o; k++)
        if (wx[k] == wx[o] && rr[k].top == rr[o].top && rr[k].post_bets == rr[o].post_bets &&
            rr[k].post_calls == rr[o].post_calls) { which[o] = which[k]; break; }
      if (which[o] >= 0) continue;
      if (wx[o]) {
        uint64_t m = kmask;
        for (i = 0; i < AI_NCOMBOS; i++)
          w[i] = (g_combo_mask[i] & m) || !(wx[o][i] > 0.0f) ? 0.0f : (wx[o][i] > 1.0f ? 1.0f : wx[o][i]);
      } else {
        ai_range_weights(&rr[o], known, nknown, board, nboard, w);
      }
      dist_build(&dist[ndist], w);
      which[o] = ndist++;
    }
    for (t = 0; t < trials; t++) {
      uint64_t used = kmask;
      Card newc[10];
      int nnew = 0, mine, lost = 0, ties = 0;
      double iw = 1.0;
      for (o = 0; o < nopp; o++) {
        const ComboDist *d = &dist[which[o]];
        int c = -1, tries;
        if (nnew > 0 && d->total > 0)
          iw *= (double)dist_free_weight(d, newc, nnew) / (double)d->total;
        for (tries = 0; tries < 256 && d->n > 0; tries++) {
          int x = dist_sample(d, rng);
          if (!(g_combo_mask[x] & used)) { c = x; break; }
        }
        if (c < 0) {
          /* Nothing left in this range (or astronomically unlucky): any
             two free cards. Keeps the loop bounded. */
          int a, b;
          do a = (int)rng_below(rng, (uint32_t)navail); while (used & ai_card_bit(avail[a]));
          do b = (int)rng_below(rng, (uint32_t)navail);
          while (b == a || (used & ai_card_bit(avail[b])));
          c = ai_combo_index(avail[a], avail[b]);
        }
        used |= g_combo_mask[c];
        combo[o] = c;
        newc[nnew++] = g_ai_combo[c][0];
        newc[nnew++] = g_ai_combo[c][1];
      }
      for (i = 0; i < need_board; i++) {
        int j;
        do j = (int)rng_below(rng, (uint32_t)navail); while (used & ai_card_bit(avail[j]));
        used |= ai_card_bit(avail[j]);
        h[2 + nboard + i] = g_ai_ck[avail[j]];
      }
      h[0] = mine_ck[0]; h[1] = mine_ck[1];
      mine = eval7_ck(h);
      for (o = 0; o < nopp; o++) {
        int r;
        h[0] = g_ai_ck[g_ai_combo[combo[o]][0]];
        h[1] = g_ai_ck[g_ai_combo[combo[o]][1]];
        r = eval7_ck(h);
        if (r < mine) { lost = 1; break; }
        if (r == mine) ties++;
      }
      if (!lost) acc += iw * (double)(60 / (ties + 1));
      accw += iw;
    }
    return accw > 0.0 ? acc / (60.0 * accw) : 0.0;
  }
  return (double)share60 / (60.0 * (double)trials);
}

double ai_equity(const Card hole[2], const Card *board, int nboard,
                 int nopp, const AiRange *ranges, int trials, Rng *rng)
{
  return equity_core(hole, board, nboard, nopp, ranges, NULL, trials, rng);
}

double ai_equity_w(const Card hole[2], const Card *board, int nboard,
                   int nopp, const float *const w[], int trials, Rng *rng)
{
  return equity_core(hole, board, nboard, nopp, NULL, w, trials, rng);
}
