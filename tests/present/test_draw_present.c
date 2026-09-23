/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* test_draw_present.c - headless smoke test of the Draw Poker presentation.
 *
 * The one test that links render/: it drives draw_view_update (which makes
 * no GL calls, see render/draw_view.h) with the events of real play - the
 * game run by draw_tick, a scripted player following the strategy hint,
 * doubling up now and then, mashing buttons, 1-4 ticks per frame - and
 * checks, frame by frame:
 *   - the view never writes the game (a byte copy of DrawGame before the
 *     update equals it after);
 *   - no card face is shown before the game turned it (CONTRACT section 5:
 *     the presentation reveals nothing early);
 *   - nothing is allocated per frame (malloc / calloc / realloc are wrapped
 *     at link time and must not be called after the warm-up);
 *   - every win tier's celebration starts and can be skipped by a button,
 *     including a jackpot fed straight to the view, without touching the game;
 * with the effect toggles all on, then all off (the non-animated paths). */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/rng.h"
#include "games/draw/draw_game.h"
#include "games/draw/draw_hint.h"
#include "platform/app.h"
#include "platform/fx_settings.h"
#include "platform/gpustats.h"
#include "render/celebrate.h"
#include "render/draw_view.h"

/* ---- allocation counting ------------------------------------------------ */

void *__real_malloc(size_t n);
void *__real_calloc(size_t a, size_t b);
void *__real_realloc(void *p, size_t n);
static int g_count_allocs;
static long g_allocs;
void *__wrap_malloc(size_t n) { if (g_count_allocs) g_allocs++; return __real_malloc(n); }
void *__wrap_calloc(size_t a, size_t b) { if (g_count_allocs) g_allocs++; return __real_calloc(a, b); }
void *__wrap_realloc(void *p, size_t n) { if (g_count_allocs) g_allocs++; return __real_realloc(p, n); }

static int g_fail;
static void fail(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "FAIL: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    if (++g_fail > 20) exit(1);
}

/* ---- the scripted player -------------------------------------------------- */

typedef struct {
    DrawGame g;
    Wallet   w;
    DrawHint hint;
    int      hint_ok;
    Rng      r;             /* the player's choices: its own stream        */
    uint8_t  target;
    int      wait;
    long     wins[DT_JACKPOT + 1], doubles, frames, celebrations[WIN_JACKPOT + 1], skips;
} Play;

static uint32_t player_input(Play *p)
{
    DrawGame *g = &p->g;
    if (p->wait > 0) { p->wait--; return 0; }
    uint32_t pr = 0;
    /* Now and then a random button, as a player leaning on the panel. */
    if (rng_below(&p->r, 40) == 0) {
        static const uint32_t any[] = { BTN_HOLD1, BTN_HOLD2, BTN_HOLD3, BTN_HOLD4, BTN_HOLD5, BTN_BET_ONE, BTN_DEAL };
        pr = any[rng_below(&p->r, 7)];
        if (!(g->state == DS_IDLE && (pr & BTN_BET_ONE))) return pr;
    }
    switch (g->state) {
    case DS_IDLE:
        p->wait = (int)rng_below(&p->r, 30);
        return rng_below(&p->r, 4) == 0 ? BTN_BET_MAX : BTN_DEAL;
    case DS_HOLD: {
        uint8_t todo = (uint8_t)((p->target ^ g->held) & 0x1F);
        if (todo) {
            int i = 0;
            while (!(todo >> i & 1)) i++;
            p->wait = (int)rng_below(&p->r, 6);
            return BTN_HOLD1 << i;
        }
        p->wait = 4;
        return BTN_DEAL;
    }
    case DS_OFFER:
        p->wait = (int)rng_below(&p->r, 40);
        if (rng_below(&p->r, 2) == 0) { p->doubles++; return BTN_HOLD1; }
        return rng_below(&p->r, 2) ? BTN_HOLD5 : BTN_DEAL;
    case DS_DOUBLE:
        p->wait = (int)rng_below(&p->r, 20);
        return rng_below(&p->r, 2) ? BTN_HOLD1 : BTN_HOLD5;
    default:
        return 0;
    }
}

/* One rendered frame: 1-4 ticks, then the view's update with every event. */
static GameEvent g_frame_ev[1024];

static void frame(Play *p, int ticks)
{
    int n = 0;
    uint32_t pressed_all = 0;
    for (int t = 0; t < ticks; t++) {
        InputFrame in;
        memset(&in, 0, sizeof in);
        in.pressed = player_input(p);
        in.down = in.pressed;
        pressed_all |= in.pressed;
        EventQueue q;
        q.n = 0;
        for (int b = 0; b < 19; b++)
            if (in.pressed & (1u << b)) ev_push(&q, APP_EV_BUTTON, b, 0, 0);
        draw_tick(&p->g, &in, &p->w, &q);
        for (int i = 0; i < q.n; i++) {
            const GameEvent *e = &q.e[i];
            if (e->type == EV_DRAW_HAND_START) p->hint_ok = 0;
            if (e->type == EV_DRAW_HOLD_PHASE) {
                p->hint_ok = draw_hint_compute(p->g.variant, p->g.bet, p->g.cards, &p->hint) == 0;
                p->target = p->hint_ok ? p->hint.best : 0;
                p->wait = 10;
            }
            if (e->type == EV_DRAW_WIN && e->b >= 0 && e->b <= DT_JACKPOT) p->wins[e->b]++;
            if (n < 1024) g_frame_ev[n++] = *e;
        }
        if (p->w.credits < 100) p->w.credits = 100000;   /* the test's own play money */
    }
    (void)pressed_all;
    DrawViewInfo v;
    memset(&v, 0, sizeof v);
    v.game = &p->g;
    v.credits = p->w.credits;
    v.denom_cents = 25;
    v.hint_on = (p->frames / 700) & 1;
    v.hint = p->hint_ok ? &p->hint : NULL;
    static DrawGame before;
    memcpy(&before, &p->g, sizeof before);
    draw_view_update(&v, g_frame_ev, n, (float)ticks / 60.0f);
    if (memcmp(&before, &p->g, sizeof before) != 0) fail("frame %ld: the view changed the game", p->frames);
    DrawViewProbe pr;
    draw_view_probe(&pr);
    if (pr.slots_face_up & ~p->g.face_up)
        fail("frame %ld: a card is shown face up before the game turned it (view %02x game %02x state %s)",
             p->frames, pr.slots_face_up, p->g.face_up, draw_state_name(p->g.state));
    if (pr.celebrating >= 0 && pr.celebrating <= WIN_JACKPOT) p->celebrations[pr.celebrating]++;
    p->frames++;
}

static void play(const char *label, uint64_t seed, int variant, long frames)
{
    Play p;
    memset(&p, 0, sizeof p);
    DrawConfig cfg;
    draw_config_default(&cfg);
    cfg.variant = variant;
    draw_init(&p.g, &cfg, seed);
    p.w.credits = 100000;
    p.w.denom = 25;
    rng_seed(&p.r, seed * 7 + 1);
    draw_view_reset();
    /* Warm up (first use of every path), then count allocations. */
    for (int i = 0; i < 3000; i++) frame(&p, 1);
    g_allocs = 0;
    g_count_allocs = 1;
    for (long f = 0; f < frames; f++) {
        uint32_t k = rng_below(&p.r, 100);
        frame(&p, k < 85 ? 1 : k < 95 ? 2 : k < 99 ? 3 : 4);
    }
    g_count_allocs = 0;
    if (g_allocs) fail("%s: %ld allocations during %ld frames of play", label, g_allocs, frames);
    printf("%-22s %ld frames, %u hands, wins S/M/B/J %ld/%ld/%ld/%ld, doubles %ld, celebration frames %ld/%ld/%ld/%ld\n",
           label, frames, p.g.hand_no, p.wins[DT_SMALL], p.wins[DT_MEDIUM], p.wins[DT_BIG], p.wins[DT_JACKPOT],
           p.doubles, p.celebrations[WIN_SMALL], p.celebrations[WIN_MEDIUM], p.celebrations[WIN_BIG],
           p.celebrations[WIN_JACKPOT]);
    if (!p.celebrations[WIN_SMALL] || !p.celebrations[WIN_MEDIUM]) fail("%s: no small / medium celebration seen", label);
}

/* A jackpot straight to the view: the takeover runs, a button skips it, the
 * game is untouched throughout. */
static void jackpot(void)
{
    DrawGame g;
    DrawConfig cfg;
    draw_config_default(&cfg);
    draw_init(&g, &cfg, 99);
    Wallet w = { 1000, 25 };
    InputFrame in;
    memset(&in, 0, sizeof in);
    in.pressed = BTN_DEAL;
    EventQueue q;
    q.n = 0;
    draw_tick(&g, &in, &w, &q);
    draw_view_reset();
    DrawViewInfo v;
    memset(&v, 0, sizeof v);
    v.game = &g;
    v.credits = w.credits;
    draw_view_update(&v, q.e, q.n, 1.0f / 60);
    DrawGame before = g;
    GameEvent win = { EV_DRAW_WIN, DC_ROYAL_FLUSH, DT_JACKPOT, 4000 };
    g_allocs = 0;
    g_count_allocs = 1;
    draw_view_update(&v, &win, 1, 1.0f / 60);
    DrawViewProbe pr;
    int seen = 0;
    for (int f = 0; f < 120; f++) {
        draw_view_update(&v, NULL, 0, 1.0f / 60);
        draw_view_probe(&pr);
        /* With the takeover toggle off, a jackpot celebrates as a big win. */
        if (pr.celebrating == (g_effects.takeover ? WIN_JACKPOT : WIN_BIG)) seen++;
    }
    if (seen < 100) fail("jackpot celebration ran %d of 120 frames", seen);
    GameEvent btn = { APP_EV_BUTTON, 5, 0, 0 };   /* DEAL */
    draw_view_update(&v, &btn, 1, 1.0f / 60);
    int left = 0;
    for (int f = 0; f < 40; f++) {
        draw_view_update(&v, NULL, 0, 1.0f / 60);
        draw_view_probe(&pr);
        if (pr.celebrating) left = f + 1;
    }
    g_count_allocs = 0;
    if (left > 30) fail("skipped jackpot still running after %d frames", left);
    if (pr.win_shown != 4000) fail("skipped jackpot: WIN meter shows %lld, not 4000", pr.win_shown);
    if (memcmp(&before, &g, sizeof g) != 0) fail("jackpot: the view changed the game");
    if (g_allocs) fail("jackpot: %ld allocations", g_allocs);
    printf("jackpot takeover       %d frames shown, skipped in %d frames, WIN %lld\n", seen, left, pr.win_shown);
}

int main(void)
{
    (void)gpustats_peek();   /* pulls in the GL counting shims the DRM link wraps to */
    draw_rules_init();
    fx_settings_defaults(&g_effects);
    play("JoB, effects on", 0xD1CE, DRAW_JOB, 60000);
    play("Deuces, effects on", 0xBEE5, DRAW_DEUCES, 30000);
    jackpot();
    memset(&g_effects, 0, sizeof g_effects);
    play("Bonus, effects off", 0xC0FFEE, DRAW_BONUS, 30000);
    jackpot();
    if (g_fail) { printf("%d failure(s)\n", g_fail); return 1; }
    printf("draw presentation smoke test: OK\n");
    return 0;
}
