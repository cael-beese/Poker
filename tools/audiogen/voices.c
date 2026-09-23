/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* voices.c - see voices.h. */
#include "voices.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static float *tmp(long n)
{
    float *p = calloc((size_t)n, sizeof(float));
    if (!p) abort();
    return p;
}

/* Render `n` samples of mono voice into a temp buffer, mix, free. */
#define MIXFREE(b, at, t, n, amp, pan) do { buf_mix((b), (at), (t), (n), (amp), (pan)); free(t); } while (0)

void v_modal(Buf *b, long at, double amp, double pan,
             const double *f, const double *a, const double *tau, int n)
{
    double tmax = 0;
    for (int k = 0; k < n; k++) if (tau[k] > tmax) tmax = tau[k];
    long len = secs(tmax * 7.0) + 1;
    float *t = tmp(len);
    for (int k = 0; k < n; k++) {
        if (f[k] >= SR * 0.47) continue;
        double d = exp(-1.0 / (tau[k] * SR)), e = a[k], w = TAU * f[k] / SR;
        for (long i = 0; i < len; i++) {
            t[i] += (float)(e * sin(w * i));
            e *= d;
        }
    }
    MIXFREE(b, at, t, len, amp, pan);
}

static double env_ae(double t, double att, double tau)
{
    if (t < att) return att > 0 ? t / att : 1.0;
    return exp(-(t - att) / tau);
}

void v_noise(Buf *b, long at, double amp, double pan, double att, double tau,
             int ftype, double f, double Q, Rng *r)
{
    long len = secs(att + tau * 7.0) + 1;
    float *t = tmp(len);
    Biquad q;
    if (ftype == NF_LP) bq_lp(&q, f, Q);
    else if (ftype == NF_HP) bq_hp(&q, f, Q);
    else bq_bp(&q, f, Q);
    for (long i = 0; i < len; i++)
        t[i] = (float)(bq_run(&q, rng_bi(r)) * env_ae((double)i / SR, att, tau));
    MIXFREE(b, at, t, len, amp, pan);
}

void v_sweep(Buf *b, long at, double dur, double f0, double f1, double Q,
             double amp, double pan0, double pan1, int shape, Rng *r)
{
    long len = secs(dur);
    Svf s = { 0, 0 };
    for (long i = 0; i < len; i++) {
        double x = (double)i / len;
        double fc = f0 * pow(f1 / f0, x);
        double bp;
        svf_step(&s, rng_bi(r), fc, Q, NULL, &bp, NULL);
        double e;
        if (shape == 0) e = sin(PI * x) * sin(PI * x);
        else if (shape == 1) e = pow(x, 2.2) * (1.0 - smoothstep01((x - 0.97) / 0.03));
        else e = pow(1.0 - x, 2.0) * smoothstep01(x / 0.02);
        buf_add(b, at + i, bp * e * amp, pan0 + (pan1 - pan0) * x);
    }
}

void v_chip(Buf *b, long at, double amp, double pan, Rng *r)
{
    /* Clay/composite chips: a short, bright, slightly inharmonic clack. */
    double f0 = rng_range(r, 2500, 3400);
    double f[5] = { f0, f0 * rng_range(r, 1.55, 1.7), f0 * rng_range(r, 2.25, 2.45),
                    f0 * rng_range(r, 3.0, 3.25), rng_range(r, 850, 1100) };
    double a[5] = { 1.0, 0.7, 0.45, 0.25, 0.35 };
    double tau[5] = { rng_range(r, 0.007, 0.012), rng_range(r, 0.005, 0.008),
                      0.004, 0.003, 0.004 };
    v_modal(b, at, amp * 0.8, pan, f, a, tau, 5);
    v_noise(b, at, amp * 0.9, pan, 0.0002, 0.0007, NF_HP, 4000, 0.7, r);
}

void v_coin(Buf *b, long at, double amp, double pan, Rng *r)
{
    /* A struck coin rings for a long time at several inharmonic modes. */
    double f0 = rng_range(r, 2300, 3300);
    double f[6] = { f0, f0 * 1.47 * rng_range(r, 0.98, 1.02), f0 * 2.09 * rng_range(r, 0.98, 1.02),
                    f0 * 2.56 * rng_range(r, 0.98, 1.02), f0 * 3.9 * rng_range(r, 0.98, 1.02),
                    f0 * 5.2 };
    double a[6] = { 0.6, 1.0, 0.7, 0.5, 0.3, 0.15 };
    double tau[6] = { rng_range(r, 0.15, 0.3), rng_range(r, 0.1, 0.25), rng_range(r, 0.08, 0.2),
                      0.08, 0.05, 0.03 };
    v_modal(b, at, amp * 0.45, pan, f, a, tau, 6);
    v_noise(b, at, amp * 0.6, pan, 0.0002, 0.0012, NF_HP, 5000, 0.7, r);
}

void v_bell(Buf *b, long at, double fr, double amp, double pan, double tau)
{
    /* Glockenspiel-like partials; the upper ones die quickly, so the
       strike is bright and the tail is a clean tone. */
    double f[5] = { fr, fr * 2.76, fr * 5.40, fr * 8.93, fr * 2.0 };
    double a[5] = { 1.0, 0.45, 0.22, 0.1, 0.18 };
    double t[5] = { tau, tau * 0.45, tau * 0.22, tau * 0.12, tau * 0.6 };
    v_modal(b, at, amp, pan, f, a, t, 5);
}

void v_vibe(Buf *b, long at, double fr, double amp, double pan, double tau)
{
    long len = secs(tau * 6.0) + 1;
    float *t = tmp(len);
    for (long i = 0; i < len; i++) {
        double s = (double)i / SR;
        double att = smoothstep01(s / 0.002);
        double v = sin(TAU * fr * s) * exp(-s / tau)
                 + 0.25 * sin(TAU * fr * 4.0 * s) * exp(-s / (tau * 0.2))
                 + 0.07 * sin(TAU * fr * 10.0 * s) * exp(-s / (tau * 0.05));
        double trem = 1.0 - 0.25 * (0.5 - 0.5 * cos(TAU * 5.5 * s));
        t[i] = (float)(v * att * trem);
    }
    MIXFREE(b, at, t, len, amp, pan);
}

void v_glass(Buf *b, long at, double amp, double pan, Rng *r)
{
    double f0 = rng_range(r, 1900, 2600);
    double f[4] = { f0, f0 * 2.32, f0 * 3.87, f0 * 5.1 };
    double a[4] = { 1.0, 0.6, 0.35, 0.2 };
    double tau[4] = { 0.35, 0.22, 0.12, 0.07 };
    v_modal(b, at, amp * 0.5, pan, f, a, tau, 4);
    v_noise(b, at, amp * 0.3, pan, 0.0001, 0.0008, NF_HP, 6000, 0.7, r);
}

void v_brass(Buf *b, long at, long len, double fr, double amp, double pan, Rng *r)
{
    const int NV = 5;
    double det[5] = { -11, -4, 0, 5, 12 };  /* cents */
    double rel = 0.18;
    long total = len + secs(rel) + 1;
    for (int v = 0; v < NV; v++) {
        float *t = tmp(total);
        Osc o = { rng_uni(r) };
        Svf s = { 0, 0 };
        double vp = pan + 0.35 * (v - 2) / 2.0;
        for (long i = 0; i < total; i++) {
            double x = (double)i / SR, dur = (double)len / SR;
            /* pitch scoop into the note, then a delayed vibrato */
            double cents = det[v] - 35.0 * exp(-x / 0.03)
                         + 9.0 * smoothstep01((x - 0.25) / 0.3) * sin(TAU * 5.3 * x + v);
            double f = fr * pow(2.0, cents / 1200.0);
            double a = x < 0.02 ? x / 0.02 : 0.82 + 0.18 * exp(-(x - 0.02) / 0.12);
            if (x > dur) a *= exp(-(x - dur) / (rel / 4));
            /* brassy "blat": the filter opens hard on the attack */
            double fc = fr * 2.0 + (2500 + fr * 4) * (1 - exp(-x / 0.03)) * (0.55 + 0.45 * exp(-x / 0.25));
            double lp;
            svf_step(&s, osc_saw(&o, f), fc, 0.9, &lp, NULL, NULL);
            t[i] = (float)(lp * a);
        }
        MIXFREE(b, at, t, total, amp / NV * 1.6, vp);
    }
}

void v_kick(Buf *b, long at, double amp, double pan)
{
    long len = secs(0.55);
    float *t = tmp(len);
    double ph = 0;
    for (long i = 0; i < len; i++) {
        double x = (double)i / SR;
        double f = 46 + 110 * exp(-x / 0.03);
        ph += TAU * f / SR;
        double e = smoothstep01(x / 0.0015) * exp(-x / 0.22);
        double v = sin(ph) * e;
        t[i] = (float)(tanh(1.6 * v) / tanh(1.6));
    }
    /* beater click */
    Biquad q;
    bq_bp(&q, 3200, 0.8);
    Rng r;
    rng_seed(&r, 77);
    for (long i = 0; i < secs(0.006); i++)
        t[i] += (float)(0.18 * bq_run(&q, rng_bi(&r)) * exp(-(double)i / (0.0012 * SR)));
    MIXFREE(b, at, t, len, amp, pan);
}

void v_snare(Buf *b, long at, double amp, double pan, Rng *r)
{
    double f[3] = { 185, 330, 245 }, a[3] = { 0.6, 0.3, 0.2 }, tau[3] = { 0.06, 0.04, 0.05 };
    v_modal(b, at, amp * 0.9, pan, f, a, tau, 3);
    v_noise(b, at, amp * 0.9, pan, 0.0008, 0.12, NF_BP, 3800, 0.6, r);
    v_noise(b, at, amp * 0.4, pan, 0.0004, 0.05, NF_HP, 6000, 0.7, r);
}

void v_clap(Buf *b, long at, double amp, double pan, Rng *r)
{
    for (int k = 0; k < 3; k++)
        v_noise(b, at + secs(0.009 * k), amp * (0.6 + 0.2 * k), pan, 0.0003, 0.003, NF_BP, 1300, 1.3, r);
    v_noise(b, at + secs(0.027), amp, pan, 0.001, 0.07, NF_BP, 1200, 1.1, r);
}

void v_hat(Buf *b, long at, double amp, double pan, double tau, Rng *r)
{
    static const double HF[6] = { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
    long len = secs(tau * 7 + 0.002);
    float *t = tmp(len);
    Osc o[6];
    for (int k = 0; k < 6; k++) o[k].ph = rng_uni(r);
    Biquad bp, hp;
    bq_bp(&bp, 10000, 0.9);
    bq_hp(&hp, 7000, 0.7);
    for (long i = 0; i < len; i++) {
        double m = 0;
        for (int k = 0; k < 6; k++) m += osc_sqr(&o[k], HF[k] * 1.6, 0.5);
        m = m / 6 + 0.5 * rng_bi(r);
        double x = (double)i / SR;
        double e = smoothstep01(x / 0.0005) * exp(-x / tau);
        t[i] = (float)(bq_run(&hp, bq_run(&bp, m)) * e);
    }
    MIXFREE(b, at, t, len, amp * 1.5, pan);
}

void v_rim(Buf *b, long at, double amp, double pan, Rng *r)
{
    double f[3] = { 820, 1650, 2450 }, a[3] = { 0.6, 0.8, 0.3 }, tau[3] = { 0.012, 0.009, 0.006 };
    v_modal(b, at, amp, pan, f, a, tau, 3);
    v_noise(b, at, amp * 0.5, pan, 0.0002, 0.006, NF_BP, 2200, 1.0, r);
}

void v_shaker(Buf *b, long at, double amp, double pan, Rng *r)
{
    v_noise(b, at, amp, pan, 0.012, 0.03, NF_BP, 7000, 1.4, r);
}

void v_crash(Buf *b, long at, double amp, double tau, Rng *r)
{
    long len = secs(tau * 5.5);
    for (int c = 0; c < 2; c++) {
        float *t = tmp(len);
        Osc o[8];
        double fr[8];
        for (int k = 0; k < 8; k++) { o[k].ph = rng_uni(r); fr[k] = rng_range(r, 300, 1400); }
        Biquad hp, pk;
        bq_hp(&hp, 3500, 0.6);
        bq_peak(&pk, 8000, 0.7, 4);
        for (long i = 0; i < len; i++) {
            double m = 0;
            for (int k = 0; k < 8; k++) m += osc_sqr(&o[k], fr[k], 0.5);
            m = m / 8 * 0.7 + rng_bi(r);
            double x = (double)i / SR;
            double e = smoothstep01(x / 0.002) * (0.55 * exp(-x / 0.08) + 0.45 * exp(-x / tau));
            t[i] = (float)(bq_run(&pk, bq_run(&hp, m)) * e);
        }
        MIXFREE(b, at, t, len, amp * 0.6, c ? 0.6 : -0.6);
    }
}

void v_boom(Buf *b, long at, double amp, double pan)
{
    long len = secs(2.0);
    float *t = tmp(len);
    double ph = 0;
    Rng r;
    rng_seed(&r, 991);
    Biquad lp;
    bq_lp(&lp, 180, 0.7);
    for (long i = 0; i < len; i++) {
        double x = (double)i / SR;
        double f = 36 + 80 * exp(-x / 0.1);
        ph += TAU * f / SR;
        double e = smoothstep01(x / 0.003) * exp(-x / 0.55);
        double v = sin(ph) * e + 0.5 * bq_run(&lp, rng_bi(&r)) * exp(-x / 0.12) * smoothstep01(x / 0.002);
        t[i] = (float)(tanh(1.4 * v) / tanh(1.4));
    }
    MIXFREE(b, at, t, len, amp, pan);
}

void v_timpani(Buf *b, long at, double fr, double amp, double pan, Rng *r)
{
    long len = secs(2.2);
    float *t = tmp(len);
    double ratio[4] = { 1, 1.5, 1.98, 2.44 }, a[4] = { 1, 0.5, 0.3, 0.2 }, tau[4] = { 0.9, 0.55, 0.4, 0.28 };
    double ph[4] = { 0, 0, 0, 0 };
    for (long i = 0; i < len; i++) {
        double x = (double)i / SR;
        double bend = 1.0 + 0.035 * exp(-x / 0.08);
        double v = 0;
        for (int k = 0; k < 4; k++) {
            ph[k] += TAU * fr * ratio[k] * bend / SR;
            v += a[k] * sin(ph[k]) * exp(-x / tau[k]);
        }
        t[i] = (float)(v * smoothstep01(x / 0.002) * 0.6);
    }
    MIXFREE(b, at, t, len, amp, pan);
    v_noise(b, at, amp * 0.35, pan, 0.0005, 0.012, NF_LP, 1200, 0.7, r);
}

void v_tom(Buf *b, long at, double fr, double amp, double pan, Rng *r)
{
    long len = secs(0.7);
    float *t = tmp(len);
    double ph = 0;
    for (long i = 0; i < len; i++) {
        double x = (double)i / SR;
        ph += TAU * fr * (1.0 + 0.5 * exp(-x / 0.06)) / SR;
        t[i] = (float)(sin(ph) * smoothstep01(x / 0.0015) * exp(-x / 0.2));
    }
    MIXFREE(b, at, t, len, amp, pan);
    v_noise(b, at, amp * 0.25, pan, 0.0005, 0.02, NF_BP, 2500, 0.8, r);
}
