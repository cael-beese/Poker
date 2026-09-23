/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* analyze.c - level and click report for 16-bit WAV files.
 *
 * usage: analyze [--loop] FILE.wav...
 *
 * Per file: duration, peak and RMS in dBFS, the loudest 100 ms window
 * K-weighted (the loudness figure the generator normalises to), DC offset,
 * and the edge values in LSB: a file that does not start and end at (near)
 * zero clicks when it starts or is cut off. With --loop, the seam is also
 * checked: the jump from the last sample back to the first, compared with
 * the largest sample-to-sample step in the 4096 samples either side of the
 * seam (a seam that is a step stands out against those) and, for scale, the
 * 99.9th percentile step over the whole file. Exit status 1 on a bad seam.
 */
#include "dsp.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned rd16(const unsigned char *p) { return p[0] | (p[1] << 8); }
static unsigned long rd32(const unsigned char *p) { return rd16(p) | ((unsigned long)rd16(p + 2) << 16); }

static int load(const char *path, Buf *out, int *rate)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return -1; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *d = malloc((size_t)sz);
    if (!d || fread(d, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); free(d); return -1; }
    fclose(f);
    if (sz < 12 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "WAVE", 4)) { free(d); return -1; }
    int ch = 0, bits = 0;
    long pos = 12;
    while (pos + 8 <= sz) {
        unsigned long len = rd32(d + pos + 4);
        if (!memcmp(d + pos, "fmt ", 4)) {
            ch = (int)rd16(d + pos + 10);
            *rate = (int)rd32(d + pos + 12);
            bits = (int)rd16(d + pos + 22);
        } else if (!memcmp(d + pos, "data", 4)) {
            if (bits != 16 || ch < 1 || ch > 2) { free(d); return -1; }
            long frames = (long)(len / (2u * ch));
            *out = buf_new(frames, ch);
            const unsigned char *p = d + pos + 8;
            for (long i = 0; i < frames; i++)
                for (int c = 0; c < ch; c++) {
                    int v = (int)(short)rd16(p + 2 * (i * ch + c));
                    (c ? out->r : out->l)[i] = (float)(v / 32768.0);
                }
            free(d);
            return 0;
        }
        pos += 8 + (long)len + (len & 1);
    }
    free(d);
    return -1;
}

static int cmpf(const void *a, const void *b)
{
    float x = *(const float *)a, y = *(const float *)b;
    return x < y ? -1 : x > y;
}

int main(int argc, char **argv)
{
    int loop = 0, first = 1;
    if (argc > 1 && !strcmp(argv[1], "--loop")) { loop = 1; first = 2; }
    printf("%-28s %2s %7s %7s %7s %7s %6s %9s%s\n", "file", "ch", "dur_s", "peak", "rms", "Lmax", "dc_%",
           "edge_lsb", loop ? "   seam_lsb p999_lsb local_max" : "");
    int fail = 0;
    for (int a = first; a < argc; a++) {
        Buf b;
        int rate = 0;
        if (load(argv[a], &b, &rate)) { printf("%s: unreadable\n", argv[a]); fail = 1; continue; }
        const char *name = strrchr(argv[a], '/');
        name = name ? name + 1 : argv[a];
        double dc = 0;
        for (int c = 0; c < b.ch; c++) {
            const float *x = c ? b.r : b.l;
            double s = 0;
            for (long i = 0; i < b.n; i++) s += x[i];
            if (fabs(s / b.n) > fabs(dc)) dc = s / b.n;
        }
        double e0 = fabs(b.l[0]), e1 = fabs(b.l[b.n - 1]);
        if (b.r) { e0 = fmax(e0, fabs(b.r[0])); e1 = fmax(e1, fabs(b.r[b.n - 1])); }
        double edge = fmax(e0, e1) * 32768.0;
        printf("%-28s %2d %7.3f %7.2f %7.2f %7.2f %6.3f %9.0f", name, b.ch, (double)b.n / rate,
               lin2db(buf_peak(&b)), rms_db(&b), loud_max(&b, 0.1), dc * 100.0, edge);
        if (loop) {
            double seam = 0;
            float *d = malloc(sizeof(float) * (size_t)b.n * b.ch);
            long nd = 0;
            for (int c = 0; c < b.ch; c++) {
                const float *x = c ? b.r : b.l;
                seam = fmax(seam, fabs(x[0] - x[b.n - 1]));
                for (long i = 1; i < b.n; i++) d[nd++] = fabsf(x[i] - x[i - 1]);
            }
            qsort(d, (size_t)nd, sizeof(float), cmpf);
            double p999 = d[(long)(nd * 0.999)];
            /* Locally: the steps in the 4096 samples either side of the
               seam. A seam that is a step would stand out against these. */
            long W = 4096 < b.n / 2 ? 4096 : b.n / 2;
            nd = 0;
            for (int c = 0; c < b.ch; c++) {
                const float *x = c ? b.r : b.l;
                for (long i = b.n - W + 1; i < b.n; i++) d[nd++] = fabsf(x[i] - x[i - 1]);
                for (long i = 1; i < W; i++) d[nd++] = fabsf(x[i] - x[i - 1]);
            }
            qsort(d, (size_t)nd, sizeof(float), cmpf);
            double lmax = d[nd - 1];
            printf("   %8.0f %8.0f %9.0f%s", seam * 32768.0, p999 * 32768.0, lmax * 32768.0,
                   seam > lmax ? "  SEAM!" : "");
            if (seam > lmax) fail = 1;
            free(d);
        }
        printf("\n");
        buf_free(&b);
    }
    return fail;
}
