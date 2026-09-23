/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
#include "games/holdem/holdem_bots.h"

#include <string.h>

static void random_legal(const AiView *v, Rng *r, AiDecision *out)
{
    int me = v->me, s;
    int64_t max_to = v->bet[me] + v->stack[me], top = 0;
    uint32_t roll = rng_below(r, 100);

    for (s = 0; s < HOLDEM_SEATS; s++)
        if (v->bet[s] > top) top = v->bet[s];
    out->think_ticks = HOLDEM_AI_MIN_THINK + (int)rng_below(r, 3) * (int)rng_below(r, 20);
    out->tell = (float)rng_unit(r);
    /* Mostly small bets, and big bets mostly folded to, so games last long
       enough to see every street, side pots and the later blind levels. */
    if (v->to_call > 0 && v->to_call * 3 > v->stack[me] && roll < 55) {
        out->action = ACT_FOLD;
    } else if (v->to_call > 0 && roll < 20) {
        out->action = ACT_FOLD;
    } else if (roll < 80 || v->min_raise <= 0) {
        out->action = v->to_call > 0 ? ACT_CALL : ACT_CHECK;
    } else if (roll < 98) {
        double u = rng_unit(r);
        int64_t span = max_to - v->min_raise;
        out->action = top == 0 ? ACT_BET : ACT_RAISE;
        out->amount = v->min_raise + (int64_t)((double)span * u * u * u * u * 0.3);
    } else {
        out->action = ACT_ALLIN;
        out->amount = max_to;
    }
}

void holdem_bot_decide(int kind, const AiView *v, uint64_t seed, AiDecision *out)
{
    Rng r;
    memset(out, 0, sizeof *out);
    rng_seed(&r, seed);
    switch (kind) {
    case HOLDEM_BOT_RANDOM:
        random_legal(v, &r, out);
        break;
    case HOLDEM_BOT_CHAOS:
        if (rng_below(&r, 100) < 35) {
            int64_t max_to = v->bet[v->me] + v->stack[v->me];
            out->action = (int)rng_below(&r, 10);            /* 6..9 are not actions */
            out->amount = (int64_t)rng_below(&r, (uint32_t)(2 * max_to + 200)) - 100;
            out->think_ticks = (int)rng_below(&r, 400) - 50;
            out->tell = (float)(rng_unit(&r) * 1e6) - 5e5f;
        } else {
            random_legal(v, &r, out);
        }
        break;
    default:
        out->action = v->to_call > 0 ? ACT_CALL : ACT_CHECK;
        out->think_ticks = HOLDEM_AI_MIN_THINK;
        break;
    }
}

static void bots_begin(void *ctx, int seat, const AiView *v, uint64_t seed)
{
    HoldemBots *b = ctx;
    if (seat < 0 || seat >= HOLDEM_SEATS) { b->protocol_errors++; return; }
    if (b->has_pending[seat]) b->protocol_errors++;
    holdem_bot_decide(b->kind[seat], v, seed, &b->pending[seat]);
    b->has_pending[seat] = 1;
    b->begins++;
}

static void bots_collect(void *ctx, int seat, AiDecision *out)
{
    HoldemBots *b = ctx;
    memset(out, 0, sizeof *out);
    if (seat < 0 || seat >= HOLDEM_SEATS || !b->has_pending[seat]) {
        b->protocol_errors++;
        out->action = ACT_FOLD;
        return;
    }
    *out = b->pending[seat];
    b->has_pending[seat] = 0;
    b->collects++;
}

void holdem_bots_init(HoldemBots *b, int kind)
{
    int s;
    memset(b, 0, sizeof *b);
    for (s = 0; s < HOLDEM_SEATS; s++) b->kind[s] = kind;
}

HoldemAiHooks holdem_bots_hooks(HoldemBots *b)
{
    HoldemAiHooks h;
    h.ctx = b;
    h.begin = bots_begin;
    h.collect = bots_collect;
    return h;
}
