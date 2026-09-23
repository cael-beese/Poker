/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
#ifndef BPL_AI_AI_VIEW_H
#define BPL_AI_AI_VIEW_H

#include <stdint.h>

#include "engine/card.h"
#include "engine/rng.h"

/* ai/ai_view.h - the only thing the Hold'em AI may know about a hand, and
   the interface between the table and the AI (docs/CONTRACT.md section 5).
   The AI never sees hidden cards or the deck. */

typedef struct {
  int   seats, me, button, street;            /* street 0 pre-flop .. 3 river */
  Card  hole[2];                              /* MY cards only                */
  Card  board[5]; int nboard;
  int64_t stack[6], bet[6], pot, to_call, min_raise, big_blind;
  uint8_t active[6], allin[6];                /* public seat state            */
  uint8_t history[64]; int nhist;             /* public action history codes  */
  int   personality;                          /* AI_ROCK, AI_MANIAC, AI_SHARK, AI_FISH */
} AiView;
typedef struct { int action; int64_t amount; int think_ticks; float tell; } AiDecision;
void ai_decide(const AiView *v, Rng *ai_rng, AiDecision *out);

enum { ACT_FOLD, ACT_CHECK, ACT_CALL, ACT_BET, ACT_RAISE, ACT_ALLIN, ACT_POST_SB, ACT_POST_BB };
/* AiDecision.action is one of FOLD/CHECK/CALL/BET/RAISE/ALLIN; amount is the
   TOTAL this seat has in front of it this street after acting ("raise to"),
   not the increment.  The table validates and clamps it (min-raise, stack). */
/* history[i] = (street << 6) | (seat << 3) | action - every public action of
   the hand in order, including the blind posts. */
#define AI_HIST(street, seat, act) ((uint8_t)(((street) << 6) | ((seat) << 3) | (act)))

/* How the table asks for a decision without linking the AI library: the app
   wires these hooks when it creates the Hold'em game. */
typedef struct {
  void *ctx;
  /* the seat's turn begins: start thinking (may start a worker thread).
     seed is derived by the table from the hand's seed and the action number,
     so the same hand replays the same decisions. */
  void (*begin)(void *ctx, int seat, const AiView *v, uint64_t seed);
  /* blocks until that decision is ready and returns it */
  void (*collect)(void *ctx, int seat, AiDecision *out);
} HoldemAiHooks;

#endif
