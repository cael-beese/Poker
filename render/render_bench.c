/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* render_bench.c - render-bench: GPU cost of the RENDERTEST scenes without
 * taking over the display.
 *
 * The cabinet's display belongs to EmulationStation and to whatever the
 * owner is playing; a measurement must not take it over. This tool renders
 * the RENDERTEST mode headless on the GPU's render node (/dev/dri/renderD128,
 * EGL on GBM, surfaceless: no KMS, no display, no audio) into the same
 * 1280x720 play-space target the game uses, waits for the GPU after every
 * frame (glFinish) and reports the time per frame = the GPU cost of the
 * frame, as the game's --gpu-finish render_ms. Nothing is presented, so
 * there is no vsync: it measures cost, not frame pacing.
 *
 * It refuses to run while RetroArch / runcommand (a game) is running, and
 * stops at once if one starts, so it never competes with someone playing.
 *
 * usage: render-bench [--frames N] [--scene S] [--seg N] [--profile M,M,...]
 *                     [--fx LIST] [--quality 0|1|2] [--particles N]
 *   --scene 0 showcase, 1 jackpot (default), 2 deck, 3 holdem, 4 particles
 *   --profile   layer skip masks per segment, as BPL_RT_PROFILE (rendertest.c)
 *   --seg       frames per segment (default 480 = one jackpot celebration)
 *   --quality   bloom: 0 = 1/4 9-tap, 1 = 1/8, 2 = 1/4 5-tap
 *   --particles particle scene count (500, 1000, 2000) */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <fcntl.h>
#include <gbm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "raylib.h"
#include "rlgl.h"
#include "platform/app.h"
#include "platform/fx_settings.h"
#include "platform/gpustats.h"
#include "platform/screen.h"
#include "platform/texreg.h"
#include "render/render.h"

extern const AppMode mode_rendertest;

/* The mode table platform/app.c refers to (the game's lives in app_modes.c). */
const AppMode *const app_modes[APP_NSTATES] = { [APP_RENDERTEST] = &mode_rendertest };

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

/* A game (RetroArch through runcommand) is running on the cabinet. */
static int game_running(void)
{
    return system("pgrep -f '^/opt/retropie/emulators/retroarch/bin/retroarch' > /dev/null 2>&1 || "
                  "pgrep -f 'runcommand.sh' > /dev/null 2>&1") == 0;
}

static int cmp_f(const void *a, const void *b)
{
    float x = *(const float *)a, y = *(const float *)b;
    return x < y ? -1 : x > y;
}

static void report(const char *label, float *ms, unsigned *dc, int n)
{
    if (n <= 0) return;
    double sum = 0, dsum = 0;
    for (int i = 0; i < n; i++) { sum += ms[i]; dsum += dc[i]; }
    qsort(ms, (size_t)n, sizeof ms[0], cmp_f);
    printf("%-14s frames %4d  gpu ms mean %6.2f  p50 %6.2f  p99 %6.2f  max %6.2f  draw calls %5.1f\n", label, n, sum / n,
           ms[n / 2], ms[(int)(0.99 * (n - 1))], ms[n - 1], dsum / n);
}

int main(int argc, char **argv)
{
    int frames = 960, scene = 1, seg = 480, quality = 0, particles = 1000;
    const char *profile = NULL;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : "";
        if (!strcmp(a, "--frames")) { frames = atoi(v); i++; }
        else if (!strcmp(a, "--scene")) { scene = atoi(v); i++; }
        else if (!strcmp(a, "--seg")) { seg = atoi(v); i++; }
        else if (!strcmp(a, "--profile")) { profile = v; i++; }
        else if (!strcmp(a, "--quality")) { quality = atoi(v); i++; }
        else if (!strcmp(a, "--particles")) { particles = atoi(v); i++; }
        else if (!strcmp(a, "--fx")) {
            if (fx_settings_parse(&g_effects, v) != 0) { fprintf(stderr, "bad --fx\n"); return 2; }
            i++;
        } else { fprintf(stderr, "unknown option %s\n", a); return 2; }
    }
    if (game_running()) {
        fprintf(stderr, "render-bench: a game is running on the cabinet; not measuring\n");
        return 3;
    }

    int fd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
    if (fd < 0) { perror("render-bench: /dev/dri/renderD128"); return 1; }
    struct gbm_device *gbm = gbm_create_device(fd);
    PFNEGLGETPLATFORMDISPLAYEXTPROC get_dpy = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDisplay dpy = get_dpy ? get_dpy(EGL_PLATFORM_GBM_KHR, gbm, NULL) : eglGetDisplay((EGLNativeDisplayType)gbm);
    if (!eglInitialize(dpy, NULL, NULL)) { fprintf(stderr, "render-bench: eglInitialize failed\n"); return 1; }
    eglBindAPI(EGL_OPENGL_ES_API);
    const EGLint cfg_attr[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
                                EGL_ALPHA_SIZE, 8, EGL_NONE };
    EGLConfig cfg;
    EGLint ncfg = 0;
    eglChooseConfig(dpy, cfg_attr, &cfg, 1, &ncfg);
    const EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, ncfg ? cfg : NULL, EGL_NO_CONTEXT, ctx_attr);
    if (ctx == EGL_NO_CONTEXT || !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
        fprintf(stderr, "render-bench: no surfaceless GLES context\n");
        return 1;
    }
    rlLoadExtensions((void *)eglGetProcAddress);
    rlglInit(PLAY_W, PLAY_H);
    SetTraceLogLevel(LOG_WARNING);
    setenv("BPL_NO_AUDIO", "1", 1);
    if (profile) setenv("BPL_RT_PROFILE", profile, 1);
    char segs[16];
    snprintf(segs, sizeof segs, "%d", seg);
    setenv("BPL_RT_SEG", segs, 1);

    /* The play space as the gpu-path render target, as on a desktop. */
    ScreenConfig sc;
    screen_config_defaults(&sc);
    sc.side_art = 0;
    screen_init(&sc, 0, PLAY_W, PLAY_H);

    AppCtx app;
    memset(&app, 0, sizeof app);
    app.session_seed = 0xC0FFEE;
    app.state = APP_RENDERTEST;
    double t0 = now_ms();
    mode_rendertest.init(&app);
    double t_init = now_ms() - t0;
    mode_rendertest.enter(&app, APP_MENU);
    post_params()->quality = (BloomQuality)(quality % BLOOM_NQUALITY);

    float *ms = calloc((size_t)frames, sizeof(float));
    unsigned *dc = calloc((size_t)frames, sizeof(unsigned));
    /* Walk to the requested scene with RIGHT presses, and set the particle count. */
    int presses = scene, n = 0;
    int pk = particles >= 2000 ? 2 : particles >= 1000 ? 1 : 0, pk_now = 1;
    gpustats_init();
    for (int f = 0; f < frames; f++) {
        if (f % 60 == 0 && game_running()) {
            fprintf(stderr, "render-bench: a game started; stopping\n");
            break;
        }
        app.input.pressed = 0;
        if (presses > 0 && f >= 1) { app.input.pressed = BTN_RIGHT; presses--; }
        else if (scene == 4 && presses == 0 && pk_now != pk && f >= 2) {
            app.input.pressed = pk > pk_now ? BTN_UP : BTN_DOWN;
            pk_now += pk > pk_now ? 1 : -1;
        }
        double a = now_ms();
        screen_begin_play((Color){ 14, 12, 16, 255 });
        mode_rendertest.present_update(&app, NULL, 0, 1.0f / 60.0f);
        mode_rendertest.present_draw(&app);
        screen_end_play();
        gpustats_finish();
        ms[f] = (float)(now_ms() - a);
        dc[f] = gpustats_take();
        app.time += 1.0 / 60.0;
        n = f + 1;
    }
    printf("render-bench: scene %d, %d frames, render_init %.0f ms (%s), bloom %s, textures %.2f MB\n", scene, n,
           t_init, "headless, render node", g_effects.bloom ? post_quality_name(post_params()->quality) : "off",
           texreg_bytes() / (1024.0 * 1024.0));
    const int skip = 30;   /* first frames of each segment: scene restart, shader warm-up */
    if (profile) {
        for (int s = 0; s * seg < n; s++) {
            int a = s * seg + skip, b = (s + 1) * seg < n ? (s + 1) * seg : n;
            char lab[32];
            const char *p = profile;
            for (int k = 0; k < s && p; k++) { p = strchr(p, ','); if (p) p++; }
            snprintf(lab, sizeof lab, "mask %.*s", p ? (int)strcspn(p, ",") : 1, p ? p : "?");
            if (b > a) report(lab, ms + a, dc + a, b - a);
        }
    } else if (n > 60) {
        report("all", ms + 60, dc + 60, n - 60);
    }
    mode_rendertest.shutdown();
    screen_shutdown();
    rlglClose();
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(dpy, ctx);
    eglTerminate(dpy);
    gbm_device_destroy(gbm);
    close(fd);
    return 0;
}
