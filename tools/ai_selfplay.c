/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* Self-play harness for the Hold'em AI.

   A minimal, self-contained No-Limit Hold'em table (blinds, four betting
   rounds, min-raise, all-ins, side pots, split pots) that asks the AI for
   decisions through AiView only, so the personalities can be measured and
   tuned before the real Hold'em engine exists. Like the real table it knows
   every card, and like the real table it shows each AI only its own.

       ai_selfplay [mode] [hands] [trials] [threads] [seed]

   modes:
     cash     6-max, every stack reset to 100 BB each hand; each hand seats
              all four personalities plus two more at random, in random
              seats. Reports VPIP, PFR, aggression factor, WTSD, W$SD,
              bb/100 and sanity counters per personality.
     exploit  each personality, three seats, against three copies of a
              simple exploit bot (calling station, pot-raising maniac,
              min-raiser, a nit that folds everything but premiums, and a
              bot that moves all-in every hand).
              A personality that loses to one of these is exploitable.
     sng      sit-and-go: six seats (all four personalities plus two at
              random), 1500 chips, blinds 10/20 rising every 12 hands,
              played to the end. Reports finishing places per personality.

   trials 0 = the AI's default trial counts. Output is deterministic for a
   given seed and thread count. */

#include "ai/ai.h"

#include "engine/deck.h"
#include "engine/eval.h"

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { BOT_CALL = 4, BOT_RAISE, BOT_MINRAISE, BOT_NIT, BOT_SHOVE, NTYPES };
static const char *type_name(int t)
{
  static const char *n[NTYPES] = { "Rock", "Maniac", "Shark", "Fish",
                                   "CallBot", "PotBot", "MinRaiseBot", "NitBot", "ShoveBot" };
  return t >= 0 && t < NTYPES ? n[t] : "?";
}

typedef struct {
  double hands, vpip, pfr, pf_opps3, threebet;
  double post_aggr, post_calls, post_folds, post_checks, faced_bet, folded_to_bet;
  double saw_flop, wtsd, wsd, net_bb, net_bb2;
  double raises, minraises, allins, clamps, illegal;
  double decisions, think_ticks, bluffs, tell_bluff, value_bets, tell_value;
  double sng_played, sng_place_sum, sng_wins, sng_itm;
} Stats;

typedef struct {
  int nseats;
  int type[6];
  int64_t stack[6], bet[6], contrib[6], pot, bb;
  int in[6], allin[6], dealt[6], acted[6];
  Card hole[6][2], board[5];
  int nboard, button, street;
  uint8_t hist[64];
  int nhist;
  int64_t max_bet, last_incr;
  /* per-hand flags for stats */
  int vpip[6], pfr[6], saw_flop[6], showdown[6], won_sd[6];
} Table;

typedef struct {
  int mode, trials, thread, nthreads;
  long hands;
  uint64_t seed;
  Stats st[NTYPES];
  long exploit_type, exploit_bot;
} Job;

/* ---- decisions ---------------------------------------------------------- */

static void hist_push(Table *t, int seat, int act)
{
  if (t->nhist < 64) t->hist[t->nhist++] = AI_HIST(t->street, seat, act);
}

static void build_view(const Table *t, int s, AiView *v)
{
  int i;
  memset(v, 0, sizeof *v);
  v->seats = t->nseats;
  v->me = s;
  v->button = t->button;
  v->street = t->street;
  v->hole[0] = t->hole[s][0];            /* this seat's own cards only */
  v->hole[1] = t->hole[s][1];
  for (i = 0; i < 5; i++) v->board[i] = i < t->nboard ? t->board[i] : CARD_NONE;
  v->nboard = t->nboard;
  for (i = 0; i < t->nseats; i++) {
    v->stack[i] = t->stack[i];
    v->bet[i] = t->bet[i];
    v->active[i] = (uint8_t)(t->in[i] && !t->allin[i]);
    v->allin[i] = (uint8_t)(t->in[i] && t->allin[i]);
  }
  v->pot = t->pot;
  v->to_call = t->max_bet - t->bet[s];
  v->min_raise = t->max_bet + t->last_incr;       /* raise-to form */
  v->big_blind = t->bb;
  memcpy(v->history, t->hist, sizeof v->history);
  v->nhist = t->nhist;
  v->personality = t->type[s] < 4 ? t->type[s] : AI_SHARK;
}

static void bot_decide(const Table *t, int s, AiDecision *d)
{
  int64_t to_call = t->max_bet - t->bet[s];
  int64_t all = t->bet[s] + t->stack[s];
  int64_t pot = t->pot, potraise;
  int i;
  for (i = 0; i < t->nseats; i++) pot += t->bet[i];
  d->think_ticks = 24;
  d->tell = 0.5f;
  switch (t->type[s]) {
  case BOT_CALL:
    d->action = to_call > 0 ? ACT_CALL : ACT_CHECK;
    d->amount = t->bet[s] + (to_call < t->stack[s] ? to_call : t->stack[s]);
    return;
  case BOT_RAISE:
    potraise = t->max_bet + pot + to_call;
    d->action = t->max_bet > 0 ? ACT_RAISE : ACT_BET;
    d->amount = potraise < all ? potraise : all;
    return;
  case BOT_SHOVE:
    d->action = ACT_ALLIN;
    d->amount = all;
    return;
  case BOT_MINRAISE:
    d->action = t->max_bet > 0 ? ACT_RAISE : ACT_BET;
    d->amount = t->max_bet + t->last_incr;
    return;
  default: {                              /* BOT_NIT */
    int r0 = card_rank(t->hole[s][0]), r1 = card_rank(t->hole[s][1]);
    int premium = (r0 == r1 && r0 >= RANK_Q) || (r0 + r1 == RANK_A + RANK_K);
    if (premium) { d->action = ACT_ALLIN; d->amount = all; }
    else { d->action = to_call > 0 ? ACT_FOLD : ACT_CHECK; d->amount = t->bet[s]; }
    return;
  }
  }
}

/* ---- the table ---------------------------------------------------------- */

static int next_in(const Table *t, int from, const int *flag)
{
  int k;
  for (k = 1; k <= t->nseats; k++) {
    int s = (from + k) % t->nseats;
    if (flag[s]) return s;
  }
  return -1;
}

static int count(const Table *t, const int *flag)
{
  int i, n = 0;
  for (i = 0; i < t->nseats; i++) n += flag[i] != 0;
  return n;
}

static void put_chips(Table *t, int s, int64_t amount)
{
  if (amount > t->stack[s]) amount = t->stack[s];
  t->stack[s] -= amount;
  t->bet[s] += amount;
  t->contrib[s] += amount;
  if (t->stack[s] == 0) t->allin[s] = 1;
}

static void apply(Table *t, int s, AiDecision d, Stats *st)
{
  int64_t to_call = t->max_bet - t->bet[s];
  int64_t all = t->bet[s] + t->stack[s];
  int act = d.action, voluntary = 0, raised = 0;
  Stats *x = &st[t->type[s]];

  if (act == ACT_FOLD && to_call == 0) { act = ACT_CHECK; x->clamps++; }
  if (act == ACT_CHECK && to_call > 0) { act = ACT_FOLD; x->illegal++; }
  if (act == ACT_CALL && to_call == 0) { act = ACT_CHECK; x->clamps++; }
  if (act == ACT_BET || act == ACT_RAISE) {
    int64_t amt = d.amount;
    if (amt >= all) act = ACT_ALLIN;
    else if (amt < t->max_bet + t->last_incr) {
      amt = t->max_bet + t->last_incr;
      x->clamps++;
      if (amt >= all) act = ACT_ALLIN;
    }
    if (act != ACT_ALLIN) d.amount = amt;
    if (act != ACT_ALLIN && d.amount == t->max_bet + t->last_incr && t->max_bet > 0) x->minraises++;
  }
  if (act == ACT_ALLIN && all <= t->max_bet) act = ACT_CALL;   /* all-in for a call */
  if (act < ACT_FOLD || act > ACT_ALLIN) { act = to_call > 0 ? ACT_FOLD : ACT_CHECK; x->illegal++; }

  if (t->street > 0 && to_call > 0) {
    x->faced_bet++;
    if (act == ACT_FOLD) x->folded_to_bet++;
  }
  switch (act) {
  case ACT_FOLD:
    t->in[s] = 0;
    hist_push(t, s, ACT_FOLD);
    if (t->street > 0) x->post_folds++;
    break;
  case ACT_CHECK:
    hist_push(t, s, ACT_CHECK);
    if (t->street > 0) x->post_checks++;
    break;
  case ACT_CALL:
    put_chips(t, s, to_call);
    hist_push(t, s, ACT_CALL);
    voluntary = 1;
    if (t->street > 0) x->post_calls++;
    break;
  default: {                              /* BET, RAISE, ALLIN that raises */
    int64_t to = act == ACT_ALLIN ? all : d.amount;
    int64_t incr = to - t->max_bet;
    int i;
    put_chips(t, s, to - t->bet[s]);
    hist_push(t, s, act == ACT_ALLIN ? ACT_ALLIN : (t->max_bet > 0 ? ACT_RAISE : ACT_BET));
    if (incr >= t->last_incr) {
      t->last_incr = incr;
      for (i = 0; i < t->nseats; i++) if (i != s) t->acted[i] = 0;   /* reopened */
    }
    t->max_bet = to;
    voluntary = raised = 1;
    x->raises++;
    if (act == ACT_ALLIN) x->allins++;
    if (t->street > 0) x->post_aggr++;
    break;
  }
  }
  t->acted[s] = 1;
  if (t->street == 0 && voluntary) t->vpip[s] = 1;
  if (t->street == 0 && raised) t->pfr[s] = 1;
}

static void betting_round(Table *t, int first, Rng *ai_rng, const AiOptions *opt, Stats *st)
{
  int s = first, guard = 0, i;
  for (i = 0; i < t->nseats; i++) t->acted[i] = 0;
  while (guard++ < 200) {
    int live = 0, need = 0;
    for (i = 0; i < t->nseats; i++) {
      if (!t->in[i]) continue;
      if (!t->allin[i]) {
        live++;
        if (!t->acted[i] || t->bet[i] < t->max_bet) need++;
      }
    }
    if (count(t, t->in) <= 1 || need == 0) break;
    if (live == 1) {
      /* The last player with chips only has to match the all-ins. */
      int only = -1;
      for (i = 0; i < t->nseats; i++) if (t->in[i] && !t->allin[i]) only = i;
      if (t->bet[only] >= t->max_bet) break;
    }
    /* Find the next seat that has to act, starting at s. */
    for (i = 0; i < t->nseats; i++) {
      int c = (s + i) % t->nseats;
      if (t->in[c] && !t->allin[c] && (!t->acted[c] || t->bet[c] < t->max_bet)) { s = c; break; }
    }
    {
      AiDecision d;
      Stats *x = &st[t->type[s]];
      if (t->type[s] < 4) {
        AiView v;
        AiTrace tr;
        build_view(t, s, &v);
        ai_decide_ex(&v, ai_rng, opt, &d, &tr);
        x->decisions++;
        x->think_ticks += d.think_ticks;
        if (d.action == ACT_BET || d.action == ACT_RAISE || d.action == ACT_ALLIN) {
          if (tr.bluff) { x->bluffs++; x->tell_bluff += d.tell; }
          else { x->value_bets++; x->tell_value += d.tell; }
        }
      } else {
        bot_decide(t, s, &d);
      }
      /* Three-bet opportunity: facing exactly one pre-flop raise. */
      if (t->street == 0) {
        int raises = 0, k;
        for (k = 0; k < t->nhist; k++) {
          int a = t->hist[k] & 7;
          if (a == ACT_RAISE || a == ACT_BET || a == ACT_ALLIN) raises++;
        }
        if (raises == 1) {
          x->pf_opps3++;
          if (d.action == ACT_RAISE || d.action == ACT_ALLIN) x->threebet++;
        }
      }
      apply(t, s, d, st);
    }
    s = (s + 1) % t->nseats;
  }
}

static void collect_bets(Table *t)
{
  int i;
  for (i = 0; i < t->nseats; i++) { t->pot += t->bet[i]; t->bet[i] = 0; }
  t->max_bet = 0;
  t->last_incr = t->bb;
}

/* Side pots by contribution level; odd chips go to the first winner left
   of the button. Returns chips won per seat in won[]. */
static void showdown(Table *t, int64_t won[6])
{
  int rank[6], i;
  int64_t levels[6];
  int nl = 0, l;
  memset(won, 0, sizeof(int64_t) * 6);
  for (i = 0; i < t->nseats; i++) {
    Card c[7];
    int k;
    rank[i] = 1 << 30;
    if (!t->in[i]) continue;
    c[0] = t->hole[i][0]; c[1] = t->hole[i][1];
    for (k = 0; k < 5; k++) c[2 + k] = t->board[k];
    rank[i] = eval7(c);
  }
  for (i = 0; i < t->nseats; i++) {
    int k, dup = 0;
    if (!t->in[i] || t->contrib[i] == 0) continue;
    for (k = 0; k < nl; k++) if (levels[k] == t->contrib[i]) dup = 1;
    if (!dup) levels[nl++] = t->contrib[i];
  }
  for (i = 1; i < nl; i++) {                /* sort ascending */
    int64_t x = levels[i];
    int k = i;
    while (k > 0 && levels[k - 1] > x) { levels[k] = levels[k - 1]; k--; }
    levels[k] = x;
  }
  {
    int64_t prev = 0;
    for (l = 0; l < nl; l++) {
      int64_t pot = 0, share;
      int best = 1 << 30, nwin = 0, k, first = 1;
      for (i = 0; i < t->nseats; i++) {
        int64_t c = t->contrib[i];
        int64_t part = c > levels[l] ? levels[l] - prev : c - prev;
        if (part > 0) pot += part;
      }
      for (i = 0; i < t->nseats; i++)
        if (t->in[i] && t->contrib[i] >= levels[l] && rank[i] < best) best = rank[i];
      for (i = 0; i < t->nseats; i++)
        if (t->in[i] && t->contrib[i] >= levels[l] && rank[i] == best) nwin++;
      if (nwin == 0) { prev = levels[l]; continue; }
      share = pot / nwin;
      for (k = 1; k <= t->nseats; k++) {
        int s = (t->button + k) % t->nseats;
        if (t->in[s] && t->contrib[s] >= levels[l] && rank[s] == best) {
          won[s] += share + (first ? pot - share * nwin : 0);
          first = 0;
        }
      }
      prev = levels[l];
    }
    /* Chips above the highest live level (uncalled) go back. */
    for (i = 0; i < t->nseats; i++)
      if (t->contrib[i] > levels[nl - 1]) won[i] += t->contrib[i] - levels[nl - 1];
  }
}

/* Plays one hand. Returns 0, or -1 if fewer than two players have chips. */
static int play_hand(Table *t, Rng *game, Rng *ai_rng, const AiOptions *opt, Stats *st)
{
  Deck deck;
  int i, sb, bbs, first;
  int64_t start[6], won[6];
  int ndealt;

  for (i = 0; i < t->nseats; i++) {
    t->dealt[i] = t->stack[i] > 0;
    t->in[i] = t->dealt[i];
    t->allin[i] = 0;
    t->bet[i] = t->contrib[i] = 0;
    t->vpip[i] = t->pfr[i] = t->saw_flop[i] = t->showdown[i] = t->won_sd[i] = 0;
    start[i] = t->stack[i];
  }
  ndealt = count(t, t->dealt);
  if (ndealt < 2) return -1;
  t->pot = 0;
  t->nhist = 0;
  t->nboard = 0;
  t->street = 0;
  t->button = next_in(t, t->button, t->dealt);
  sb = ndealt == 2 ? t->button : next_in(t, t->button, t->dealt);
  bbs = next_in(t, sb, t->dealt);

  deck_init(&deck);
  deck_shuffle(&deck, game);
  for (i = 0; i < t->nseats; i++)
    if (t->dealt[i]) { t->hole[i][0] = deck_draw(&deck); t->hole[i][1] = deck_draw(&deck); }

  put_chips(t, sb, t->bb / 2);
  hist_push(t, sb, ACT_POST_SB);
  put_chips(t, bbs, t->bb);
  hist_push(t, bbs, ACT_POST_BB);
  t->max_bet = t->bet[bbs] > t->bet[sb] ? t->bet[bbs] : t->bet[sb];
  if (t->max_bet < t->bb) t->max_bet = t->bb;   /* a short blind still sets the price */
  t->last_incr = t->bb;
  for (i = 0; i < t->nseats; i++) if (t->dealt[i]) st[t->type[i]].hands++;

  first = next_in(t, bbs, t->dealt);
  betting_round(t, first, ai_rng, opt, st);
  for (t->street = 1; t->street <= 3 && count(t, t->in) > 1; t->street++) {
    int k, n = t->street == 1 ? 3 : 1;
    collect_bets(t);
    deck_draw(&deck);                     /* burn */
    for (k = 0; k < n; k++) t->board[t->nboard++] = deck_draw(&deck);
    if (t->street == 1) for (i = 0; i < t->nseats; i++) if (t->in[i]) t->saw_flop[i] = 1;
    first = next_in(t, t->button, t->in);
    betting_round(t, first, ai_rng, opt, st);
  }
  collect_bets(t);
  if (count(t, t->in) == 1) {
    for (i = 0; i < t->nseats; i++) if (t->in[i]) t->stack[i] += t->pot;
  } else {
    while (t->nboard < 5) { deck_draw(&deck); t->board[t->nboard++] = deck_draw(&deck); }
    showdown(t, won);
    for (i = 0; i < t->nseats; i++) {
      t->stack[i] += won[i];
      if (t->in[i]) {
        t->showdown[i] = 1;
        if (won[i] > t->contrib[i]) t->won_sd[i] = 1;
      }
    }
  }
  for (i = 0; i < t->nseats; i++) {
    Stats *x = &st[t->type[i]];
    double net = (double)(t->stack[i] - start[i]) / (double)t->bb;
    if (!t->dealt[i]) continue;
    x->vpip += t->vpip[i];
    x->pfr += t->pfr[i];
    x->saw_flop += t->saw_flop[i];
    x->wtsd += t->saw_flop[i] && t->showdown[i];
    x->wsd += t->saw_flop[i] && t->won_sd[i];
    x->net_bb += net;
    x->net_bb2 += net * net;
  }
  return 0;
}

/* ---- modes -------------------------------------------------------------- */

static void shuffle_ints(int *a, int n, Rng *r)
{
  int i;
  for (i = n - 1; i > 0; i--) {
    int j = (int)rng_below(r, (uint32_t)(i + 1));
    int x = a[i]; a[i] = a[j]; a[j] = x;
  }
}

static void *run_job(void *arg)
{
  Job *j = (Job *)arg;
  AiOptions opt;
  Rng lineup, game, ai;
  long h;
  memset(&opt, 0, sizeof opt);
  opt.trials = j->trials;
  rng_seed(&lineup, j->seed ^ (0x9E3779B97F4A7C15ull * (uint64_t)(j->thread + 1)));
  rng_seed(&game, rng_next(&lineup));
  rng_seed(&ai, rng_next(&lineup));

  if (j->mode == 2) {
    /* Sit-and-go: whole tournaments. */
    for (h = 0; h < j->hands; h++) {
      Table t;
      int types[6] = { AI_ROCK, AI_MANIAC, AI_SHARK, AI_FISH, 0, 0 };
      int place[6], nleft = 6, i, hand = 0;
      memset(&t, 0, sizeof t);
      types[4] = (int)rng_below(&lineup, 4);
      types[5] = (int)rng_below(&lineup, 4);
      shuffle_ints(types, 6, &lineup);
      t.nseats = 6;
      for (i = 0; i < 6; i++) { t.type[i] = types[i]; t.stack[i] = 1500; place[i] = 0; }
      t.button = (int)rng_below(&lineup, 6);
      t.bb = 20;
      while (nleft > 1 && hand < 2000) {
        int64_t before[6];
        int busted = 0;
        for (i = 0; i < 6; i++) before[i] = t.stack[i];
        t.bb = 20 << (hand / 12 < 10 ? hand / 12 : 10);   /* doubles every 12 hands */
        if (play_hand(&t, &game, &ai, &opt, j->st) != 0) break;
        hand++;
        for (i = 0; i < 6; i++) if (before[i] > 0 && t.stack[i] == 0) busted++;
        /* Players busted on the same hand share the place (ties by start
           stack would need more bookkeeping than a harness wants). */
        for (i = 0; i < 6; i++)
          if (before[i] > 0 && t.stack[i] == 0) place[i] = nleft - busted + 1;
        nleft -= busted;
      }
      for (i = 0; i < 6; i++) {
        Stats *x = &j->st[t.type[i]];
        int p = place[i] ? place[i] : 1;
        x->sng_played++;
        x->sng_place_sum += p;
        if (p == 1) x->sng_wins++;
        if (p <= 2) x->sng_itm++;
      }
    }
    return NULL;
  }

  for (h = 0; h < j->hands; h++) {
    Table t;
    int i;
    memset(&t, 0, sizeof t);
    t.nseats = 6;
    t.bb = 100;
    if (j->mode == 0) {
      int types[6] = { AI_ROCK, AI_MANIAC, AI_SHARK, AI_FISH, 0, 0 };
      types[4] = (int)rng_below(&lineup, 4);
      types[5] = (int)rng_below(&lineup, 4);
      shuffle_ints(types, 6, &lineup);
      for (i = 0; i < 6; i++) t.type[i] = types[i];
    } else {
      int types[6];
      for (i = 0; i < 3; i++) { types[i] = (int)j->exploit_type; types[i + 3] = (int)j->exploit_bot; }
      shuffle_ints(types, 6, &lineup);
      for (i = 0; i < 6; i++) t.type[i] = types[i];
    }
    for (i = 0; i < 6; i++) t.stack[i] = 100 * t.bb;
    t.button = (int)rng_below(&lineup, 6);
    play_hand(&t, &game, &ai, &opt, j->st);
  }
  return NULL;
}

static void run_parallel(Job *proto, int nthreads, Stats out[NTYPES])
{
  Job jobs[64];
  pthread_t th[64];
  int i, k;
  if (nthreads > 64) nthreads = 64;
  for (i = 0; i < nthreads; i++) {
    jobs[i] = *proto;
    memset(jobs[i].st, 0, sizeof jobs[i].st);
    jobs[i].thread = i;
    jobs[i].hands = proto->hands / nthreads + (i < proto->hands % nthreads);
    pthread_create(&th[i], NULL, run_job, &jobs[i]);
  }
  memset(out, 0, sizeof(Stats) * NTYPES);
  for (i = 0; i < nthreads; i++) {
    double *dst, *src;
    size_t n = sizeof(Stats) / sizeof(double), f;
    pthread_join(th[i], NULL);
    for (k = 0; k < NTYPES; k++) {
      dst = (double *)&out[k];
      src = (double *)&jobs[i].st[k];
      for (f = 0; f < n; f++) dst[f] += src[f];
    }
  }
}

static double pct(double a, double b) { return b > 0 ? 100.0 * a / b : 0.0; }

static void print_cash(const Stats *st, int first, int last)
{
  int k;
  printf("%-11s %8s %5s %5s %5s %5s %5s %5s %5s %8s %6s %5s %5s %5s\n",
         "type", "hands", "VPIP", "PFR", "3bet", "AF", "WTSD", "W$SD", "Fbet",
         "bb/100", "+-", "minR", "think", "tellB/V");
  for (k = first; k <= last; k++) {
    const Stats *x = &st[k];
    double n = x->hands, mean, sd;
    if (n <= 0) continue;
    mean = x->net_bb / n;
    sd = sqrt(x->net_bb2 / n - mean * mean);
    printf("%-11s %8.0f %5.1f %5.1f %5.1f %5.2f %5.1f %5.1f %5.1f %8.1f %6.1f %5.1f %5.2f %.2f/%.2f\n",
           type_name(k), n, pct(x->vpip, n), pct(x->pfr, n), pct(x->threebet, x->pf_opps3),
           x->post_calls > 0 ? x->post_aggr / x->post_calls : 0.0,
           pct(x->wtsd, x->saw_flop), pct(x->wsd, x->wtsd), pct(x->folded_to_bet, x->faced_bet),
           100.0 * mean, 100.0 * 2.0 * sd / sqrt(n), pct(x->minraises, x->raises),
           x->decisions > 0 ? x->think_ticks / x->decisions / 60.0 : 0.0,
           x->bluffs > 0 ? x->tell_bluff / x->bluffs : 0.0,
           x->value_bets > 0 ? x->tell_value / x->value_bets : 0.0);
  }
}

int main(int argc, char **argv)
{
  const char *mode = argc > 1 ? argv[1] : "cash";
  long hands = argc > 2 ? atol(argv[2]) : 20000;
  int trials = argc > 3 ? atoi(argv[3]) : 0;
  int nthreads = argc > 4 ? atoi(argv[4]) : 8;
  uint64_t seed = argc > 5 ? strtoull(argv[5], NULL, 0) : 1;
  Job proto;
  Stats st[NTYPES];
  struct timespec t0, t1;
  int k;

  ai_init();
  memset(&proto, 0, sizeof proto);
  proto.hands = hands;
  proto.trials = trials;
  proto.seed = seed;
  if (nthreads < 1) nthreads = 1;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  printf("ai_selfplay %s: %ld %s, trials %s, %d threads, seed %llu\n", mode, hands,
         strcmp(mode, "sng") == 0 ? "tournaments" : "hands",
         trials ? argv[3] : "default", nthreads, (unsigned long long)seed);

  if (strcmp(mode, "cash") == 0) {
    proto.mode = 0;
    run_parallel(&proto, nthreads, st);
    print_cash(st, 0, 3);
    for (k = 0; k < 4; k++)
      if (st[k].illegal || st[k].clamps)
        printf("  %s: %.0f illegal, %.0f clamped actions\n", type_name(k), st[k].illegal, st[k].clamps);
  } else if (strcmp(mode, "exploit") == 0) {
    int bot;
    proto.mode = 1;
    printf("%-8s", "");
    for (bot = BOT_CALL; bot < NTYPES; bot++) printf(" %17s", type_name(bot));
    printf("\n");
    for (k = 0; k < 4; k++) {
      printf("%-8s", type_name(k));
      for (bot = BOT_CALL; bot < NTYPES; bot++) {
        const Stats *x;
        double n, mean, sd;
        proto.exploit_type = k;
        proto.exploit_bot = bot;
        run_parallel(&proto, nthreads, st);
        x = &st[k];
        n = x->hands;
        mean = n > 0 ? x->net_bb / n : 0;
        sd = n > 0 ? sqrt(x->net_bb2 / n - mean * mean) : 0;
        printf(" %+7.0f +-%6.0f", 100.0 * mean, 100.0 * 2.0 * sd / sqrt(n > 0 ? n : 1));
        fflush(stdout);
      }
      printf("   (bb/100 of the personality, 95%% CI)\n");
    }
  } else if (strcmp(mode, "sng") == 0) {
    proto.mode = 2;
    run_parallel(&proto, nthreads, st);
    printf("%-8s %8s %9s %7s %7s\n", "type", "entries", "avgplace", "win%", "top2%");
    for (k = 0; k < 4; k++) {
      const Stats *x = &st[k];
      if (x->sng_played <= 0) continue;
      printf("%-8s %8.0f %9.2f %7.1f %7.1f\n", type_name(k), x->sng_played,
             x->sng_place_sum / x->sng_played, pct(x->sng_wins, x->sng_played), pct(x->sng_itm, x->sng_played));
    }
    print_cash(st, 0, 3);
  } else {
    fprintf(stderr, "unknown mode '%s' (cash, exploit, sng)\n", mode);
    return 2;
  }
  clock_gettime(CLOCK_MONOTONIC, &t1);
  printf("%.1f s\n", (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) * 1e-9);
  return 0;
}
