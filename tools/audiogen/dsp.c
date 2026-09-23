/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* dsp.c - see dsp.h. */
#include "dsp.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ rng */

void rng_seed(Rng *r, uint64_t seed) { r->s = seed ^ 0x6A09E667F3BCC909ull; }

uint64_t rng_u64(Rng *r)
{
    uint64_t z = (r->s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

double rng_uni(Rng *r) { return (double)(rng_u64(r) >> 11) * (1.0 / 9007199254740992.0); }
double rng_bi(Rng *r) { return rng_uni(r) * 2.0 - 1.0; }
double rng_range(Rng *r, double a, double b) { return a + (b - a) * rng_uni(r); }
int rng_int(Rng *r, int n) { return (int)(rng_uni(r) * n); }

uint64_t hash_str(const char *s)
{
    uint64_t h = 1469598103934665603ull;
    while (*s) { h ^= (unsigned char)*s++; h *= 1099511628211ull; }
    return h;
}

/* -------------------------------------------------------------- buffers */

Buf buf_new(long n, int ch)
{
    Buf b;
    b.n = n;
    b.ch = ch;
    b.l = calloc((size_t)n, sizeof(float));
    b.r = ch == 2 ? calloc((size_t)n, sizeof(float)) : NULL;
    if (!b.l || (ch == 2 && !b.r)) { fprintf(stderr, "audiogen: out of memory\n"); exit(1); }
    return b;
}

void buf_free(Buf *b)
{
    free(b->l);
    free(b->r);
    b->l = b->r = NULL;
    b->n = 0;
}

void buf_add(Buf *b, long i, double v, double pan)
{
    if (i < 0 || i >= b->n) return;
    if (b->ch == 1) { b->l[i] += (float)v; return; }
    double a = (clampd(pan, -1, 1) + 1.0) * PI / 4.0;
    b->l[i] += (float)(v * cos(a));
    b->r[i] += (float)(v * sin(a));
}

void buf_add2(Buf *b, long i, double vl, double vr)
{
    if (i < 0 || i >= b->n) return;
    if (b->ch == 1) { b->l[i] += (float)(0.5 * (vl + vr)); return; }
    b->l[i] += (float)vl;
    b->r[i] += (float)vr;
}

void buf_mix(Buf *b, long at, const float *src, long n, double gain, double pan)
{
    double a = (clampd(pan, -1, 1) + 1.0) * PI / 4.0;
    double gl = b->ch == 2 ? gain * cos(a) : gain, gr = gain * sin(a);
    for (long i = 0; i < n; i++) {
        long j = at + i;
        if (j < 0) continue;
        if (j >= b->n) break;
        b->l[j] += (float)(src[i] * gl);
        if (b->ch == 2) b->r[j] += (float)(src[i] * gr);
    }
}

void buf_scale(Buf *b, double g)
{
    for (long i = 0; i < b->n; i++) {
        b->l[i] = (float)(b->l[i] * g);
        if (b->r) b->r[i] = (float)(b->r[i] * g);
    }
}

double buf_peak(const Buf *b)
{
    double p = 0;
    for (long i = 0; i < b->n; i++) {
        if (fabs(b->l[i]) > p) p = fabs(b->l[i]);
        if (b->r && fabs(b->r[i]) > p) p = fabs(b->r[i]);
    }
    return p;
}

/* -------------------------------------------------------------- helpers */

double mtof(double m) { return 440.0 * pow(2.0, (m - 69.0) / 12.0); }
double db2lin(double db) { return pow(10.0, db / 20.0); }
double lin2db(double x) { return x > 1e-12 ? 20.0 * log10(x) : -240.0; }
long secs(double s) { return (long)floor(s * SR + 0.5); }
double clampd(double x, double a, double b) { return x < a ? a : (x > b ? b : x); }
double smoothstep01(double x) { x = clampd(x, 0, 1); return x * x * (3 - 2 * x); }

/* ---------------------------------------------------------- oscillators */

/* PolyBLEP residual: removes the step discontinuity's aliasing. */
static double blep(double t, double dt)
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0; }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
    return 0.0;
}

static void osc_adv(Osc *o, double dt)
{
    o->ph += dt;
    o->ph -= floor(o->ph);
}

double osc_sin(Osc *o, double f)
{
    double v = sin(TAU * o->ph);
    osc_adv(o, f / SR);
    return v;
}

double osc_saw(Osc *o, double f)
{
    double dt = f / SR;
    double v = 2.0 * o->ph - 1.0 - blep(o->ph, dt);
    osc_adv(o, dt);
    return v;
}

double osc_sqr(Osc *o, double f, double pw)
{
    double dt = f / SR;
    double t2 = o->ph + (1.0 - pw);
    t2 -= floor(t2);
    double v = (o->ph < pw ? 1.0 : -1.0) + blep(o->ph, dt) - blep(t2, dt);
    osc_adv(o, dt);
    return v;
}

double osc_tri(Osc *o, double f)
{
    double v = o->ph < 0.5 ? 4.0 * o->ph - 1.0 : 3.0 - 4.0 * o->ph;
    osc_adv(o, f / SR);
    return v;
}

/* -------------------------------------------------------------- filters */

static void bq_set(Biquad *q, double b0, double b1, double b2, double a0, double a1, double a2)
{
    q->b0 = b0 / a0; q->b1 = b1 / a0; q->b2 = b2 / a0;
    q->a1 = a1 / a0; q->a2 = a2 / a0;
    q->z1 = q->z2 = 0;
}

static double bq_w(double f) { return TAU * clampd(f, 5.0, SR * 0.49) / SR; }

void bq_lp(Biquad *q, double f, double Q)
{
    double w = bq_w(f), c = cos(w), al = sin(w) / (2 * Q);
    bq_set(q, (1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
}

void bq_hp(Biquad *q, double f, double Q)
{
    double w = bq_w(f), c = cos(w), al = sin(w) / (2 * Q);
    bq_set(q, (1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
}

void bq_bp(Biquad *q, double f, double Q)
{
    double w = bq_w(f), c = cos(w), al = sin(w) / (2 * Q);
    bq_set(q, al, 0, -al, 1 + al, -2 * c, 1 - al);
}

void bq_peak(Biquad *q, double f, double Q, double db)
{
    double A = pow(10, db / 40), w = bq_w(f), c = cos(w), al = sin(w) / (2 * Q);
    bq_set(q, 1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
}

void bq_lowshelf(Biquad *q, double f, double db)
{
    double A = pow(10, db / 40), w = bq_w(f), c = cos(w), s = sin(w);
    double al = s / 2 * sqrt(2.0), sa = 2 * sqrt(A) * al;
    bq_set(q, A * ((A + 1) - (A - 1) * c + sa), 2 * A * ((A - 1) - (A + 1) * c),
           A * ((A + 1) - (A - 1) * c - sa), (A + 1) + (A - 1) * c + sa,
           -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sa);
}

void bq_highshelf(Biquad *q, double f, double db)
{
    double A = pow(10, db / 40), w = bq_w(f), c = cos(w), s = sin(w);
    double al = s / 2 * sqrt(2.0), sa = 2 * sqrt(A) * al;
    bq_set(q, A * ((A + 1) + (A - 1) * c + sa), -2 * A * ((A - 1) + (A + 1) * c),
           A * ((A + 1) + (A - 1) * c - sa), (A + 1) - (A - 1) * c + sa,
           2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sa);
}

double bq_run(Biquad *q, double x)
{
    double y = q->b0 * x + q->z1;
    q->z1 = q->b1 * x - q->a1 * y + q->z2;
    q->z2 = q->b2 * x - q->a2 * y;
    return y;
}

void bq_apply(Biquad q, float *x, long n)
{
    for (long i = 0; i < n; i++) x[i] = (float)bq_run(&q, x[i]);
}

void svf_step(Svf *s, double x, double fc, double Q, double *lp, double *bp, double *hp)
{
    double g = tan(PI * clampd(fc, 10.0, SR * 0.45) / SR);
    double k = 1.0 / Q;
    double a1 = 1.0 / (1.0 + g * (g + k)), a2 = g * a1, a3 = g * a2;
    double v3 = x - s->ic2;
    double v1 = a1 * s->ic1 + a2 * v3;
    double v2 = s->ic2 + a2 * s->ic1 + a3 * v3;
    s->ic1 = 2 * v1 - s->ic1;
    s->ic2 = 2 * v2 - s->ic2;
    if (lp) *lp = v2;
    if (bp) *bp = v1;
    if (hp) *hp = x - k * v1 - v2;
}

double pink_step(Pink *p, double w)
{
    p->b0 = 0.99886 * p->b0 + w * 0.0555179;
    p->b1 = 0.99332 * p->b1 + w * 0.0750759;
    p->b2 = 0.96900 * p->b2 + w * 0.1538520;
    p->b3 = 0.86650 * p->b3 + w * 0.3104856;
    p->b4 = 0.55000 * p->b4 + w * 0.5329522;
    p->b5 = -0.7616 * p->b5 - w * 0.0168980;
    double v = p->b0 + p->b1 + p->b2 + p->b3 + p->b4 + p->b5 + p->b6 + w * 0.5362;
    p->b6 = w * 0.115926;
    return v * 0.11;
}

void smooth_init(Smooth *s, double hz, double y0)
{
    s->y = y0;
    s->a = 1.0 - exp(-TAU * hz / SR);
}

double smooth_step(Smooth *s, double x) { return s->y += s->a * (x - s->y); }

/* --------------------------------------------------------------- reverb */

#define NCOMB 8
#define NALL 4
static const int COMB_T[NCOMB] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
static const int ALL_T[NALL] = { 556, 441, 341, 225 };
#define SPREAD 23

typedef struct { float *buf; int n, i; double store; } Comb;
typedef struct { float *buf; int n, i; } Allp;

struct Reverb {
    Comb cl[NCOMB], cr[NCOMB];
    Allp al[NALL], ar[NALL];
    double fb, damp, wet1, wet2;
    float *pre; long npre, ipre;
};

static float *zalloc(long n)
{
    float *p = calloc((size_t)(n > 0 ? n : 1), sizeof(float));
    if (!p) { fprintf(stderr, "audiogen: out of memory\n"); exit(1); }
    return p;
}

Reverb *rev_new(double room, double damp, double width, double predelay_s)
{
    Reverb *rv = calloc(1, sizeof *rv);
    if (!rv) exit(1);
    for (int k = 0; k < NCOMB; k++) {
        rv->cl[k].n = COMB_T[k];
        rv->cr[k].n = COMB_T[k] + SPREAD;
        rv->cl[k].buf = zalloc(rv->cl[k].n);
        rv->cr[k].buf = zalloc(rv->cr[k].n);
    }
    for (int k = 0; k < NALL; k++) {
        rv->al[k].n = ALL_T[k];
        rv->ar[k].n = ALL_T[k] + SPREAD;
        rv->al[k].buf = zalloc(rv->al[k].n);
        rv->ar[k].buf = zalloc(rv->ar[k].n);
    }
    rv->fb = room * 0.28 + 0.7;
    rv->damp = damp * 0.4;
    rv->wet1 = width / 2 + 0.5;
    rv->wet2 = (1 - width) / 2;
    rv->npre = secs(predelay_s) + 1;
    rv->pre = zalloc(rv->npre * 2);
    return rv;
}

static double comb_run(Comb *c, double x, double fb, double damp)
{
    double y = c->buf[c->i];
    c->store = y * (1 - damp) + c->store * damp;
    c->buf[c->i] = (float)(x + c->store * fb);
    if (++c->i >= c->n) c->i = 0;
    return y;
}

static double all_run(Allp *a, double x)
{
    double b = a->buf[a->i];
    double y = -x + b;
    a->buf[a->i] = (float)(x + b * 0.5);
    if (++a->i >= a->n) a->i = 0;
    return y;
}

void rev_process(Reverb *rv, const float *inl, const float *inr, float *outl, float *outr, long n)
{
    for (long i = 0; i < n; i++) {
        /* pre-delay line holds (l+r) mono input */
        double x = rv->pre[rv->ipre];
        rv->pre[rv->ipre] = (float)((inl[i] + (inr ? inr[i] : inl[i])) * 0.015);
        if (++rv->ipre >= rv->npre) rv->ipre = 0;
        double yl = 0, yr = 0;
        for (int k = 0; k < NCOMB; k++) {
            yl += comb_run(&rv->cl[k], x, rv->fb, rv->damp);
            yr += comb_run(&rv->cr[k], x, rv->fb, rv->damp);
        }
        for (int k = 0; k < NALL; k++) {
            yl = all_run(&rv->al[k], yl);
            yr = all_run(&rv->ar[k], yr);
        }
        outl[i] += (float)(yl * rv->wet1 + yr * rv->wet2);
        if (outr) outr[i] += (float)(yr * rv->wet1 + yl * rv->wet2);
    }
}

void rev_free(Reverb *rv)
{
    for (int k = 0; k < NCOMB; k++) { free(rv->cl[k].buf); free(rv->cr[k].buf); }
    for (int k = 0; k < NALL; k++) { free(rv->al[k].buf); free(rv->ar[k].buf); }
    free(rv->pre);
    free(rv);
}

void buf_reverb(Buf *b, double room, double damp, double width, double predelay_s, double wet)
{
    Reverb *rv = rev_new(room, damp, width, predelay_s);
    Buf w = buf_new(b->n, 2);
    rev_process(rv, b->l, b->r, w.l, w.r, b->n);
    for (long i = 0; i < b->n; i++) {
        if (b->ch == 2) {
            b->l[i] += (float)(w.l[i] * wet);
            b->r[i] += (float)(w.r[i] * wet);
        } else {
            b->l[i] += (float)(0.5 * (w.l[i] + w.r[i]) * wet);
        }
    }
    buf_free(&w);
    rev_free(rv);
}

void pingpong(const float *inl, const float *inr, float *outl, float *outr,
              long n, long dly, double fb, double lp_hz)
{
    float *dl = zalloc(dly), *dr = zalloc(dly);
    Biquad fl, fr;
    bq_lp(&fl, lp_hz, 0.707);
    bq_lp(&fr, lp_hz, 0.707);
    long k = 0;
    for (long i = 0; i < n; i++) {
        double yl = dl[k], yr = dr[k];
        double in = 0.5 * (inl[i] + (inr ? inr[i] : inl[i]));
        /* left is fed by the input and by the right tap, right by the left
           tap, so repeats bounce L, R, L, R. */
        dl[k] = (float)bq_run(&fl, in + yr * fb);
        dr[k] = (float)bq_run(&fr, yl);
        if (++k >= dly) k = 0;
        outl[i] += (float)yl;
        outr[i] += (float)yr;
    }
    free(dl);
    free(dr);
}

/* ------------------------------------------------------------- loudness */

static void kweight_init(Biquad *s1, Biquad *s2)
{
    /* BS.1770 pre-filter and RLB filter, re-derived for 44.1 kHz. */
    double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
    double K = tan(PI * f0 / SR), Vh = pow(10.0, G / 20.0), Vb = pow(Vh, 0.4996667741545416);
    double a0 = 1.0 + K / Q + K * K;
    s1->b0 = (Vh + Vb * K / Q + K * K) / a0;
    s1->b1 = 2.0 * (K * K - Vh) / a0;
    s1->b2 = (Vh - Vb * K / Q + K * K) / a0;
    s1->a1 = 2.0 * (K * K - 1.0) / a0;
    s1->a2 = (1.0 - K / Q + K * K) / a0;
    s1->z1 = s1->z2 = 0;
    f0 = 38.13547087602444; Q = 0.5003270373238773;
    K = tan(PI * f0 / SR);
    a0 = 1.0 + K / Q + K * K;
    s2->b0 = 1.0; s2->b1 = -2.0; s2->b2 = 1.0;
    s2->a1 = 2.0 * (K * K - 1.0) / a0;
    s2->a2 = (1.0 - K / Q + K * K) / a0;
    s2->z1 = s2->z2 = 0;
}

double loud_max(const Buf *b, double win_s)
{
    long n = b->n;
    double *cum = calloc((size_t)n + 1, sizeof(double));
    if (!cum) exit(1);
    for (int c = 0; c < b->ch; c++) {
        const float *x = c ? b->r : b->l;
        Biquad s1, s2;
        kweight_init(&s1, &s2);
        double acc = 0;
        for (long i = 0; i < n; i++) {
            double y = bq_run(&s2, bq_run(&s1, x[i]));
            acc += y * y;
            cum[i + 1] += acc;
        }
    }
    long W = secs(win_s), hop = SR / 100;
    double chmul = b->ch == 1 ? 2.0 : 1.0, best = 0;
    if (n <= W) {
        best = cum[n] / W;
    } else {
        for (long s = 0; s + W <= n; s += hop) {
            double ms = (cum[s + W] - cum[s]) / W;
            if (ms > best) best = ms;
        }
    }
    free(cum);
    best *= chmul;
    return best > 1e-20 ? -0.691 + 10.0 * log10(best) : -200.0;
}

double rms_db(const Buf *b)
{
    double acc = 0;
    for (long i = 0; i < b->n; i++) {
        acc += (double)b->l[i] * b->l[i];
        if (b->r) acc += (double)b->r[i] * b->r[i];
    }
    acc /= (double)b->n * b->ch;
    return acc > 0 ? 10 * log10(acc) : -200;
}

/* ------------------------------------------------------------ finishing */

void fx_dcblock(Buf *b)
{
    Biquad h;
    bq_hp(&h, 18.0, 0.707);
    bq_apply(h, b->l, b->n);
    if (b->r) bq_apply(h, b->r, b->n);
}

void fx_trim(Buf *b, double start_db, double end_db, double pad_ms)
{
    double pk = buf_peak(b);
    if (pk <= 0) return;
    double ts = pk * db2lin(start_db), te = pk * db2lin(end_db);
    long first = 0, last = b->n - 1;
    for (long i = 0; i < b->n; i++) {
        double v = fabs(b->l[i]);
        if (b->r && fabs(b->r[i]) > v) v = fabs(b->r[i]);
        if (v >= ts) { first = i; break; }
    }
    for (long i = b->n - 1; i >= 0; i--) {
        double v = fabs(b->l[i]);
        if (b->r && fabs(b->r[i]) > v) v = fabs(b->r[i]);
        if (v >= te) { last = i; break; }
    }
    long pad = secs(pad_ms / 1000.0);
    first = first - pad < 0 ? 0 : first - pad;
    last = last + pad >= b->n ? b->n - 1 : last + pad;
    long n = last - first + 1;
    memmove(b->l, b->l + first, (size_t)n * sizeof(float));
    if (b->r) memmove(b->r, b->r + first, (size_t)n * sizeof(float));
    b->n = n;
}

void fx_fade(Buf *b, double in_ms, double out_ms)
{
    long ni = secs(in_ms / 1000.0), no = secs(out_ms / 1000.0);
    if (ni > b->n / 2) ni = b->n / 2;
    if (no > b->n / 2) no = b->n / 2;
    /* Raised-cosine ramps that reach exactly zero on the first and last
       sample, so no file starts or ends on a step. */
    for (long i = 0; i < ni; i++) {
        double g = 0.5 - 0.5 * cos(PI * (double)i / ni);
        b->l[i] = (float)(b->l[i] * g);
        if (b->r) b->r[i] = (float)(b->r[i] * g);
    }
    for (long i = 0; i < no; i++) {
        long j = b->n - 1 - i;
        double g = 0.5 - 0.5 * cos(PI * (double)i / no);
        b->l[j] = (float)(b->l[j] * g);
        if (b->r) b->r[j] = (float)(b->r[j] * g);
    }
}

static double softclip(double x, double knee, double ceil)
{
    double a = fabs(x);
    if (a <= knee) return x;
    double w = ceil - knee;
    double y = knee + w * tanh((a - knee) / w);
    return x < 0 ? -y : y;
}

void fx_normalize(Buf *b, double target, double ceil_db, double clip_db)
{
    double L = loud_max(b, 0.1);
    buf_scale(b, db2lin(target - L));
    double ceil = db2lin(ceil_db) * 0.998;
    double pk = buf_peak(b);
    if (pk <= ceil) return;
    double excess = lin2db(pk / ceil);
    if (excess > clip_db) {
        buf_scale(b, db2lin(-(excess - clip_db)));
        excess = clip_db;
    }
    if (excess <= 0) return;
    double knee = ceil * db2lin(-2.0 * excess);
    for (long i = 0; i < b->n; i++) {
        b->l[i] = (float)softclip(b->l[i], knee, ceil);
        if (b->r) b->r[i] = (float)softclip(b->r[i], knee, ceil);
    }
}

static void put16(FILE *f, unsigned v) { fputc((int)(v & 255), f); fputc((int)((v >> 8) & 255), f); }
static void put32(FILE *f, unsigned long v) { put16(f, (unsigned)(v & 0xFFFF)); put16(f, (unsigned)(v >> 16)); }

int wav_write(const char *path, const Buf *b)
{
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return -1; }
    unsigned long data = (unsigned long)b->n * b->ch * 2;
    fwrite("RIFF", 1, 4, f); put32(f, 36 + data); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); put32(f, 16); put16(f, 1); put16(f, (unsigned)b->ch);
    put32(f, SR); put32(f, (unsigned long)SR * b->ch * 2); put16(f, (unsigned)b->ch * 2); put16(f, 16);
    fwrite("data", 1, 4, f); put32(f, data);
    for (long i = 0; i < b->n; i++) {
        for (int c = 0; c < b->ch; c++) {
            double v = (c ? b->r[i] : b->l[i]) * 32767.0;
            long s = lround(clampd(v, -32768.0, 32767.0));
            put16(f, (unsigned)(s & 0xFFFF));
        }
    }
    return fclose(f);
}
