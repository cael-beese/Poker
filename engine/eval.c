/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* Poker hand evaluator.

   Five cards: Cactus Kev's scheme. Each card is a 32-bit word
       xxxbbbbb bbbbbbbb cdhsrrrr xxpppppp
   (b = one bit per rank, cdhs = one bit per suit, r = rank, p = rank prime).
     - Five cards of one suit: the OR of the rank bits indexes T_flush.
     - Five distinct ranks, not a flush: the same mask indexes T_unique5
       (straights and high-card hands).
     - Anything with a repeated rank: the product of the five rank primes is
       unique to the rank multiset (unique factorisation), and is looked up in
       an open-addressing hash built here.
   Every table is filled by enumerating the hand classes in order, best
   first, so rank numbers come out of the loops rather than from a copied
   table.

   Six and seven cards: a direct path instead of trying every 5-subset.
     - If five or more cards share a suit, the hand is a flush or better and
       nothing without that suit can beat it (quads or a full house would need
       at least 8 cards alongside a 5-card flush), so the answer is
       T_flush[mask of that suit's ranks]; T_flush covers masks of 5..7 bits.
     - Otherwise the best hand depends only on the multiset of ranks. That
       multiset is written in base 5 (one digit per rank, digit = count <= 4)
       by adding a per-card weight, split into a low part (ranks 2..8, 7
       digits) and a high part (ranks 9..A, 6 digits) packed into one word.
       idx = T_base7[high] + T_lo_rank[low] is a perfect, minimal index of the
       49,205 seven-card rank multisets: T_base7 gives each high part the
       start of a block as long as the number of low parts that complete it,
       and T_lo_rank numbers the low parts of each digit sum consecutively.
       T_noflush7[idx] holds the best five-card rank. At start-up the
       six-card table (the same scheme with T_base6) is filled from eval5
       over the 6 subsets of a representative hand, and each seven-card entry
       is the best six-card entry over the ways of dropping one card. */

#include "eval.h"
#include "eval_internal.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint32_t PRIME[13] = { 2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41 };

/* Straight masks best first; the wheel (5-4-3-2-A) is last. */
static const uint16_t STRAIGHT_MASK[10] = {
    0x1F00, 0x0F80, 0x07C0, 0x03E0, 0x01F0, 0x00F8, 0x007C, 0x003E, 0x001F, 0x100F
};

#define N_PAIRED 4888      /* 156 + 156 + 858 + 858 + 2860 */
#define N_NOFLUSH6 18395   /* 6-card rank multisets, each count <= 4 */
#define N_NOFLUSH7 49205   /* 7-card rank multisets, each count <= 4 */
#define Q_LO_SIZE 78125    /* 5^7 */
#define Q_HI_SIZE 15625    /* 5^6 */
#define Q_LO_BITS 17       /* 5^7 < 2^17 */

#ifndef BPL_EVAL_HBITS
#define BPL_EVAL_HBITS 13
#endif
#define H_SIZE (1u << BPL_EVAL_HBITS)
#define H_MASK (H_SIZE - 1u)

typedef struct { uint32_t key; uint32_t val; } HSlot;

static uint32_t T_ck[52];
static uint32_t T_qw[52];                 /* quinary weight of each card     */
static uint16_t T_flush[8192];            /* rank mask (5..7 bits) -> rank   */
static uint16_t T_unique5[8192];          /* 5 distinct ranks, no flush      */
static HSlot    T_hash[H_SIZE];           /* prime product -> rank           */
static uint32_t T_hmul;
static uint32_t T_pkey[N_PAIRED];         /* sorted prime products           */
static uint16_t T_pval[N_PAIRED];
static uint16_t T_lo_rank[Q_LO_SIZE];
static uint16_t T_base6[Q_HI_SIZE];
static uint16_t T_base7[Q_HI_SIZE];
static uint16_t T_noflush6[N_NOFLUSH6];
static uint16_t T_noflush7[N_NOFLUSH7];
static uint32_t T_desc[EVAL_WORST_RANK + 1];  /* rank -> 5 rank nibbles     */
static uint8_t  K7[21][5];
static uint8_t  K6[6][5];

static pthread_once_t g_once = PTHREAD_ONCE_INIT;

/* ---- start-up construction --------------------------------------------- */

static int  g_next_rank;
static int  g_npaired;

static void fatal(const char *what)
{
    /* Only reachable if the construction itself is wrong; a wrong evaluator
       must never run silently. */
    fprintf(stderr, "eval_init: internal error: %s\n", what);
    abort();
}

static uint32_t pack_desc(int a, int b, int c, int d, int e)
{
    return (uint32_t)a << 16 | (uint32_t)b << 12 | (uint32_t)c << 8 | (uint32_t)d << 4 | (uint32_t)e;
}

static uint32_t pack_mask_desc(unsigned m)
{
    uint32_t d = 0;
    int r;
    for (r = 12; r >= 0; r--)
        if (m & (1u << r)) d = (d << 4) | (uint32_t)r;
    return d;
}

static int is_straight(unsigned m)
{
    int i;
    for (i = 0; i < 10; i++)
        if (m == STRAIGHT_MASK[i]) return 1;
    return 0;
}

static void add_paired(uint32_t key, uint32_t desc)
{
    if (g_npaired >= N_PAIRED) fatal("too many paired classes");
    T_pkey[g_npaired] = key;
    T_pval[g_npaired] = (uint16_t)g_next_rank;
    T_desc[g_next_rank] = desc;
    g_npaired++;
    g_next_rank++;
}

static uint32_t hash_slot(uint32_t key, uint32_t mul)
{
    return (key * mul) >> (32 - BPL_EVAL_HBITS);
}

/* Total probe count of filling the hash with multiplier mul. */
static unsigned hash_fill(uint32_t mul)
{
    unsigned total = 0;
    int i;
    memset(T_hash, 0, sizeof T_hash);
    for (i = 0; i < N_PAIRED; i++) {
        uint32_t s = hash_slot(T_pkey[i], mul);
        total++;
        while (T_hash[s].key != 0) { s = (s + 1) & H_MASK; total++; }
        T_hash[s].key = T_pkey[i];
        T_hash[s].val = T_pval[i];
    }
    return total;
}

static void build_hash(void)
{
    /* Try a fixed list of odd multipliers (a splitmix64 stream from a fixed
       seed, so the table is the same on every run) and keep the one with the
       shortest probe sequences. */
    uint64_t sm = 0x5EEDB0B5ull;
    uint32_t best_mul = 0x9E3779B1u;
    unsigned best = hash_fill(best_mul);
    int i;
    for (i = 0; i < 32; i++) {
        uint64_t z = (sm += 0x9E3779B97F4A7C15ull);
        uint32_t mul;
        unsigned cost;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        mul = (uint32_t)(z ^ (z >> 31)) | 1u;
        cost = hash_fill(mul);
        if (cost < best) { best = cost; best_mul = mul; }
    }
    T_hmul = best_mul;
    hash_fill(best_mul);
}

static int cmp_u32_idx(const void *a, const void *b)
{
    uint32_t x = T_pkey[*(const int *)a], y = T_pkey[*(const int *)b];
    return x < y ? -1 : x > y;
}

static void sort_paired(void)
{
    static int idx[N_PAIRED];
    static uint32_t k2[N_PAIRED];
    static uint16_t v2[N_PAIRED];
    int i;
    for (i = 0; i < N_PAIRED; i++) idx[i] = i;
    qsort(idx, N_PAIRED, sizeof idx[0], cmp_u32_idx);
    for (i = 0; i < N_PAIRED; i++) { k2[i] = T_pkey[idx[i]]; v2[i] = T_pval[idx[i]]; }
    memcpy(T_pkey, k2, sizeof k2);
    memcpy(T_pval, v2, sizeof v2);
    for (i = 1; i < N_PAIRED; i++)
        if (T_pkey[i] == T_pkey[i - 1]) fatal("duplicate prime product");
}

static void build_five(void)
{
    int a, b, c, d, i;
    unsigned m;

    for (i = 0; i < 52; i++) {
        int r = i >> 2, s = i & 3;
        T_ck[i] = PRIME[r] | ((uint32_t)r << 8) | (0x8000u >> s) | (1u << (16 + r));
    }

    g_next_rank = 1;
    g_npaired = 0;

    /* Straight flushes, royal first. */
    for (i = 0; i < 10; i++) {
        T_flush[STRAIGHT_MASK[i]] = (uint16_t)g_next_rank;
        T_desc[g_next_rank++] = pack_mask_desc(STRAIGHT_MASK[i]);
    }
    T_desc[10] = pack_desc(3, 2, 1, 0, 12);   /* the steel wheel reads 5-4-3-2-A */

    /* Four of a kind: quad rank, then kicker. */
    for (a = 12; a >= 0; a--)
        for (b = 12; b >= 0; b--)
            if (b != a)
                add_paired(PRIME[a] * PRIME[a] * PRIME[a] * PRIME[a] * PRIME[b],
                           pack_desc(a, a, a, a, b));
    /* Full house: trips rank, then pair rank. */
    for (a = 12; a >= 0; a--)
        for (b = 12; b >= 0; b--)
            if (b != a)
                add_paired(PRIME[a] * PRIME[a] * PRIME[a] * PRIME[b] * PRIME[b],
                           pack_desc(a, a, a, b, b));

    /* Flushes: every 5-bit mask that is not a straight. Comparing two such
       masks as integers compares the ranks highest first, which is exactly
       poker order, so a descending loop enumerates them best first. */
    for (m = 0x1F00; m >= 0x1F; m--) {
        if (__builtin_popcount(m) != 5 || is_straight(m)) continue;
        T_flush[m] = (uint16_t)g_next_rank;
        T_desc[g_next_rank++] = pack_mask_desc(m);
    }

    /* Straights. */
    for (i = 0; i < 10; i++) {
        T_unique5[STRAIGHT_MASK[i]] = (uint16_t)g_next_rank;
        T_desc[g_next_rank++] = pack_mask_desc(STRAIGHT_MASK[i]);
    }
    T_desc[1609] = pack_desc(3, 2, 1, 0, 12);

    /* Three of a kind: trips rank, then two kickers high first. */
    for (a = 12; a >= 0; a--)
        for (b = 12; b >= 0; b--)
            for (c = b - 1; c >= 0; c--)
                if (b != a && c != a)
                    add_paired(PRIME[a] * PRIME[a] * PRIME[a] * PRIME[b] * PRIME[c],
                               pack_desc(a, a, a, b, c));
    /* Two pair: high pair, low pair, kicker. */
    for (a = 12; a >= 0; a--)
        for (b = a - 1; b >= 0; b--)
            for (c = 12; c >= 0; c--)
                if (c != a && c != b)
                    add_paired(PRIME[a] * PRIME[a] * PRIME[b] * PRIME[b] * PRIME[c],
                               pack_desc(a, a, b, b, c));
    /* One pair: pair rank, then three kickers high first. */
    for (a = 12; a >= 0; a--)
        for (b = 12; b >= 0; b--)
            for (c = b - 1; c >= 0; c--)
                for (d = c - 1; d >= 0; d--)
                    if (b != a && c != a && d != a)
                        add_paired(PRIME[a] * PRIME[a] * PRIME[b] * PRIME[c] * PRIME[d],
                                   pack_desc(a, a, b, c, d));

    /* High card: the same masks as the flushes. */
    for (m = 0x1F00; m >= 0x1F; m--) {
        if (__builtin_popcount(m) != 5 || is_straight(m)) continue;
        T_unique5[m] = (uint16_t)g_next_rank;
        T_desc[g_next_rank++] = pack_mask_desc(m);
    }

    if (g_next_rank != EVAL_WORST_RANK + 1) fatal("rank count is not 7462");
    if (g_npaired != N_PAIRED) fatal("paired class count is not 4888");

    /* Flush masks of 6 and 7 bits take their best 5-bit submask. Every
       submask is a smaller number, so an ascending loop has it ready. */
    for (m = 0; m < 8192; m++) {
        int pc = __builtin_popcount(m);
        unsigned bits, best = 0xFFFF;
        if (pc < 6 || pc > 7) continue;
        for (bits = m; bits; bits &= bits - 1) {
            unsigned sub = m & ~(bits & -bits);
            if (T_flush[sub] < best) best = T_flush[sub];
        }
        T_flush[m] = (uint16_t)best;
    }

    sort_paired();
    build_hash();

    /* Subset index tuples for eval_best and the reference 7-card path. */
    {
        int n = 0, e;
        for (a = 0; a < 7; a++) for (b = a + 1; b < 7; b++) for (c = b + 1; c < 7; c++)
        for (d = c + 1; d < 7; d++) for (e = d + 1; e < 7; e++) {
            K7[n][0] = (uint8_t)a; K7[n][1] = (uint8_t)b; K7[n][2] = (uint8_t)c;
            K7[n][3] = (uint8_t)d; K7[n][4] = (uint8_t)e; n++;
        }
        n = 0;
        for (a = 0; a < 6; a++) {   /* leave out card a */
            int k = 0;
            for (b = 0; b < 6; b++) if (b != a) K6[n][k++] = (uint8_t)b;
            n++;
        }
    }
}

/* ---- quinary (rank multiset) index for 6 and 7 cards -------------------- */

static uint32_t g_pow5[8];
static uint16_t g_lo_count[8];   /* low parts with each digit sum 0..7 */
static uint8_t  g_filled[N_NOFLUSH7];

static int digit_sum(uint32_t v, int digits)
{
    int s = 0, i;
    for (i = 0; i < digits; i++) { s += (int)(v % 5); v /= 5; }
    return s;
}

static int build_base(uint16_t *base, int n)
{
    uint32_t hi;
    int off = 0;
    for (hi = 0; hi < Q_HI_SIZE; hi++) {
        int s = digit_sum(hi, 6);
        if (s > n) continue;
        base[hi] = (uint16_t)off;
        off += g_lo_count[n - s];
    }
    return off;
}

static int     g_q[13];
static uint16_t *g_nf_table;
static const uint16_t *g_nf_base;
static int     g_nf_n, g_nf_size;

static int best_of_subsets(const uint32_t *ck, int n)
{
    int best = 0x7FFF, i;
    if (n == 7) {
        for (i = 0; i < 21; i++) {
            int r = eval5_ck(ck[K7[i][0]], ck[K7[i][1]], ck[K7[i][2]], ck[K7[i][3]], ck[K7[i][4]]);
            if (r < best) best = r;
        }
    } else {
        for (i = 0; i < 6; i++) {
            int r = eval5_ck(ck[K6[i][0]], ck[K6[i][1]], ck[K6[i][2]], ck[K6[i][3]], ck[K6[i][4]]);
            if (r < best) best = r;
        }
    }
    return best;
}

static void noflush_leaf(void)
{
    uint32_t lo = 0, hi = 0, ck[7];
    int r, k, n = 0, idx;
    for (r = 0; r < 7; r++) lo += (uint32_t)g_q[r] * g_pow5[r];
    for (r = 7; r < 13; r++) hi += (uint32_t)g_q[r] * g_pow5[r - 7];
    idx = g_nf_base[hi] + T_lo_rank[lo];
    if (idx < 0 || idx >= g_nf_size || g_filled[idx]) fatal("quinary index is not a bijection");
    g_filled[idx] = 1;
    if (g_nf_n == 7) {
        /* The best five of seven lie inside some six of them, so the best
           seven-card value is the best six-card value over every way of
           removing one card: at most 7 lookups instead of 21 evaluations. */
        int best = 0x7FFF;
        for (r = 0; r < 13; r++) {
            uint32_t lo6 = lo, hi6 = hi;
            int v;
            if (!g_q[r]) continue;
            if (r < 7) lo6 -= g_pow5[r]; else hi6 -= g_pow5[r - 7];
            v = T_noflush6[T_base6[hi6] + T_lo_rank[lo6]];
            if (v < best) best = v;
        }
        g_nf_table[idx] = (uint16_t)best;
        return;
    }
    /* A representative hand: suits dealt round-robin, so copies of a rank get
       different suits and no suit reaches five (6 cards -> 2,2,1,1). */
    for (r = 0; r < 13; r++)
        for (k = 0; k < g_q[r]; k++, n++)
            ck[n] = T_ck[r * 4 + (n & 3)];
    g_nf_table[idx] = (uint16_t)best_of_subsets(ck, g_nf_n);
}

static void noflush_walk(int r, int left)
{
    int c;
    if (r == 13) {
        if (left == 0) noflush_leaf();
        return;
    }
    for (c = 0; c <= 4 && c <= left; c++) {
        g_q[r] = c;
        noflush_walk(r + 1, left - c);
    }
    g_q[r] = 0;
}

static void build_noflush(uint16_t *table, const uint16_t *base, int n, int size)
{
    int i;
    memset(g_filled, 0, sizeof g_filled);
    g_nf_table = table;
    g_nf_base = base;
    g_nf_n = n;
    g_nf_size = size;
    noflush_walk(0, n);
    for (i = 0; i < size; i++)
        if (!g_filled[i]) fatal("quinary index has a gap");
}

static void build_multi(void)
{
    uint32_t lo;
    int i;
    g_pow5[0] = 1;
    for (i = 1; i < 8; i++) g_pow5[i] = g_pow5[i - 1] * 5;
    for (i = 0; i < 52; i++) {
        int r = i >> 2;
        T_qw[i] = r < 7 ? g_pow5[r] : g_pow5[r - 7] << Q_LO_BITS;
    }
    memset(g_lo_count, 0, sizeof g_lo_count);
    for (lo = 0; lo < Q_LO_SIZE; lo++) {
        int s = digit_sum(lo, 7);
        if (s <= 7) T_lo_rank[lo] = g_lo_count[s]++;
    }
    if (build_base(T_base6, 6) != N_NOFLUSH6) fatal("6-card multiset count");
    if (build_base(T_base7, 7) != N_NOFLUSH7) fatal("7-card multiset count");
    build_noflush(T_noflush6, T_base6, 6, N_NOFLUSH6);
    build_noflush(T_noflush7, T_base7, 7, N_NOFLUSH7);
}

static void eval_build(void)
{
    build_five();
    build_multi();
}

void eval_init(void)
{
    pthread_once(&g_once, eval_build);
}

size_t eval_table_bytes(void)
{
    return sizeof T_ck + sizeof T_qw + sizeof T_flush + sizeof T_unique5 + sizeof T_hash
         + sizeof T_pkey + sizeof T_pval + sizeof T_lo_rank + sizeof T_base6 + sizeof T_base7
         + sizeof T_noflush6 + sizeof T_noflush7 + sizeof T_desc + sizeof K7 + sizeof K6;
}

/* ---- evaluation --------------------------------------------------------- */

uint32_t eval_ck(Card c)
{
    return card_valid(c) ? T_ck[c] : 0;
}

static inline int paired_lookup(uint32_t p)
{
    uint32_t s = hash_slot(p, T_hmul);
    for (;;) {
        uint32_t k = T_hash[s].key;
        if (k == p) return (int)T_hash[s].val;
        if (k == 0) return 0;              /* not a hand (duplicate cards) */
        s = (s + 1) & H_MASK;
    }
}

int eval5_ck(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    uint32_t q = (a | b | c | d | e) >> 16;
    int s;
    if (a & b & c & d & e & 0xF000) return T_flush[q];
    s = T_unique5[q];
    if (s) return s;
    return paired_lookup((a & 0xFF) * (b & 0xFF) * (c & 0xFF) * (d & 0xFF) * (e & 0xFF));
}

int eval5_ck_bsearch(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    uint32_t q = (a | b | c | d | e) >> 16, p;
    int s, lo = 0, hi = N_PAIRED - 1;
    if (a & b & c & d & e & 0xF000) return T_flush[q];
    s = T_unique5[q];
    if (s) return s;
    p = (a & 0xFF) * (b & 0xFF) * (c & 0xFF) * (d & 0xFF) * (e & 0xFF);
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        if (T_pkey[mid] < p) lo = mid + 1;
        else if (T_pkey[mid] > p) hi = mid - 1;
        else return T_pval[mid];
    }
    return 0;
}

int eval5(const Card c[5])
{
    return eval5_ck(T_ck[c[0]], T_ck[c[1]], T_ck[c[2]], T_ck[c[3]], T_ck[c[4]]);
}

/* Per-suit card counts are kept as four 4-bit fields of one word; a count of
   five or more is detected for all suits at once: adding 3 to each field sets
   its top bit exactly when the field is >= 5 (fields never exceed 7, so
   nothing carries into the next one). */
#define SUIT_FIVE(sk) (((sk) + 0x3333u) & 0x8888u)

static inline int flush_rank(const Card *c, int n, uint32_t five)
{
    int s = __builtin_ctz(five) >> 2, i;
    uint32_t m = 0;
    for (i = 0; i < n; i++)
        if ((c[i] & 3) == s) m |= 1u << (c[i] >> 2);
    return T_flush[m];
}

int eval7(const Card c[7])
{
    uint32_t key = T_qw[c[0]] + T_qw[c[1]] + T_qw[c[2]] + T_qw[c[3]]
                 + T_qw[c[4]] + T_qw[c[5]] + T_qw[c[6]];
    uint32_t sk = (1u << ((c[0] & 3) << 2)) + (1u << ((c[1] & 3) << 2))
                + (1u << ((c[2] & 3) << 2)) + (1u << ((c[3] & 3) << 2))
                + (1u << ((c[4] & 3) << 2)) + (1u << ((c[5] & 3) << 2))
                + (1u << ((c[6] & 3) << 2));
    uint32_t five = SUIT_FIVE(sk);
    if (five) return flush_rank(c, 7, five);
    return T_noflush7[T_base7[key >> Q_LO_BITS] + T_lo_rank[key & ((1u << Q_LO_BITS) - 1)]];
}

int eval6(const Card c[6])
{
    uint32_t key = T_qw[c[0]] + T_qw[c[1]] + T_qw[c[2]] + T_qw[c[3]] + T_qw[c[4]] + T_qw[c[5]];
    uint32_t sk = (1u << ((c[0] & 3) << 2)) + (1u << ((c[1] & 3) << 2))
                + (1u << ((c[2] & 3) << 2)) + (1u << ((c[3] & 3) << 2))
                + (1u << ((c[4] & 3) << 2)) + (1u << ((c[5] & 3) << 2));
    uint32_t five = SUIT_FIVE(sk);
    if (five) return flush_rank(c, 6, five);
    return T_noflush6[T_base6[key >> Q_LO_BITS] + T_lo_rank[key & ((1u << Q_LO_BITS) - 1)]];
}

/* CK suit bit (1, 2, 4, 8 in bits 12..15) -> its 4-bit counter field, so
   suit bit 1 << j counts in field j. */
static const uint16_t CK_SUIT_FIELD[16] = {
    0, 0x0001, 0x0010, 0, 0x0100, 0, 0, 0, 0x1000, 0, 0, 0, 0, 0, 0, 0
};

#define CK_QW(x) T_qw[((x) >> 6) & 0x3C]   /* rank*4: the weight of the rank's clubs card */
#define CK_SF(x) CK_SUIT_FIELD[((x) >> 12) & 0xF]

int eval7_ck(const uint32_t ck[7])
{
    uint32_t key = CK_QW(ck[0]) + CK_QW(ck[1]) + CK_QW(ck[2]) + CK_QW(ck[3])
                 + CK_QW(ck[4]) + CK_QW(ck[5]) + CK_QW(ck[6]);
    uint32_t sk = (uint32_t)CK_SF(ck[0]) + CK_SF(ck[1]) + CK_SF(ck[2]) + CK_SF(ck[3])
                + CK_SF(ck[4]) + CK_SF(ck[5]) + CK_SF(ck[6]);
    uint32_t five = SUIT_FIVE(sk);
    if (five) {
        uint32_t bit = 0x1000u << (__builtin_ctz(five) >> 2), m = 0;
        int i;
        for (i = 0; i < 7; i++)
            if (ck[i] & bit) m |= ck[i] >> 16;
        return T_flush[m];
    }
    return T_noflush7[T_base7[key >> Q_LO_BITS] + T_lo_rank[key & ((1u << Q_LO_BITS) - 1)]];
}

int eval7_combo(const Card c[7])
{
    uint32_t ck[7];
    int i;
    for (i = 0; i < 7; i++) ck[i] = T_ck[c[i]];
    return best_of_subsets(ck, 7);
}

int eval_best(const Card *c, int n, Card best[5])
{
    uint32_t ck[7];
    int i, bi = 0, br = 0x7FFF;
    const uint8_t (*k)[5];
    int nk;

    if (!c || n < 5 || n > 7) return 0;
    for (i = 0; i < n; i++) {
        if (!card_valid(c[i])) return 0;
        ck[i] = T_ck[c[i]];
    }
    if (n == 5) {
        if (best) memcpy(best, c, 5);
        return eval5_ck(ck[0], ck[1], ck[2], ck[3], ck[4]);
    }
    k = (n == 7) ? (const uint8_t (*)[5])K7 : (const uint8_t (*)[5])K6;
    nk = (n == 7) ? 21 : 6;
    for (i = 0; i < nk; i++) {
        int r = eval5_ck(ck[k[i][0]], ck[k[i][1]], ck[k[i][2]], ck[k[i][3]], ck[k[i][4]]);
        if (r < br) { br = r; bi = i; }
    }
    if (best)
        for (i = 0; i < 5; i++) best[i] = c[k[bi][i]];
    return br;
}

void eval_hash_stats(unsigned *slots, unsigned *keys, double *avg_probes, unsigned *max_probes)
{
    unsigned total = 0, worst = 0;
    int i;
    for (i = 0; i < N_PAIRED; i++) {
        uint32_t s = hash_slot(T_pkey[i], T_hmul);
        unsigned n = 1;
        while (T_hash[s].key != T_pkey[i]) { s = (s + 1) & H_MASK; n++; }
        total += n;
        if (n > worst) worst = n;
    }
    if (slots) *slots = H_SIZE;
    if (keys) *keys = N_PAIRED;
    if (avg_probes) *avg_probes = (double)total / N_PAIRED;
    if (max_probes) *max_probes = worst;
}

/* ---- categories and names ----------------------------------------------- */

int eval_category(int rank)
{
    if (rank < 1 || rank > EVAL_WORST_RANK) return 0;
    if (rank <= 10) return HC_STRAIGHT_FLUSH;
    if (rank <= 166) return HC_QUADS;
    if (rank <= 322) return HC_FULL_HOUSE;
    if (rank <= 1599) return HC_FLUSH;
    if (rank <= 1609) return HC_STRAIGHT;
    if (rank <= 2467) return HC_TRIPS;
    if (rank <= 3325) return HC_TWO_PAIR;
    if (rank <= 6185) return HC_PAIR;
    return HC_HIGH_CARD;
}

int eval_is_royal(int rank)
{
    return rank == 1;
}

const char *eval_category_name(int cat)
{
    static const char *const NAMES[10] = {
        "", "STRAIGHT FLUSH", "FOUR OF A KIND", "FULL HOUSE", "FLUSH", "STRAIGHT",
        "THREE OF A KIND", "TWO PAIR", "ONE PAIR", "HIGH CARD"
    };
    return (cat >= 1 && cat <= 9) ? NAMES[cat] : "";
}

const char *eval_rank_word(int rank, int plural)
{
    static const char *const ONE[13] = {
        "Two", "Three", "Four", "Five", "Six", "Seven", "Eight", "Nine", "Ten",
        "Jack", "Queen", "King", "Ace"
    };
    static const char *const MANY[13] = {
        "Twos", "Threes", "Fours", "Fives", "Sixes", "Sevens", "Eights", "Nines", "Tens",
        "Jacks", "Queens", "Kings", "Aces"
    };
    if (rank < 0 || rank > 12) return "";
    return plural ? MANY[rank] : ONE[rank];
}

int eval_rank_ranks(int rank, int out[5])
{
    uint32_t d;
    int i;
    if (rank < 1 || rank > EVAL_WORST_RANK) return -1;
    d = T_desc[rank];
    for (i = 0; i < 5; i++) out[i] = (int)((d >> (16 - 4 * i)) & 0xF);
    return 0;
}

const char *eval_describe(int rank, char *out, size_t cap)
{
    int r[5];
    if (!out || cap == 0) return out;
    out[0] = '\0';
    if (eval_rank_ranks(rank, r) != 0) return out;
    switch (eval_category(rank)) {
    case HC_STRAIGHT_FLUSH:
        if (rank == 1) snprintf(out, cap, "Royal Flush");
        else snprintf(out, cap, "Straight Flush, %s High", eval_rank_word(r[0], 0));
        break;
    case HC_QUADS:
        snprintf(out, cap, "Four %s", eval_rank_word(r[0], 1));
        break;
    case HC_FULL_HOUSE:
        snprintf(out, cap, "%s Full of %s", eval_rank_word(r[0], 1), eval_rank_word(r[3], 1));
        break;
    case HC_FLUSH:
        snprintf(out, cap, "Flush, %s High", eval_rank_word(r[0], 0));
        break;
    case HC_STRAIGHT:
        snprintf(out, cap, "Straight, %s High", eval_rank_word(r[0], 0));
        break;
    case HC_TRIPS:
        snprintf(out, cap, "Three %s", eval_rank_word(r[0], 1));
        break;
    case HC_TWO_PAIR:
        snprintf(out, cap, "%s and %s", eval_rank_word(r[0], 1), eval_rank_word(r[2], 1));
        break;
    case HC_PAIR:
        snprintf(out, cap, "Pair of %s", eval_rank_word(r[0], 1));
        break;
    default:
        snprintf(out, cap, "%s High", eval_rank_word(r[0], 0));
        break;
    }
    return out;
}
