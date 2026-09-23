/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
#ifndef BPL_ENGINE_RNG_H
#define BPL_ENGINE_RNG_H

#include <stdint.h>

/* xoshiro256** (Blackman and Vigna), seeded through splitmix64 so any 64-bit
   seed, including 0, gives a well-mixed non-zero state. The whole state is
   the struct, so copying an Rng forks the stream and saving it resumes it. */
typedef struct { uint64_t s[4]; } Rng;

void     rng_seed(Rng *r, uint64_t seed);
uint64_t rng_next(Rng *r);
uint32_t rng_below(Rng *r, uint32_t n);             /* unbiased, [0,n); 0 when n == 0 */
double   rng_unit(Rng *r);                          /* [0,1), 53 bits           */
uint64_t rng_os_seed(void);                         /* getrandom(); falls back to
                                                       /dev/urandom, then clock^pid */

/* One splitmix64 step: advances *state and returns the next output. Exposed
   because it is the documented way to derive seeds (per-hand seeds, the
   separate presentation / AI / attract streams) from one session seed. */
uint64_t rng_splitmix64(uint64_t *state);

#endif
