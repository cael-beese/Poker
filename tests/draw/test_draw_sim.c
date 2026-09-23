/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* SPEC section 7, paytable return test: simulate hands of 9/6 Jacks or
   Better at 5 coins with optimal holds and check the return converges to
   the exact figure (99.5439043695 %).

   The hands are played through the real game: draw_init / draw_tick with a
   Wallet, the deck shuffled by the game's own Rng, holds pressed as HOLD
   buttons taken from the strategy table, credits debited at the deal and
   paid at collect (double-up off, all animation delays 0, so a hand is two
   ticks). The return is (credits paid) / (credits bet); its standard error
   comes from the sample variance of each hand's pay.

   Default: 10,000,000 JoB hands plus 2,000,000 each of Bonus Poker and
   Deuces Wild. BPL_DRAW_SIM_HANDS=N sets the JoB count (others N/5). */

#include "games/draw/draw_game.h"
#include "games/draw/draw_strategy.h"
#include "test_util.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#ifndef BPL_SOURCE_DIR
#define BPL_SOURCE_DIR "."
#endif

static DrawStrategyTable g_tab;

static void sim(int variant, long hands, uint64_t seed)
{
    char path[1024];
    DrawConfig cfg;
    DrawGame g;
    Wallet w;
    EventQueue q;
    InputFrame in;
    double sum = 0, sumsq = 0, t0 = test_now(), exact, ret, se, z;
    int64_t start = 1000000000000LL, paid_total = 0;
    long h, bad_state = 0;

    snprintf(path, sizeof path, "%s/assets/strategy/%s", BPL_SOURCE_DIR, draw_strategy_file(variant));
    if (draw_strategy_load(&g_tab, variant, path) != 0) {
        fprintf(stderr, "cannot load %s\n", path);
        CHECK(0);
        return;
    }
    exact = (double)g_tab.ret_num[DRAW_SECT_MAX] / (double)g_tab.ret_den[DRAW_SECT_MAX];

    draw_config_default(&cfg);
    cfg.variant = variant;
    cfg.bet = 5;
    cfg.double_up = 0;
    cfg.deal_gap_ticks = cfg.flip_delay_ticks = cfg.result_delay_ticks = 0;
    draw_init(&g, &cfg, seed);
    w.credits = start;
    w.denom = 1;
    memset(&in, 0, sizeof in);

    for (h = 0; h < hands; h++) {
        uint8_t hold;
        q.n = 0;
        in.pressed = BTN_DEAL;
        draw_tick(&g, &in, &w, &q);
        if (g.state != DS_HOLD) { bad_state++; break; }
        hold = draw_strategy_hold(&g_tab, 5, g.cards);
        q.n = 0;
        in.pressed = (uint32_t)hold | BTN_DEAL;         /* HOLD1..5 are bits 0..4 */
        draw_tick(&g, &in, &w, &q);
        if (g.state != DS_IDLE || g.held != hold) { bad_state++; break; }
        sum += g.paid;
        sumsq += (double)g.paid * g.paid;
        paid_total += g.paid;
    }
    CHECK_EQ_INT(bad_state, 0);
    CHECK_EQ_INT(w.credits, start - 5LL * hands + paid_total);
    CHECK_EQ_INT((int64_t)g.hand_no, hands);

    ret = sum / (5.0 * hands);
    {
        double mean = sum / hands, var = sumsq / hands - mean * mean;
        se = sqrt(var / hands) / 5.0;
    }
    z = (ret - exact) / se;
    printf("%s: %ld hands at 5 coins through draw_tick, return %.4f %% (exact %.4f %%), "
           "SE %.4f %%, z = %+.2f, %.1f s\n", draw_variant_name(variant), hands, 100 * ret,
           100 * exact, 100 * se, z, test_now() - t0);
    /* 4.5 standard errors: a false failure about once in 150,000 runs. */
    CHECK(fabs(z) < 4.5);
}

int main(void)
{
    const char *e = getenv("BPL_DRAW_SIM_HANDS");
    long n = (e && *e) ? atol(e) : 10000000L;
    draw_rules_init();
    draw_classes_init();
    sim(DRAW_JOB, n, 0x9E3779B97F4A7C15ull);
    sim(DRAW_BONUS, n / 5, 0x0123456789ABCDEFull);
    sim(DRAW_DEUCES, n / 5, 0xFEDCBA9876543210ull);
    return test_finish("draw_sim_return");
}
