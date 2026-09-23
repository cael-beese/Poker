/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* music.c - the lounge loop, the attract loop and the crowd ambience.
 *
 * The music is a small sequencer over nine stems (kick, snare, hats, fx,
 * bass, electric piano, pad, arp, lead). Each stem is rendered on its own,
 * measured, and set to a target level before it is summed, which is how the
 * mix is balanced without anyone listening to it: every stem lands at a known
 * active RMS, in the proportions of a typical lounge/synthwave mix.
 *
 * Seamless loops: the song is rendered for its length L plus a tail T, and
 * the tail (release, reverb and delay spilling past the loop end) is added
 * back onto the start. That is exactly what a looping player hears, so the
 * seam is continuous. Every LFO is tempo-synced to a whole number of cycles
 * per loop, and the one stateful master filter is primed by running it over
 * the loop once before the pass that is kept.
 */
#include "music.h"
#include "voices.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 98 bpm makes a beat exactly 27000 samples at 44.1 kHz, so every note
   starts on a whole sample and a loop of whole bars is a whole number of
   samples: no drift and no rounding at the seam. */
#define BPM 98
#define BEAT (SR * 60 / BPM)
#define STEP (BEAT / 4)
#define BAR (BEAT * 4)

/* ---------------------------------------------------------------- harmony */

typedef struct { int bass; int v[4]; } Chord;
enum { Cm9, Abmaj9, Fm9, G7sus, G7b9, Ebmaj9, BbD, Gm7, Abmaj7, Dbmaj9, Gm9, G7alt, NCHORD };
static const Chord CHORDS[NCHORD] = {
    [Cm9]    = { 36, { 63, 67, 70, 74 } },
    [Abmaj9] = { 44, { 60, 63, 67, 70 } },
    [Fm9]    = { 41, { 56, 60, 63, 67 } },
    [G7sus]  = { 43, { 53, 60, 62, 67 } },
    [G7b9]   = { 43, { 53, 59, 62, 68 } },
    [Ebmaj9] = { 39, { 55, 62, 65, 70 } },
    [BbD]    = { 38, { 58, 62, 65, 72 } },
    [Gm7]    = { 43, { 55, 58, 62, 65 } },
    [Abmaj7] = { 44, { 56, 60, 63, 67 } },
    [Dbmaj9] = { 37, { 60, 63, 65, 68 } },
    [Gm9]    = { 43, { 58, 62, 65, 69 } },
    [G7alt]  = { 43, { 53, 59, 63, 68 } },
};

static const int SEC_A[4][2] = { { Cm9, Cm9 }, { Abmaj9, Abmaj9 }, { Fm9, Fm9 }, { G7sus, G7b9 } };
static const int SEC_B[8][2] = { { Ebmaj9, Ebmaj9 }, { BbD, BbD }, { Cm9, Cm9 }, { Abmaj9, Abmaj9 },
                                 { Fm9, Fm9 }, { Gm7, Gm7 }, { Abmaj7, Abmaj7 }, { G7sus, G7b9 } };
static const int SEC_C[8][2] = { { Abmaj9, Abmaj9 }, { Abmaj9, Abmaj9 }, { Dbmaj9, Dbmaj9 }, { Dbmaj9, Dbmaj9 },
                                 { Fm9, Fm9 }, { Gm9, Gm9 }, { Abmaj7, Abmaj7 }, { G7alt, G7alt } };

/* Melodies: bar within the 8-bar phrase, 16th step, length in steps, note. */
typedef struct { int bar, step, len, note; } MelNote;
static const MelNote MEL_A[] = {
    { 0, 0, 6, 79 }, { 0, 6, 2, 82 }, { 0, 8, 4, 84 }, { 0, 12, 2, 82 }, { 0, 14, 2, 79 },
    { 1, 0, 8, 75 }, { 1, 10, 2, 77 }, { 1, 12, 4, 79 },
    { 2, 0, 4, 80 }, { 2, 4, 2, 79 }, { 2, 6, 2, 77 }, { 2, 8, 6, 75 }, { 2, 14, 2, 72 },
    { 3, 0, 8, 74 }, { 3, 8, 4, 77 }, { 3, 12, 4, 74 },
    { 4, 0, 6, 79 }, { 4, 6, 2, 82 }, { 4, 8, 6, 84 }, { 4, 14, 2, 86 },
    { 5, 0, 4, 87 }, { 5, 4, 4, 86 }, { 5, 8, 8, 84 },
    { 6, 0, 6, 80 }, { 6, 6, 2, 79 }, { 6, 8, 4, 77 }, { 6, 12, 4, 75 },
    { 7, 0, 8, 74 }, { 7, 8, 4, 71 }, { 7, 12, 3, 74 },
};
static const MelNote MEL_B[] = {
    { 0, 0, 2, 74 }, { 0, 2, 2, 77 }, { 0, 4, 4, 79 }, { 0, 8, 8, 82 },
    { 1, 0, 4, 77 }, { 1, 4, 4, 74 }, { 1, 8, 8, 72 },
    { 2, 0, 2, 75 }, { 2, 2, 2, 79 }, { 2, 4, 6, 82 }, { 2, 10, 2, 84 }, { 2, 12, 4, 82 },
    { 3, 0, 12, 79 }, { 3, 12, 4, 75 },
    { 4, 0, 4, 80 }, { 4, 4, 4, 79 }, { 4, 8, 4, 77 }, { 4, 12, 4, 75 },
    { 5, 0, 8, 74 }, { 5, 8, 4, 77 }, { 5, 12, 4, 79 },
    { 6, 0, 6, 75 }, { 6, 6, 2, 72 }, { 6, 8, 4, 75 }, { 6, 12, 4, 79 },
    { 7, 0, 8, 77 }, { 7, 8, 4, 74 }, { 7, 12, 3, 71 },
};

/* ------------------------------------------------------------ arrangement */

enum { P_INTRO, P_FULL, P_BRIDGE };
enum { F_CRASH = 1, F_FILL = 2, F_RISER = 4 };
typedef struct { int ch[2]; int part; int mel; int mel_bar; int flags; } Bar;

typedef struct { Bar bar[64]; int n; } Song;

static void add_section(Song *s, const int (*sec)[2], int nbars, int reps, int part, int mel, int flags)
{
    for (int r = 0; r < reps; r++)
        for (int k = 0; k < nbars; k++) {
            Bar *b = &s->bar[s->n];
            b->ch[0] = sec[k][0];
            b->ch[1] = sec[k][1];
            b->part = part;
            b->mel = mel;
            b->mel_bar = r * nbars + k;
            b->flags = (r == 0 && k == 0) ? flags : 0;
            s->n++;
        }
}

static void song_lounge(Song *s)
{
    memset(s, 0, sizeof *s);
    add_section(s, SEC_A, 4, 2, P_INTRO, 0, 0);
    add_section(s, SEC_A, 4, 2, P_FULL, 1, F_CRASH);
    add_section(s, SEC_B, 8, 1, P_FULL, 2, F_CRASH);
    add_section(s, SEC_C, 8, 1, P_BRIDGE, 0, 0);
    s->bar[s->n - 1].flags |= F_RISER;
    add_section(s, SEC_A, 4, 2, P_FULL, 1, F_CRASH);
    add_section(s, SEC_B, 8, 1, P_FULL, 2, F_CRASH);
    s->bar[s->n - 1].flags |= F_FILL;
}

static void song_attract(Song *s)
{
    memset(s, 0, sizeof *s);
    add_section(s, SEC_B, 8, 1, P_FULL, 2, F_CRASH);
    add_section(s, SEC_A, 4, 2, P_FULL, 1, F_CRASH);
    s->bar[s->n - 1].flags |= F_FILL;
}

static const Chord *chord_at(const Song *s, int bar, int step)
{
    return &CHORDS[s->bar[bar].ch[step < 8 ? 0 : 1]];
}

/* ------------------------------------------------------------ instruments */

static void ep_note(float *out, long n, long at, long len, double midi, double vel)
{
    double f = mtof(midi), tau = 1.4 * pow(2.0, -(midi - 60) / 24.0), rel = 0.1;
    long total = len + secs(rel * 6);
    for (long i = 0; i < total && at + i < n; i++) {
        if (at + i < 0) continue;
        double x = (double)i / SR, dur = (double)len / SR;
        double I = vel * (1.6 * exp(-x / 0.25) + 0.3);
        double c = sin(TAU * f * x + I * sin(TAU * f * x));
        double tine = 0.1 * vel * sin(TAU * f * 14.0 * x) * exp(-x / 0.012);
        double e = smoothstep01(x / 0.002) * exp(-x / tau);
        if (x > dur) e *= exp(-(x - dur) / (rel / 3));
        double v = (0.8 * c + tine) * e * vel;
        out[at + i] += (float)(tanh(1.3 * v) / 1.3);
    }
}

static void bass_note(float *out, long n, long at, long len, double midi, double vel)
{
    double f = mtof(midi);
    long total = len + secs(0.08);
    Osc o = { 0 };
    Svf s = { 0, 0 };
    for (long i = 0; i < total && at + i < n; i++) {
        double x = (double)i / SR, dur = (double)len / SR;
        double e = smoothstep01(x / 0.003);
        if (x > dur) e *= exp(-(x - dur) / 0.012);
        double fc = 140 + 2.0 * f + 1000 * vel * exp(-x / 0.07);
        double lp;
        svf_step(&s, osc_saw(&o, f), fc, 1.0, &lp, NULL, NULL);
        double v = (lp + 0.6 * sin(TAU * f * x)) * e * vel;
        out[at + i] += (float)(tanh(1.4 * v) / 1.4);
    }
}

static void pluck_note(float *out, long n, long at, long len, double midi, double vel)
{
    double f = mtof(midi);
    long total = len + secs(0.12);
    Osc o1 = { 0 }, o2 = { 0.25 };
    Svf s = { 0, 0 };
    for (long i = 0; i < total && at + i < n; i++) {
        double x = (double)i / SR, dur = (double)len / SR;
        double e = smoothstep01(x / 0.002) * exp(-x / 0.2);
        if (x > dur) e *= exp(-(x - dur) / 0.02);
        double fc = 300 + 3800 * vel * exp(-x / 0.07);
        double lp;
        svf_step(&s, 0.6 * osc_saw(&o1, f) + 0.4 * osc_sqr(&o2, f * 1.003, 0.3), fc, 1.3, &lp, NULL, NULL);
        out[at + i] += (float)(lp * e * vel);
    }
}

/* Pad: three detuned saws per side, a slow filter LFO locked to two bars. */
static void pad_note(Buf *b, long at, long len, double midi, double vel, Rng *r)
{
    static const double DL[3] = { -9, 3, 11 }, DR[3] = { -11, -2, 8 };
    double f = mtof(midi), rel = 0.9;
    long total = len + secs(rel * 5);
    Osc ol[3], orr[3];
    for (int k = 0; k < 3; k++) { ol[k].ph = rng_uni(r); orr[k].ph = rng_uni(r); }
    Svf sl = { 0, 0 }, sr = { 0, 0 };
    for (long i = 0; i < total && at + i < b->n; i++) {
        double x = (double)i / SR, dur = (double)len / SR;
        double tabs = (double)(at + i) / BAR;
        double e = smoothstep01(x / 0.5);
        if (x > dur) e *= exp(-(x - dur) / (rel / 3));
        double fc = 1500 + 600 * sin(TAU * tabs / 2.0);
        double a = 0, c = 0;
        for (int k = 0; k < 3; k++) {
            a += osc_saw(&ol[k], f * pow(2.0, DL[k] / 1200.0));
            c += osc_saw(&orr[k], f * pow(2.0, DR[k] / 1200.0));
        }
        double lpl, lpr;
        svf_step(&sl, a / 3, fc, 0.7, &lpl, NULL, NULL);
        svf_step(&sr, c / 3, fc, 0.7, &lpr, NULL, NULL);
        buf_add2(b, at + i, lpl * e * vel, lpr * e * vel);
    }
}

/* Lead: one continuous monophonic voice so that legato notes glide. */
typedef struct { long at, len; double midi, vel; } LeadNote;

static void render_lead(Buf *b, const LeadNote *nt, int nn, double pan)
{
    long n = b->n;
    float *mono = calloc((size_t)n, sizeof(float));
    if (!mono) abort();
    Osc o1 = { 0 }, o2 = { 0.5 };
    Svf s = { 0, 0 };
    double pitch = nn ? nt[0].midi : 60, amp = 0;
    int k = 0;
    long since = 0;
    double cur_vel = 0.8;
    for (long i = 0; i < n; i++) {
        while (k < nn && i >= nt[k].at + nt[k].len) k++;
        int gate = k < nn && i >= nt[k].at;
        if (gate && i == nt[k].at) { since = 0; cur_vel = nt[k].vel; }
        if (gate) pitch += (nt[k].midi - pitch) * (1.0 - exp(-1.0 / (0.025 * SR)));
        double target = gate ? cur_vel : 0.0;
        double tc = gate ? 0.012 : 0.07;
        amp += (target - amp) * (1.0 - exp(-1.0 / (tc * SR)));
        double xs = (double)since / SR;
        double vib = 14.0 * smoothstep01((xs - 0.2) / 0.3) * sin(TAU * 5.0 * xs);
        double f = mtof(pitch + vib / 100.0);
        double fc = 900 + 2600 * exp(-xs / 0.15) + 900 * amp;
        double lp;
        svf_step(&s, 0.55 * osc_saw(&o1, f * 1.0023) + 0.45 * osc_sqr(&o2, f * 0.9977, 0.5), fc, 0.8, &lp, NULL, NULL);
        mono[i] = (float)(lp * amp);
        since++;
    }
    buf_mix(b, 0, mono, n, 1.0, pan);
    free(mono);
}

/* ------------------------------------------------------------ mix helpers */

/* RMS over the 100 ms windows within 40 dB of the loudest one: the level of
   a stem while it is actually playing, ignoring the bars it sits out. */
static double active_rms_db(const Buf *b)
{
    long W = SR / 10, nw = b->n / W;
    double *ms = calloc((size_t)nw + 1, sizeof(double)), mx = 0;
    if (!ms) abort();
    for (long w = 0; w < nw; w++) {
        double a = 0;
        for (long i = w * W; i < (w + 1) * W; i++) {
            a += (double)b->l[i] * b->l[i];
            if (b->r) a += (double)b->r[i] * b->r[i];
        }
        ms[w] = a / (W * b->ch);
        if (ms[w] > mx) mx = ms[w];
    }
    double sum = 0;
    long cnt = 0;
    for (long w = 0; w < nw; w++)
        if (ms[w] > mx * 1e-4) { sum += ms[w]; cnt++; }
    free(ms);
    return cnt ? 10 * log10(sum / cnt) : -200;
}

typedef struct { Buf mix, rev, dly; } Bus;

static void stem_add(Bus *bus, Buf *stem, const char *name, double target_db, double rev, double dly)
{
    double lvl = active_rms_db(stem);
    double g = db2lin(target_db - lvl);
    for (long i = 0; i < stem->n; i++) {
        double l = stem->l[i] * g, r = (stem->r ? stem->r[i] : stem->l[i]) * g;
        bus->mix.l[i] += (float)l; bus->mix.r[i] += (float)r;
        bus->rev.l[i] += (float)(l * rev); bus->rev.r[i] += (float)(r * rev);
        bus->dly.l[i] += (float)(l * dly); bus->dly.r[i] += (float)(r * dly);
    }
    printf("    stem %-6s active %6.1f dBFS -> %6.1f\n", name, lvl, target_db);
    buf_free(stem);
}

static void mono_to_stereo_trem(Buf *dst, const float *mono, double rate_per_beat, double depth)
{
    for (long i = 0; i < dst->n; i++) {
        double ph = TAU * rate_per_beat * (double)i / BEAT;
        double s = sin(ph);
        dst->l[i] += (float)(mono[i] * (1.0 - depth * (0.5 + 0.5 * s)));
        dst->r[i] += (float)(mono[i] * (1.0 - depth * (0.5 - 0.5 * s)));
    }
}

static double hum(Rng *r, double ms) { return rng_bi(r) * ms / 1000.0 * SR; }

/* ------------------------------------------------------------- the render */

static Buf render_song(const Song *s, uint64_t seed)
{
    long L = (long)s->n * BAR, T = secs(5.0), N = L + T;
    Bus bus = { buf_new(N, 2), buf_new(N, 2), buf_new(N, 2) };
    Rng r;

    /* ---- kick ---- */
    Buf st = buf_new(N, 2);
    rng_seed(&r, seed + 1);
    for (int b = 0; b < s->n; b++) {
        const Bar *bar = &s->bar[b];
        long b0 = (long)b * BAR;
        if (bar->part == P_FULL) {
            v_kick(&st, b0, 1.0, 0);
            v_kick(&st, b0 + 8 * STEP, 0.95, 0);
            if (b % 2 == 1) v_kick(&st, b0 + 10 * STEP, 0.6, 0);
            if (bar->flags & F_FILL) v_kick(&st, b0 + 14 * STEP, 0.7, 0);
        } else if (bar->part == P_INTRO) {
            v_kick(&st, b0, 0.65, 0);
            v_kick(&st, b0 + 8 * STEP, 0.5, 0);
        } else if (bar->flags & F_RISER) {
            for (int q = 0; q < 4; q++) v_kick(&st, b0 + q * BEAT, 0.3 + 0.15 * q, 0);
        }
    }
    stem_add(&bus, &st, "kick", -17.0, 0.02, 0.0);

    /* ---- snare / clap / rim ---- */
    st = buf_new(N, 2);
    rng_seed(&r, seed + 2);
    for (int b = 0; b < s->n; b++) {
        const Bar *bar = &s->bar[b];
        long b0 = (long)b * BAR;
        if (bar->part == P_FULL) {
            for (int q = 4; q < 16; q += 8) {
                if ((bar->flags & F_FILL) && q == 12) continue;
                v_snare(&st, b0 + q * STEP + (long)hum(&r, 1.5), 0.9, 0, &r);
                v_clap(&st, b0 + q * STEP + (long)hum(&r, 2), 0.5, 0.1, &r);
            }
            if (bar->flags & F_FILL)
                for (int q = 12; q < 16; q++) v_snare(&st, b0 + q * STEP, 0.45 + 0.12 * (q - 12), 0, &r);
        } else {
            v_rim(&st, b0 + 12 * STEP + (long)hum(&r, 2), 0.7, -0.1, &r);
            if (bar->part == P_INTRO) {
                v_rim(&st, b0 + 4 * STEP + (long)hum(&r, 2), 0.6, -0.1, &r);
                if (b % 2 == 1) v_rim(&st, b0 + 7 * STEP + (long)hum(&r, 2), 0.35, -0.1, &r);
            }
        }
    }
    stem_add(&bus, &st, "snare", -21.0, 0.2, 0.0);

    /* ---- hats + shaker ---- */
    st = buf_new(N, 2);
    rng_seed(&r, seed + 3);
    static const double HV[4] = { 0.9, 0.35, 0.6, 0.35 };
    for (int b = 0; b < s->n; b++) {
        const Bar *bar = &s->bar[b];
        long b0 = (long)b * BAR;
        for (int q = 0; q < 16; q++) {
            long at = b0 + q * STEP + (long)hum(&r, 2);
            double vj = rng_range(&r, 0.9, 1.1);
            if (bar->part == P_FULL) {
                if ((bar->flags & F_FILL) && q >= 8) continue;
                if (q == 14 && b % 2 == 1) v_hat(&st, at, 0.6 * vj, 0.25, 0.2, &r);
                else v_hat(&st, at, HV[q % 4] * vj, 0.25, 0.03, &r);
            } else {
                if (bar->part == P_INTRO && q % 2 == 0) v_hat(&st, at, 0.45 * vj, 0.25, 0.03, &r);
                v_shaker(&st, at, (q % 2 ? 0.55 : 0.3) * vj, -0.3, &r);
            }
        }
    }
    stem_add(&bus, &st, "hats", -28.0, 0.06, 0.0);

    /* ---- fx: crash, riser, fill toms ---- */
    st = buf_new(N, 2);
    rng_seed(&r, seed + 4);
    for (int b = 0; b < s->n; b++) {
        const Bar *bar = &s->bar[b];
        long b0 = (long)b * BAR;
        if (bar->flags & F_CRASH) v_crash(&st, b0, 1.0, 1.4, &r);
        if (bar->flags & F_RISER) v_sweep(&st, b0, (double)BAR / SR, 300, 6000, 1.5, 0.6, -0.5, 0.5, 1, &r);
        if (bar->flags & F_FILL) {
            v_tom(&st, b0 + 8 * STEP, 196, 0.8, 0.3, &r);
            v_tom(&st, b0 + 10 * STEP, 147, 0.8, 0.0, &r);
            v_tom(&st, b0 + 11 * STEP, 110, 0.8, -0.3, &r);
        }
    }
    stem_add(&bus, &st, "fx", -28.0, 0.2, 0.0);

    /* ---- bass ---- */
    st = buf_new(N, 2);
    {
        float *m = calloc((size_t)N, sizeof(float));
        rng_seed(&r, seed + 5);
        static const int BP[8] = { 0, 0, 12, 0, 0, 0, 12, 7 };   /* per eighth */
        for (int b = 0; b < s->n; b++) {
            const Bar *bar = &s->bar[b];
            long b0 = (long)b * BAR;
            if (bar->part == P_FULL) {
                for (int e = 0; e < 8; e++) {
                    const Chord *c = chord_at(s, b, e * 2);
                    double vel = (e % 2 ? 0.7 : 0.95) * rng_range(&r, 0.95, 1.05);
                    bass_note(m, N, b0 + e * 2 * STEP, 2 * STEP - secs(0.02), c->bass + BP[e], vel);
                }
            } else if (bar->part == P_INTRO) {
                bass_note(m, N, b0, 7 * STEP, CHORDS[bar->ch[0]].bass, 0.85);
                bass_note(m, N, b0 + 8 * STEP, 5 * STEP, CHORDS[bar->ch[1]].bass, 0.75);
                bass_note(m, N, b0 + 14 * STEP, 2 * STEP - secs(0.02), CHORDS[bar->ch[1]].bass + 7, 0.6);
            } else {
                bass_note(m, N, b0, 12 * STEP, CHORDS[bar->ch[0]].bass, 0.8);
                bass_note(m, N, b0 + 12 * STEP, 4 * STEP - secs(0.02), CHORDS[bar->ch[1]].bass + 7, 0.6);
            }
        }
        buf_mix(&st, 0, m, N, 1.0, 0);
        free(m);
    }
    stem_add(&bus, &st, "bass", -17.0, 0.0, 0.0);

    /* ---- electric piano ---- */
    st = buf_new(N, 2);
    {
        float *m = calloc((size_t)N, sizeof(float));
        rng_seed(&r, seed + 6);
        static const int HIT[3][2] = { { 0, 5 }, { 6, 2 }, { 10, 5 } };
        static const double HVEL[3] = { 0.85, 0.6, 0.75 };
        for (int b = 0; b < s->n; b++) {
            const Bar *bar = &s->bar[b];
            long b0 = (long)b * BAR;
            if (bar->part == P_FULL) {
                for (int h = 0; h < 3; h++) {
                    const Chord *c = chord_at(s, b, HIT[h][0]);
                    for (int k = 0; k < 4; k++)
                        ep_note(m, N, b0 + HIT[h][0] * STEP + (long)hum(&r, 3) + k * secs(0.006),
                                HIT[h][1] * STEP, c->v[k], HVEL[h] * rng_range(&r, 0.9, 1.05));
                }
            } else {
                int halves = bar->part == P_INTRO ? 2 : 1;
                for (int h = 0; h < halves; h++) {
                    const Chord *c = &CHORDS[bar->ch[h]];
                    for (int k = 0; k < 4; k++)
                        ep_note(m, N, b0 + h * 8 * STEP + k * secs(0.012), (16 / halves - 1) * STEP,
                                c->v[k], 0.7 * rng_range(&r, 0.9, 1.05));
                }
            }
        }
        mono_to_stereo_trem(&st, m, 2.0, 0.35);
        free(m);
    }
    stem_add(&bus, &st, "piano", -19.0, 0.25, 0.08);

    /* ---- pad: one held note per voice while the chord does not change ---- */
    st = buf_new(N, 2);
    rng_seed(&r, seed + 7);
    {
        int prev = -1;
        long start = 0;
        for (int h = 0; h <= s->n * 2; h++) {
            int ch = h < s->n * 2 ? s->bar[h / 2].ch[h % 2] : -1;
            if (ch != prev) {
                if (prev >= 0)
                    for (int k = 0; k < 4; k++)
                        pad_note(&st, start, (long)h * 8 * STEP - start, CHORDS[prev].v[k] + 12, 0.5, &r);
                prev = ch;
                start = (long)h * 8 * STEP;
            }
        }
        /* sidechain-style pump in the full sections, locked to the beat */
        for (long i = 0; i < N; i++) {
            int b = (int)(i / BAR);
            if (b >= s->n || s->bar[b].part != P_FULL) continue;
            double since = (double)(i % BEAT) / SR;
            double g = 1.0 - 0.35 * exp(-since / 0.09);
            st.l[i] = (float)(st.l[i] * g);
            st.r[i] = (float)(st.r[i] * g);
        }
    }
    stem_add(&bus, &st, "pad", -25.0, 0.35, 0.0);

    /* ---- arp ---- */
    st = buf_new(N, 2);
    {
        float *m = calloc((size_t)N, sizeof(float));
        rng_seed(&r, seed + 8);
        static const int SEQ[8] = { 0, 1, 2, 3, 4, 3, 2, 1 };
        for (int b = 0; b < s->n; b++) {
            const Bar *bar = &s->bar[b];
            if (bar->part == P_INTRO) continue;
            long b0 = (long)b * BAR;
            for (int q = 0; q < 16; q++) {
                const Chord *c = chord_at(s, b, q);
                int idx = SEQ[(q + (b % 2) * 3) % 8];
                int note = idx < 4 ? c->v[idx] + 12 : c->v[0] + 24;
                double vel = (q % 4 == 0 ? 0.9 : 0.65) * rng_range(&r, 0.92, 1.05);
                if (bar->part == P_BRIDGE) vel *= 0.8;
                pluck_note(m, N, b0 + q * STEP, STEP - secs(0.01), note, vel);
            }
        }
        buf_mix(&st, 0, m, N, 1.0, -0.15);
        free(m);
    }
    stem_add(&bus, &st, "arp", -25.0, 0.3, 0.5);

    /* ---- lead ---- */
    st = buf_new(N, 2);
    {
        LeadNote *ln = calloc(1024, sizeof *ln);
        int nn = 0;
        rng_seed(&r, seed + 9);
        for (int b = 0; b < s->n; b++) {
            const Bar *bar = &s->bar[b];
            if (!bar->mel) continue;
            const MelNote *mel = bar->mel == 1 ? MEL_A : MEL_B;
            int cnt = bar->mel == 1 ? (int)(sizeof MEL_A / sizeof MEL_A[0]) : (int)(sizeof MEL_B / sizeof MEL_B[0]);
            for (int k = 0; k < cnt; k++) {
                if (mel[k].bar != bar->mel_bar) continue;
                ln[nn].at = (long)b * BAR + mel[k].step * STEP + (long)fabs(hum(&r, 4));
                ln[nn].len = mel[k].len * STEP - secs(0.03);
                ln[nn].midi = mel[k].note;
                ln[nn].vel = rng_range(&r, 0.85, 1.0);
                nn++;
            }
        }
        render_lead(&st, ln, nn, 0.05);
        free(ln);
    }
    stem_add(&bus, &st, "lead", -18.5, 0.3, 0.3);

    /* ---- returns ---- */
    Buf wet = buf_new(N, 2);
    pingpong(bus.dly.l, bus.dly.r, wet.l, wet.r, N, 3 * STEP, 0.38, 3500);
    /* the delay also feeds the reverb, a little */
    for (long i = 0; i < N; i++) { bus.rev.l[i] += wet.l[i] * 0.3f; bus.rev.r[i] += wet.r[i] * 0.3f; }
    double dl = active_rms_db(&wet), dg = db2lin(-27.0 - dl);
    for (long i = 0; i < N; i++) { bus.mix.l[i] += (float)(wet.l[i] * dg); bus.mix.r[i] += (float)(wet.r[i] * dg); }
    printf("    return delay  %6.1f dBFS -> %6.1f\n", dl, -27.0);
    buf_free(&wet);

    wet = buf_new(N, 2);
    Reverb *rv = rev_new(0.78, 0.7, 1.0, 0.02);
    rev_process(rv, bus.rev.l, bus.rev.r, wet.l, wet.r, N);
    rev_free(rv);
    /* a warm plate, not a bright hall: the return is rolled off above 6 kHz */
    Biquad wl;
    bq_lp(&wl, 6000, 0.707);
    bq_apply(wl, wet.l, N);
    bq_apply(wl, wet.r, N);
    double rl = active_rms_db(&wet), rg = db2lin(-24.0 - rl);
    for (long i = 0; i < N; i++) { bus.mix.l[i] += (float)(wet.l[i] * rg); bus.mix.r[i] += (float)(wet.r[i] * rg); }
    printf("    return reverb %6.1f dBFS -> %6.1f\n", rl, -24.0);
    buf_free(&wet);
    buf_free(&bus.rev);
    buf_free(&bus.dly);

    /* ---- fold the tail onto the start: the loop seam ---- */
    Buf out = buf_new(L, 2);
    for (long i = 0; i < L; i++) { out.l[i] = bus.mix.l[i]; out.r[i] = bus.mix.r[i]; }
    for (long i = 0; i < T; i++) { out.l[i] += bus.mix.l[L + i]; out.r[i] += bus.mix.r[L + i]; }
    buf_free(&bus.mix);
    return out;
}

/* Run a filter around the loop twice and keep the second pass, so its state
   at the start of the kept pass is the state it has at the end: no seam. */
static void loop_filter(Buf *b, Biquad q)
{
    for (int c = 0; c < b->ch; c++) {
        float *x = c ? b->r : b->l;
        Biquad s = q;
        for (long i = 0; i < b->n; i++) (void)bq_run(&s, x[i]);
        for (long i = 0; i < b->n; i++) x[i] = (float)bq_run(&s, x[i]);
    }
}

/* Master: high-pass, a gentle soft-saturation "glue", then gain to a
   modest RMS with the peak kept well down, leaving room for the game's
   stingers on top. No limiter: the music is quiet, not squashed. */
static void master(Buf *b, double rms_target, double peak_ceiling)
{
    Biquad hp;
    bq_hp(&hp, 30, 0.707);
    loop_filter(b, hp);
    double pk = buf_peak(b);
    for (long i = 0; i < b->n; i++) {
        b->l[i] = (float)(tanh(1.2 * b->l[i] / pk) * pk / 1.2);
        b->r[i] = (float)(tanh(1.2 * b->r[i] / pk) * pk / 1.2);
    }
    double g = db2lin(rms_target - rms_db(b));
    double gp = db2lin(peak_ceiling) / buf_peak(b);
    buf_scale(b, g < gp ? g : gp);
}

Buf music_lounge(void)
{
    Song s;
    song_lounge(&s);
    Buf b = render_song(&s, hash_str("lounge"));
    master(&b, -19.0, -3.0);
    return b;
}

Buf music_attract(void)
{
    Song s;
    song_attract(&s);
    Buf b = render_song(&s, hash_str("attract"));
    master(&b, -18.0, -3.0);
    return b;
}

/* ------------------------------------------------------------- ambience */

/* A lounge full of people: sixteen talkers made of a glottal buzz shaped by
   moving formants, speaking in syllables and phrases, far enough away and
   reverberant enough that nothing is intelligible - walla, not words. */
static const double VOWELS[6][3] = {
    { 730, 1090, 2440 }, { 530, 1840, 2480 }, { 270, 2290, 3010 },
    { 570, 840, 2410 }, { 300, 870, 2240 }, { 640, 1190, 2390 },
};

static void talker(Buf *b, Rng *r, double f0base, double gain, double pan, double lp_hz)
{
    long n = b->n;
    float *m = calloc((size_t)n, sizeof(float));
    if (!m) abort();
    Osc o = { rng_uni(r) };
    Svf f1 = { 0, 0 }, f2 = { 0, 0 }, f3 = { 0, 0 };
    Smooth sf1, sf2, sf3, sp, sa;
    smooth_init(&sf1, 25, 500); smooth_init(&sf2, 25, 1500); smooth_init(&sf3, 25, 2500);
    smooth_init(&sp, 6, f0base); smooth_init(&sa, 40, 0);
    Biquad glot;
    bq_lp(&glot, 900, 0.6);
    long i = (long)(rng_uni(r) * SR * 3);
    while (i < n) {
        /* one phrase of syllables, then a pause */
        double phrase = rng_range(r, 1.2, 5.0), t = 0, decl = rng_range(r, 1.05, 1.15);
        while (t < phrase && i < n) {
            double syl = rng_range(r, 0.11, 0.28), amp = rng_range(r, 0.55, 1.0);
            const double *vw = VOWELS[rng_int(r, 6)];
            double pf = f0base * decl * (1.0 - 0.1 * t / phrase) * rng_range(r, 0.92, 1.08);
            long len = secs(syl);
            if (rng_uni(r) < 0.4) v_noise(b, i, gain * 0.05, pan, 0.002, 0.02, NF_HP, 3500, 0.7, r);
            for (long k = 0; k < len && i < n; k++, i++) {
                double x = (double)k / len;
                double env = pow(sin(PI * x), 0.6) * amp;
                double a = smooth_step(&sa, env);
                double p = smooth_step(&sp, pf);
                double src = bq_run(&glot, osc_saw(&o, p));
                double b1, b2, b3;
                svf_step(&f1, src, smooth_step(&sf1, vw[0]), 6, NULL, &b1, NULL);
                svf_step(&f2, src, smooth_step(&sf2, vw[1]), 9, NULL, &b2, NULL);
                svf_step(&f3, src, smooth_step(&sf3, vw[2]), 12, NULL, &b3, NULL);
                m[i] = (float)((b1 + 0.6 * b2 + 0.25 * b3) * a);
            }
            long gap = secs(rng_range(r, 0.0, 0.05));
            for (long k = 0; k < gap && i < n; k++, i++) (void)smooth_step(&sa, 0);
            t += syl + (double)gap / SR;
        }
        long pause = secs(rng_range(r, 0.6, 3.5));
        for (long k = 0; k < pause && i < n; k++, i++) (void)smooth_step(&sa, 0);
    }
    Biquad lp;
    bq_lp(&lp, lp_hz, 0.7);
    bq_apply(lp, m, n);
    buf_mix(b, 0, m, n, gain, pan);
    free(m);
}

Buf ambience_lounge(double seconds)
{
    Rng r;
    rng_seed(&r, hash_str("ambience"));
    long P = secs(3.0), L = secs(seconds), X = secs(3.0), N = P + L + X;
    Buf b = buf_new(N, 2);
    for (int k = 0; k < 16; k++) {
        int female = k % 2;
        double f0 = female ? rng_range(&r, 170, 230) : rng_range(&r, 95, 135);
        talker(&b, &r, f0, rng_range(&r, 0.3, 1.0), rng_range(&r, -0.9, 0.9), rng_range(&r, 2200, 4500));
    }
    for (int k = 0; k < 7; k++)
        v_glass(&b, P + (long)(rng_uni(&r) * (L + X - secs(1))), rng_range(&r, 0.08, 0.18), rng_range(&r, -0.8, 0.8), &r);
    buf_reverb(&b, 0.85, 0.6, 1.0, 0.02, 1.4);
    /* room tone */
    Pink pl = { 0 }, pr = { 0 };
    Biquad lpl, lpr;
    bq_lp(&lpl, 700, 0.7);
    bq_lp(&lpr, 700, 0.7);
    for (long i = 0; i < N; i++) {
        b.l[i] += (float)(bq_run(&lpl, pink_step(&pl, rng_bi(&r))) * 0.02);
        b.r[i] += (float)(bq_run(&lpr, pink_step(&pr, rng_bi(&r))) * 0.02);
    }
    Biquad hp;
    bq_hp(&hp, 110, 0.7);
    bq_apply(hp, b.l, N);
    bq_apply(hp, b.r, N);
    Biquad lp;
    bq_lp(&lp, 5000, 0.7);
    bq_apply(lp, b.l, N);
    bq_apply(lp, b.r, N);

    /* drop the pre-roll, then crossfade the overrun onto the start so the
       end flows into the beginning (equal power: the two are uncorrelated) */
    Buf out = buf_new(L, 2);
    for (long i = 0; i < L; i++) { out.l[i] = b.l[P + i]; out.r[i] = b.r[P + i]; }
    for (long i = 0; i < X; i++) {
        double th = 0.5 * PI * (double)i / X;
        double gi = sin(th), go = cos(th);
        out.l[i] = (float)(out.l[i] * gi + b.l[P + L + i] * go);
        out.r[i] = (float)(out.r[i] * gi + b.r[P + L + i] * go);
    }
    buf_free(&b);
    buf_scale(&out, db2lin(-27.0 - rms_db(&out)));
    return out;
}
