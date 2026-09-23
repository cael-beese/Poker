/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* Exact hold EVs. See draw_hint.h for the method; the details:

   For a hold, let h[r] be the held count of rank r and n[r] the number of
   rank-r cards in the 47-card stub (the cards not dealt; discards cannot
   come back). A replacement set is described by its rank multiset m[r]
   (sum k); it can be realised in prod C(n[r], m[r]) ways, and every
   realisation has the same final rank counts h + m. Whether a realisation is
   a flush depends on suits, so those are counted apart:

   - A flush needs every card (every natural card, in Deuces Wild) in one
     suit s. That is possible only if the held naturals are all suit s (any
     suit if none are held) and each drawn natural rank is drawn once, from
     suit s, which the stub must still hold. So for each multiset we keep a
     4-bit set of suits still possible, narrowed rank by rank; the number of
     flush realisations is popcount(that set) times C(n[deuce], m[deuce]) in
     Deuces Wild (drawn deuces may be any suit) or times 1 otherwise. The
     suits give disjoint sets of hands because every final hand holds at
     least one natural card (there are only four deuces).
   - The other realisations are the non-flush category of the counts.

   A drawn rank that pairs a held card (or is drawn twice) cannot be in a
   flush, and the suit set handles that by itself: the held card's suit is
   not in the stub for that rank.                                           */

#include "draw_hint.h"
#include "draw_internal.h"

#include <string.h>

static const int32_t C47[6] = { 1, 47, 1081, 16215, 178365, 1533939 };
static const uint8_t BINOM[5][5] = {
    { 1, 0, 0, 0, 0 }, { 1, 1, 0, 0, 0 }, { 1, 2, 1, 0, 0 }, { 1, 3, 3, 1, 0 }, { 1, 4, 6, 4, 1 }
};

typedef struct {
    int       variant, deuces;
    uint8_t   n[13];          /* stub cards per rank                     */
    uint8_t   avail[13];      /* suits of rank r still in the stub       */
    uint8_t   suffix[14];     /* stub cards of ranks r..12               */
    uint8_t   c[13];          /* final counts, built up by the walk      */
    unsigned  mask;           /* natural ranks with c > 0                */
    uint32_t *ways;
} Walk;

static void leaf(Walk *e, uint32_t w, unsigned sm, uint32_t dfac)
{
    uint32_t f = sm ? (uint32_t)__builtin_popcount(sm) * dfac : 0;
    if (e->deuces) {
        e->ways[draw_dw_from_counts(e->c, e->mask, 0)] += w - f;
        if (f) e->ways[draw_dw_from_counts(e->c, e->mask, 1)] += f;
    } else {
        e->ways[draw_jb_from_counts(e->variant, e->c, e->mask, 0)] += w - f;
        if (f) e->ways[draw_jb_from_counts(e->variant, e->c, e->mask, 1)] += f;
    }
}

static void walk(Walk *e, int r, int left, uint32_t w, unsigned sm, uint32_t dfac)
{
    int nat, top, m;
    unsigned saved;
    if (left == 0) { leaf(e, w, sm, dfac); return; }
    if (e->suffix[r] < left) return;       /* also stops at r == 13 */
    nat = !(e->deuces && r == RANK_2);
    top = e->n[r] < left ? e->n[r] : left;
    saved = e->mask;
    for (m = 0; m <= top; m++) {
        uint32_t b = BINOM[e->n[r]][m];
        unsigned sm2 = sm;
        uint32_t d2 = dfac;
        if (nat) {
            if (m == 1) sm2 &= e->avail[r];
            else if (m > 1) sm2 = 0;
            if (m) e->mask = saved | (1u << r);
        } else {
            d2 = b;
        }
        e->c[r] = (uint8_t)(e->c[r] + m);
        walk(e, r + 1, left - m, w * b, sm2, d2);
        e->c[r] = (uint8_t)(e->c[r] - m);
    }
    e->mask = saved;
}

static int hand_ok(const Card hand[5])
{
    uint64_t seen = 0;
    int i;
    for (i = 0; i < 5; i++) {
        if (!card_valid(hand[i]) || (seen >> hand[i] & 1)) return 0;
        seen |= 1ull << hand[i];
    }
    return 1;
}

static void setup(Walk *e, int variant, const Card hand[5])
{
    int i, r;
    memset(e, 0, sizeof *e);
    e->variant = variant;
    e->deuces = variant == DRAW_DEUCES;
    for (r = 0; r < 13; r++) { e->n[r] = 4; e->avail[r] = 0xF; }
    for (i = 0; i < 5; i++) {
        r = card_rank(hand[i]);
        e->n[r]--;
        e->avail[r] &= (uint8_t)~(1u << card_suit(hand[i]));
    }
    e->suffix[13] = 0;
    for (r = 12; r >= 0; r--) e->suffix[r] = (uint8_t)(e->suffix[r + 1] + e->n[r]);
}

static int32_t hold_ways(Walk *e, const Card hand[5], unsigned mask, uint32_t *ways)
{
    unsigned suits = 0;
    int i, held = 0;
    memset(ways, 0, sizeof(uint32_t) * DC_COUNT);
    memset(e->c, 0, sizeof e->c);
    e->mask = 0;
    e->ways = ways;
    for (i = 0; i < 5; i++) {
        if (!(mask >> i & 1)) continue;
        int r = card_rank(hand[i]);
        held++;
        e->c[r]++;
        if (!(e->deuces && r == RANK_2)) {
            e->mask |= 1u << r;
            suits |= 1u << card_suit(hand[i]);
        }
    }
    /* Suits a flush could still be in: any, one, or none. */
    if (__builtin_popcount(suits) > 1) suits = 0;
    else if (suits == 0) suits = 0xF;
    walk(e, 0, 5 - held, 1, suits, 1);
    return C47[5 - held];
}

int32_t draw_hold_ways(int variant, const Card hand[5], unsigned mask, uint32_t ways[DC_COUNT])
{
    Walk e;
    if (variant < 0 || variant >= DRAW_VARIANTS || !hand_ok(hand) || mask > 31) return -1;
    draw_rules_init();
    setup(&e, variant, hand);
    return hold_ways(&e, hand, mask, ways);
}

int draw_hint_cmp(const DrawHint *h, unsigned a, unsigned b)
{
    /* num <= 1,533,939 * 4000 and den <= 1,533,939: products < 2^63. */
    int64_t l = h->num[a] * (int64_t)h->den[b], r = h->num[b] * (int64_t)h->den[a];
    return (l > r) - (l < r);
}

static void finish(DrawHint *out, int variant, int bet)
{
    int pay[DC_COUNT], m, c;
    draw_pay_vector(variant, bet, pay);
    out->variant = variant;
    out->bet = bet;
    out->best = 0;
    for (m = 0; m < 32; m++) {
        int64_t s = 0;
        for (c = 1; c < DC_COUNT; c++) s += (int64_t)out->ways[m][c] * pay[c];
        out->num[m] = s * bet;
        out->den[m] = C47[5 - __builtin_popcount((unsigned)m)];
        out->ev[m] = (double)out->num[m] / (double)out->den[m];
    }
    for (m = 1; m < 32; m++)
        if (draw_hint_cmp(out, (unsigned)m, out->best) > 0) out->best = (uint8_t)m;
    out->best_ev = out->ev[out->best];
}

int draw_hint_compute(int variant, int bet, const Card hand[5], DrawHint *out)
{
    Walk e;
    unsigned m;
    if (variant < 0 || variant >= DRAW_VARIANTS || bet < 1 || bet > DRAW_MAX_BET || !hand_ok(hand))
        return -1;
    draw_rules_init();
    setup(&e, variant, hand);
    for (m = 0; m < 32; m++) hold_ways(&e, hand, m, out->ways[m]);
    finish(out, variant, bet);
    return 0;
}

int draw_hint_compute_bruteforce(int variant, int bet, const Card hand[5], DrawHint *out)
{
    Card stub[47];
    uint64_t dealt = 0;
    int i, ns = 0;
    unsigned m;
    if (variant < 0 || variant >= DRAW_VARIANTS || bet < 1 || bet > DRAW_MAX_BET || !hand_ok(hand))
        return -1;
    draw_rules_init();
    for (i = 0; i < 5; i++) dealt |= 1ull << hand[i];
    for (i = 0; i < 52; i++)
        if (!(dealt >> i & 1)) stub[ns++] = (Card)i;
    for (m = 0; m < 32; m++) {
        Card h[5];
        int held = 0, k, idx[5], j;
        uint32_t *ways = out->ways[m];
        memset(ways, 0, sizeof out->ways[m]);
        for (i = 0; i < 5; i++)
            if (m >> i & 1) h[held++] = hand[i];
        k = 5 - held;
        /* Every k-subset of the stub, in lexicographic order. */
        for (j = 0; j < k; j++) idx[j] = j;
        for (;;) {
            for (j = 0; j < k; j++) h[held + j] = stub[idx[j]];
            ways[draw_classify(variant, h)]++;
            j = k - 1;
            while (j >= 0 && idx[j] == ns - k + j) j--;
            if (j < 0) break;
            idx[j]++;
            for (j = j + 1; j < k; j++) idx[j] = idx[j - 1] + 1;
        }
    }
    finish(out, variant, bet);
    return 0;
}

/* ---- worker thread ----------------------------------------------------- */

void draw_hint_task_init(DrawHintTask *t)
{
    memset(t, 0, sizeof *t);
    atomic_init(&t->state, 0);
}

static void *task_main(void *arg)
{
    DrawHintTask *t = arg;
    t->rc = draw_hint_compute(t->variant, t->bet, t->hand, &t->result);
    atomic_store(&t->state, 2);
    return NULL;
}

int draw_hint_task_start(DrawHintTask *t, int variant, int bet, const Card hand[5])
{
    int s = atomic_load(&t->state);
    if (s == 1 || s == 2) pthread_join(t->thread, NULL);
    t->variant = variant;
    t->bet = bet;
    memcpy(t->hand, hand, sizeof t->hand);
    t->rc = -1;
    /* Make sure eval tables exist before the thread needs them. */
    draw_rules_init();
    atomic_store(&t->state, 1);
    if (pthread_create(&t->thread, NULL, task_main, t) != 0) {
        /* No thread available: compute here rather than fail. */
        t->rc = draw_hint_compute(variant, bet, hand, &t->result);
        atomic_store(&t->state, 3);
        return t->rc;
    }
    return 0;
}

int draw_hint_task_ready(DrawHintTask *t)
{
    int s = atomic_load(&t->state);
    return (s == 2 || s == 3) && t->rc == 0;
}

const DrawHint *draw_hint_task_wait(DrawHintTask *t)
{
    int s = atomic_load(&t->state);
    if (s == 0) return NULL;
    if (s == 1 || s == 2) {
        pthread_join(t->thread, NULL);
        atomic_store(&t->state, 3);
    }
    return t->rc == 0 ? &t->result : NULL;
}
