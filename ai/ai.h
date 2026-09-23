/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
#ifndef BPL_AI_H
#define BPL_AI_H

/* The AI library's own API, beyond the contract in ai_view.h: equity,
   starting-hand tables, personalities, difficulty, and the worker-thread
   implementation of HoldemAiHooks. The app includes this to wire the hooks;
   the Hold'em module needs only ai_view.h.

   How the AI reads an AiView (the Hold'em module should fill it this way):
     - seats: seats at the table (<= 6); only indices < seats are read.
     - stack[i]: chips BEHIND (not counting bet[i]); bet[i]: chips in front
       of seat i on this street; pot: chips already collected from earlier
       streets, NOT including this street's bet[]. The AI adds sum(bet[]).
     - to_call: max(bet) - bet[me]; the AI caps it at its stack itself.
     - min_raise: either the minimum raise-to total or the minimum raise
       increment - both are recognised (a raise-to total is always at least
       highest bet + big blind; an increment never reaches that while there
       is a bet, and with no bet both forms are the same number).
     - active[i]: still holding cards this hand (not folded, not busted);
       allin[i]: has no chips behind. A seat is taken to be in the hand if
       either flag is set.
     - board[0..nboard) and history[0..nhist) only; the rest is never read.
   Every read of the view stays inside those bounds; the integrity test
   checks that decisions ignore everything else. */

#include <stdint.h>

#include "ai/ai_view.h"

/* ---- set-up ------------------------------------------------------------ */

/* Builds the AI's small tables and calls eval_init(). Idempotent and
   thread-safe; ai_decide calls it itself, so calling it is optional (it
   just moves the one-off cost, well under a millisecond, to start-up). */
void ai_init(void);

/* ---- starting hands ---------------------------------------------------- */

#define AI_NCLASSES 169
#define AI_NCOMBOS  1326

/* The 169 starting-hand classes on the usual 13x13 grid: pairs on the
   diagonal (r*13 + r), suited hands above it (hi*13 + lo), offsuit below
   (lo*13 + hi); ranks 0 = deuce .. 12 = ace. */
int         ai_hand_class(Card a, Card b);
const char *ai_class_name(int cls, char out[4]);          /* "AKs", "T9o", "77" */
int         ai_class_combos(int cls);                      /* 6, 4 or 12        */
/* Where a class sits among all 1326 starting hands, strongest first, as a
   fraction 0..1 of hands (the midpoint of its own combos). "play" ranks for
   multi-way pots (blends equity against one and against three random
   hands); "allin" ranks by equity against one random hand, the right order
   for push/fold. */
float       ai_class_pct(int cls);
float       ai_class_pct_allin(int cls);
/* Equity of a class against one / three random hands (from the generated
   table; tools/ai_gen_preflop.c rebuilds it). */
float       ai_class_eq1(int cls);
float       ai_class_eq3(int cls);

/* Approximate Nash push range for an unopened pot: the fraction of hands to
   move all-in with at an effective stack of bb big blinds with n_behind
   players still to act. */
float       ai_push_range(float bb, int n_behind);
/* Positional open-raise range (fraction of hands) for a deep unopened pot,
   6-max: n_behind 5 = UTG, 4 = HJ, 3 = CO, 2 = BTN, 1 = SB; ndealt 2 is
   heads-up, where the button is the small blind. */
float       ai_open_range(int n_behind, int ndealt);

/* ---- equity ------------------------------------------------------------ */

/* A public-information range model for one opponent. */
typedef struct {
  float   top;         /* pre-flop: centred on the best `top` fraction of
                          starting hands ("play" order), with a soft tail;
                          1 = any two cards                                */
  uint8_t post_bets;   /* bets/raises this player made after the flop      */
  uint8_t post_calls;  /* calls this player made after the flop            */
} AiRange;

/* Combo index for two distinct cards: hi*(hi-1)/2 + lo, 0..1325. */
static inline int ai_combo_index(Card a, Card b)
{
  int hi = a > b ? a : b, lo = a > b ? b : a;
  return hi * (hi - 1) / 2 + lo;
}
void ai_combo_cards(int idx, Card out[2]);

/* The relative weight (0..1) of every combo in a range, given the cards the
   AI can see: 0 for combos that use a known card (known = my hole cards and
   the board), otherwise pre-flop weight times post-flop weight from how the
   combo plays on the current board. */
void ai_range_weights(const AiRange *r, const Card *known, int nknown,
                      const Card *board, int nboard, float w[AI_NCOMBOS]);

/* Monte Carlo equity (my share of the pot, ties split) of hole against
   nopp opponents (1..5) whose hands are drawn from the cards the AI cannot
   see, weighted by ranges[i] (NULL = every opponent holds any two cards),
   with the rest of the board dealt at random. A fixed number of trials, so
   the result depends only on the inputs and the Rng. */
double ai_equity(const Card hole[2], const Card *board, int nboard,
                 int nopp, const AiRange *ranges, int trials, Rng *rng);

/* The same with explicit weights: w[o] is opponent o's weight (0..1) for
   each combo index, or NULL for any two cards; w itself may be NULL. Combos
   that use a known card are ignored. For tools and tests (e.g. equity
   against exactly the six QQ combos). */
double ai_equity_w(const Card hole[2], const Card *board, int nboard,
                   int nopp, const float *const w[], int trials, Rng *rng);

/* The default trial count for nopp opponents: sized so the heaviest
   decision stays well inside 30 ms on the Pi 4 (see the report in
   tests/ai/test_ai_timing.c for measured numbers). */
int ai_default_trials(int nopp);

/* ---- personalities and difficulty -------------------------------------- */

typedef struct {
  const char *name;
  /* pre-flop */
  float open_mult;     /* scales the positional open-raise ranges            */
  float limp_mult;     /* extra range entered by limping / flat calling      */
  float reraise_mult;  /* scales the value 3-bet / 4-bet ranges              */
  float bluff3;        /* chance of a light 3-bet with a playable hand       */
  float pf_slack;      /* equity discount when calling a raise pre-flop      */
  float push_bb;       /* at or below this effective stack: push / fold      */
  float push_mult;     /* scales the push ranges                             */
  /* post-flop */
  float value_thr;     /* per-opponent strength to bet for value             */
  float raise_thr;     /* per-opponent strength to raise for value           */
  float aggr;          /* chance to bet/raise when the value test passes     */
  float bluff;         /* base bluff frequency                               */
  float semibluff;     /* bet/raise frequency with a real draw               */
  float call_margin;   /* equity added to the pot-odds price (<0 calls light)*/
  float allin_margin;  /* extra equity wanted to commit most of the stack    */
  float hand_reading;  /* 0..1: how much opponents' actions narrow ranges    */
  float size_lo, size_hi; /* bet sizes, fraction of the pot                  */
  float mix;           /* threshold jitter, so boundaries are not sharp      */
  /* presentation */
  float tempo;         /* think-time multiplier                              */
  float tell_honesty;  /* 0..1: how much the tell tracks the real decision   */
} AiPersonality;

const AiPersonality *ai_personality(int id);   /* AI_ROCK .. AI_FISH; NULL if bad */
const char          *ai_personality_name(int id);

enum { AI_DIFF_EASY, AI_DIFF_NORMAL, AI_DIFF_HARD, AI_DIFF_EXPERT, AI_NDIFF };
/* Personalities for the five AI seats at a difficulty (clamped to
   0..AI_NDIFF-1): the mix is fixed per difficulty, the seat order is
   shuffled with r. Easy is mostly Fish; Expert is Sharks and a Rock. */
void ai_assign_personalities(int difficulty, Rng *r, int out[5]);

/* ---- decisions with options and a trace (tools and tests) --------------- */

typedef struct {
  int trials;                        /* 0 = ai_default_trials()            */
  const AiPersonality *personality;  /* NULL = from view->personality      */
} AiOptions;

typedef struct {
  double equity;        /* Monte Carlo equity, or -1 when none was run     */
  double strength;      /* per-opponent strength equity^(1/n), or -1       */
  double price;         /* pot odds of the call, 0 when nothing to call    */
  float  pct;           /* my starting-hand class percentile               */
  float  closeness;     /* 0 = easy decision .. 1 = on the boundary        */
  int    trials;        /* Monte Carlo trials run                          */
  int    bluff;         /* 1 when the bet/raise is a bluff or semi-bluff   */
  const char *why;      /* short reason, for logs                          */
} AiTrace;

void ai_decide_ex(const AiView *v, Rng *ai_rng, const AiOptions *opt,
                  AiDecision *out, AiTrace *trace);

/* ---- worker threads: the HoldemAiHooks implementation ------------------ */

/* A small pool of worker threads. begin() copies the view into the seat's
   slot and returns at once; a worker runs ai_decide with an Rng seeded from
   the seed; collect() waits for that result. Results depend only on the
   view and the seed, never on scheduling. Nothing is allocated per
   decision. If the threads cannot be started, collect() computes the
   decision itself (still deterministic, just on the caller's thread). */
typedef struct AiPool AiPool;

AiPool       *ai_pool_create(int nthreads);    /* 1..4; NULL on out of memory */
void          ai_pool_destroy(AiPool *p);      /* joins the workers            */
HoldemAiHooks ai_pool_hooks(AiPool *p);
/* Options for every decision the pool makes (trial count override for the
   tools). Call only while no decision is in flight. */
void          ai_pool_set_options(AiPool *p, const AiOptions *opt);

#endif
