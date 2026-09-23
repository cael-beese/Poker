/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* ai_decide: one No-Limit Hold'em decision from an AiView.

   Everything here is computed from the view (my two cards, the board, the
   public chip counts and the public action history) and the AI's own Rng.
   The view is read only inside its declared bounds: seats < v->seats,
   board[0..nboard), history[0..nhist).

   Pre-flop: the 169-class ranking (ai_preflop.c) cut by position, table
   size and personality; push/fold when the effective stack is short;
   calls of raises priced by Monte Carlo equity against the raisers'
   estimated ranges. Post-flop: equity against the range model versus pot
   odds, adjusted for position, stack-to-pot ratio, draws, board texture and
   the number of opponents. Every threshold is jittered and every bet or
   bluff is a probability, drawn from the AI's Rng, so play is mixed. */

#include "ai/ai_internal.h"

#include "engine/eval.h"

#include <math.h>
#include <string.h>

/* ---- trial counts ------------------------------------------------------- */

int ai_default_trials(int nopp)
{
  /* Equal time per decision whatever the opponent count: a trial costs
     about (nopp + 1) evaluations plus the draws. Sized from measurements
     (tests/ai/test_ai_timing.c prints them) so the heaviest decision uses
     roughly a third of the 30 ms Pi budget. */
  static const int t[6] = { 1, 6000, 5000, 4000, 3000, 2500 };
  if (nopp < 1) nopp = 1;
  if (nopp > 5) nopp = 5;
  return t[nopp];
}

/* ---- the view, digested ------------------------------------------------- */

typedef struct {
  const AiView *v;
  const AiPersonality *P;
  int seats, me, street, nboard;
  int in_hand[6], dealt[6], ndealt;
  int nopp, nopp_live;           /* opponents in the hand / able to bet   */
  int sb, bb_seat;
  int pf_raises, limpers, last_pf_raiser, street_raises;
  int pf_level[6], pf_allin[6], pf_calls[6], pf_called_raise[6], post_bets[6], post_calls[6];
  int pf_order[6], npf;          /* dealt seats in pre-flop acting order  */
  int po_order[6], npo;          /* dealt seats in post-flop acting order */
  int n_behind;                  /* pre-flop: players left to act after me */
  int ip;                        /* post-flop: no live opponent acts after me */
  int64_t bb, pot_total, max_bet, my_bet, my_stack, cost, min_raise_to, allin_to;
  int64_t eff_behind;            /* chips I can win or lose from here     */
  float eff_bb;                  /* effective stack in big blinds         */
  int can_raise;
  float pct, pct_allin;
  AiRange range[6];
} Ctx;

static int valid_seat(const Ctx *c, int s) { return s >= 0 && s < c->seats; }

static int next_dealt(const Ctx *c, int from)
{
  int k;
  for (k = 1; k <= c->seats; k++) {
    int s = (from + k) % c->seats;
    if (c->dealt[s]) return s;
  }
  return from;
}

static int digest(Ctx *c, const AiView *v, const AiPersonality *P)
{
  int i, k;
  uint64_t seen = 0;
  memset(c, 0, sizeof *c);
  c->v = v;
  c->P = P;
  c->seats = v->seats;
  c->me = v->me;
  if (c->seats < 2 || c->seats > 6 || c->me < 0 || c->me >= c->seats) return -1;
  c->street = v->street < 0 ? 0 : v->street > 3 ? 3 : v->street;
  c->nboard = v->nboard < 0 ? 0 : v->nboard > 5 ? 5 : v->nboard;
  /* My cards and the board must be real, distinct cards. */
  for (i = 0; i < 2; i++) {
    if (!card_valid(v->hole[i]) || (seen & ai_card_bit(v->hole[i]))) return -1;
    seen |= ai_card_bit(v->hole[i]);
  }
  for (i = 0; i < c->nboard; i++) {
    if (!card_valid(v->board[i]) || (seen & ai_card_bit(v->board[i]))) return -1;
    seen |= ai_card_bit(v->board[i]);
  }
  c->bb = v->big_blind > 0 ? v->big_blind : 1;

  for (i = 0; i < c->seats; i++) {
    c->in_hand[i] = v->active[i] || v->allin[i];
    c->dealt[i] = c->in_hand[i];
  }
  c->in_hand[c->me] = 1;
  c->dealt[c->me] = 1;

  /* The public history: who posted, raised, called, and when. */
  c->sb = c->bb_seat = -1;
  c->last_pf_raiser = -1;
  {
    int nh = v->nhist < 0 ? 0 : v->nhist > 64 ? 64 : v->nhist;
    for (i = 0; i < nh; i++) {
      int h = v->history[i], st = h >> 6, s = (h >> 3) & 7, a = h & 7;
      if (!valid_seat(c, s)) continue;
      c->dealt[s] = 1;
      if (a == ACT_POST_SB) { c->sb = s; continue; }
      if (a == ACT_POST_BB) { c->bb_seat = s; continue; }
      if (st == 0) {
        if (a == ACT_RAISE || a == ACT_BET || a == ACT_ALLIN) {
          c->pf_raises++;
          c->pf_level[s] = c->pf_raises;
          c->pf_allin[s] = a == ACT_ALLIN;
          c->last_pf_raiser = s;
        } else if (a == ACT_CALL) {
          c->pf_calls[s]++;
          if (c->pf_raises == 0) c->limpers++;
          else c->pf_called_raise[s] = c->pf_raises;
        }
      } else {
        if (a == ACT_RAISE || a == ACT_BET || a == ACT_ALLIN) c->post_bets[s]++;
        else if (a == ACT_CALL) c->post_calls[s]++;
      }
      if (st == c->street && (a == ACT_RAISE || a == ACT_BET || a == ACT_ALLIN))
        c->street_raises++;
    }
  }
  for (i = 0; i < c->seats; i++) c->ndealt += c->dealt[i];
  if (c->ndealt < 2) c->ndealt = 2;

  /* Blinds from the history when it has them, else from the button. */
  {
    int btn = valid_seat(c, v->button) ? v->button : c->me;
    if (c->sb < 0) c->sb = (c->ndealt == 2) ? btn : next_dealt(c, btn);
    if (c->bb_seat < 0) c->bb_seat = next_dealt(c, c->sb);
    /* Pre-flop order starts after the big blind; post-flop after the
       button (heads-up the button is the small blind, and acts first
       pre-flop and last after the flop). */
    k = c->bb_seat;
    for (i = 0; i < c->ndealt && i < 6; i++) { k = next_dealt(c, k); c->pf_order[c->npf++] = k; }
    k = btn;
    for (i = 0; i < c->ndealt && i < 6; i++) { k = next_dealt(c, k); c->po_order[c->npo++] = k; }
  }

  /* Chips. */
  c->pot_total = v->pot > 0 ? v->pot : 0;
  for (i = 0; i < c->seats; i++) {
    if (v->bet[i] > 0) c->pot_total += v->bet[i];
    if (v->bet[i] > c->max_bet) c->max_bet = v->bet[i];
  }
  c->my_bet = v->bet[c->me] > 0 ? v->bet[c->me] : 0;
  c->my_stack = v->stack[c->me] > 0 ? v->stack[c->me] : 0;
  c->cost = v->to_call > 0 ? v->to_call : 0;
  if (c->cost > c->my_stack) c->cost = c->my_stack;
  c->allin_to = c->my_bet + c->my_stack;
  /* min_raise may be the raise-to total or the increment. A raise-to total
     is at least max_bet + big blind; an increment is at most
     max(max_bet, big blind), which is below that whenever max_bet > 0 (and
     at max_bet == 0 both forms mean the same). */
  if (v->min_raise >= c->max_bet + c->bb) c->min_raise_to = v->min_raise;
  else c->min_raise_to = c->max_bet + (v->min_raise > c->bb ? v->min_raise : c->bb);

  {
    int64_t opp_max_total = 0, opp_max_behind = 0;
    for (i = 0; i < c->seats; i++) {
      int64_t tot;
      if (i == c->me || !c->in_hand[i]) continue;
      c->nopp++;
      if (!v->allin[i] && v->stack[i] > 0) c->nopp_live++;
      tot = (v->stack[i] > 0 ? v->stack[i] : 0) + (v->bet[i] > 0 ? v->bet[i] : 0);
      if (tot > opp_max_total) opp_max_total = tot;
      if (tot - c->my_bet > opp_max_behind) opp_max_behind = tot - c->my_bet;
    }
    c->eff_behind = c->my_stack < opp_max_behind ? c->my_stack : opp_max_behind;
    c->eff_bb = (float)(c->allin_to < opp_max_total ? c->allin_to : opp_max_total) / (float)c->bb;
  }
  c->can_raise = c->nopp_live > 0 && c->my_stack > c->cost;

  /* Position. */
  for (i = 0; i < c->npf; i++)
    if (c->pf_order[i] == c->me) {
      for (k = i + 1; k < c->npf; k++) if (c->in_hand[c->pf_order[k]]) c->n_behind++;
      break;
    }
  c->ip = 1;
  for (i = 0; i < c->npo; i++)
    if (c->po_order[i] == c->me) {
      for (k = i + 1; k < c->npo; k++) {
        int s = c->po_order[k];
        if (c->in_hand[s] && !v->allin[s]) c->ip = 0;
      }
      break;
    }

  {
    int cls = ai_hand_class(v->hole[0], v->hole[1]);
    c->pct = ai_class_pct(cls);
    c->pct_allin = ai_class_pct_allin(cls);
  }

  /* Range model for every opponent, from public actions only. */
  for (i = 0; i < c->seats; i++) {
    float top = 1.0f, hr = P->hand_reading;
    int nb = c->post_bets[i], nc = c->post_calls[i];
    if (c->pf_level[i] >= 3) top = 0.05f;
    else if (c->pf_level[i] == 2) top = 0.11f;
    else if (c->pf_allin[i]) {
      /* An open shove: a push range for its size while the size is still
         on the table, else a middling one. A deep shove is strength. */
      float size_bb = c->street == 0 ? (float)v->bet[i] / (float)c->bb : 15.0f;
      int nbh = 0, j;
      for (j = 0; j < c->npf; j++)
        if (c->pf_order[j] == i) { nbh = c->npf - 1 - j; break; }
      top = size_bb > 25.0f ? 0.08f : ai_push_range(size_bb, nbh > 0 ? nbh : 1);
    } else if (c->pf_level[i] == 1) {
      int nbh = 0, j;
      for (j = 0; j < c->npf; j++)
        if (c->pf_order[j] == i) { nbh = c->npf - 1 - j; break; }
      top = ai_open_range(nbh, c->ndealt) * 0.9f;
      if (top < 0.15f) top = 0.15f;
    } else if (c->pf_called_raise[i] >= 2) top = 0.12f;
    else if (c->pf_called_raise[i] == 1) top = 0.30f;
    else if (c->pf_calls[i] > 0) top = 0.55f;
    /* How much this personality believes what it sees. Even the worst
       reader respects a big pre-flop re-raise or a shove when its own stack
       is at stake: calling those blind is a leak, not a personality. */
    if ((c->pf_level[i] >= 2 || c->pf_allin[i]) && hr < 0.6f) hr = 0.6f;
    top = top + (1.0f - top) * (1.0f - hr);
    if (hr < 0.25f) nb = nc = 0;
    else if (hr < 0.5f) { if (nb > 1) nb = 1; if (nc > 1) nc = 1; }
    c->range[i].top = top;
    c->range[i].post_bets = (uint8_t)(nb > 4 ? 4 : nb);
    c->range[i].post_calls = (uint8_t)(nc > 4 ? 4 : nc);
  }
  return 0;
}

/* ---- helpers ------------------------------------------------------------ */

typedef struct {
  Ctx *c;
  Rng *rng;
  const AiOptions *opt;
  AiDecision *out;
  AiTrace *tr;
  float closeness;      /* 0 obvious .. 1 on the boundary               */
  int trivial;          /* a snap decision                              */
  int bluff;
  float signal;         /* what an honest tell would show, 0 calm .. 1 tense */
} Dec;

static float unit(Rng *r) { return (float)rng_unit(r); }

/* A symmetric jitter in [-m, m]. */
static float jit(Rng *r, float m) { return (unit(r) * 2.0f - 1.0f) * m; }

static float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }

static float close_to(float x, float edge, float width)
{
  return clampf(1.0f - fabsf(x - edge) / width, 0.0f, 1.0f);
}

static int64_t round_chips(const Ctx *c, int64_t x)
{
  int64_t unit = c->bb >= 2 ? c->bb / 2 : 1;
  return (x + unit / 2) / unit * unit;
}

static void do_check_or_fold(Dec *d, const char *why)
{
  Ctx *c = d->c;
  if (c->cost == 0) { d->out->action = ACT_CHECK; d->out->amount = c->my_bet; }
  else { d->out->action = ACT_FOLD; d->out->amount = c->my_bet; }
  d->tr->why = why;
}

static void do_call(Dec *d, const char *why)
{
  Ctx *c = d->c;
  if (c->cost == 0) { d->out->action = ACT_CHECK; d->out->amount = c->my_bet; }
  else if (c->cost >= c->my_stack) { d->out->action = ACT_ALLIN; d->out->amount = c->allin_to; }
  else { d->out->action = ACT_CALL; d->out->amount = c->my_bet + c->cost; }
  d->tr->why = why;
}

static void do_allin(Dec *d, const char *why)
{
  Ctx *c = d->c;
  d->out->action = ACT_ALLIN;
  d->out->amount = c->allin_to;
  d->tr->why = why;
}

/* Bet or raise to a total of `to`, made legal: at least the minimum
   raise, all-in when it would leave too little behind to matter. */
static void do_raise_to(Dec *d, int64_t to, const char *why)
{
  Ctx *c = d->c;
  if (!c->can_raise) { do_call(d, why); return; }
  to = round_chips(c, to);
  if (to < c->min_raise_to) to = c->min_raise_to;
  if (to >= c->allin_to || (to - c->my_bet) * 10 >= c->my_stack * 7) { do_allin(d, why); return; }
  d->out->action = c->max_bet > 0 ? ACT_RAISE : ACT_BET;
  d->out->amount = to;
  d->tr->why = why;
}

static double run_equity(Dec *d, int use_ranges)
{
  Ctx *c = d->c;
  AiRange r[5];
  int n = 0, i, trials;
  double eq;
  for (i = 0; i < c->seats && n < 5; i++)
    if (i != c->me && c->in_hand[i]) r[n++] = c->range[i];
  if (n == 0) return 1.0;
  trials = d->opt && d->opt->trials > 0 ? d->opt->trials : ai_default_trials(n);
  eq = ai_equity(c->v->hole, c->v->board, c->nboard, n, use_ranges ? r : NULL, trials, d->rng);
  d->tr->equity = eq;
  d->tr->strength = pow(eq, 1.0 / n);
  d->tr->trials = trials;
  return eq;
}

/* ---- pre-flop ----------------------------------------------------------- */

static void preflop(Dec *d)
{
  Ctx *c = d->c;
  const AiPersonality *P = c->P;
  Rng *r = d->rng;
  float mix = P->mix;

  if (c->pf_raises == 0) {
    /* Nobody has raised: open, limp, check the option, or fold. */
    int short_stack = c->eff_bb <= P->push_bb + jit(r, 1.0f);
    if (short_stack && c->can_raise) {
      float range = ai_push_range(c->eff_bb, c->n_behind > 0 ? c->n_behind : 1) * P->push_mult;
      if (c->limpers > 0) range *= 0.7f;
      range *= 1.0f + jit(r, mix * 2.0f);
      d->closeness = close_to(c->pct_allin, range, 0.08f);
      if (c->pct_allin <= range) {
        d->signal = c->pct_allin < range * 0.4f ? 0.2f : 0.6f;
        do_allin(d, "push");
        return;
      }
      if (c->cost == 0) { d->trivial = 1; do_check_or_fold(d, "short: check option"); return; }
      /* Only the loosest limp short; the rest of push/fold is fold. */
      if (P->limp_mult > 2.0f && c->pct <= 0.25f && c->cost <= c->bb) {
        d->signal = 0.4f;
        do_call(d, "short limp");
        return;
      }
      d->trivial = c->pct_allin > range * 1.6f;
      d->signal = 0.3f;
      do_check_or_fold(d, "short: fold");
      return;
    }
    {
      float open = ai_open_range(c->n_behind, c->ndealt) * P->open_mult;
      float limp;
      int am_bb = c->me == c->bb_seat, am_sb = c->me == c->sb && c->ndealt > 2;
      if (c->limpers > 0) open *= 0.75f;                /* isolate tighter */
      if (am_bb) open = 0.14f * P->open_mult * (c->limpers > 0 ? 1.0f : 1.5f);
      open = clampf(open * (1.0f + jit(r, mix * 2.0f)), 0.01f, 0.95f);
      limp = clampf(open * P->limp_mult * (am_sb ? 1.3f : 1.0f), open, 0.70f);
      d->closeness = close_to(c->pct, open, 0.05f + open * 0.2f);
      if (c->pct <= open && c->can_raise) {
        float size = P->size_lo + unit(r) * (P->size_hi - P->size_lo);
        float open_bb = 2.0f + 2.0f * size + (float)c->limpers;
        if (c->ndealt == 2 || am_sb) open_bb -= 0.5f;
        if (am_bb && c->limpers == 0) open_bb += 0.5f;
        d->signal = c->pct < open * 0.3f ? 0.15f : 0.45f;
        do_raise_to(d, (int64_t)(open_bb * (float)c->bb + 0.5f), "open");
        return;
      }
      if (c->cost == 0) {
        d->trivial = c->pct > open * 1.5f;
        d->signal = 0.3f;
        do_check_or_fold(d, "check option");
        return;
      }
      if (c->pct <= limp && c->cost <= c->bb) {
        d->closeness = close_to(c->pct, limp, 0.05f + limp * 0.2f);
        d->signal = 0.45f;
        do_call(d, "limp");
        return;
      }
      d->trivial = c->pct > limp * 1.3f;
      d->signal = 0.3f;
      do_check_or_fold(d, "fold unopened");
      return;
    }
  }

  /* Facing a raise (or several). */
  {
    int level = c->pf_raises;
    float value = (level == 1 ? 0.045f : level == 2 ? 0.03f : 0.02f) * P->reraise_mult;
    double price = c->cost > 0 ? (double)c->cost / (double)(c->pot_total + c->cost) : 0.0;
    int commit = c->cost * 10 >= c->my_stack * 4 || c->eff_bb <= 12.0f;
    double eq, need;
    value *= 1.0f + jit(r, mix * 2.0f);
    d->tr->price = price;

    if (c->pct <= value && c->can_raise) {
      float mult = level == 1 ? (c->ip ? 3.0f : 3.6f) : 2.3f;
      d->closeness = close_to(c->pct, value, 0.02f);
      d->signal = 0.15f;
      do_raise_to(d, (int64_t)((float)c->max_bet * (mult + jit(r, 0.3f))), level == 1 ? "3-bet value" : "4-bet value");
      return;
    }
    /* A short stack re-shoves over a single open with a push-worthy hand. */
    if (level == 1 && c->can_raise && c->eff_bb <= P->push_bb * 1.6f &&
        c->pct_allin <= 0.5f * ai_push_range(c->eff_bb, c->n_behind > 0 ? c->n_behind : 1) * P->push_mult) {
      d->closeness = 0.5f;
      d->signal = 0.5f;
      do_allin(d, "re-shove");
      return;
    }
    if (level == 1 && c->can_raise && c->eff_bb > 25.0f && c->pct > value && c->pct <= 0.30f &&
        unit(r) < P->bluff3) {
      d->bluff = 1;
      d->closeness = 0.6f;
      d->signal = 0.8f;
      do_raise_to(d, (int64_t)((float)c->max_bet * (c->ip ? 3.0f : 3.5f)), "3-bet light");
      return;
    }
    /* Otherwise call or fold on price: equity against the raisers' likely
       ranges, less what position and deep stacks add after the flop. */
    if (c->pct > 0.80f && price > 0.25) {
      d->trivial = 1;
      d->signal = 0.3f;
      do_check_or_fold(d, "fold to raise (trash)");
      return;
    }
    eq = run_equity(d, 1);
    need = price;
    if (!commit) need -= P->pf_slack;       /* loose calls, but not for the stack */
    if (!commit && c->eff_bb > 30.0f) need -= c->ip ? 0.07 : 0.03;
    if (commit) need += P->allin_margin;
    need += jit(r, mix);
    d->closeness = close_to((float)eq, (float)need, 0.08f);
    if (eq >= need) {
      /* A strong hand that is not a value re-raise still re-raises
         sometimes, so the calling range is not capped. */
      if (c->can_raise && eq > need + 0.25 && eq > 0.55 && level <= 2 && unit(r) < P->aggr * 0.3f) {
        d->signal = 0.2f;
        do_raise_to(d, (int64_t)((float)c->max_bet * 2.8f), "re-raise");
        return;
      }
      d->signal = 0.5f;
      do_call(d, "call raise");
    } else {
      d->signal = 0.35f;
      do_check_or_fold(d, "fold to raise");
    }
  }
}

/* ---- post-flop ---------------------------------------------------------- */

static float board_wetness(const Ctx *c)
{
  int suits[4] = {0, 0, 0, 0}, i, maxs = 0, span_hits = 0, lo;
  unsigned m = 0;
  const AiView *v = c->v;
  for (i = 0; i < c->nboard; i++) {
    suits[card_suit(v->board[i])]++;
    m |= 1u << card_rank(v->board[i]);
  }
  for (i = 0; i < 4; i++) if (suits[i] > maxs) maxs = suits[i];
  /* The most board ranks inside any five-rank window: three or more means
     straights are live. */
  for (lo = -1; lo <= 8; lo++) {
    int k, hits = 0;
    for (k = 0; k < 5; k++) {
      int rr = lo + k;
      if (rr == -1) rr = 12;           /* the ace plays low in the wheel */
      if (m & (1u << rr)) hits++;
    }
    if (hits > span_hits) span_hits = hits;
  }
  {
    float fw = maxs >= 3 ? 1.0f : maxs == 2 && c->nboard < 5 ? 0.5f : 0.0f;
    float sw = span_hits >= 3 ? 0.8f : span_hits == 2 ? 0.3f : 0.0f;
    return fw > sw ? fw : sw;
  }
}

static void postflop(Dec *d)
{
  Ctx *c = d->c;
  const AiPersonality *P = c->P;
  Rng *r = d->rng;
  double eq, e1;
  float wet = board_wetness(c), mix = P->mix;
  int fd, so, draw, n = c->nopp > 0 ? c->nopp : 1;
  double spr = c->pot_total > 0 ? (double)c->eff_behind / (double)c->pot_total : 10.0;
  int river = c->street >= 3 || c->nboard >= 5;
  int aggressor = c->last_pf_raiser == c->me && c->street == 1 && c->street_raises == 0;

  if (c->nopp == 0) { do_call(d, "alone"); d->trivial = 1; return; }
  ai_draw_info(c->v->hole, c->v->board, c->nboard, &fd, &so);
  draw = !river && (fd || so >= 2);
  eq = run_equity(d, 1);
  e1 = pow(eq, 1.0 / n);

  if (c->cost == 0) {
    float vthr = P->value_thr + jit(r, mix) - (c->ip ? 0.02f : 0.0f);
    float frac = P->size_lo + unit(r) * (P->size_hi - P->size_lo);
    if (c->nopp_live == 0) { do_check_or_fold(d, "no one to bet into"); d->trivial = 1; return; }
    d->closeness = close_to((float)e1, vthr, 0.12f);
    if (e1 >= vthr) {
      if (e1 > 0.85 && !river && wet < 0.5f && unit(r) < (1.0f - P->aggr) * 0.8f) {
        d->signal = 0.1f;
        do_check_or_fold(d, "slow-play");
        return;
      }
      if (unit(r) < P->aggr + 0.25f) {
        frac += 0.2f * wet;
        if (river && e1 > 0.9) frac += 0.25f;
        d->signal = e1 > 0.8 ? 0.1f : 0.35f;
        if (spr < 1.3) { do_allin(d, "value shove"); return; }
        do_raise_to(d, c->my_bet + (int64_t)(frac * (float)c->pot_total), "value bet");
        return;
      }
      d->signal = 0.25f;
      do_check_or_fold(d, "check strong");
      return;
    }
    if (draw && unit(r) < P->semibluff * (n == 1 ? 1.0f : 0.6f)) {
      d->bluff = 1;
      d->signal = 0.7f;
      do_raise_to(d, c->my_bet + (int64_t)(frac * (float)c->pot_total), "semi-bluff bet");
      return;
    }
    {
      float bp = P->bluff * (aggressor ? 1.6f : 1.0f) * (c->ip ? 1.25f : 0.8f) *
                 (n == 1 ? 1.0f : n == 2 ? 0.45f : 0.15f) * (river ? 0.8f : 1.0f) *
                 (1.2f - 0.4f * wet);
      /* Hands with showdown value mostly check; air is what bluffs. */
      if (e1 >= 0.45) bp *= 0.35f;
      if (unit(r) < bp) {
        d->bluff = 1;
        d->signal = 0.85f;
        /* A balanced player bluffs with the same sizes it value-bets
           with; the others give it away by betting small. */
        if (P->tell_honesty > 0.3f) frac = P->size_lo;
        do_raise_to(d, c->my_bet + (int64_t)(frac * (float)c->pot_total), aggressor ? "c-bet" : "bluff");
        return;
      }
    }
    d->trivial = e1 < vthr - 0.25;
    d->signal = 0.3f;
    do_check_or_fold(d, "check");
    return;
  }

  /* Facing a bet. */
  {
    double price = (double)c->cost / (double)(c->pot_total + c->cost);
    int commit = c->cost * 20 >= c->my_stack * 7;
    double need = price + P->call_margin + (commit ? P->allin_margin : 0.0f);
    float rthr = P->raise_thr + jit(r, mix);
    d->tr->price = price;
    if (draw && !commit && spr > 2.0) need -= 0.06;       /* implied odds */
    need += jit(r, mix * 0.6f);
    d->closeness = close_to((float)eq, (float)need, 0.10f);
    if (c->can_raise && e1 >= rthr) {
      if (unit(r) < P->aggr) {
        float frac = 0.6f + unit(r) * 0.5f + 0.2f * wet;
        d->signal = 0.1f;
        if (spr < 1.5) { do_allin(d, "value shove"); return; }
        do_raise_to(d, c->max_bet + (int64_t)(frac * (float)(c->pot_total + c->cost)), "value raise");
        return;
      }
      d->signal = 0.2f;
      do_call(d, "slow-play call");
      return;
    }
    if (eq >= need) {
      d->signal = eq > need + 0.15 ? 0.3f : 0.55f;
      d->trivial = eq > need + 0.35;
      do_call(d, draw ? "call (draw)" : "call");
      return;
    }
    if (c->can_raise && draw && n == 1 && spr > 1.5 && unit(r) < P->semibluff * 0.35f) {
      d->bluff = 1;
      d->signal = 0.75f;
      do_raise_to(d, c->max_bet + (int64_t)(0.8f * (float)(c->pot_total + c->cost)), "semi-bluff raise");
      return;
    }
    if (c->can_raise && n == 1 && c->cost * 4 < c->my_stack && unit(r) < P->bluff * 0.15f) {
      d->bluff = 1;
      d->signal = 0.9f;
      do_raise_to(d, c->max_bet + (int64_t)(0.75f * (float)(c->pot_total + c->cost)), "bluff raise");
      return;
    }
    d->trivial = eq < need - 0.25;
    d->signal = 0.35f;
    do_check_or_fold(d, "fold");
  }
}

/* ---- think time and tell ------------------------------------------------ */

static void finish(Dec *d)
{
  Ctx *c = d->c;
  const AiPersonality *P = c->P;
  AiDecision *o = d->out;
  float u = unit(d->rng), u2 = unit(d->rng);
  int64_t put = o->amount - c->my_bet;
  float stake, potf, ticks, h, tell;
  int64_t chips = c->allin_to > 0 ? c->allin_to : 1;
  if (o->action == ACT_FOLD || o->action == ACT_CHECK) put = c->cost;   /* what was declined */
  if (put < 0) put = 0;
  stake = clampf((float)put / (float)chips, 0.0f, 1.0f);
  potf = clampf((float)c->pot_total / (float)(chips + c->pot_total), 0.0f, 1.0f);

  /* 0.4 s floor; close decisions, big commitments, big pots and later
     streets take longer; snap decisions stay near the floor. */
  if (d->trivial && stake < 0.1f) ticks = 24.0f + u * 14.0f;
  else {
    ticks = 28.0f + 60.0f * d->closeness + 30.0f * stake + 16.0f * potf + 4.0f * (float)c->street;
    ticks *= P->tempo * (0.8f + 0.4f * u);
    if (d->bluff) ticks += 10.0f * u2;       /* a bluff sometimes tanks... */
  }
  if (ticks < 24.0f) ticks = 24.0f;
  if (ticks > 120.0f) ticks = 120.0f;
  o->think_ticks = (int)ticks;

  /* The tell: 0 calm .. 1 tense. An honest player's tell follows what it is
     doing (tense when bluffing or on a close call); a guarded one's is
     mostly noise. It is made from this decision only. */
  h = P->tell_honesty;
  tell = h * d->signal + (1.0f - h) * u2 + 0.1f * d->closeness * h;
  o->tell = clampf(tell, 0.0f, 1.0f);

  d->tr->closeness = d->closeness;
  d->tr->bluff = d->bluff;
}

/* ---- entry points ------------------------------------------------------- */

void ai_decide_ex(const AiView *v, Rng *ai_rng, const AiOptions *opt,
                  AiDecision *out, AiTrace *trace)
{
  Ctx c;
  Dec d;
  AiTrace dummy;
  const AiPersonality *P;
  ai_init();
  if (!trace) trace = &dummy;
  memset(trace, 0, sizeof *trace);
  trace->equity = trace->strength = -1.0;
  P = opt && opt->personality ? opt->personality : ai_personality(v->personality);
  if (!P) P = ai_personality(AI_SHARK);

  memset(&d, 0, sizeof d);
  d.c = &c;
  d.rng = ai_rng;
  d.opt = opt;
  d.out = out;
  d.tr = trace;
  out->action = ACT_FOLD;
  out->amount = 0;

  if (digest(&c, v, P) != 0) {
    /* A view that is not a real position: do the safe thing. */
    int64_t tc = v->to_call;
    int me_ok = v->me >= 0 && v->me < 6;
    out->action = tc > 0 ? ACT_FOLD : ACT_CHECK;
    out->amount = me_ok && v->bet[v->me] > 0 ? v->bet[v->me] : 0;
    out->think_ticks = 24;
    out->tell = 0.3f;
    trace->why = "invalid view";
    return;
  }
  trace->pct = c.pct;
  if (c.my_stack == 0) {
    /* All-in already (the cost is capped at the stack, so it is 0): there
       is nothing to decide, and checking changes nothing. */
    d.trivial = 1;
    do_check_or_fold(&d, "no chips");
  } else if (c.street == 0 && c.nboard == 0) {
    preflop(&d);
  } else {
    postflop(&d);
  }
  finish(&d);
}

void ai_decide(const AiView *v, Rng *ai_rng, AiDecision *out)
{
  ai_decide_ex(v, ai_rng, NULL, out, NULL);
}
