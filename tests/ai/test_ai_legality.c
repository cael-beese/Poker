/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* Legality: for thousands of random valid views (every street, 2-6 seats,
   short and deep stacks, all-ins, both min_raise forms, all four
   personalities) the decision is a legal action with a consistent amount,
   think time in 0.4-2.0 s, and a tell in 0..1. Plus a few spots where only
   one answer is sensible, and checks that the AI is not a pattern: it
   folds sometimes, does not always min-raise, and mixes its actions. */

#include "ai/ai.h"
#include "ai_test_views.h"
#include "test_util.h"

#include <stdio.h>
#include <string.h>

static void base_view(AiView *v, const char *hole, const char *board, int personality)
{
  memset(v, 0, sizeof *v);
  v->seats = 6;
  v->big_blind = 100;
  v->personality = personality;
  cards_parse(hole, v->hole, 2);
  v->nboard = board ? cards_parse(board, v->board, 5) : 0;
  v->street = v->nboard == 0 ? 0 : v->nboard - 2;
}

int main(void)
{
  Rng g;
  int n, total = 60000, bad = 0, i;
  long acts[6] = {0}, raises = 0, minraises = 0, sizes[8] = {0};
  long ticks_hist[5] = {0};
  const char *first_reason = NULL;

  ai_init();
  rng_seed(&g, 77);
  for (n = 0; n < total; n++) {
    AiView v;
    HiddenState hid;
    AiDecision d;
    AiOptions opt;
    Rng r;
    const char *why;
    memset(&v, 0, sizeof v);
    tv_gen(&g, &v, &hid);
    rng_seed(&r, (uint64_t)n);
    memset(&opt, 0, sizeof opt);
    opt.trials = 300;                  /* legality does not need precision */
    ai_decide_ex(&v, &r, &opt, &d, NULL);
    why = tv_illegal(&v, &hid, &d);
    if (why) {
      if (!first_reason) {
        first_reason = why;
        fprintf(stderr, "first illegal (%s): seats %d me %d street %d to_call %lld stack %lld bet %lld "
                "min_raise %lld (%s) -> action %d amount %lld\n", why, v.seats, v.me, v.street,
                (long long)v.to_call, (long long)v.stack[v.me], (long long)v.bet[v.me],
                (long long)v.min_raise, hid.minraise_is_incr ? "incr" : "to",
                d.action, (long long)d.amount);
      }
      bad++;
    }
    if (d.action >= 0 && d.action < 6) acts[d.action]++;
    if (d.action == ACT_RAISE) {
      int64_t mb = 0;
      for (i = 0; i < v.seats; i++) if (v.bet[i] > mb) mb = v.bet[i];
      raises++;
      if (d.amount == hid.min_raise_to) minraises++;
      {
        double x = (double)d.amount / (double)(mb > 0 ? mb : 1);
        sizes[x < 2.2 ? 0 : x < 2.7 ? 1 : x < 3.2 ? 2 : x < 4 ? 3 : 4]++;
      }
    }
    ticks_hist[d.think_ticks < 36 ? 0 : d.think_ticks < 60 ? 1 : d.think_ticks < 84 ? 2 : d.think_ticks < 108 ? 3 : 4]++;
  }
  CHECK_EQ_INT(bad, 0);
  printf("%d random views: %d illegal\n", total, bad);
  printf("  actions: fold %ld check %ld call %ld bet %ld raise %ld all-in %ld\n",
         acts[0], acts[1], acts[2], acts[3], acts[4], acts[5]);
  printf("  raises: %ld, exactly the minimum %ld (%.1f%%); raise-to / current bet: "
         "<2.2x %ld, 2.2-2.7x %ld, 2.7-3.2x %ld, 3.2-4x %ld, >4x %ld\n",
         raises, minraises, raises ? 100.0 * minraises / raises : 0.0,
         sizes[0], sizes[1], sizes[2], sizes[3], sizes[4]);
  printf("  think time: <0.6 s %ld, 0.6-1.0 s %ld, 1.0-1.4 s %ld, 1.4-1.8 s %ld, >=1.8 s %ld\n",
         ticks_hist[0], ticks_hist[1], ticks_hist[2], ticks_hist[3], ticks_hist[4]);
  /* Not a pattern: every action type occurs, min-raising is not the rule. */
  for (i = 0; i < 6; i++) CHECK(acts[i] > 0);
  CHECK(minraises * 2 < raises);
  CHECK(ticks_hist[0] > 0 && ticks_hist[2] + ticks_hist[3] + ticks_hist[4] > 0);

  /* Spots with one sensible answer, for every personality and many seeds. */
  {
    int p, s;
    for (p = 0; p < 4; p++)
      for (s = 0; s < 40; s++) {
        AiView v;
        AiDecision d;
        Rng r;
        rng_seed(&r, (uint64_t)(p * 100 + s));

        /* AA pre-flop facing an open raise: never fold. */
        base_view(&v, "Ah As", NULL, p);
        v.me = 3; v.button = 3;
        for (i = 0; i < 6; i++) v.stack[i] = 10000;
        v.bet[4] = 50; v.bet[5] = 100; v.bet[0] = 300;
        v.stack[4] -= 50; v.stack[5] -= 100; v.stack[0] -= 300;
        for (i = 0; i < 6; i++) v.active[i] = 1;
        v.to_call = 300; v.min_raise = 500;
        v.history[0] = AI_HIST(0, 4, ACT_POST_SB);
        v.history[1] = AI_HIST(0, 5, ACT_POST_BB);
        v.history[2] = AI_HIST(0, 0, ACT_RAISE);
        v.history[3] = AI_HIST(0, 1, ACT_FOLD);
        v.history[4] = AI_HIST(0, 2, ACT_FOLD);
        v.active[1] = v.active[2] = 0;
        v.nhist = 5;
        ai_decide(&v, &r, &d);
        CHECK(d.action != ACT_FOLD);

        /* The nuts on the river facing a bet: never fold. */
        base_view(&v, "Ah Kh", "Qh Jh Th 2c 3d", p);
        v.me = 0; v.button = 1;
        for (i = 0; i < 6; i++) v.stack[i] = 8000;
        v.active[0] = v.active[1] = 1;
        v.pot = 2000; v.bet[1] = 1500; v.stack[1] -= 1500;
        v.to_call = 1500; v.min_raise = 3000;
        v.history[0] = AI_HIST(3, 1, ACT_BET);
        v.nhist = 1;
        ai_decide(&v, &r, &d);
        CHECK(d.action == ACT_CALL || d.action == ACT_RAISE || d.action == ACT_ALLIN);

        /* 7-2 offsuit, 100 BB deep, facing a shove and a call: fold. */
        base_view(&v, "7d 2c", NULL, p);
        v.me = 2; v.button = 2;
        for (i = 0; i < 6; i++) v.stack[i] = 10000;
        v.bet[3] = 50; v.stack[3] -= 50;
        v.bet[4] = 100; v.stack[4] -= 100;
        v.bet[0] = 10000; v.stack[0] = 0; v.allin[0] = 1;
        v.bet[1] = 10000; v.stack[1] = 0; v.allin[1] = 1;
        v.active[2] = v.active[3] = v.active[4] = 1;
        v.to_call = 10000; v.min_raise = 20000;
        v.history[0] = AI_HIST(0, 3, ACT_POST_SB);
        v.history[1] = AI_HIST(0, 4, ACT_POST_BB);
        v.history[2] = AI_HIST(0, 0, ACT_ALLIN);
        v.history[3] = AI_HIST(0, 1, ACT_CALL);
        v.nhist = 4;
        ai_decide(&v, &r, &d);
        CHECK(d.action == ACT_FOLD);

        /* Nothing to call and nothing to win by raising (everyone else
           all-in): check. */
        base_view(&v, "5c 4c", "Ks Qd 9h", p);
        v.me = 1; v.button = 0;
        v.stack[1] = 5000; v.active[1] = 1;
        v.stack[0] = 0; v.allin[0] = 1;
        v.pot = 4000;
        v.min_raise = 100;
        ai_decide(&v, &r, &d);
        CHECK(d.action == ACT_CHECK);
      }
    printf("sanity spots: AA never folds pre-flop, the nuts never folds, 72o folds to a shove, "
           "no bet into all-ins (4 personalities x 40 seeds)\n");
  }
  /* Difficulty: five seats, the right mix, harder = fewer Fish and more
     Sharks, the shuffle driven by the Rng only. */
  {
    static const int want_fish[AI_NDIFF] = { 3, 2, 1, 0 };
    static const int want_shark[AI_NDIFF] = { 0, 1, 2, 3 };
    int dif, k;
    for (dif = 0; dif < AI_NDIFF; dif++)
      for (k = 0; k < 20; k++) {
        int a[5], b[5], cnt[4] = {0, 0, 0, 0};
        Rng r1, r2;
        rng_seed(&r1, (uint64_t)k);
        rng_seed(&r2, (uint64_t)k);
        ai_assign_personalities(dif, &r1, a);
        ai_assign_personalities(dif, &r2, b);
        CHECK(memcmp(a, b, sizeof a) == 0);
        for (i = 0; i < 5; i++) { CHECK(a[i] >= 0 && a[i] < 4); if (a[i] >= 0 && a[i] < 4) cnt[a[i]]++; }
        CHECK_EQ_INT(cnt[AI_FISH], want_fish[dif]);
        CHECK_EQ_INT(cnt[AI_SHARK], want_shark[dif]);
        CHECK_EQ_INT(cnt[AI_MANIAC], 1);
      }
    printf("difficulty mapping: easy F3 M1 R1, normal F2 M1 R1 S1, hard F1 M1 R1 S2, expert M1 R1 S3\n");
  }
  return test_finish("ai_legality");
}
