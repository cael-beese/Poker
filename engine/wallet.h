/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* engine/wallet.h - the player's credits, owned by the app, passed into games.
 *
 * PLACEHOLDER written from docs/CONTRACT.md section 3 by the platform branch so
 * it can build before the engine branch merges. The engine agent owns this
 * file; at merge the engine's version wins. */
#ifndef BPL_ENGINE_WALLET_H
#define BPL_ENGINE_WALLET_H

#include <stdint.h>

typedef struct { int64_t credits; int denom; } Wallet;

#endif
