/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
#ifndef BPL_ENGINE_WALLET_H
#define BPL_ENGINE_WALLET_H

#include <stdint.h>

/* Free-play credits. Owned by the app and passed into each game's tick;
   games change credits only through it. denom is what one credit is shown
   as (for display and the service menu), never used in game logic. */
typedef struct { int64_t credits; int denom; } Wallet;

/* Takes n credits if the wallet has them: 0 done, -1 refused (n < 0 or not
   enough credits), and the wallet is unchanged on refusal. */
static inline int wallet_debit(Wallet *w, int64_t n)
{
    if (n < 0 || w->credits < n) return -1;
    w->credits -= n;
    return 0;
}

/* Adds n credits; negative n is ignored rather than turned into a debit. */
static inline void wallet_credit(Wallet *w, int64_t n)
{
    if (n > 0) w->credits += n;
}

static inline int wallet_can_afford(const Wallet *w, int64_t n)
{
    return n >= 0 && w->credits >= n;
}

#endif
