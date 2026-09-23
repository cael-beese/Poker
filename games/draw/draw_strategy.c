/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* Suit-canonical classes and strategy tables. See draw_strategy.h.

   The class list is enumerated directly rather than by canonicalising all
   2.6M hands: choose four rank masks m0 >= m1 >= m2 >= m3 whose popcounts
   sum to five. Masks are grouped by popcount and sorted descending, so each
   slot's candidates are a binary search plus a scan and the work is
   proportional to the output. Every sorted 4-tuple comes out exactly once;
   sorting the keys gives the class numbering. */

#include "draw_strategy.h"
#include "draw_rules.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t g_keys[DRAW_CLASSES];
static uint8_t  g_weight[DRAW_CLASSES];
static int      g_nkeys;
static pthread_once_t g_once = PTHREAD_ONCE_INIT;

/* All 13-bit masks with at most five bits, grouped by popcount, each group
   in descending order. */
static uint16_t g_group[6][1287];
static int      g_ngroup[6];

static int cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

static int weight_of(const uint16_t m[4])
{
    /* 4! over the factorials of the runs of equal masks (m is sorted). */
    static const int FACT[5] = { 1, 1, 2, 6, 24 };
    int w = 24, i = 0;
    while (i < 4) {
        int j = i;
        while (j < 4 && m[j] == m[i]) j++;
        w /= FACT[j - i];
        i = j;
    }
    return w;
}

/* First index in popcount group p whose mask is <= limit. */
static int first_le(int p, unsigned limit)
{
    int lo = 0, hi = g_ngroup[p];
    while (lo < hi) {
        int mid = (lo + hi) >> 1;
        if (g_group[p][mid] > limit) lo = mid + 1; else hi = mid;
    }
    return lo;
}

static void enum_slots(uint16_t *m, int slot, unsigned limit, int left)
{
    int p, i;
    /* The last slot takes exactly the remaining bits. */
    for (p = slot == 3 ? left : 0; p <= left; p++) {
        for (i = first_le(p, limit); i < g_ngroup[p]; i++) {
            m[slot] = g_group[p][i];
            if (slot < 3) {
                enum_slots(m, slot + 1, m[slot], left - p);   /* next mask <= this one */
            } else {
                if (g_nkeys < DRAW_CLASSES)
                    g_keys[g_nkeys] = (uint64_t)m[0] << 39 | (uint64_t)m[1] << 26
                                    | (uint64_t)m[2] << 13 | m[3];
                g_nkeys++;      /* counted past the end so the check fails loudly */
            }
        }
    }
}

static void build(void)
{
    uint16_t m[4];
    int i;
    unsigned x;
    long total = 0;
    for (x = 8192; x-- > 0;) {
        int p = __builtin_popcount(x);
        if (p <= 5) g_group[p][g_ngroup[p]++] = (uint16_t)x;
    }
    g_nkeys = 0;
    enum_slots(m, 0, 0x1FFF, 5);
    if (g_nkeys != DRAW_CLASSES) {
        fprintf(stderr, "draw_classes_init: %d classes, expected %d\n", g_nkeys, DRAW_CLASSES);
        abort();
    }
    qsort(g_keys, DRAW_CLASSES, sizeof g_keys[0], cmp_u64);
    for (i = 0; i < DRAW_CLASSES; i++) {
        uint64_t k = g_keys[i];
        m[0] = (uint16_t)(k >> 39 & 0x1FFF);
        m[1] = (uint16_t)(k >> 26 & 0x1FFF);
        m[2] = (uint16_t)(k >> 13 & 0x1FFF);
        m[3] = (uint16_t)(k & 0x1FFF);
        g_weight[i] = (uint8_t)weight_of(m);
        total += g_weight[i];
    }
    if (total != DRAW_HANDS) {
        fprintf(stderr, "draw_classes_init: weights sum to %ld\n", total);
        abort();
    }
}

void draw_classes_init(void)
{
    pthread_once(&g_once, build);
}

uint64_t draw_class_key(int idx)
{
    draw_classes_init();
    return (idx >= 0 && idx < DRAW_CLASSES) ? g_keys[idx] : 0;
}

int draw_class_weight(int idx)
{
    draw_classes_init();
    return (idx >= 0 && idx < DRAW_CLASSES) ? g_weight[idx] : 0;
}

void draw_class_hand(int idx, Card out[5])
{
    uint64_t k = draw_class_key(idx);
    int slot, r, n = 0;
    for (slot = 0; slot < 4; slot++) {
        unsigned m = (unsigned)(k >> (39 - 13 * slot)) & 0x1FFF;
        for (r = 0; r < 13; r++)
            if (m >> r & 1 && n < 5) out[n++] = card_make(r, slot);
    }
    while (n < 5) out[n++] = CARD_NONE;
}

int draw_class_of(const Card hand[5], uint8_t pos[5])
{
    unsigned mk[4] = { 0, 0, 0, 0 };
    int order[4] = { 0, 1, 2, 3 }, slot_of[4], i, j, lo, hi;
    uint64_t key, seen = 0;
    draw_classes_init();
    for (i = 0; i < 5; i++) {
        if (!card_valid(hand[i]) || (seen >> hand[i] & 1)) return -1;
        seen |= 1ull << hand[i];
        mk[card_suit(hand[i])] |= 1u << card_rank(hand[i]);
    }
    /* Insertion sort of the suits by mask, descending; ties keep suit order. */
    for (i = 1; i < 4; i++) {
        int s = order[i];
        for (j = i; j > 0 && mk[order[j - 1]] < mk[s]; j--) order[j] = order[j - 1];
        order[j] = s;
    }
    for (i = 0; i < 4; i++) slot_of[order[i]] = i;
    key = (uint64_t)mk[order[0]] << 39 | (uint64_t)mk[order[1]] << 26
        | (uint64_t)mk[order[2]] << 13 | mk[order[3]];
    lo = 0; hi = DRAW_CLASSES - 1;
    while (lo < hi) {
        int mid = (lo + hi) >> 1;
        if (g_keys[mid] < key) lo = mid + 1; else hi = mid;
    }
    if (g_keys[lo] != key) return -1;
    if (pos) {
        /* Canonical position = how many cards sort before it by (slot, rank). */
        int sk[5];
        for (i = 0; i < 5; i++) sk[i] = slot_of[card_suit(hand[i])] * 13 + card_rank(hand[i]);
        for (i = 0; i < 5; i++) {
            int p = 0;
            for (j = 0; j < 5; j++) p += sk[j] < sk[i];
            pos[i] = (uint8_t)p;
        }
    }
    return lo;
}

uint8_t draw_hold_from_canonical(uint8_t canon_mask, const uint8_t pos[5])
{
    uint8_t m = 0;
    int i;
    for (i = 0; i < 5; i++)
        if (canon_mask >> pos[i] & 1) m |= (uint8_t)(1u << i);
    return m;
}

uint8_t draw_strategy_hold(const DrawStrategyTable *t, int bet, const Card hand[5])
{
    uint8_t pos[5];
    int idx = draw_class_of(hand, pos);
    if (idx < 0) return 0;
    return draw_hold_from_canonical(t->hold[draw_strategy_sect(bet)][idx], pos);
}

const char *draw_strategy_file(int variant)
{
    static const char *const NAMES[DRAW_VARIANTS] = { "job.bin", "bonus.bin", "deuces.bin" };
    return (variant >= 0 && variant < DRAW_VARIANTS) ? NAMES[variant] : "";
}

/* ---- file I/O ---------------------------------------------------------- */

static const char MAGIC[8] = { 'B', 'P', 'L', 'S', 'T', 'R', 'A', 'T' };

static uint32_t fnv1a(const uint8_t *p, size_t n)
{
    uint32_t h = 2166136261u;
    size_t i;
    for (i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}

static void put_u32(uint8_t *b, uint32_t v)
{
    int i;
    for (i = 0; i < 4; i++) b[i] = (uint8_t)(v >> (8 * i));
}

static void put_u64(uint8_t *b, uint64_t v)
{
    int i;
    for (i = 0; i < 8; i++) b[i] = (uint8_t)(v >> (8 * i));
}

static uint32_t get_u32(const uint8_t *b)
{
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}

static uint64_t get_u64(const uint8_t *b)
{
    return (uint64_t)get_u32(b) | (uint64_t)get_u32(b + 4) << 32;
}

int draw_strategy_save(const DrawStrategyTable *t, const char *path)
{
    uint8_t head[24], sect[20];
    FILE *f = fopen(path, "wb");
    int s, ok = 1;
    if (!f) return -1;
    memcpy(head, MAGIC, 8);
    put_u32(head + 8, 1);
    put_u32(head + 12, (uint32_t)t->variant);
    put_u32(head + 16, DRAW_CLASSES);
    put_u32(head + 20, DRAW_SECTS);
    ok &= fwrite(head, 1, sizeof head, f) == sizeof head;
    for (s = 0; s < DRAW_SECTS; s++) {
        put_u64(sect, (uint64_t)t->ret_num[s]);
        put_u64(sect + 8, (uint64_t)t->ret_den[s]);
        put_u32(sect + 16, fnv1a(t->hold[s], DRAW_CLASSES));
        ok &= fwrite(sect, 1, sizeof sect, f) == sizeof sect;
        ok &= fwrite(t->hold[s], 1, DRAW_CLASSES, f) == DRAW_CLASSES;
    }
    ok &= fflush(f) == 0;
    ok &= fclose(f) == 0;
    return ok ? 0 : -1;
}

int draw_strategy_load(DrawStrategyTable *t, int variant, const char *path)
{
    uint8_t head[24], sect[20];
    FILE *f = fopen(path, "rb");
    int s, ok = 1;
    if (!f) return -1;
    ok &= fread(head, 1, sizeof head, f) == sizeof head;
    ok = ok && memcmp(head, MAGIC, 8) == 0 && get_u32(head + 8) == 1
            && get_u32(head + 12) == (uint32_t)variant && get_u32(head + 16) == DRAW_CLASSES
            && get_u32(head + 20) == DRAW_SECTS;
    t->variant = variant;
    for (s = 0; ok && s < DRAW_SECTS; s++) {
        ok &= fread(sect, 1, sizeof sect, f) == sizeof sect;
        ok = ok && fread(t->hold[s], 1, DRAW_CLASSES, f) == DRAW_CLASSES;
        if (!ok) break;
        t->ret_num[s] = (int64_t)get_u64(sect);
        t->ret_den[s] = (int64_t)get_u64(sect + 8);
        ok &= fnv1a(t->hold[s], DRAW_CLASSES) == get_u32(sect + 16);
    }
    fclose(f);
    return ok ? 0 : -1;
}
