/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* HoldemAiHooks on a small pool of worker threads.

   One slot per seat holds the view and seed of that seat's latest request.
   begin() copies them in under the lock and returns immediately; a worker
   copies them out, runs ai_decide_ex outside the lock with an Rng seeded
   from the seed, and stores the result tagged with the request's number.
   collect() waits until the result for the latest request is there. The
   result depends only on (view, seed), so which worker ran it, and when,
   cannot change it. A superseded request's result is dropped.

   All memory is allocated in ai_pool_create; a decision allocates nothing. */

#include "ai/ai.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define AI_POOL_MAX_THREADS 4
#define AI_POOL_SEATS 6

typedef struct {
  AiView     view;
  uint64_t   seed;
  uint32_t   req;      /* number of the latest request                  */
  uint32_t   taken;    /* request a worker has started on               */
  uint32_t   done;     /* request whose result is in `result`           */
  AiDecision result;
} Slot;

struct AiPool {
  pthread_mutex_t mu;
  pthread_cond_t  work_cv, done_cv;
  pthread_t       th[AI_POOL_MAX_THREADS];
  int             nth, quit;
  AiOptions       opt;
  Slot            slot[AI_POOL_SEATS];
};

static void compute(const AiView *v, uint64_t seed, const AiOptions *opt, AiDecision *out)
{
  Rng r;
  rng_seed(&r, seed);
  ai_decide_ex(v, &r, opt, out, NULL);
}

static int find_work(AiPool *p)
{
  int s;
  for (s = 0; s < AI_POOL_SEATS; s++)
    if (p->slot[s].taken != p->slot[s].req) return s;
  return -1;
}

static void *worker(void *arg)
{
  AiPool *p = (AiPool *)arg;
  pthread_mutex_lock(&p->mu);
  for (;;) {
    int s;
    AiView v;
    AiOptions opt;
    uint64_t seed;
    uint32_t gen;
    AiDecision d;
    while (!p->quit && (s = find_work(p)) < 0) pthread_cond_wait(&p->work_cv, &p->mu);
    if (p->quit) break;
    v = p->slot[s].view;
    seed = p->slot[s].seed;
    gen = p->slot[s].req;
    opt = p->opt;
    p->slot[s].taken = gen;
    pthread_mutex_unlock(&p->mu);

    compute(&v, seed, &opt, &d);

    pthread_mutex_lock(&p->mu);
    if (p->slot[s].req == gen) {
      p->slot[s].result = d;
      p->slot[s].done = gen;
      pthread_cond_broadcast(&p->done_cv);
    }
  }
  pthread_mutex_unlock(&p->mu);
  return NULL;
}

static void hook_begin(void *ctx, int seat, const AiView *v, uint64_t seed)
{
  AiPool *p = (AiPool *)ctx;
  if (!p || !v || seat < 0 || seat >= AI_POOL_SEATS) return;
  pthread_mutex_lock(&p->mu);
  p->slot[seat].view = *v;
  p->slot[seat].seed = seed;
  p->slot[seat].req++;
  pthread_cond_signal(&p->work_cv);
  pthread_mutex_unlock(&p->mu);
}

static void hook_collect(void *ctx, int seat, AiDecision *out)
{
  AiPool *p = (AiPool *)ctx;
  Slot *sl;
  if (!out) return;
  if (!p || seat < 0 || seat >= AI_POOL_SEATS) {
    memset(out, 0, sizeof *out);
    out->action = ACT_FOLD;
    out->think_ticks = 24;
    return;
  }
  sl = &p->slot[seat];
  pthread_mutex_lock(&p->mu);
  if (p->nth == 0 && sl->done != sl->req) {
    /* No workers (they could not be started): compute here. */
    AiView v = sl->view;
    AiOptions opt = p->opt;
    uint64_t seed = sl->seed;
    uint32_t gen = sl->req;
    AiDecision d;
    pthread_mutex_unlock(&p->mu);
    compute(&v, seed, &opt, &d);
    pthread_mutex_lock(&p->mu);
    sl->result = d;
    sl->done = sl->taken = gen;
  }
  while (sl->done != sl->req) pthread_cond_wait(&p->done_cv, &p->mu);
  *out = sl->result;
  pthread_mutex_unlock(&p->mu);
}

AiPool *ai_pool_create(int nthreads)
{
  AiPool *p;
  pthread_attr_t attr;
  int i;
  ai_init();
  if (nthreads < 1) nthreads = 1;
  if (nthreads > AI_POOL_MAX_THREADS) nthreads = AI_POOL_MAX_THREADS;
  p = (AiPool *)calloc(1, sizeof *p);
  if (!p) return NULL;
  pthread_mutex_init(&p->mu, NULL);
  pthread_cond_init(&p->work_cv, NULL);
  pthread_cond_init(&p->done_cv, NULL);
  for (i = 0; i < AI_POOL_SEATS; i++) {
    p->slot[i].result.action = ACT_FOLD;
    p->slot[i].result.think_ticks = 24;
  }
  /* A decision needs about 100 KB of stack (range tables); 1 MB is ample
     and keeps the Pi's address space tidy. */
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 1u << 20);
  for (i = 0; i < nthreads; i++) {
    if (pthread_create(&p->th[p->nth], &attr, worker, p) != 0) break;
    p->nth++;
  }
  pthread_attr_destroy(&attr);
  return p;
}

void ai_pool_destroy(AiPool *p)
{
  int i;
  if (!p) return;
  pthread_mutex_lock(&p->mu);
  p->quit = 1;
  pthread_cond_broadcast(&p->work_cv);
  pthread_mutex_unlock(&p->mu);
  for (i = 0; i < p->nth; i++) pthread_join(p->th[i], NULL);
  pthread_cond_destroy(&p->work_cv);
  pthread_cond_destroy(&p->done_cv);
  pthread_mutex_destroy(&p->mu);
  free(p);
}

HoldemAiHooks ai_pool_hooks(AiPool *p)
{
  HoldemAiHooks h;
  h.ctx = p;
  h.begin = hook_begin;
  h.collect = hook_collect;
  return h;
}

void ai_pool_set_options(AiPool *p, const AiOptions *opt)
{
  if (!p) return;
  pthread_mutex_lock(&p->mu);
  if (opt) p->opt = *opt; else memset(&p->opt, 0, sizeof p->opt);
  pthread_mutex_unlock(&p->mu);
}
