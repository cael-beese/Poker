/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* test_holdem_present.c - headless smoke test of the Hold'em presentation.
 *
 * Plays whole sit-and-go games at the table (bots in every seat, then a
 * scripted human in seat 0) and feeds every event, 1-4 ticks per "frame"
 * like the real loop, through holdem_present_update. No window, no GL: the
 * update side of the presentation must not need one. Checks:
 *   - nothing crashes, over many thousands of ticks and many games;
 *   - the presentation allocates nothing (malloc/calloc/realloc are wrapped
 *     and counted while the update runs on this thread);
 *   - the view never has to snap to the game state: every chip and card it
 *     shows was put there by an event, and its bet spots always agree with
 *     the table once the chips have landed (HoldemPresentStats.desyncs);
 *   - animations drain: after a game ends nothing is left in the air. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/eval.h"
#include "engine/rng.h"
#include "games/holdem/holdem.h"
#include "games/holdem/holdem_bots.h"
#include "platform/fx_settings.h"
#include "render/fx.h"
#include "render/holdem_present.h"
#include "render/particles.h"
#include "test_util.h"

/* ---- allocation counting (the link wraps these) ---------------------------- */

void *__real_malloc(size_t n);
void *__real_calloc(size_t a, size_t b);
void *__real_realloc(void *p, size_t n);
static _Thread_local int t_watch;
static long g_allocs;

void *__wrap_malloc(size_t n) { if (t_watch) g_allocs++; return __real_malloc(n); }
void *__wrap_calloc(size_t a, size_t b) { if (t_watch) g_allocs++; return __real_calloc(a, b); }
void *__wrap_realloc(void *p, size_t n) { if (t_watch) g_allocs++; return __real_realloc(p, n); }

/* ---- driving ------------------------------------------------------------------ */

static const char *const k_names[HOLDEM_SEATS] = { "YOU", "BUZZ (ROCK)", "HONEY (SHARK)", "STINGER (FISH)",
                                                   "DRONE (MANIAC)", "QUEENIE (FISH)" };

static EventQueue g_frame_ev;     /* the events of one frame (several ticks) */

static void view_info(HoldemViewInfo *v, const HoldemGame *g, int demo, uint32_t pressed)
{
    memset(v, 0, sizeof *v);
    v->game = g;
    v->phase = g->phase == HP_GAME_OVER ? HV_RESULT : HV_PLAYING;
    v->credits = 900;
    v->buyin = 100;
    v->prize[1] = 300;
    v->prize[2] = 180;
    v->prize[3] = 120;
    v->demo = demo;
    v->pressed = pressed;
    for (int i = 0; i < HOLDEM_SEATS; i++) v->seat_name[i] = k_names[i];
}

/* One game; human = 1 plays seat 0 from a scripted pattern. Returns ticks. */
static long play_game(uint64_t seed, int bot_kind, int fast, int human, Rng *r, long max_ticks)
{
    static HoldemGame g;
    static HoldemBots bots;
    HoldemConfig cfg;
    if (fast) holdem_config_fast(&cfg);
    else holdem_config_default(&cfg);
    cfg.seat0_ai = !human;
    cfg.human_turn_ticks = human ? 90 : 0;
    holdem_bots_init(&bots, bot_kind);
    HoldemAiHooks hooks = holdem_bots_hooks(&bots);
    holdem_init(&g, &cfg, seed, &hooks);

    long ticks = 0;
    while (g.phase != HP_GAME_OVER && ticks < max_ticks) {
        int n = 1 + (int)rng_below(r, 4);
        uint32_t pressed_frame = 0;
        g_frame_ev.n = 0;
        for (int k = 0; k < n && g.phase != HP_GAME_OVER; k++) {
            InputFrame in;
            memset(&in, 0, sizeof in);
            if (human) {
                uint32_t roll = rng_below(r, 100);
                if (roll < 4) in.pressed = BTN_HOLD2;
                else if (roll < 5) in.pressed = BTN_HOLD1;
                else if (roll < 6) in.pressed = BTN_HOLD3 | BTN_DEAL;
                else if (roll < 7) in.pressed = BTN_BET_MAX;
                else if (roll < 8) in.pressed = BTN_DEAL;
                else if (roll < 9) in.pressed = BTN_BET_ONE;
            }
            in.down = in.pressed;
            pressed_frame |= in.pressed;
            EventQueue q;
            q.n = 0;
            holdem_tick(&g, &in, NULL, &q);
            for (int i = 0; i < q.n; i++) ev_push(&g_frame_ev, q.e[i].type, q.e[i].a, q.e[i].b, q.e[i].v);
            ticks++;
        }
        HoldemViewInfo v;
        view_info(&v, &g, !human && (seed & 1), pressed_frame);
        t_watch = 1;
        holdem_present_update(&v, g_frame_ev.e, g_frame_ev.n, (float)n / 60.0f);
        t_watch = 0;
    }
    /* Let the last animations and the celebration finish. */
    for (int f = 0; f < 60 * 12; f++) {
        HoldemViewInfo v;
        view_info(&v, &g, !human && (seed & 1), 0);
        t_watch = 1;
        holdem_present_update(&v, NULL, 0, 1.0f / 60.0f);
        t_watch = 0;
    }
    holdem_release(&g);
    return ticks;
}

int main(void)
{
    eval_init();
    fx_settings_defaults(&g_effects);
    fx_rng_seed(0x5EEDull);
    pfx_init();
    Rng r;
    rng_seed(&r, 0xC0FFEEull);

    long total = 0;
    int games = 0;
    /* Arcade pacing, bots in every seat (the attract demo's situation). */
    for (int i = 0; i < 2; i++, games++) total += play_game(0x1000 + (uint64_t)i, HOLDEM_BOT_RANDOM, 0, 0, &r, 400000);
    /* Fast pacing: many hands, every kind of pot, frames running behind. */
    for (int i = 0; i < 24; i++, games++)
        total += play_game(0x2000 + (uint64_t)i, i % 3 == 0 ? HOLDEM_BOT_CHAOS : HOLDEM_BOT_RANDOM, 1, 0, &r, 400000);
    /* A scripted human in seat 0 (action bar, prompts, busting out). */
    for (int i = 0; i < 12; i++, games++) total += play_game(0x3000 + (uint64_t)i, HOLDEM_BOT_RANDOM, i & 1, 1, &r, 400000);

    const HoldemPresentStats *st = holdem_present_stats();
    printf("holdem_present: %d games, %ld ticks, %ld hands, %ld events, %ld celebrations, peak %d chip flights\n",
           games, total, st->hands_seen, st->events_seen, st->celebrations, st->flights_peak);
    printf("holdem_present: %ld allocations during updates, %d desyncs, %d flights / %d cards left in the air\n",
           g_allocs, st->desyncs, st->flights_live, st->cards_live);
    CHECK(total > 100000);
    CHECK(st->hands_seen > 1000);
    CHECK(st->events_seen > 50000);
    CHECK(g_allocs == 0);
    CHECK(st->desyncs == 0);
    CHECK(st->flights_live == 0);
    return test_finish("holdem_present");
}
