/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* dsp.h - the small offline synthesiser behind every sound in the game.
 *
 * Everything here runs offline, in double precision, at 44.1 kHz. None of it
 * is linked into the game. The only requirement beyond "sounds good" is that
 * it is deterministic: the same source and toolchain always produce the same
 * samples, so the shipped assets can be regenerated bit-identically. That is
 * why randomness comes only from the seeded Rng below and never from rand().
 */
#ifndef AUDIOGEN_DSP_H
#define AUDIOGEN_DSP_H

#include <stdint.h>
#include <stddef.h>

#define SR 44100
#define PI 3.14159265358979323846
#define TAU (2.0 * PI)

/* ---- deterministic randomness (splitmix64) ---- */
typedef struct { uint64_t s; } Rng;
void     rng_seed(Rng *r, uint64_t seed);
uint64_t rng_u64(Rng *r);
double   rng_uni(Rng *r);                       /* [0,1)   */
double   rng_bi(Rng *r);                        /* [-1,1)  */
double   rng_range(Rng *r, double a, double b); /* [a,b)   */
int      rng_int(Rng *r, int n);                /* [0,n)   */
uint64_t hash_str(const char *s);               /* FNV-1a, for per-sound seeds */

/* ---- buffers: mono (r == NULL) or stereo ---- */
typedef struct { float *l, *r; long n; int ch; } Buf;
Buf  buf_new(long n, int ch);
void buf_free(Buf *b);
/* Add one sample; pan -1 (left) .. +1 (right), equal power. Mono ignores pan. */
void buf_add(Buf *b, long i, double v, double pan);
void buf_add2(Buf *b, long i, double vl, double vr);
/* Mix a mono source into b at offset `at`. */
void buf_mix(Buf *b, long at, const float *src, long n, double gain, double pan);
void buf_scale(Buf *b, double g);
double buf_peak(const Buf *b);

/* ---- helpers ---- */
double mtof(double midi);
double db2lin(double db);
double lin2db(double lin);
long   secs(double s);                   /* seconds -> samples */
double clampd(double x, double a, double b);
double smoothstep01(double x);

/* ---- band-limited oscillators (PolyBLEP) ---- */
typedef struct { double ph; } Osc;
double osc_sin(Osc *o, double f);
double osc_saw(Osc *o, double f);
double osc_sqr(Osc *o, double f, double pw);
double osc_tri(Osc *o, double f);

/* ---- filters ---- */
typedef struct { double b0, b1, b2, a1, a2, z1, z2; } Biquad;
void   bq_lp(Biquad *q, double f, double Q);
void   bq_hp(Biquad *q, double f, double Q);
void   bq_bp(Biquad *q, double f, double Q);            /* 0 dB peak gain */
void   bq_peak(Biquad *q, double f, double Q, double db);
void   bq_lowshelf(Biquad *q, double f, double db);
void   bq_highshelf(Biquad *q, double f, double db);
double bq_run(Biquad *q, double x);
void   bq_apply(Biquad q, float *x, long n);            /* fresh state copy */

/* Zero-delay-feedback state variable filter, for per-sample cutoff sweeps. */
typedef struct { double ic1, ic2; } Svf;
void svf_step(Svf *s, double x, double fc, double Q, double *lp, double *bp, double *hp);

/* Pink noise (Paul Kellet's refined filter). */
typedef struct { double b0, b1, b2, b3, b4, b5, b6; } Pink;
double pink_step(Pink *p, double white);

/* One-pole smoothing, used for random-walk control signals. */
typedef struct { double y, a; } Smooth;
void   smooth_init(Smooth *s, double hz, double y0);
double smooth_step(Smooth *s, double x);

/* ---- Freeverb-style stereo reverb (wet output only) ---- */
typedef struct Reverb Reverb;
Reverb *rev_new(double room, double damp, double width, double predelay_s);
void    rev_process(Reverb *rv, const float *inl, const float *inr,
                    float *outl, float *outr, long n);
void    rev_free(Reverb *rv);
/* Convenience: add reverb of b (wet level `wet`) back into b. */
void    buf_reverb(Buf *b, double room, double damp, double width,
                   double predelay_s, double wet);

/* Stereo ping-pong delay, wet only, into outl/outr (added). */
void pingpong(const float *inl, const float *inr, float *outl, float *outr,
              long n, long dly, double fb, double lp_hz);

/* ---- loudness (ITU-R BS.1770 K-weighting, short windows) ---- */
/* Maximum K-weighted loudness over `win_s` windows (10 ms hop), in LUFS.
 * A mono buffer counts as played on both channels, as raylib plays it. */
double loud_max(const Buf *b, double win_s);
/* Mean-square of a buffer (both channels), in dBFS. */
double rms_db(const Buf *b);

/* ---- finishing ---- */
void   fx_dcblock(Buf *b);                          /* 2nd-order HP, 18 Hz  */
void   fx_trim(Buf *b, double start_db, double end_db, double pad_ms);
void   fx_fade(Buf *b, double in_ms, double out_ms);
/* Gain to a loudness target, then keep the peak under ceil_db. Up to
 * clip_db of the excess is taken by a soft knee; the rest by lowering gain. */
void   fx_normalize(Buf *b, double target_lufs, double ceil_db, double clip_db);
int    wav_write(const char *path, const Buf *b);  /* 16-bit PCM, rounded   */

#endif
