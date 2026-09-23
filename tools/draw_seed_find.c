/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* draw_seed_find.c - find a session seed whose first Draw Poker hand is dealt
 * a given pay category, for reproducible screenshots of wins (a dealt royal
 * flush for the jackpot takeover, a full house for a medium win ...).
 *
 * Nothing is forced: it runs the real game (draw_init / draw_tick) from the
 * seed `beese-poker --seed S --mode draw` would give it - the session's seed
 * stream as platform/session.c derives it, whose first output seeds the Draw
 * game - and reports the first seed that happens to deal the category. The
 * run itself is then an ordinary run with --seed.
 *
 *   draw_seed_find <category> [variant] [start]
 *     category: royal, fourdeuces, sflush, quads, fullhouse, flush, straight,
 *               trips, twopair, jacks, anywin
 *     variant:  0 JoB (default), 1 Bonus, 2 Deuces
 * Build: tools/draw_shots.sh compiles it against the built libraries. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/rng.h"
#include "games/draw/draw_game.h"

/* platform/session.c: rng_seed(&S.seeds, session_seed ^ this). */
#define SESSION_SEED_XOR UINT64_C(0x5E551000B0B0CAFE)

static int want_cat(const char *s)
{
    static const struct { const char *n; int c; } k[] = {
        { "royal", DC_ROYAL_FLUSH }, { "fourdeuces", DC_FOUR_DEUCES }, { "sflush", DC_STRAIGHT_FLUSH },
        { "quads", DC_FOUR_KIND }, { "fullhouse", DC_FULL_HOUSE }, { "flush", DC_FLUSH },
        { "straight", DC_STRAIGHT }, { "trips", DC_THREE_KIND }, { "twopair", DC_TWO_PAIR },
        { "jacks", DC_JACKS_OR_BETTER }, { "anywin", -1 },
    };
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++)
        if (strcmp(s, k[i].n) == 0) return k[i].c;
    return -2;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: draw_seed_find <category> [variant] [start]\n");
        return 2;
    }
    int cat = want_cat(argv[1]);
    if (cat == -2) { fprintf(stderr, "unknown category %s\n", argv[1]); return 2; }
    int variant = argc > 2 ? atoi(argv[2]) : DRAW_JOB;
    uint64_t start = argc > 3 ? strtoull(argv[3], NULL, 0) : 1;
    DrawConfig cfg;
    draw_config_default(&cfg);
    cfg.variant = variant;
    InputFrame deal;
    memset(&deal, 0, sizeof deal);
    deal.pressed = BTN_DEAL;
    for (uint64_t s = start; s < start + 400000000ull; s++) {
        Rng seeds;
        rng_seed(&seeds, s ^ SESSION_SEED_XOR);
        DrawGame g;
        draw_init(&g, &cfg, rng_next(&seeds));
        Wallet w = { 1000, 25 };
        draw_tick(&g, &deal, &w, NULL);
        /* The five are dealt from the shuffled deck on the DEAL tick. */
        int c = draw_classify(variant, g.cards);
        if ((cat == -1 && c != DC_NONE) || c == cat) {
            printf("%llX\n", (unsigned long long)s);
            return 0;
        }
    }
    return 1;
}
