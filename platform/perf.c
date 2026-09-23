/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* perf.c - see perf.h. */
#include "platform/perf.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int64_t perf_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

double perf_ms(int64_t from_ns, int64_t to_ns)
{
    return (double)(to_ns - from_ns) / 1e6;
}

double perf_process_age_ms(void)
{
    /* Field 22 of /proc/self/stat is the start time in clock ticks after boot.
     * The command name (field 2) may contain spaces, so parse after its ')'. */
    char buf[1024];
    FILE *f = fopen("/proc/self/stat", "r");
    if (!f) return -1.0;
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = '\0';
    const char *p = strrchr(buf, ')');
    if (!p) return -1.0;
    p += 2;                                     /* now at field 3 */
    unsigned long long start_ticks = 0;
    for (int field = 3; field < 22 && *p; field++) {
        p = strchr(p, ' ');
        if (!p) return -1.0;
        p++;
    }
    if (sscanf(p, "%llu", &start_ticks) != 1) return -1.0;
    long hz = sysconf(_SC_CLK_TCK);
    if (hz <= 0) return -1.0;
    struct timespec ts;
    clock_gettime(CLOCK_BOOTTIME, &ts);
    double now_ms = (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
    return now_ms - (double)start_ticks * 1000.0 / (double)hz;
}

long perf_rss_kb(void)
{
    long pages_total = 0, pages_rss = 0;
    FILE *f = fopen("/proc/self/statm", "r");
    if (!f) return 0;
    int ok = fscanf(f, "%ld %ld", &pages_total, &pages_rss);
    fclose(f);
    if (ok != 2) return 0;
    return pages_rss * (sysconf(_SC_PAGESIZE) / 1024);
}

void perf_ring_push(PerfRing *r, const PerfFrame *f)
{
    r->f[r->head] = *f;
    r->head = (r->head + 1) % PERF_WINDOW;
    if (r->n < PERF_WINDOW) r->n++;
}

void perf_ring_stats(const PerfRing *r, double *avg_frame, double *max_frame, double *avg_logic,
                     double *avg_render, double *avg_compose, double *avg_swap)
{
    double sf = 0, mf = 0, sl = 0, sr = 0, sc = 0, ss = 0;
    for (int i = 0; i < r->n; i++) {
        const PerfFrame *f = &r->f[i];
        sf += f->frame_ms;
        if (f->frame_ms > mf) mf = f->frame_ms;
        sl += f->logic_ms;
        sr += f->render_ms;
        sc += f->compose_ms;
        ss += f->swap_ms;
    }
    double n = r->n > 0 ? (double)r->n : 1.0;
    *avg_frame = sf / n;
    *max_frame = mf;
    *avg_logic = sl / n;
    *avg_render = sr / n;
    *avg_compose = sc / n;
    *avg_swap = ss / n;
}

static FILE *g_csv;
static char g_csv_buf[1 << 16];

int perf_csv_open(const char *path)
{
    g_csv = fopen(path, "w");
    if (!g_csv) return -1;
    setvbuf(g_csv, g_csv_buf, _IOFBF, sizeof g_csv_buf);
    fputs("frame,frame_ms,logic_ms,render_ms,draw_calls,compose_ms,swap_ms,ticks,rss_kb\n", g_csv);
    return 0;
}

void perf_csv_row(const PerfFrame *f, long rss_kb)
{
    if (!g_csv) return;
    fprintf(g_csv, "%llu,%.3f,%.3f,%.3f,%u,%.3f,%.3f,%d,%ld\n", (unsigned long long)f->frame,
            f->frame_ms, f->logic_ms, f->render_ms, f->draw_calls, f->compose_ms, f->swap_ms,
            f->ticks, rss_kb);
}

void perf_csv_close(void)
{
    if (g_csv) fclose(g_csv);
    g_csv = NULL;
}
