/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* Timing: how long decisions take at the default trial counts, and a budget
   check. The contract gives each decision 30 ms on the Pi 4 (the worker has
   at least 24 ticks = 400 ms, but the other threads need the cores).

   Prints the evaluator's speed on this machine next to the Monte Carlo cost
   per trial and the full decision times, so the Pi can be estimated from
   an x86 run (the Pi's eval7 is about 50-70 ns against about 8-11 ns on a
   desktop x86, roughly 6-7x) and measured directly by running this test
   there. The check: the heaviest decision (median of several) must take
   under 8 ms on x86-64, a regression tripwire (the measured value is a
   fraction of that), and under the real 30 ms budget on the Pi. Also
   checks that begin() on the hooks returns at once. */

#include "ai/ai.h"
#include "engine/eval.h"
#include "test_util.h"

#include <stdio.h>
#include <string.h>

#if defined(__aarch64__) || defined(__arm__)
#define BUDGET_MS 30.0
#define ARCH "arm"
#else
#define BUDGET_MS 8.0
#define ARCH "x86"
#endif

static double median(double *x, int n)
{
  int i, j;
  for (i = 1; i < n; i++)
    for (j = i; j > 0 && x[j - 1] > x[j]; j--) { double t = x[j]; x[j] = x[j - 1]; x[j - 1] = t; }
  return x[n / 2];
}

/* Six-handed, everyone in, facing bets on the given street: the heaviest
   kind of decision (five weighted ranges, post-flop weighting). */
static void heavy_view(AiView *v, int street, int personality)
{
  static const char *boards[4] = { "", "Kd 8c 3h", "Kd 8c 3h 7s", "Kd 8c 3h 7s 2d" };
  int i;
  memset(v, 0, sizeof *v);
  v->seats = 6;
  v->me = 5;
  v->button = 4;
  v->street = street;
  v->big_blind = 100;
  v->personality = personality;
  cards_parse("Qs Js", v->hole, 2);
  v->nboard = street ? cards_parse(boards[street], v->board, 5) : 0;
  for (i = 0; i < 6; i++) { v->stack[i] = 9000; v->active[i] = 1; }
  v->history[v->nhist++] = AI_HIST(0, 5, ACT_POST_SB);
  v->history[v->nhist++] = AI_HIST(0, 0, ACT_POST_BB);
  v->history[v->nhist++] = AI_HIST(0, 1, ACT_RAISE);
  for (i = 2; i <= 4; i++) v->history[v->nhist++] = AI_HIST(0, i, ACT_CALL);
  if (street == 0) {
    for (i = 1; i <= 4; i++) v->bet[i] = 300;
    v->bet[0] = 100;
    v->bet[5] = 50;
    v->to_call = 250;
    v->min_raise = 500;
  } else {
    int s;
    for (s = 1; s <= street; s++) {
      v->history[v->nhist++] = AI_HIST(s, 0, ACT_BET);
      for (i = 1; i <= 4; i++) v->history[v->nhist++] = AI_HIST(s, i, ACT_CALL);
      if (s < street) v->history[v->nhist++] = AI_HIST(s, 5, ACT_CALL);
    }
    v->pot = 1800 * street;
    for (i = 0; i <= 4; i++) v->bet[i] = 600;
    v->to_call = 600;
    v->min_raise = 1200;
  }
}

int main(void)
{
  Rng r;
  int n, k, st, p;
  double worst = 0.0, eval_ns = 0.0;
  Card hole[2], board[5];

  ai_init();
  rng_seed(&r, 11);

  /* The evaluator on this machine, cache-hot, for scale. */
  {
    static uint32_t ck[7 * 4096];
    volatile int sink = 0;
    double best = 1e9;
    Card deck[52];
    int i, rep;
    for (i = 0; i < 4096; i++) {
      int j;
      for (j = 0; j < 52; j++) deck[j] = (Card)j;
      for (j = 0; j < 7; j++) {
        int x = j + (int)rng_below(&r, (uint32_t)(52 - j));
        Card t = deck[j]; deck[j] = deck[x]; deck[x] = t;
        ck[7 * i + j] = eval_ck(deck[j]);
      }
    }
    for (rep = 0; rep < 15; rep++) {
      double t0 = test_now();
      for (k = 0; k < 25; k++) for (i = 0; i < 4096; i++) sink += eval7_ck(ck + 7 * i);
      t0 = test_now() - t0;
      if (t0 < best) best = t0;
    }
    eval_ns = best * 1e9 / (25 * 4096);
    printf("[%s] eval7_ck: %.1f ns per hand (cache-hot, best of 15)\n", ARCH, eval_ns);
  }

  /* Monte Carlo cost per trial: random opponents and the range model. */
  cards_parse("Ah Kd", hole, 2);
  cards_parse("Qs 7h 2c 9d 3s", board, 5);
  printf("Monte Carlo ns/trial (best of 5), and ms at the default trial count:\n");
  printf("  %-7s %-7s %9s %9s %9s %9s %9s\n", "board", "", "1 opp", "2 opp", "3 opp", "4 opp", "5 opp");
  for (st = 0; st < 3; st++) {
    int nb = st == 0 ? 0 : st == 1 ? 3 : 5, ranged;
    for (ranged = 0; ranged < 2; ranged++) {
      printf("  %-7s %-7s", st == 0 ? "pre" : st == 1 ? "flop" : "river", ranged ? "ranged" : "random");
      for (n = 1; n <= 5; n++) {
        AiRange rg[5];
        double best = 1e9;
        int i, rep, trials = ai_default_trials(n);
        for (i = 0; i < 5; i++) {
          rg[i].top = ranged ? 0.2f : 1.0f;
          rg[i].post_bets = (uint8_t)(ranged && nb ? 1 : 0);
          rg[i].post_calls = 0;
        }
        for (rep = 0; rep < 5; rep++) {
          double t0 = test_now();
          ai_equity(hole, board, nb, n, rg, trials, &r);
          t0 = test_now() - t0;
          if (t0 < best) best = t0;
        }
        printf(" %4.0f/%4.2f", best * 1e9 / trials, best * 1e3);
      }
      printf("\n");
    }
  }

  /* Whole decisions, heaviest shape, every street and personality. */
  printf("full decision, 6-handed multiway facing a bet (median of 9, ms):\n");
  for (st = 0; st < 4; st++) {
    printf("  street %d:", st);
    for (p = 0; p < 4; p++) {
      AiView v;
      double t[9], m;
      heavy_view(&v, st, p);
      for (k = 0; k < 9; k++) {
        AiDecision d;
        AiTrace tr;
        double t0 = test_now();
        ai_decide_ex(&v, &r, NULL, &d, &tr);
        t[k] = (test_now() - t0) * 1e3;
      }
      m = median(t, 9);
      if (m > worst) worst = m;
      printf("  %s %.2f", ai_personality_name(p), m);
    }
    printf("\n");
  }
  /* The Pi estimate scales by the evaluator measured in this same run,
     which cancels much of the noise of a shared development machine. */
  printf("heaviest decision: %.2f ms = %.0f eval7 times (budget here %.0f ms)\n",
         worst, worst * 1e6 / eval_ns, BUDGET_MS);
  printf("Pi 4 estimate at 50-70 ns per eval7: %.1f-%.1f ms of the 30 ms budget\n",
         worst * 50.0 / eval_ns, worst * 70.0 / eval_ns);
  CHECK_TIMING(worst < BUDGET_MS);

  /* begin() must return at once: it only copies the view. */
  {
    AiPool *pool = ai_pool_create(2);
    HoldemAiHooks h = ai_pool_hooks(pool);
    AiView v;
    double max_begin = 0.0;
    heavy_view(&v, 1, AI_SHARK);
    for (k = 0; k < 50; k++) {
      AiDecision d;
      double t0 = test_now();
      h.begin(h.ctx, k % 6, &v, (uint64_t)k);
      t0 = test_now() - t0;
      if (t0 > max_begin) max_begin = t0;
      h.collect(h.ctx, k % 6, &d);
    }
    printf("hooks: begin() returns in at most %.3f ms\n", max_begin * 1e3);
    CHECK_TIMING(max_begin < 0.005);   /* a copy and a signal; 5 ms allows for a busy machine */
    ai_pool_destroy(pool);
  }
  return test_finish("ai_timing");
}
