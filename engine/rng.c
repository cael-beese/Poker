/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
#include "rng.h"

#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>

#if defined(__linux__)
#include <sys/random.h>
#endif

uint64_t rng_splitmix64(uint64_t *state)
{
    uint64_t z = (*state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

void rng_seed(Rng *r, uint64_t seed)
{
    uint64_t sm = seed;
    int i;
    for (i = 0; i < 4; i++) r->s[i] = rng_splitmix64(&sm);
}

static inline uint64_t rotl(uint64_t x, int k)
{
    return (x << k) | (x >> (64 - k));
}

uint64_t rng_next(Rng *r)
{
    uint64_t *s = r->s;
    const uint64_t result = rotl(s[1] * 5, 7) * 9;
    const uint64_t t = s[1] << 17;
    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= t;
    s[3] = rotl(s[3], 45);
    return result;
}

/* Lemire's multiply-and-reject: take the high 32 bits of a 32x32 product and
   reject the few low-word values that would make some outputs one preimage
   more likely than others. The rejection threshold (2^32 mod n) is only
   computed on the rare path where it can matter. */
uint32_t rng_below(Rng *r, uint32_t n)
{
    uint64_t m;
    uint32_t l;
    if (n == 0) return 0;
    m = (rng_next(r) >> 32) * (uint64_t)n;
    l = (uint32_t)m;
    if (l < n) {
        uint32_t t = (uint32_t)(-n) % n;
        while (l < t) {
            m = (rng_next(r) >> 32) * (uint64_t)n;
            l = (uint32_t)m;
        }
    }
    return (uint32_t)(m >> 32);
}

double rng_unit(Rng *r)
{
    return (double)(rng_next(r) >> 11) * (1.0 / 9007199254740992.0);
}

static int read_all(int fd, void *buf, size_t len)
{
    unsigned char *p = buf;
    while (len > 0) {
        ssize_t k = read(fd, p, len);
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0) return -1;
        p += k;
        len -= (size_t)k;
    }
    return 0;
}

uint64_t rng_os_seed(void)
{
    uint64_t seed = 0;

#if defined(__linux__)
    {
        ssize_t k;
        do {
            k = getrandom(&seed, sizeof seed, 0);
        } while (k < 0 && errno == EINTR);
        if (k == (ssize_t)sizeof seed) return seed;
    }
#endif

    {
        int fd = open("/dev/urandom", O_RDONLY);
        if (fd >= 0) {
            int ok = read_all(fd, &seed, sizeof seed) == 0;
            close(fd);
            if (ok) return seed;
        }
    }

    /* Last resort, only reached on a system with neither getrandom() nor
       /dev/urandom: mix the clock with the pid so two cabinets booted in the
       same second still differ. */
    {
        struct timespec ts;
        uint64_t st;
        clock_gettime(CLOCK_REALTIME, &ts);
        st = ((uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec)
           ^ ((uint64_t)getpid() << 32);
        clock_gettime(CLOCK_MONOTONIC, &ts);
        st ^= (uint64_t)ts.tv_nsec * 0x9E3779B97F4A7C15ull;
        return rng_splitmix64(&st);
    }
}
