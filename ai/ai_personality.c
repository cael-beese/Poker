/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* The four opponents and how difficulty mixes them.

   The numbers were tuned with tools/ai_selfplay.c until each personality's
   measured VPIP / PFR / aggression / showdown profile matched its type and
   none lost to the simple exploit bots in that harness (see its output). */

#include "ai/ai.h"

#include <stddef.h>

static const AiPersonality g_pers[4] = {
  /* AI_ROCK: tight-passive. Plays few hands, limps some of them, rarely
     bluffs, calls a bit tight, needs a real hand to raise or go all-in. */
  { "Rock",
    0.65f, 1.25f, 0.70f, 0.01f, -0.06f, 10.0f, 0.75f,
    0.70f, 0.87f, 0.45f, 0.04f, 0.12f, 0.03f, 0.08f, 0.80f,
    0.45f, 0.65f, 0.03f,
    1.15f, 0.60f },
  /* AI_MANIAC: loose-aggressive. Raises a wide range, 3-bets light, bets
     big, bluffs often, happy to get it in. */
  { "Maniac",
    2.10f, 1.15f, 2.50f, 0.30f, 0.06f, 15.0f, 1.50f,
    0.55f, 0.68f, 0.90f, 0.40f, 0.80f, -0.02f, 0.00f, 0.40f,
    0.70f, 1.30f, 0.08f,
    0.80f, 0.35f },
  /* AI_SHARK: tight-aggressive and solid. Positional ranges, raises or
     folds pre-flop, bets for value and bluffs at a balanced rate, prices
     every call, reads ranges from the betting, and gives little away. */
  { "Shark",
    1.00f, 1.00f, 1.20f, 0.10f, 0.00f, 13.0f, 1.00f,
    0.62f, 0.80f, 0.80f, 0.20f, 0.55f, 0.00f, 0.04f, 1.00f,
    0.50f, 0.80f, 0.05f,
    1.00f, 0.15f },
  /* AI_FISH: calling station. Limps and calls far too much, rarely raises,
     chases, barely reads what others do, and shows how it feels. */
  { "Fish",
    0.45f, 5.00f, 0.60f, 0.00f, 0.12f, 6.0f, 0.80f,
    0.72f, 0.90f, 0.35f, 0.05f, 0.15f, -0.08f, 0.02f, 0.15f,
    0.33f, 0.60f, 0.06f,
    0.90f, 0.80f },
};

const AiPersonality *ai_personality(int id)
{
  return id >= 0 && id < 4 ? &g_pers[id] : NULL;
}

const char *ai_personality_name(int id)
{
  return id >= 0 && id < 4 ? g_pers[id].name : "?";
}

void ai_assign_personalities(int difficulty, Rng *r, int out[5])
{
  /* Five seats per difficulty: harder tables swap Fish for Sharks and a
     Rock; there is always one Maniac, because a table needs some action. */
  static const int mix[AI_NDIFF][5] = {
    { AI_FISH, AI_FISH, AI_FISH, AI_MANIAC, AI_ROCK  },   /* easy   */
    { AI_FISH, AI_FISH, AI_MANIAC, AI_ROCK, AI_SHARK },   /* normal */
    { AI_FISH, AI_MANIAC, AI_ROCK, AI_SHARK, AI_SHARK },  /* hard   */
    { AI_MANIAC, AI_ROCK, AI_SHARK, AI_SHARK, AI_SHARK }, /* expert */
  };
  int i;
  if (difficulty < 0) difficulty = 0;
  if (difficulty >= AI_NDIFF) difficulty = AI_NDIFF - 1;
  for (i = 0; i < 5; i++) out[i] = mix[difficulty][i];
  for (i = 4; i > 0; i--) {               /* Fisher-Yates over the seats */
    int j = (int)rng_below(r, (uint32_t)(i + 1));
    int t = out[i]; out[i] = out[j]; out[j] = t;
  }
}
