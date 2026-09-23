/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
#ifndef BPL_TEST_UTIL_H
#define BPL_TEST_UTIL_H

/* Minimal test helpers. Tests build in Release (NDEBUG), so they must not
   rely on assert(); CHECK always runs and counts failures. */

#include <math.h>
#include <stdio.h>
#include <time.h>

static int g_test_failures;

#define CHECK(cond) do { \
    if (!(cond)) { \
        g_test_failures++; \
        if (g_test_failures <= 20) \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

#define CHECK_EQ_INT(a, b) do { \
    long long va_ = (long long)(a), vb_ = (long long)(b); \
    if (va_ != vb_) { \
        g_test_failures++; \
        if (g_test_failures <= 20) \
            fprintf(stderr, "FAIL %s:%d: %s == %lld, expected %s == %lld\n", \
                    __FILE__, __LINE__, #a, va_, #b, vb_); \
    } \
} while (0)

static inline double test_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static inline int test_finish(const char *name)
{
    if (g_test_failures) {
        printf("%s: FAILED (%d checks)\n", name, g_test_failures);
        return 1;
    }
    printf("%s: OK\n", name);
    return 0;
}

/* Regularised upper incomplete gamma Q(a, x) (Numerical Recipes style):
   series for x < a + 1, continued fraction otherwise. The chi-square
   p-value for statistic x with k degrees of freedom is Q(k/2, x/2). */
static inline double test_gammq(double a, double x)
{
    double gln = lgamma(a);
    if (x <= 0.0) return 1.0;
    if (x < a + 1.0) {
        double ap = a, sum = 1.0 / a, del = sum;
        int n;
        for (n = 0; n < 10000; n++) {
            ap += 1.0;
            del *= x / ap;
            sum += del;
            if (fabs(del) < fabs(sum) * 1e-15) break;
        }
        return 1.0 - sum * exp(-x + a * log(x) - gln);
    } else {
        double b = x + 1.0 - a, c = 1.0 / 1e-300, d = 1.0 / b, h = d;
        int i;
        for (i = 1; i < 10000; i++) {
            double an = -i * (i - a), del;
            b += 2.0;
            d = an * d + b;
            if (fabs(d) < 1e-300) d = 1e-300;
            c = b + an / c;
            if (fabs(c) < 1e-300) c = 1e-300;
            d = 1.0 / d;
            del = d * c;
            h *= del;
            if (fabs(del - 1.0) < 1e-15) break;
        }
        return exp(-x + a * log(x) - gln) * h;
    }
}

static inline double test_chi2_p(double stat, double dof)
{
    return test_gammq(dof / 2.0, stat / 2.0);
}

#endif
