/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* sfx.c - every short sound effect, as a recipe over the voices.
 *
 * Each recipe renders into a buffer that is longer than it needs; main.c
 * then removes DC, trims silence, fades the edges and normalises the result
 * to its family's loudness target, so the recipes can think in relative
 * levels only. Times are in seconds unless a variable is a sample index.
 */
#include "sfx.h"
#include "voices.h"

#include <math.h>
#include <stdlib.h>

#define AT(s) secs(s)

/* A pitched blip that glides from f0 to f1, with a touch of FM for bite. */
static void chirp(Buf *b, long at, double f0, double f1, double glide, double tau,
                  double amp, double pan, double fm_ratio, double fm_index)
{
    long len = secs(tau * 7 + glide);
    double ph = 0, mph = 0;
    for (long i = 0; i < len; i++) {
        double x = (double)i / SR;
        double f = x < glide ? f0 * pow(f1 / f0, x / glide) : f1;
        double idx = fm_index * exp(-x / (tau * 0.6));
        mph += TAU * f * fm_ratio / SR;
        ph += TAU * f / SR;
        double e = smoothstep01(x / 0.001) * exp(-x / tau);
        buf_add(b, at + i, sin(ph + idx * sin(mph)) * e * amp, pan);
    }
}

static void thud(Buf *b, long at, double f, double tau, double amp, double pan)
{
    double fr[1] = { f }, a[1] = { 1 }, t[1] = { tau };
    v_modal(b, at, amp, pan, fr, a, t, 1);
}

/* ---------------------------------------------------------------- cards */

static void s_card_deal(Buf *b, Rng *r, int v)
{
    (void)v;
    long snap = AT(rng_range(r, 0.022, 0.030));
    /* the air as the card flies in, rising into the snap */
    v_sweep(b, 0, (double)snap / SR + 0.004, rng_range(r, 1500, 2200), rng_range(r, 3500, 4800),
            0.9, 0.30, 0, 0, 1, r);
    v_noise(b, snap, 1.0, 0, 0.0002, rng_range(r, 0.0018, 0.003), NF_HP, 1800, 0.7, r);
    v_noise(b, snap, 0.55, 0, 0.0003, rng_range(r, 0.008, 0.014), NF_BP, rng_range(r, 900, 1400), 2.5, r);
    thud(b, snap, rng_range(r, 150, 210), 0.018, 0.35, 0);
}

static void s_card_flip(Buf *b, Rng *r, int v)
{
    (void)v;
    v_noise(b, 0, 0.35, 0, 0.0002, 0.0012, NF_HP, 2500, 0.7, r);
    v_sweep(b, 0, 0.055, rng_range(r, 1800, 2300), rng_range(r, 3200, 4000), 0.9, 0.28, 0, 0, 0, r);
    long slap = AT(rng_range(r, 0.045, 0.056));
    v_noise(b, slap, 1.0, 0, 0.0002, rng_range(r, 0.002, 0.003), NF_HP, 1500, 0.7, r);
    v_noise(b, slap, 0.5, 0, 0.0003, 0.012, NF_BP, rng_range(r, 1000, 1400), 2.0, r);
    thud(b, slap, rng_range(r, 160, 200), 0.015, 0.3, 0);
}

static void s_card_slide(Buf *b, Rng *r, int v)
{
    (void)v;
    double dur = rng_range(r, 0.16, 0.22);
    long len = AT(dur);
    Svf s = { 0, 0 };
    Smooth grain;
    smooth_init(&grain, 90, 0.5);
    for (long i = 0; i < len; i++) {
        double x = (double)i / len;
        double g = smooth_step(&grain, rng_uni(r));
        double bp;
        svf_step(&s, rng_bi(r), 1800 + 800 * x, 0.8, NULL, &bp, NULL);
        double e = pow(sin(PI * x), 0.7) * (0.55 + 0.9 * g);
        buf_add(b, i, bp * e * 0.45, 0);
    }
    long tap = len - AT(0.02);
    v_noise(b, tap, 0.45, 0, 0.0002, 0.002, NF_HP, 2000, 0.7, r);
    thud(b, tap, 180, 0.012, 0.25, 0);
}

static void s_card_shuffle(Buf *b, Rng *r, int v)
{
    (void)v;
    /* The riffle: 52 flicks that accelerate as the halves interleave. */
    double t = 0.0, iv = 0.026;
    for (int k = 0; k < 52 && t < 0.78; k++) {
        double env = smoothstep01(t / 0.12) * (0.75 + 0.25 * rng_uni(r));
        v_noise(b, AT(t), rng_range(r, 0.45, 0.9) * env, 0, 0.0002,
                rng_range(r, 0.0015, 0.003), NF_BP, rng_range(r, 2500, 5500), 1.5, r);
        t += iv * rng_range(r, 0.8, 1.2);
        iv = iv * 0.955 < 0.011 ? 0.011 : iv * 0.955;
    }
    v_sweep(b, 0, 0.8, 3000, 4200, 0.8, 0.1, 0, 0, 0, r);
    /* the bridge: the cards cascade back together */
    v_sweep(b, AT(0.78), 0.34, 2600, 1400, 0.8, 0.45, 0, 0, 0, r);
    for (double u = 0.78; u < 1.1; u += rng_range(r, 0.004, 0.008))
        v_noise(b, AT(u), 0.22 * sin(PI * (u - 0.78) / 0.32), 0, 0.0002, 0.0015, NF_BP,
                rng_range(r, 2000, 4500), 1.5, r);
    /* squaring the deck on the felt */
    for (int k = 0; k < 2; k++) {
        long at = AT(1.16 + 0.09 * k);
        v_noise(b, at, 0.45 - 0.12 * k, 0, 0.0002, 0.002, NF_HP, 1600, 0.7, r);
        thud(b, at, 150 + 20 * k, 0.02, 0.45 - 0.1 * k, 0);
    }
}

static void s_fold(Buf *b, Rng *r, int v)
{
    (void)v;
    double sw = rng_range(r, 0.13, 0.17);
    v_sweep(b, 0, sw, rng_range(r, 2800, 3500), rng_range(r, 1200, 1600), 0.8, 0.55, 0, 0, 0, r);
    long slap = AT(sw - 0.02);
    v_noise(b, slap, 0.5, 0, 0.0002, 0.0025, NF_HP, 1200, 0.7, r);
    v_noise(b, slap, 0.35, 0, 0.0003, 0.01, NF_BP, 900, 1.5, r);
    thud(b, slap, 150, 0.02, 0.28, 0);
    Biquad lp;
    bq_lp(&lp, 5000, 0.7);
    bq_apply(lp, b->l, b->n);
}

/* ------------------------------------------------------------ table/ui */

static void s_check(Buf *b, Rng *r, int v)
{
    (void)v;
    for (int k = 0; k < 2; k++) {
        long at = AT(0.125 * k);
        double g = k ? 0.8 : 1.0, p = k ? 0.95 : 1.0;
        double f[3] = { 160 * p, 470 * p, 1100 * p }, a[3] = { 1, 0.5, 0.25 }, t[3] = { 0.03, 0.02, 0.01 };
        v_modal(b, at, g, 0, f, a, t, 3);
        v_noise(b, at, 0.4 * g, 0, 0.0005, 0.012, NF_BP, 700, 1.5, r);
        v_noise(b, at, 0.3 * g, 0, 0.0002, 0.001, NF_LP, 2000, 0.7, r);
    }
}

static void s_button(Buf *b, Rng *r, int v)
{
    static const double BODY[3] = { 2300, 2750, 1950 };
    double body = BODY[v % 3] * rng_range(r, 0.97, 1.03);
    v_noise(b, 0, 0.8, 0, 0.0001, 0.0006, NF_HP, 3500, 0.7, r);
    double f[4] = { body, body * 2.1, 700 + 90 * v, 230 + 15 * v };
    double a[4] = { 0.6, 0.3, 0.5, 0.45 };
    double t[4] = { 0.007, 0.004, 0.012, 0.014 };
    v_modal(b, 0, 1.0, 0, f, a, t, 4);
}

static void s_hold_on(Buf *b, Rng *r, int v)
{
    (void)v;
    chirp(b, 0, 1100, 1650, 0.025, 0.06, 0.8, 0, 2.0, 0.8);
    v_bell(b, AT(0.012), 3300, 0.2, 0, 0.08);
    v_noise(b, 0, 0.5, 0, 0.0001, 0.0006, NF_HP, 3000, 0.7, r);
}

static void s_hold_off(Buf *b, Rng *r, int v)
{
    (void)v;
    chirp(b, 0, 1500, 1000, 0.03, 0.045, 0.8, 0, 1.0, 0.4);
    v_noise(b, 0, 0.45, 0, 0.0001, 0.0006, NF_HP, 2500, 0.7, r);
}

static void s_menu_move(Buf *b, Rng *r, int v)
{
    (void)v;
    chirp(b, 0, 1200, 1350, 0.01, 0.018, 0.8, 0, 2.0, 0.3);
    v_noise(b, 0, 0.25, 0, 0.0001, 0.0005, NF_HP, 4000, 0.7, r);
}

static void s_menu_select(Buf *b, Rng *r, int v)
{
    (void)v;
    chirp(b, 0, 900, 900, 0.001, 0.05, 0.7, 0, 2.0, 0.5);
    chirp(b, AT(0.055), 1350, 1350, 0.001, 0.07, 0.8, 0, 2.0, 0.5);
    v_noise(b, 0, 0.3, 0, 0.0001, 0.0005, NF_HP, 4000, 0.7, r);
}

static void s_service_beep(Buf *b, Rng *r, int v)
{
    (void)r; (void)v;
    long len = AT(0.12);
    for (long i = 0; i < len; i++) {
        double x = (double)i / SR;
        double e = smoothstep01(x / 0.003) * smoothstep01((0.12 - x) / 0.012);
        buf_add(b, i, (sin(TAU * 1000 * x) + 0.15 * sin(TAU * 2000 * x)) * e * 0.8, 0);
    }
}

static void s_error(Buf *b, Rng *r, int v)
{
    (void)r; (void)v;
    for (int k = 0; k < 2; k++) {
        long at = AT(0.15 * k), len = AT(0.11);
        Osc o1 = { 0 }, o2 = { 0.3 };
        Biquad lp;
        bq_lp(&lp, 1400, 0.7);
        for (long i = 0; i < len; i++) {
            double x = (double)i / SR;
            double e = smoothstep01(x / 0.004) * smoothstep01((0.11 - x) / 0.008);
            double s = osc_sqr(&o1, 155, 0.5) + osc_sqr(&o2, 161, 0.5);
            buf_add(b, at + i, bq_run(&lp, s) * e * 0.5, 0);
        }
    }
}

static void s_neon_flicker(Buf *b, Rng *r, int v)
{
    (void)v;
    /* Mains buzz from a neon transformer, gated on and off like a tube that
       is struggling to strike. */
    long len = AT(0.3), i = 0;
    Osc o = { 0 };
    Biquad hp;
    bq_hp(&hp, 180, 0.7);
    int on = 1;
    Smooth g;
    smooth_init(&g, 400, 0);
    while (i < len) {
        long seg = AT(rng_range(r, 0.01, 0.045));
        for (long k = 0; k < seg && i < len; k++, i++) {
            double gate = smooth_step(&g, on ? 1.0 : 0.0);
            double s = osc_saw(&o, 120) * 0.6 + rng_bi(r) * 0.25;
            double x = (double)i / len;
            buf_add(b, i, bq_run(&hp, s) * gate * (1.0 - 0.6 * x), 0);
        }
        on = !on;
        if (on) v_noise(b, i, 0.5, 0, 0.0001, 0.0008, NF_HP, 3000, 0.7, r);
    }
}

/* --------------------------------------------------------------- credits */

static void s_bet_one(Buf *b, Rng *r, int v)
{
    (void)v;
    v_chip(b, 0, 0.7, 0, r);
    v_bell(b, AT(0.004), 1760, 0.5, 0, 0.12);
    v_bell(b, AT(0.028), 2637, 0.3, 0, 0.1);
}

static void s_bet_max(Buf *b, Rng *r, int v)
{
    (void)v;
    static const int N[5] = { 81, 85, 88, 93, 97 };
    for (int k = 0; k < 5; k++) {
        long at = AT(0.045 * k);
        v_chip(b, at, 0.45, 0, r);
        v_bell(b, at, mtof(N[k]), 0.45, 0, k == 4 ? 0.4 : 0.12);
    }
    for (int k = 0; k < 4; k++) v_coin(b, AT(0.2 + 0.05 * k + 0.02 * rng_uni(r)), 0.25, 0, r);
    buf_reverb(b, 0.5, 0.5, 1.0, 0.01, 0.12);
}

static void s_credit_tick(Buf *b, Rng *r, int v)
{
    (void)v;
    /* A pure, harmonic tick at C7 so a rising playback pitch reads as a
       rising note, not as a changing noise. */
    long len = AT(0.045);
    for (long i = 0; i < len; i++) {
        double x = (double)i / SR;
        double e = smoothstep01(x / 0.0005) * exp(-x / 0.010);
        buf_add(b, i, (sin(TAU * 2093 * x) + 0.2 * sin(TAU * 4186 * x)) * e * 0.8, 0);
    }
    v_noise(b, 0, 0.2, 0, 0.0001, 0.0004, NF_HP, 5000, 0.7, r);
}

static void s_credit_end(Buf *b, Rng *r, int v)
{
    (void)r; (void)v;
    v_bell(b, 0, 2093, 0.7, 0, 0.4);
    v_bell(b, AT(0.03), 3136, 0.35, 0, 0.35);
    v_bell(b, AT(0.06), 4186, 0.2, 0, 0.3);
    buf_reverb(b, 0.55, 0.5, 1.0, 0.01, 0.15);
}

static void s_coin_insert(Buf *b, Rng *r, int v)
{
    (void)v;
    v_coin(b, 0, 0.7, 0, r);
    long roll = AT(0.18);
    Biquad bp;
    bq_bp(&bp, 4000, 3.0);
    for (long i = 0; i < roll; i++) {
        double x = (double)i / SR;
        double am = 0.5 + 0.5 * sin(TAU * 30 * x);
        buf_add(b, i, bq_run(&bp, rng_bi(r)) * am * 0.15 * (1 - x / 0.18), 0);
    }
    v_coin(b, AT(0.06), 0.35, 0, r);
    v_coin(b, AT(0.11), 0.25, 0, r);
    double f[3] = { 620, 1340, 180 }, a[3] = { 1, 0.6, 0.8 }, t[3] = { 0.05, 0.03, 0.06 };
    v_modal(b, AT(0.2), 0.6, 0, f, a, t, 3);
    v_noise(b, AT(0.2), 0.3, 0, 0.0003, 0.01, NF_LP, 1500, 0.7, r);
    v_bell(b, AT(0.30), 1318.5, 0.5, 0, 0.25);
    v_bell(b, AT(0.38), 1760, 0.55, 0, 0.35);
    buf_reverb(b, 0.4, 0.5, 1.0, 0.005, 0.1);
}

static void s_cash_out(Buf *b, Rng *r, int v)
{
    (void)v;
    /* the hopper motor */
    long len = AT(1.9);
    Osc o = { 0 };
    Biquad lp;
    bq_lp(&lp, 300, 0.8);
    for (long i = 0; i < len; i++) {
        double x = (double)i / SR;
        double e = smoothstep01((x - 0.02) / 0.1) * smoothstep01((1.9 - x) / 0.15);
        buf_add(b, i, bq_run(&lp, osc_saw(&o, 55) + 0.5 * rng_bi(r)) * e * 0.12, 0);
    }
    /* coins falling into the tray, eleven a second */
    for (double t = 0.1; t < 1.8; t += 1.0 / 11 + rng_range(r, -0.012, 0.012)) {
        double p = rng_range(r, -0.35, 0.35);
        v_coin(b, AT(t), rng_range(r, 0.4, 0.55), p, r);
        v_coin(b, AT(t + rng_range(r, 0.015, 0.03)), 0.18, p, r);
    }
    static const int N[4] = { 84, 88, 91, 96 };
    for (int k = 0; k < 4; k++) v_bell(b, AT(1.95 + 0.05 * k), mtof(N[k]), 0.4, -0.3 + 0.2 * k, k == 3 ? 0.6 : 0.25);
    buf_reverb(b, 0.55, 0.5, 1.0, 0.01, 0.15);
}

/* ----------------------------------------------------------------- chips */

static void s_chip_single(Buf *b, Rng *r, int v)
{
    (void)v;
    v_chip(b, 0, 1.0, 0, r);
    if (rng_uni(r) < 0.75) v_chip(b, AT(rng_range(r, 0.012, 0.025)), rng_range(r, 0.25, 0.45), 0, r);
    thud(b, 0, rng_range(r, 160, 200), 0.01, 0.25, 0);
}

static void s_chip_stack(Buf *b, Rng *r, int v)
{
    int n = 4 + v + rng_int(r, 2);
    double t = 0;
    for (int k = 0; k < n; k++) {
        v_chip(b, AT(t), rng_range(r, 0.55, 1.0), 0, r);
        if (rng_uni(r) < 0.4) v_chip(b, AT(t + rng_range(r, 0.008, 0.015)), 0.3, 0, r);
        t += rng_range(r, 0.018, 0.04);
    }
    thud(b, 0, 170, 0.015, 0.3, 0);
}

static void s_chip_pot(Buf *b, Rng *r, int v)
{
    /* chips sliding across the felt, then tumbling onto the pile */
    v_sweep(b, 0, 0.35 + 0.05 * v, 1300, 900, 0.7, 0.22, 0, 0, 0, r);
    int n = 12 + 3 * v + rng_int(r, 4);
    for (int k = 0; k < n; k++) {
        double t = 0.12 + fabs(rng_bi(r) + rng_bi(r)) * 0.2 + 0.1 * rng_uni(r);
        double env = exp(-(t - 0.18) * (t - 0.18) / 0.03);
        v_chip(b, AT(t), rng_range(r, 0.35, 1.0) * (0.35 + 0.65 * env), 0, r);
    }
    for (int k = 0; k < 3; k++) v_chip(b, AT(0.5 + 0.05 * k + 0.04 * rng_uni(r)), 0.25 - 0.06 * k, 0, r);
    thud(b, AT(0.14), 150, 0.03, 0.3, 0);
}

/* ------------------------------------------------------------ win tiers */

static void s_win_small(Buf *b, Rng *r, int v)
{
    (void)r; (void)v;
    static const int N[3] = { 84, 88, 91 };
    for (int k = 0; k < 3; k++) v_bell(b, AT(0.06 * k), mtof(N[k]), 0.55, -0.4 + 0.4 * k, 0.45);
    v_bell(b, AT(0.18), mtof(96), 0.35, 0, 0.6);
    /* the soft "pulse" under the chime */
    long len = AT(0.9);
    for (long i = 0; i < len; i++) {
        double x = (double)i / SR;
        double e = smoothstep01(x / 0.03) * exp(-x / 0.3);
        double s = sin(TAU * mtof(72) * x) + 0.6 * sin(TAU * mtof(79) * x);
        buf_add(b, i, s * e * 0.18, 0);
    }
    buf_reverb(b, 0.5, 0.5, 1.0, 0.01, 0.25);
}

static void s_win_medium(Buf *b, Rng *r, int v)
{
    (void)v;
    v_sweep(b, 0, 0.16, 800, 5000, 1.0, 0.3, -0.5, 0.5, 1, r);
    static const int N[6] = { 84, 88, 91, 96, 100, 103 };
    for (int k = 0; k < 6; k++) v_bell(b, AT(0.1 + 0.045 * k), mtof(N[k]), 0.45, -0.6 + 0.24 * k, 0.4);
    for (int k = 0; k < 26; k++) {
        double t = 0.1 + fmin(1.3, -log(1.0 - rng_uni(r) * 0.98) * 0.3);
        v_coin(b, AT(t), rng_range(r, 0.25, 0.5) * exp(-(t - 0.1) / 0.9), rng_range(r, -0.8, 0.8), r);
    }
    v_bell(b, AT(0.4), mtof(96), 0.3, -0.3, 0.8);
    v_bell(b, AT(0.4), mtof(100), 0.25, 0.3, 0.8);
    buf_reverb(b, 0.55, 0.5, 1.0, 0.01, 0.25);
}

static void s_win_big(Buf *b, Rng *r, int v)
{
    (void)v;
    for (int k = 0; k < 3; k++) {
        v_brass(b, AT(0.09 * k), AT(0.07), mtof(67), 0.45, 0, r);
        v_brass(b, AT(0.09 * k), AT(0.07), mtof(55), 0.25, 0, r);
    }
    long h = AT(0.27);
    static const int CH[6] = { 48, 60, 64, 67, 72, 76 };
    for (int k = 0; k < 6; k++) v_brass(b, h, AT(1.1), mtof(CH[k]), k == 0 ? 0.35 : 0.42, -0.5 + 0.2 * k, r);
    v_boom(b, h, 0.8, 0);
    v_timpani(b, h, 65.4, 0.6, 0, r);
    v_crash(b, h, 0.5, 1.4, r);
    static const int RUN[12] = { 84, 86, 88, 89, 91, 93, 95, 96, 98, 100, 101, 103 };
    for (int k = 0; k < 12; k++) v_bell(b, h + AT(0.05 + 0.038 * k), mtof(RUN[k]), 0.22, -0.7 + 0.13 * k, 0.3);
    for (int k = 0; k < 40; k++) {
        double t = 0.2 + rng_uni(r) * rng_uni(r) * 2.2;
        v_coin(b, h + AT(t), rng_range(r, 0.2, 0.4), rng_range(r, -0.9, 0.9), r);
    }
    static const int SP[3] = { 96, 100, 103 };
    for (int k = 0; k < 3; k++) v_bell(b, h + AT(0.6 + 0.04 * k), mtof(SP[k]), 0.25, -0.4 + 0.4 * k, 0.6);
    buf_reverb(b, 0.6, 0.5, 1.0, 0.015, 0.3);
}

typedef struct { double t, len; int note; } Nt;

static void s_win_jackpot(Buf *b, Rng *r, int v)
{
    (void)v;
    /* 120 bpm: a triplet pickup, the call up the C major triad, a turn
       through F and G, and the landing on a big C that rings out. */
    static const Nt MEL[] = {
        { 0.000, 0.14, 67 }, { 0.167, 0.14, 67 }, { 0.333, 0.14, 67 },
        { 0.5, 0.45, 72 }, { 1.0, 0.45, 76 }, { 1.5, 0.95, 79 },
        { 2.5, 0.15, 81 }, { 2.667, 0.15, 79 }, { 2.833, 0.15, 77 },
        { 3.0, 0.45, 81 }, { 3.5, 0.45, 83 }, { 4.0, 2.0, 84 },
    };
    static const Nt HAR[] = {
        { 0.000, 0.14, 60 }, { 0.167, 0.14, 60 }, { 0.333, 0.14, 60 },
        { 0.5, 0.45, 67 }, { 1.0, 0.45, 72 }, { 1.5, 0.95, 76 },
        { 2.5, 0.15, 77 }, { 2.667, 0.15, 76 }, { 2.833, 0.15, 72 },
        { 3.0, 0.45, 77 }, { 3.5, 0.45, 79 }, { 4.0, 2.0, 79 },
    };
    static const Nt CH[] = {
        { 0.5, 0.95, 48 }, { 0.5, 0.95, 55 }, { 0.5, 0.95, 64 },
        { 1.5, 0.95, 52 }, { 1.5, 0.95, 60 }, { 1.5, 0.95, 67 },
        { 2.5, 0.95, 41 }, { 2.5, 0.95, 57 }, { 2.5, 0.95, 65 },
        { 3.5, 0.48, 43 }, { 3.5, 0.48, 59 }, { 3.5, 0.48, 62 },
        { 4.0, 2.0, 36 }, { 4.0, 2.0, 48 }, { 4.0, 2.0, 60 }, { 4.0, 2.0, 64 }, { 4.0, 2.0, 72 },
    };
    for (size_t k = 0; k < sizeof MEL / sizeof MEL[0]; k++) {
        v_brass(b, AT(MEL[k].t), AT(MEL[k].len), mtof(MEL[k].note), 0.55, -0.15, r);
        v_brass(b, AT(HAR[k].t), AT(HAR[k].len), mtof(HAR[k].note), 0.38, 0.2, r);
    }
    for (size_t k = 0; k < sizeof CH / sizeof CH[0]; k++)
        v_brass(b, AT(CH[k].t), AT(CH[k].len), mtof(CH[k].note), CH[k].note < 50 ? 0.3 : 0.26,
                -0.5 + (double)(k % 3) * 0.5, r);
    static const struct { double t, f; } TIMP[] = { { 0.5, 65.4 }, { 1.5, 98.0 }, { 2.5, 87.3 }, { 3.5, 98.0 }, { 4.0, 65.4 } };
    for (int k = 0; k < 5; k++) v_timpani(b, AT(TIMP[k].t), TIMP[k].f, 0.55, 0, r);
    v_boom(b, AT(0.5), 0.6, 0);
    v_crash(b, AT(0.5), 0.35, 1.2, r);
    /* snare roll crescendo into the landing */
    for (double t = 2.5; t < 3.98; t += 1.0 / 16) v_snare(b, AT(t), 0.08 + 0.35 * (t - 2.5) / 1.5, 0.1, r);
    v_boom(b, AT(4.0), 0.9, 0);
    v_crash(b, AT(4.0), 0.6, 1.8, r);
    static const int SCALE[7] = { 0, 2, 4, 5, 7, 9, 11 };
    for (int k = 0; k < 16; k++)
        v_bell(b, AT(4.0 + 0.035 * k), mtof(84 + 12 * (k / 7) + SCALE[k % 7]), 0.2, -0.8 + 0.1 * k, 0.35);
    for (int k = 0; k < 60; k++) {
        double t = 4.1 + rng_uni(r) * rng_uni(r) * 2.2;
        v_coin(b, AT(t), rng_range(r, 0.15, 0.35), rng_range(r, -0.9, 0.9), r);
    }
    static const int SPARK[5] = { 96, 100, 103, 108, 91 };
    for (int k = 0; k < 14; k++)
        v_bell(b, AT(4.5 + rng_uni(r) * 1.6), mtof(SPARK[rng_int(r, 5)]), 0.12, rng_range(r, -0.8, 0.8), 0.5);
    buf_reverb(b, 0.7, 0.5, 1.0, 0.02, 0.3);
    /* A composed ending: the whole thing fades over its last second, so the
       fanfare is 7.6 s long by design rather than by where a tail is cut. */
    for (long i = AT(6.6); i < b->n; i++) {
        double g = 1.0 - smoothstep01(((double)i / SR - 6.6) / 1.0);
        b->l[i] = (float)(b->l[i] * g);
        b->r[i] = (float)(b->r[i] * g);
    }
}

static void s_double_win(Buf *b, Rng *r, int v)
{
    (void)v;
    static const int G[3] = { 55, 59, 62 }, C[4] = { 60, 64, 67, 72 };
    for (int k = 0; k < 3; k++) v_brass(b, 0, AT(0.08), mtof(G[k]), 0.35, -0.3 + 0.3 * k, r);
    for (int k = 0; k < 4; k++) v_brass(b, AT(0.12), AT(0.45), mtof(C[k]), 0.35, -0.3 + 0.2 * k, r);
    static const int N[4] = { 84, 88, 91, 96 };
    for (int k = 0; k < 4; k++) v_bell(b, AT(0.12 + 0.04 * k), mtof(N[k]), 0.3, -0.4 + 0.27 * k, 0.35);
    v_coin(b, AT(0.14), 0.3, 0.3, r);
    buf_reverb(b, 0.5, 0.5, 1.0, 0.01, 0.22);
}

static void s_double_lose(Buf *b, Rng *r, int v)
{
    (void)r; (void)v;
    static const int N[3] = { 67, 63, 60 };
    for (int k = 0; k < 3; k++) v_vibe(b, AT(0.16 * k), mtof(N[k]), 0.5, 0, 0.5);
    /* a soft falling saw, the filter closing as it drops */
    long len = AT(0.6);
    Osc o = { 0 };
    Svf s = { 0, 0 };
    for (long i = 0; i < len; i++) {
        double x = (double)i / len;
        double f = 392 * pow(262.0 / 392.0, x);
        double lp;
        svf_step(&s, osc_saw(&o, f), 1500 * pow(0.2, x), 0.9, &lp, NULL, NULL);
        buf_add(b, i, lp * 0.3 * smoothstep01(x / 0.05) * (1 - x), 0);
    }
    thud(b, AT(0.32), 110, 0.08, 0.35, 0);
    buf_reverb(b, 0.5, 0.6, 1.0, 0.01, 0.15);
}

static void s_all_in(Buf *b, Rng *r, int v)
{
    (void)v;
    v_sweep(b, 0, 0.2, 1400, 800, 0.7, 0.3, 0, 0, 0, r);
    for (int k = 0; k < 10; k++) v_chip(b, AT(rng_uni(r) * 0.14), rng_range(r, 0.4, 0.8), rng_range(r, -0.3, 0.3), r);
    long h = AT(0.06);
    v_boom(b, h, 1.0, 0);
    v_timpani(b, h, 65.4, 0.5, 0, r);
    static const int CM[4] = { 36, 48, 55, 63 };
    for (int k = 0; k < 4; k++) v_brass(b, h, AT(0.45), mtof(CM[k]), 0.35, -0.3 + 0.2 * k, r);
    v_sweep(b, h, 0.9, 2000, 7000, 3.0, 0.12, -0.6, 0.6, 2, r);
    buf_reverb(b, 0.6, 0.5, 1.0, 0.015, 0.28);
}

static void s_reveal(Buf *b, Rng *r, int v)
{
    (void)v;
    v_boom(b, 0, 0.8, 0);
    v_timpani(b, 0, 73.4, 0.4, 0, r);
    v_bell(b, AT(0.01), mtof(84), 0.3, -0.3, 0.6);
    v_bell(b, AT(0.01), mtof(91), 0.25, 0.3, 0.6);
    v_crash(b, 0, 0.25, 0.8, r);
    buf_reverb(b, 0.55, 0.5, 1.0, 0.01, 0.25);
}

static void s_your_turn(Buf *b, Rng *r, int v)
{
    (void)r; (void)v;
    v_vibe(b, 0, mtof(79), 0.6, -0.2, 0.35);
    v_vibe(b, AT(0.13), mtof(84), 0.6, 0.2, 0.4);
    v_bell(b, AT(0.13), mtof(96), 0.12, 0.2, 0.4);
    buf_reverb(b, 0.5, 0.5, 1.0, 0.01, 0.15);
}

static void s_blinds_up(Buf *b, Rng *r, int v)
{
    (void)r; (void)v;
    v_bell(b, 0, 880, 0.7, 0, 0.4);
    v_bell(b, AT(0.22), 880, 0.7, 0, 0.45);
    buf_reverb(b, 0.5, 0.5, 1.0, 0.01, 0.12);
}

static void s_bust(Buf *b, Rng *r, int v)
{
    (void)r; (void)v;
    static const int N[3] = { 72, 68, 65 };
    for (int k = 0; k < 3; k++) v_vibe(b, AT(0.28 * k), mtof(N[k]), 0.5, 0, 0.6);
    static const int P[3] = { 53, 56, 60 };
    long len = AT(1.6);
    for (int k = 0; k < 3; k++) {
        Osc o1 = { 0.1 * k }, o2 = { 0.5 };
        Svf s = { 0, 0 };
        for (long i = 0; i < len; i++) {
            double x = (double)i / SR;
            double e = smoothstep01(x / 0.35) * smoothstep01((1.6 - x) / 0.8);
            double f = mtof(P[k]);
            double lp;
            svf_step(&s, osc_saw(&o1, f * 1.003) + osc_saw(&o2, f * 0.997), 700 * exp(-x / 1.5) + 200, 0.7, &lp, NULL, NULL);
            buf_add(b, i, lp * e * 0.12, 0);
        }
    }
    buf_reverb(b, 0.5, 0.6, 1.0, 0.01, 0.18);
}

static void s_whoosh(Buf *b, Rng *r, int v)
{
    (void)v;
    v_sweep(b, 0, 0.5, 350, 4500, 1.2, 0.8, -0.8, 0.8, 0, r);
    v_sweep(b, 0, 0.45, 200, 900, 0.7, 0.4, -0.5, 0.5, 0, r);
}

/* ------------------------------------------------------------- registry */

const SfxRecipe SFX_RECIPES[] = {
    /* name            fn               var ch  len   target clip  stream */
    { "card_deal",     s_card_deal,     3, 1, 0.20, -20.0, 4.0, 0 },
    { "card_flip",     s_card_flip,     3, 1, 0.20, -20.0, 4.0, 0 },
    { "card_slide",    s_card_slide,    2, 1, 0.35, -21.0, 3.0, 0 },
    { "card_shuffle",  s_card_shuffle,  1, 1, 1.50, -20.0, 3.0, 0 },
    { "fold",          s_fold,          2, 1, 0.35, -21.0, 3.0, 0 },
    { "check",         s_check,         1, 1, 0.35, -20.0, 4.0, 0 },
    { "button",        s_button,        3, 1, 0.12, -21.0, 4.0, 0 },
    { "hold_on",       s_hold_on,       1, 1, 0.50, -21.0, 1.0, 0 },
    { "hold_off",      s_hold_off,      1, 1, 0.40, -22.0, 1.0, 0 },
    { "menu_move",     s_menu_move,     1, 1, 0.25, -23.0, 1.0, 0 },
    { "menu_select",   s_menu_select,   1, 1, 0.60, -21.0, 1.0, 0 },
    { "service_beep",  s_service_beep,  1, 1, 0.20, -21.0, 0.0, 0 },
    { "error",         s_error,         1, 1, 0.40, -20.0, 0.0, 0 },
    { "neon_flicker",  s_neon_flicker,  1, 1, 0.40, -28.0, 0.0, 0 },
    { "bet_one",       s_bet_one,       1, 1, 1.00, -20.0, 2.0, 0 },
    { "bet_max",       s_bet_max,       1, 1, 2.50, -18.0, 2.0, 0 },
    { "credit_tick",   s_credit_tick,   1, 1, 0.08, -23.0, 0.0, 0 },
    { "credit_end",    s_credit_end,    1, 1, 3.50, -19.0, 0.0, 0 },
    { "coin_insert",   s_coin_insert,   1, 1, 2.50, -19.0, 2.0, 0 },
    { "cash_out",      s_cash_out,      1, 2, 4.50, -17.0, 2.0, 1 },
    { "chip_single",   s_chip_single,   4, 1, 0.20, -20.0, 4.0, 0 },
    { "chip_stack",    s_chip_stack,    3, 1, 0.45, -19.0, 4.0, 0 },
    { "chip_pot",      s_chip_pot,      3, 1, 0.90, -19.0, 4.0, 0 },
    { "win_small",     s_win_small,     1, 2, 3.50, -16.0, 0.0, 1 },
    { "win_medium",    s_win_medium,    1, 2, 4.50, -15.0, 1.0, 1 },
    { "win_big",       s_win_big,       1, 2, 6.50, -13.0, 1.5, 1 },
    { "win_jackpot",   s_win_jackpot,   1, 2, 8.00, -12.0, 1.5, 1 },
    { "double_win",    s_double_win,    1, 2, 3.00, -16.0, 1.0, 1 },
    { "double_lose",   s_double_lose,   1, 1, 3.00, -18.0, 0.0, 1 },
    { "all_in",        s_all_in,        1, 2, 4.00, -14.0, 1.5, 1 },
    { "reveal",        s_reveal,        1, 2, 3.80, -15.0, 1.5, 1 },
    { "your_turn",     s_your_turn,     1, 2, 3.00, -19.0, 0.0, 1 },
    { "blinds_up",     s_blinds_up,     1, 1, 4.00, -19.0, 0.0, 1 },
    { "bust",          s_bust,          1, 1, 4.00, -18.0, 0.0, 1 },
    { "whoosh",        s_whoosh,        1, 2, 0.60, -20.0, 0.0, 0 },
};
const int SFX_RECIPE_COUNT = (int)(sizeof SFX_RECIPES / sizeof SFX_RECIPES[0]);
