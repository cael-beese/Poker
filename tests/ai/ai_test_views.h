/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
#ifndef BPL_AI_TEST_VIEWS_H
#define BPL_AI_TEST_VIEWS_H

/* Random but self-consistent AiViews for the AI tests: 2-6 seats, any
   street, stacks from under one big blind to 300, bets and blinds that add
   up, some seats folded or all-in, a plausible public history, and
   min_raise in either of the two forms the AI accepts. The generator also
   deals every other seat's hole cards and orders the remaining cards, as a
   table would, and hands them back separately (hidden) - they are never
   put in the view, which is the point of the integrity test. */

#include "ai/ai.h"

#include <string.h>

typedef struct {
  Card opp_hole[6][2];      /* every other seat's cards (CARD_NONE for mine) */
  Card undealt[52];         /* the rest of the cards, in "deck" order         */
  int  nundealt;
  int  minraise_is_incr;    /* which min_raise form the view uses             */
  int64_t min_raise_to;     /* the minimum legal raise-to total               */
} HiddenState;

static inline void tv_shuffle(Card *c, int n, Rng *r)
{
  int i;
  for (i = n - 1; i > 0; i--) {
    int j = (int)rng_below(r, (uint32_t)(i + 1));
    Card t = c[i]; c[i] = c[j]; c[j] = t;
  }
}

/* Fills v (which the caller may have pre-filled with junk: only declared
   fields and in-bounds slots are written) and hidden. */
static inline void tv_gen(Rng *r, AiView *v, HiddenState *hid)
{
  static const int nb_of_street[4] = { 0, 3, 4, 5 };
  Card deck[52];
  int i, pos = 0, seats, street, me, btn, sb, bbs, nin = 0;
  int64_t bb, max_bet = 0, incr;

  for (i = 0; i < 52; i++) deck[i] = (Card)i;
  tv_shuffle(deck, 52, r);

  seats = 2 + (int)rng_below(r, 5);
  if (seats > 6) seats = 6;   /* never true; tells GCC -O3 the bound (else -Wstringop-overflow) */
  street = (int)rng_below(r, 4);
  me = (int)rng_below(r, (uint32_t)seats);
  btn = (int)rng_below(r, (uint32_t)seats);
  bb = 2 * (1 + (int64_t)rng_below(r, 500));
  v->seats = seats;
  v->me = me;
  v->button = btn;
  v->street = street;
  v->big_blind = bb;
  v->personality = (int)rng_below(r, 4);

  v->hole[0] = deck[pos++];
  v->hole[1] = deck[pos++];
  memset(hid, 0, sizeof *hid);
  for (i = 0; i < 6; i++) {
    hid->opp_hole[i][0] = hid->opp_hole[i][1] = CARD_NONE;
    if (i < seats && i != me) { hid->opp_hole[i][0] = deck[pos++]; hid->opp_hole[i][1] = deck[pos++]; }
  }
  v->nboard = nb_of_street[street];
  for (i = 0; i < v->nboard; i++) v->board[i] = deck[pos++];
  hid->nundealt = 52 - pos;
  memcpy(hid->undealt, deck + pos, (size_t)hid->nundealt);

  /* Who is still in: me always, others 70%, at least one opponent. */
  for (i = 0; i < seats; i++) {
    int in = i == me || rng_below(r, 10) < 7;
    v->active[i] = (uint8_t)in;
    v->allin[i] = 0;
    if (in && i != me) nin++;
  }
  if (nin == 0) { int o = (me + 1) % seats; v->active[o] = 1; }

  /* Chips: a street's bets, one of them the highest. */
  if (street == 0) max_bet = bb * (1 + (int64_t)rng_below(r, 4) * (int64_t)rng_below(r, 8));
  else max_bet = rng_below(r, 3) == 0 ? 0 : bb * (int64_t)(1 + rng_below(r, 40));
  for (i = 0; i < seats; i++) {
    int64_t total = bb / 2 + bb * (int64_t)rng_below(r, 300) + (int64_t)rng_below(r, (uint32_t)bb);
    int64_t b = 0;
    if (v->active[i]) {
      uint32_t k = rng_below(r, 3);
      b = k == 0 ? max_bet : k == 1 ? 0 : (int64_t)rng_below(r, (uint32_t)(max_bet + 1));
    } else if (street == 0) {
      b = rng_below(r, 4) == 0 ? bb / 2 : 0;
    }
    if (b > total) b = total;
    v->bet[i] = b;
    v->stack[i] = total - b;
    if (v->active[i] && v->stack[i] == 0) { v->allin[i] = 1; v->active[i] = rng_below(r, 2) ? 1 : 0; }
  }
  /* Recompute the true highest bet after stacks capped some. */
  max_bet = 0;
  for (i = 0; i < seats; i++) if (v->bet[i] > max_bet) max_bet = v->bet[i];
  v->to_call = max_bet - v->bet[me];
  v->pot = street == 0 ? 0 : bb * (int64_t)(1 + rng_below(r, 60));

  incr = bb;
  if (max_bet > bb && rng_below(r, 2)) incr = bb + (int64_t)rng_below(r, (uint32_t)(max_bet - bb + 1));
  if (incr > max_bet && max_bet > 0) incr = max_bet;
  if (incr < bb) incr = bb;
  hid->min_raise_to = max_bet + incr;
  hid->minraise_is_incr = (int)rng_below(r, 2);
  v->min_raise = hid->minraise_is_incr ? incr : max_bet + incr;

  /* History: the blind posts, then some actions on each street so far. */
  sb = seats == 2 ? btn : (btn + 1) % seats;
  bbs = (sb + 1) % seats;
  v->nhist = 0;
  v->history[v->nhist++] = AI_HIST(0, sb, ACT_POST_SB);
  v->history[v->nhist++] = AI_HIST(0, bbs, ACT_POST_BB);
  {
    int st;
    for (st = 0; st <= street; st++) {
      int n = (int)rng_below(r, 8);
      while (n-- > 0 && v->nhist < 64) {
        static const int acts[6] = { ACT_FOLD, ACT_CHECK, ACT_CALL, ACT_BET, ACT_RAISE, ACT_ALLIN };
        int s = (int)rng_below(r, (uint32_t)seats);
        v->history[v->nhist++] = AI_HIST(st, s, acts[rng_below(r, 6)]);
      }
    }
  }
}

/* Is d a legal, sensible action for v? Returns NULL or a reason. */
static inline const char *tv_illegal(const AiView *v, const HiddenState *hid, const AiDecision *d)
{
  int64_t my_bet = v->bet[v->me], stack = v->stack[v->me], max_bet = 0;
  int64_t cost, allin_to = my_bet + stack;
  int i, live = 0;
  for (i = 0; i < v->seats; i++) {
    if (v->bet[i] > max_bet) max_bet = v->bet[i];
    if (i != v->me && (v->active[i] || v->allin[i]) && !v->allin[i] && v->stack[i] > 0) live++;
  }
  cost = v->to_call < stack ? v->to_call : stack;
  if (cost < 0) cost = 0;
  if (d->think_ticks < 24 || d->think_ticks > 120) return "think_ticks out of 24..120";
  if (!(d->tell >= 0.0f && d->tell <= 1.0f)) return "tell out of 0..1";
  switch (d->action) {
  case ACT_FOLD:
    if (cost == 0) return "fold when checking is free";
    return NULL;
  case ACT_CHECK:
    if (cost > 0) return "check facing a bet";
    if (d->amount != my_bet) return "check amount";
    return NULL;
  case ACT_CALL:
    if (cost == 0) return "call with nothing to call";
    if (cost >= stack) return "call for the whole stack is ALLIN";
    if (d->amount != my_bet + cost) return "call amount";
    return NULL;
  case ACT_BET:
  case ACT_RAISE:
    if (d->action == ACT_BET && max_bet > 0) return "bet when there is a bet";
    if (d->action == ACT_RAISE && max_bet == 0) return "raise with no bet";
    if (live == 0) return "raise with nobody left to call";
    if (stack <= cost) return "raise without the chips";
    if (d->amount < hid->min_raise_to) return "below the minimum raise";
    if (d->amount >= allin_to) return "raise of the whole stack is ALLIN";
    return NULL;
  case ACT_ALLIN:
    if (stack <= 0) return "all-in with no chips";
    if (d->amount != allin_to) return "all-in amount";
    if (live == 0 && cost < stack) return "all-in raise with nobody left to call";
    return NULL;
  default:
    return "action code";
  }
}

#endif
