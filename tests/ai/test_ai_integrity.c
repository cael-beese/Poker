/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* AI integrity, part 2: the AI decides from the AiView and nothing else.

   1. The types: ai_decide takes exactly (const AiView *, Rng *, AiDecision *)
      and the view holds exactly two cards of mine and five board slots -
      checked at compile time, so a change to the interface breaks the build.
   2. Sentinels: each random position is laid out in memory the way a table
      might hold it - the other seats' hole cards and the undealt cards
      right before and after the view - and decided twice with the same
      seed: once with the real hidden cards, once with those replaced by
      other cards and junk. Every unused part of the view (board slots past
      nboard, history past nhist, seats past `seats`, struct padding) is
      filled with junk that differs between the two runs. The decisions must
      be identical, bit for bit, and identical to a clean copy of the view.
   3. The same through the worker-thread hooks. */

#include "ai/ai.h"
#include "ai_test_views.h"
#include "test_util.h"

#include <stdio.h>
#include <string.h>

/* 1. Compile-time interface checks. */
static void (*const decide_signature)(const AiView *, Rng *, AiDecision *) = ai_decide;
typedef char hole_is_two_cards[sizeof(((AiView *)0)->hole) == 2 * sizeof(Card) ? 1 : -1];
typedef char board_is_five_cards[sizeof(((AiView *)0)->board) == 5 * sizeof(Card) ? 1 : -1];
typedef char card_is_one_byte[sizeof(Card) == 1 ? 1 : -1];

/* How a table might lay things out: hidden state around the view. */
typedef struct {
  Card hidden_before[6][2];
  Card undealt_before[52];
  AiView v;
  Card undealt_after[52];
  Card hidden_after[6][2];
} World;

static int same_decision(const AiDecision *a, const AiDecision *b)
{
  return a->action == b->action && a->amount == b->amount &&
         a->think_ticks == b->think_ticks && memcmp(&a->tell, &b->tell, sizeof a->tell) == 0;
}

/* Builds the world for a position: real hidden cards (variant 0) or
   other cards and junk (variant 1), junk in every unused byte of the view. */
static void build_world(World *w, uint64_t pos_seed, int variant, HiddenState *hid)
{
  Rng g, junk;
  int i;
  rng_seed(&junk, pos_seed * 31u + (uint64_t)variant * 0x5bd1e995u + 7u);
  /* Junk everywhere first, including the view's padding and unused slots. */
  for (i = 0; i < (int)sizeof *w; i++) ((uint8_t *)w)[i] = (uint8_t)rng_next(&junk);
  /* The position itself, generated from pos_seed only: the same declared
     fields both times, written over the junk. */
  rng_seed(&g, pos_seed);
  tv_gen(&g, &w->v, hid);
  if (variant == 0) {
    memcpy(w->hidden_before, hid->opp_hole, sizeof w->hidden_before);
    memcpy(w->hidden_after, hid->opp_hole, sizeof w->hidden_after);
    memcpy(w->undealt_before, hid->undealt, (size_t)hid->nundealt);
    memcpy(w->undealt_after, hid->undealt, (size_t)hid->nundealt);
  } else {
    /* Sentinels: the hidden cards swapped for other cards (some of them
       copies of my own and the board's - impossible, which is the point),
       and the undealt order reversed. */
    for (i = 0; i < 6; i++) {
      w->hidden_before[i][0] = w->hidden_after[i][0] = w->v.hole[i & 1];
      w->hidden_before[i][1] = w->hidden_after[i][1] = (Card)(51 - i);
    }
    for (i = 0; i < hid->nundealt; i++)
      w->undealt_before[i] = w->undealt_after[i] = hid->undealt[hid->nundealt - 1 - i];
  }
}

static void clean_copy(const AiView *src, AiView *dst)
{
  int i;
  memset(dst, 0, sizeof *dst);
  dst->seats = src->seats; dst->me = src->me; dst->button = src->button; dst->street = src->street;
  dst->hole[0] = src->hole[0]; dst->hole[1] = src->hole[1];
  for (i = 0; i < 5; i++) dst->board[i] = i < src->nboard ? src->board[i] : CARD_NONE;
  dst->nboard = src->nboard;
  for (i = 0; i < src->seats; i++) {
    dst->stack[i] = src->stack[i]; dst->bet[i] = src->bet[i];
    dst->active[i] = src->active[i]; dst->allin[i] = src->allin[i];
  }
  dst->pot = src->pot; dst->to_call = src->to_call; dst->min_raise = src->min_raise;
  dst->big_blind = src->big_blind;
  memcpy(dst->history, src->history, (size_t)src->nhist);
  dst->nhist = src->nhist;
  dst->personality = src->personality;
}

int main(void)
{
  static World w0, w1;
  AiView clean;
  AiPool *pool;
  HoldemAiHooks hooks;
  int n, differ_hidden = 0, positions = 600, mc = 0;
  AiOptions opt;

  ai_init();
  CHECK(decide_signature == ai_decide);
  memset(&opt, 0, sizeof opt);

  for (n = 0; n < positions; n++) {
    HiddenState h0, h1;
    AiDecision d0, d1, dc;
    AiTrace tr;
    Rng r0, r1, rc;
    uint64_t seed = 0xC0FFEEu + (uint64_t)n;
    build_world(&w0, 1000u + (uint64_t)n, 0, &h0);
    build_world(&w1, 1000u + (uint64_t)n, 1, &h1);
    /* The hidden cards really are different in memory. */
    if (memcmp(w0.hidden_before, w1.hidden_before, sizeof w0.hidden_before) != 0) differ_hidden++;
    CHECK(memcmp(&w0.v, &w1.v, sizeof w0.v) != 0);   /* the junk differs too */
    clean_copy(&w0.v, &clean);
    rng_seed(&r0, seed); rng_seed(&r1, seed); rng_seed(&rc, seed);
    ai_decide_ex(&w0.v, &r0, &opt, &d0, &tr);
    ai_decide(&w1.v, &r1, &d1);
    ai_decide(&clean, &rc, &dc);
    if (tr.trials > 0) mc++;
    CHECK(same_decision(&d0, &d1));
    CHECK(same_decision(&d0, &dc));
    /* The AI's Rng advanced identically: it read nothing else either. */
    CHECK(memcmp(&r0, &r1, sizeof r0) == 0);
  }
  CHECK_EQ_INT(differ_hidden, positions);
  printf("%d positions (%d with Monte Carlo): decisions identical with real hidden cards, "
         "sentinel cards and junk around and inside the view\n", positions, mc);

  /* 3. Through the hooks: the pool copies the view, so junk and hidden
     cards around the caller's copy cannot matter either. */
  pool = ai_pool_create(2);
  CHECK(pool != NULL);
  hooks = ai_pool_hooks(pool);
  for (n = 0; n < 60; n++) {
    HiddenState h0, h1;
    AiDecision d0, d1;
    int seat;
    build_world(&w0, 5000u + (uint64_t)n, 0, &h0);
    build_world(&w1, 5000u + (uint64_t)n, 1, &h1);
    seat = w0.v.me;
    hooks.begin(hooks.ctx, seat, &w0.v, 99u + (uint64_t)n);
    hooks.collect(hooks.ctx, seat, &d0);
    hooks.begin(hooks.ctx, seat, &w1.v, 99u + (uint64_t)n);
    hooks.collect(hooks.ctx, seat, &d1);
    CHECK(same_decision(&d0, &d1));
  }
  ai_pool_destroy(pool);
  return test_finish("ai_integrity");
}
