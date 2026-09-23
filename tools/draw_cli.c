/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* Text-mode Draw Poker, for debugging the state machine without graphics.

       draw_cli [--seed N] [--variant job|bonus|deuces] [--credits N]
       draw_cli --bench N [--variant ...]     time the hint (fast and brute force)

   One command per line; each is one tick of button presses, after which
   empty ticks run until the game waits for input again. Every event is
   printed as it fires, with its tick.
       d  DEAL / DRAW          1-5  toggle holds (e.g. "134")
       b  BET ONE              m    BET MAX
       y  double up (HOLD1)    n    collect (HOLD5)
       r  red (LEFT)           k    black (RIGHT)
       u  variant up           h    hint: EV of every hold for the table cards
       a  hold what the hint says is best     q  quit                      */

#include "games/draw/draw_game.h"
#include "games/draw/draw_hint.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static int parse_variant(const char *s)
{
    if (!strcmp(s, "job")) return DRAW_JOB;
    if (!strcmp(s, "bonus")) return DRAW_BONUS;
    if (!strcmp(s, "deuces")) return DRAW_DEUCES;
    return -1;
}

static void random_hand(Rng *r, Card h[5])
{
    Deck d;
    int i;
    deck_init(&d);
    for (i = 0; i < 5; i++) h[i] = deck_draw_random(&d, r);
}

static int bench(int variant, int n)
{
    Rng r;
    Card h[5];
    DrawHint hint, bf;
    double t0, t_fast, t_brute;
    int i, nb = n < 50 ? n : 50, bad = 0;
    long long sink = 0;
    rng_seed(&r, 12345);
    draw_rules_init();
    t0 = now();
    for (i = 0; i < n; i++) {
        random_hand(&r, h);
        draw_hint_compute(variant, 5, h, &hint);
        sink += hint.best;
    }
    t_fast = (now() - t0) / n;
    rng_seed(&r, 12345);
    t0 = now();
    for (i = 0; i < nb; i++) {
        random_hand(&r, h);
        draw_hint_compute_bruteforce(variant, 5, h, &bf);
        draw_hint_compute(variant, 5, h, &hint);
        bad += memcmp(bf.ways, hint.ways, sizeof bf.ways) != 0;
    }
    t_brute = (now() - t0) / nb - t_fast;
    printf("%s: fast %.3f ms/hand over %d hands, brute force %.2f ms/hand over %d (%.0fx), "
           "%d mismatches (sink %lld)\n", draw_variant_name(variant), t_fast * 1e3, n,
           t_brute * 1e3, nb, t_brute / t_fast, bad, sink);
    return bad != 0;
}

static void print_table(const DrawGame *g, const Wallet *w)
{
    char s[3];
    int i;
    printf("[%s] %s bet %d credits %" PRId64 " meter %d  |", draw_state_name(g->state),
           draw_variant_name(g->variant), g->bet, w->credits, g->meter);
    for (i = 0; i < 5; i++)
        printf(" %s%s", g->cards[i] == CARD_NONE ? "--" : card_str(g->cards[i], s),
               (g->state == DS_HOLD && (g->held >> i & 1)) ? "*" : " ");
    printf("\n");
}

static void print_events(const EventQueue *q, uint64_t tick)
{
    char s[3];
    int i;
    for (i = 0; i < q->n; i++) {
        const GameEvent *e = &q->e[i];
        printf("  t=%-6" PRIu64 " %-13s a=%d b=%d v=%d", tick, draw_event_name(e->type), e->a, e->b, e->v);
        if (e->type == EV_DRAW_DEAL_CARD || e->type == EV_DRAW_FLIP_CARD || e->type == EV_DRAW_DRAW_CARD
            || e->type == EV_DRAW_DOUBLE_CARD)
            printf("  (%s)", card_str((Card)e->b, s));
        if (e->type == EV_DRAW_WIN || e->type == EV_DRAW_HOLD_PHASE || e->type == EV_DRAW_HAND_END)
            printf("  (%s)", draw_cat_name(e->a));
        printf("\n");
    }
}

static void show_hint(const DrawGame *g)
{
    DrawHint h;
    char s[3];
    int order[32], i, j;
    if (g->state != DS_HOLD || draw_hint_compute(g->variant, g->bet, g->cards, &h) != 0) {
        printf("  (hint needs a hand in the hold phase)\n");
        return;
    }
    for (i = 0; i < 32; i++) order[i] = i;
    for (i = 1; i < 32; i++)
        for (j = i; j > 0 && draw_hint_cmp(&h, (unsigned)order[j], (unsigned)order[j - 1]) > 0; j--) {
            int t = order[j]; order[j] = order[j - 1]; order[j - 1] = t;
        }
    for (i = 0; i < 8; i++) {
        int m = order[i], k;
        printf("  %2d. hold", i + 1);
        for (k = 0; k < 5; k++) printf(" %s", (m >> k & 1) ? card_str(g->cards[k], s) : "..");
        printf("   EV %9.5f credits = %" PRId64 "/%d\n", h.ev[m], h.num[m], h.den[m]);
    }
}

int main(int argc, char **argv)
{
    DrawConfig cfg;
    DrawGame g;
    Wallet w = { 1000, 1 };
    EventQueue q;
    uint64_t seed = 1, tick = 0;
    char line[256];
    int i, bench_n = 0;

    draw_config_default(&cfg);
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = strtoull(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--variant") && i + 1 < argc) {
            cfg.variant = parse_variant(argv[++i]);
            if (cfg.variant < 0) { fprintf(stderr, "unknown variant\n"); return 2; }
        }
        else if (!strcmp(argv[i], "--credits") && i + 1 < argc) w.credits = atoll(argv[++i]);
        else if (!strcmp(argv[i], "--bench") && i + 1 < argc) bench_n = atoi(argv[++i]);
        else { fprintf(stderr, "usage: see the comment at the top of tools/draw_cli.c\n"); return 2; }
    }
    if (bench_n > 0) {
        int v, rc = 0;
        for (v = 0; v < DRAW_VARIANTS; v++) rc |= bench(v, bench_n);
        return rc;
    }

    draw_init(&g, &cfg, seed);
    print_table(&g, &w);
    while (fgets(line, sizeof line, stdin)) {
        InputFrame in;
        char *p;
        memset(&in, 0, sizeof in);
        for (p = line; *p; p++) {
            switch (*p) {
            case 'd': in.pressed |= BTN_DEAL; break;
            case 'b': in.pressed |= BTN_BET_ONE; break;
            case 'm': in.pressed |= BTN_BET_MAX; break;
            case 'y': in.pressed |= BTN_HOLD1; break;
            case 'n': in.pressed |= BTN_HOLD5; break;
            case 'r': in.pressed |= BTN_LEFT; break;
            case 'k': in.pressed |= BTN_RIGHT; break;
            case 'u': in.pressed |= BTN_UP; break;
            case 'h': show_hint(&g); break;
            case 'a':
                if (g.state == DS_HOLD) {
                    DrawHint h;
                    draw_hint_compute(g.variant, g.bet, g.cards, &h);
                    in.pressed |= (uint32_t)(h.best ^ g.held);   /* toggle to the best mask */
                }
                break;
            case 'q': return 0;
            default:
                if (*p >= '1' && *p <= '5') in.pressed |= BTN_HOLD_N(*p - '1');
                break;
            }
        }
        in.down = in.pressed;
        /* The pressed tick, then idle ticks until the game waits for input. */
        do {
            ev_clear(&q);
            draw_tick(&g, &in, &w, &q);
            print_events(&q, tick);
            tick++;
            memset(&in, 0, sizeof in);
        } while (g.state == DS_DEALING || g.state == DS_DRAWING || g.state == DS_DOUBLE_REVEAL);
        print_table(&g, &w);
    }
    return 0;
}
