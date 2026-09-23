/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
#ifndef BPL_GAMES_HOLDEM_BOTS_H
#define BPL_GAMES_HOLDEM_BOTS_H

#include "games/holdem/holdem.h"

/* Trivial built-in players behind the same HoldemAiHooks interface the real
   AI uses, for tests, simulations and as a stand-in before the AI is wired.
   They see only the AiView, like the real AI. Each decision is a pure
   function of the view and the seed the table passes to begin(). */

enum {
    HOLDEM_BOT_CALL,    /* checks, or calls anything                          */
    HOLDEM_BOT_RANDOM,  /* a random legal action and size                     */
    HOLDEM_BOT_CHAOS    /* random, plus illegal actions, amounts and think times
                           the table must clamp                               */
};

typedef struct {
    int        kind[HOLDEM_SEATS];
    AiDecision pending[HOLDEM_SEATS];
    uint8_t    has_pending[HOLDEM_SEATS];
    long       begins, collects;
    long       protocol_errors;  /* begin twice, or collect without begin     */
} HoldemBots;

void holdem_bots_init(HoldemBots *b, int kind);
HoldemAiHooks holdem_bots_hooks(HoldemBots *b);
void holdem_bot_decide(int kind, const AiView *v, uint64_t seed, AiDecision *out);

#endif
