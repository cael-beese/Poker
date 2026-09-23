/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
#ifndef BPL_ENGINE_EVAL_INTERNAL_H
#define BPL_ENGINE_EVAL_INTERNAL_H

/* Alternative evaluation paths kept for tests and tools/bench_eval only.
   Game code uses eval.h. */

#include <stdint.h>

#include "card.h"

/* eval5_ck with the paired-hand lookup done by binary search over the sorted
   prime products instead of the hash (the classic Cactus Kev method). */
int eval5_ck_bsearch(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e);

/* Seven cards the straightforward way: the minimum eval5 over the 21 five-card
   subsets. eval7 uses a direct 7-card path instead; this is the reference. */
int eval7_combo(const Card c[7]);

/* Hash statistics: slots, keys, average and worst probe length. */
void eval_hash_stats(unsigned *slots, unsigned *keys, double *avg_probes, unsigned *max_probes);

#endif
