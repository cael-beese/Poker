/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* Determinism: the same view and seed give the same decision - directly,
   repeated, from several threads at once, and through the worker-thread
   hooks however the workers are scheduled (1 or 4 workers, all six seats in
   flight, collected in a shuffled order, requests superseded before they
   are collected, several pools running concurrently). */

#include "ai/ai.h"
#include "ai_test_views.h"
#include "test_util.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define NPOS 240

static AiView g_view[NPOS];
static AiDecision g_ref[NPOS];

static uint64_t seed_of(int i) { return 0x5EEDu * (uint64_t)(i + 1) + 17u; }

static int same(const AiDecision *a, const AiDecision *b)
{
  return a->action == b->action && a->amount == b->amount &&
         a->think_ticks == b->think_ticks && memcmp(&a->tell, &b->tell, sizeof a->tell) == 0;
}

static void decide_seeded(int i, AiDecision *d)
{
  Rng r;
  rng_seed(&r, seed_of(i));
  ai_decide(&g_view[i], &r, d);
}

/* Direct calls from several threads at once. */
static void *direct_worker(void *arg)
{
  int *bad = (int *)arg, i, k;
  for (k = 0; k < 2; k++)
    for (i = 0; i < NPOS; i++) {
      AiDecision d;
      decide_seeded((i * 7 + k * 13) % NPOS, &d);
      if (!same(&d, &g_ref[(i * 7 + k * 13) % NPOS])) (*bad)++;
    }
  return NULL;
}

/* A table's view of the hooks: all six seats begun, collected shuffled. */
static int run_pool(int nthreads, int rounds, uint64_t order_seed)
{
  AiPool *p = ai_pool_create(nthreads);
  HoldemAiHooks h;
  Rng order;
  int bad = 0, rd;
  if (!p) return 1000;
  h = ai_pool_hooks(p);
  rng_seed(&order, order_seed);
  for (rd = 0; rd < rounds; rd++) {
    int seats[6] = { 0, 1, 2, 3, 4, 5 }, pos[6], s, k;
    for (s = 0; s < 6; s++) {
      pos[s] = (int)rng_below(&order, NPOS);
      /* Sometimes supersede: begin with another view first, then the real
         one before anything is collected. Only the latest may come back. */
      if (rng_below(&order, 4) == 0) {
        int other = (pos[s] + 1) % NPOS;
        h.begin(h.ctx, s, &g_view[other], seed_of(other));
      }
      h.begin(h.ctx, s, &g_view[pos[s]], seed_of(pos[s]));
    }
    for (s = 5; s > 0; s--) {
      int j = (int)rng_below(&order, (uint32_t)(s + 1));
      int t = seats[s]; seats[s] = seats[j]; seats[j] = t;
    }
    for (k = 0; k < 6; k++) {
      AiDecision d;
      s = seats[k];
      h.collect(h.ctx, s, &d);
      if (!same(&d, &g_ref[pos[s]])) bad++;
    }
  }
  ai_pool_destroy(p);
  return bad;
}

typedef struct { int nthreads, rounds, bad; uint64_t seed; } PoolJob;

static void *pool_worker(void *arg)
{
  PoolJob *j = (PoolJob *)arg;
  j->bad = run_pool(j->nthreads, j->rounds, j->seed);
  return NULL;
}

int main(void)
{
  Rng g;
  int i, bad[4] = {0, 0, 0, 0};
  pthread_t th[4];
  PoolJob pj[3];
  AiOptions opt;

  ai_init();
  rng_seed(&g, 2026);
  for (i = 0; i < NPOS; i++) {
    HiddenState hid;
    memset(&g_view[i], 0, sizeof g_view[i]);
    tv_gen(&g, &g_view[i], &hid);
    decide_seeded(i, &g_ref[i]);
  }

  /* Same seed, same answer, repeatedly; a different seed may differ. */
  {
    int differ = 0;
    for (i = 0; i < NPOS; i++) {
      AiDecision a, b, c;
      Rng r;
      decide_seeded(i, &a);
      decide_seeded(i, &b);
      CHECK(same(&a, &g_ref[i]) && same(&b, &g_ref[i]));
      rng_seed(&r, seed_of(i) ^ 0xFFFFu);
      ai_decide(&g_view[i], &r, &c);
      if (!same(&c, &a)) differ++;
    }
    CHECK(differ > NPOS / 4);            /* the seed does drive the mixing */
    printf("repeat: %d positions identical; %d differ under another seed\n", NPOS, differ);
  }

  /* ai_decide_ex with default options is ai_decide. */
  memset(&opt, 0, sizeof opt);
  for (i = 0; i < 40; i++) {
    AiDecision a;
    Rng r;
    rng_seed(&r, seed_of(i));
    ai_decide_ex(&g_view[i], &r, &opt, &a, NULL);
    CHECK(same(&a, &g_ref[i]));
  }

  for (i = 0; i < 4; i++) pthread_create(&th[i], NULL, direct_worker, &bad[i]);
  for (i = 0; i < 4; i++) { pthread_join(th[i], NULL); CHECK_EQ_INT(bad[i], 0); }
  printf("4 threads x %d direct decisions: identical\n", 2 * NPOS);

  CHECK_EQ_INT(run_pool(1, 40, 1), 0);
  CHECK_EQ_INT(run_pool(4, 40, 2), 0);
  printf("hooks: 1 and 4 workers, 6 seats in flight, shuffled collects: identical\n");

  /* Three pools (tables) at once on top of each other. */
  for (i = 0; i < 3; i++) {
    pj[i].nthreads = 1 + i;
    pj[i].rounds = 25;
    pj[i].seed = 100u + (uint64_t)i;
    pthread_create(&th[i], NULL, pool_worker, &pj[i]);
  }
  for (i = 0; i < 3; i++) { pthread_join(th[i], NULL); CHECK_EQ_INT(pj[i].bad, 0); }
  printf("3 pools concurrently: identical\n");

  /* collect without begin, and a bad seat, return a safe fold. */
  {
    AiPool *p = ai_pool_create(1);
    HoldemAiHooks h = ai_pool_hooks(p);
    AiDecision d;
    h.collect(h.ctx, 3, &d);
    CHECK(d.action == ACT_FOLD && d.think_ticks >= 24);
    h.collect(h.ctx, 9, &d);
    CHECK(d.action == ACT_FOLD);
    ai_pool_destroy(p);
  }
  return test_finish("ai_determinism");
}
