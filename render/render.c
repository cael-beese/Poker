/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* render.c - see render.h. */
#include "render/render.h"

#if defined(__GLIBC__)
#include <malloc.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "raylib.h"
#include "render/jobs.h"

/* GL 1.0 / ES 2.0 core, exported by libGL and libGLESv2 alike. */
extern void glGetIntegerv(unsigned int pname, int *data);
#define BPL_GL_MAX_TEXTURE_SIZE 0x0D33

static RenderStats g_stats;
static int g_inited;

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

const RenderStats *render_stats(void) { return &g_stats; }

void render_asset_path(char *out, size_t n, const char *dir, const char *file)
{
    char exe[400];
    const char *env = getenv("BPL_ASSETS");
    if (env && *env) {
        snprintf(out, n, "%s/%s/%s", env, dir, file);
        if (access(out, R_OK) == 0) return;
    }
    snprintf(out, n, "assets/%s/%s", dir, file);
    if (access(out, R_OK) == 0) return;
    ssize_t len = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (len > 0) {
        exe[len] = '\0';
        char *slash = strrchr(exe, '/');
        if (slash) {
            *slash = '\0';
            static const char *const up[] = { "/assets", "/../assets", "/../../assets" };
            for (int i = 0; i < 3; i++) {
                snprintf(out, n, "%s%s/%s/%s", exe, up[i], dir, file);
                if (access(out, R_OK) == 0) return;
            }
        }
    }
    snprintf(out, n, "assets/%s/%s", dir, file);
}

/* ---- the parallel job lists --------------------------------------------- */

static void raster_job(int i, void *u)
{
    (void)u;
    /* Glyph rasterising and the shared card stock have no dependencies. */
    if (i < FONT_COUNT + 2) text_raster_job(i);
    else cards_prepare_job(i - FONT_COUNT - 2);
}

static int g_n_cards, g_n_ui, g_n_text, g_n_spr;
static float g_job_ms[512];
static int g_profile;

static void paint_one(int i);

static void paint_job(int i, void *u)
{
    (void)u;
    double t = g_profile ? now_ms() : 0;
    paint_one(i);
    if (g_profile && i < 512) g_job_ms[i] = (float)(now_ms() - t);
}

static void paint_one(int i)
{
    if (i < g_n_cards) { cards_paint_job(i); return; }
    i -= g_n_cards;
    if (i < g_n_ui) { ui_paint_job(i); return; }
    i -= g_n_ui;
    if (i < g_n_text) { text_paint_job(i); return; }
    i -= g_n_text;
    sprites_paint_job(i);
}

int render_init(uint64_t seed)
{
    if (g_inited) return 0;
    double t0 = now_ms();
    memset(&g_stats, 0, sizeof g_stats);
    fx_rng_seed(seed);
    g_stats.threads = jobs_cpus();

    g_stats.fonts_ok = text_load() == 0;
    double t1 = now_ms();
    jobs_run(raster_job, NULL, FONT_COUNT + 2 + cards_prepare_count(), 0);
    double t2 = now_ms();

    atlas_reset();
    cards_declare();
    text_declare();
    sprites_declare();
    ui_declare();
    int max_tex = 0;
    glGetIntegerv(BPL_GL_MAX_TEXTURE_SIZE, &max_tex);
    if (max_tex <= 0) max_tex = 2048;
    g_stats.max_tex = max_tex;
    if (atlas_pack(max_tex) != 0) {
        fprintf(stderr, "render: atlas packing failed\n");
        return -1;
    }

    g_n_cards = cards_job_count();
    g_n_ui = ui_job_count();
    g_n_text = FONT_COUNT;
    g_n_spr = sprites_job_count();
    g_profile = getenv("BPL_RENDER_PROFILE") != NULL;
    int njobs = g_n_cards + g_n_ui + g_n_text + g_n_spr;
    jobs_run(paint_job, NULL, njobs, 0);
    double t3 = now_ms();
    if (g_profile) {
        /* Where the start-up time goes: CPU ms per job group, and the slowest jobs. */
        double grp[4] = { 0 };
        for (int i = 0; i < njobs && i < 512; i++)
            grp[i < g_n_cards ? 0 : i < g_n_cards + g_n_ui ? 1 : i < g_n_cards + g_n_ui + g_n_text ? 2 : 3] += g_job_ms[i];
        fprintf(stderr, "RENDER PROFILE: cpu ms cards %.0f, backgrounds %.0f, glyphs %.0f, sprites %.0f\n", grp[0], grp[1], grp[2], grp[3]);
        for (int k = 0; k < 12; k++) {
            int best = -1;
            for (int i = 0; i < njobs && i < 512; i++)
                if (g_job_ms[i] >= 0 && (best < 0 || g_job_ms[i] > g_job_ms[best])) best = i;
            if (best < 0) break;
            fprintf(stderr, "RENDER PROFILE:   job %3d  %.1f ms\n", best, g_job_ms[best]);
            g_job_ms[best] = -1;
        }
    }

    cards_prepare_free();
    atlas_upload();
    cards_finish();
    text_finish();
    sprites_finish();
    ui_finish();
    double t4 = now_ms();

    g_stats.bloom_ok = post_init() == 0;
    pfx_init();
    ui_side_art_install();
    double t5 = now_ms();

#if defined(__GLIBC__)
    malloc_trim(0);   /* the CPU copies of the atlas are gone; give the pages back */
#endif
    g_stats.load_ms = t1 - t0;
    g_stats.raster_ms = t2 - t1;
    g_stats.paint_ms = t3 - t2;
    g_stats.upload_ms = t4 - t3;
    g_stats.post_ms = t5 - t4;
    g_stats.total_ms = now_ms() - t0;
    g_stats.pages = atlas_pages();
    if (g_stats.pages > 0) atlas_page_size(0, &g_stats.page_w, &g_stats.page_h);
    g_stats.items = atlas_items();
    g_stats.atlas_bytes = atlas_bytes();
    fprintf(stderr,
            "RENDER: init %.0f ms (fonts load %.0f, raster %.0f, paint %.0f, upload %.0f, shaders+targets %.0f) on %d threads; "
            "atlas %d page(s) %dx%d, %d images, %.1f MB; max texture %d\n",
            g_stats.total_ms, g_stats.load_ms, g_stats.raster_ms, g_stats.paint_ms, g_stats.upload_ms, g_stats.post_ms,
            g_stats.threads, g_stats.pages, g_stats.page_w, g_stats.page_h, g_stats.items,
            (double)g_stats.atlas_bytes / (1024.0 * 1024.0), g_stats.max_tex);
    g_inited = 1;
    return 0;
}

void render_shutdown(void)
{
    if (!g_inited) return;
    post_shutdown();
    ui_shutdown();
    text_shutdown();
    atlas_shutdown();
    g_inited = 0;
}

void render_begin(void)
{
    post_begin();
    gfx_begin();
}

void render_end(void)
{
    gfx_end();
    post_end();
}
