/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* main.c - beese-poker: display bring-up and the main loop.
 *
 * One iteration = one rendered frame:
 *   1. sample input (raylib polled the devices at the end of the last frame)
 *   2. run 0..4 fixed 60 Hz ticks (engine/step.h), each with its InputFrame;
 *      the first tick of the frame receives every edge seen since the last
 *      tick, so a press shows in the frame that follows it
 *   3. present: present_update + present_draw into the 1280x720 play space
 *   4. compose on the screen: side art, upscale (both skipped when a display
 *      plane does the scaling, see screen.h), F1 overlay
 *   5. EndDrawing: swap (on DRM this waits for the vblank), poll input
 * Frame times of each phase go to the overlay and, with --perf-csv, to a CSV. */
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "raylib.h"
#include "engine/rng.h"
#include "engine/step.h"
#include "platform/app.h"
#include "platform/display_probe.h"
#include "platform/drm_present.h"
#include "platform/gpustats.h"
#include "platform/gputest.h"
#include "platform/input.h"
#include "platform/joy_evdev.h"
#include "platform/options.h"
#include "platform/overlay.h"
#include "platform/perf.h"
#include "platform/screen.h"
#include "platform/texreg.h"

static volatile sig_atomic_t g_stop;

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

/* The session seed comes from the engine: getrandom(), with fallbacks. */
#define STEP_HZ STEP_DEFAULT_HZ

/* One tick in ns, rounded UP: 1e9/60 truncated is a hair short of a tick, and
 * feeding exactly that per frame would make the stepper run 0 ticks now and
 * then. Rounded up, it gains 20 ns*hz per frame: one extra tick per ~2 years. */
#define TICK_NS ((1000000000LL + STEP_HZ - 1) / STEP_HZ)

/* A frame that took almost exactly k refresh periods counts as exactly k, so
 * vsync jitter does not make the fixed step run 0 or 2 ticks at random. */
static int64_t snap_to_vsync(int64_t ns)
{
    const int64_t period = TICK_NS;
    for (int64_t k = 1; k <= 4; k++) {
        int64_t d = ns - k * period;
        if (d < 1000000 && d > -1000000) return k * period;
    }
    return ns;
}

int main(int argc, char **argv)
{
    int64_t t_main = perf_now_ns();

    Options o;
    int rc = options_parse(&o, argc, argv);
    if (rc > 0) return 0;
    if (rc < 0) return 2;

    SetTraceLogLevel(o.verbose ? LOG_INFO : LOG_WARNING);
    uint64_t seed = o.have_seed ? o.seed : rng_os_seed();

    int w = o.width, h = o.height;
    DisplayInfo di;
    int probed = display_probe_native(&di) == 0;
#if defined(BPL_PLATFORM_DRM)
    if (w <= 0 && probed) { w = di.width; h = di.height; }
    /* raylib picks the DRM mode whose refresh equals the target FPS (60 when
     * unset); tell it the native refresh, then turn its limiter off again. */
    if (probed && di.refresh > 0 && di.refresh != 60) SetTargetFPS(di.refresh);
    /* Let the display controller scale the play space (screen.h). raylib's
     * screen then is the 1280x720 play space itself. */
    if (o.screen.present != PRESENT_GPU && w > 0 && h > 0) {
        ScreenLayout plan = screen_plan(&o.screen, w, h);
        BplDrmConfigurePlane(PLAY_W, PLAY_H, (int)plan.play.x, (int)plan.play.y, (int)plan.play.width,
                             (int)plan.play.height, plan.point);
    }
#else
    if (w <= 0) { w = 1280; h = 720; }
#endif
    if (o.vsync) SetConfigFlags(FLAG_VSYNC_HINT);
    int64_t t_before_init = perf_now_ns();
    InitWindow(w, h, "Beese's Poker Lounge");
    int64_t t_window = perf_now_ns();
    if (!IsWindowReady()) {
        fprintf(stderr, "beese-poker: could not open the display\n");
        return 1;
    }
    SetExitKey(KEY_NULL);   /* exits go through the input config (ESC, COIN+START) */
#if defined(BPL_PLATFORM_DRM)
    /* The DRM swap blocks until the vblank; raylib's own limiter would only add sleeps. */
    SetTargetFPS(0);
#else
    /* Xvfb has no vblank to wait for; keep the desktop build near 60 fps. */
    SetTargetFPS(60);
#endif

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGHUP, on_signal);

    gpustats_init();
    texreg_add_texture("raylib default font", GetFontDefault().texture);
    int plane = BplDrmPlaneActive();
    if (o.screen.present == PRESENT_PLANE && !plane)
        fprintf(stderr, "beese-poker: present = plane unavailable, composing on the GPU\n");
    screen_init(&o.screen, plane, w, h);
    joy_init();
    Input in;
    input_init(&in, &o.input);
    gputest_configure(o.sprites, o.shader);

    int64_t t_platform = perf_now_ns();
    AppCtx ctx;
    app_init(&ctx, seed, (AppState)o.start_state);
    int64_t t_app = perf_now_ns();

    if (o.perf_csv && perf_csv_open(o.perf_csv) != 0) {
        fprintf(stderr, "beese-poker: cannot write %s\n", o.perf_csv);
        return 1;
    }

    const ScreenLayout *L = screen_layout();
    fprintf(stderr, "beese-poker: %dx%d%s%s, play %dx%d at %d,%d (%.2fx, %s, %s), seed %016llx, config %s\n",
            L->screen_w, L->screen_h, probed ? " on " : "", probed ? di.connector : "",
            (int)L->play.width, (int)L->play.height, (int)L->play.x, (int)L->play.y, L->scale,
            L->point ? "point" : "bilinear", L->plane ? "display-plane scaling" : "GPU compose",
            (unsigned long long)seed,
            o.config_path[0] ? o.config_path : "(defaults)");

    FixedStep step;
    step_init(&step, STEP_HZ, 4);
    PerfRing ring;
    memset(&ring, 0, sizeof ring);
    PerfFrame last;
    memset(&last, 0, sizeof last);
    int overlay_on = o.overlay;
    uint32_t script_prev = 0;
    long rss_kb = perf_rss_kb();
    double startup_ms = -1.0;
    double sum_frame_ms = 0.0;
    int64_t t_prev = perf_now_ns();
    uint64_t frame = 0;

    while (!g_stop) {
        int64_t t0 = perf_now_ns();
        PerfFrame pf;
        memset(&pf, 0, sizeof pf);
        pf.frame = frame;
        pf.frame_ms = frame ? perf_ms(t_prev, t0) : 0.0;

        /* 1. input */
        input_sample(&in, screen_touch_rect());
        if (in.exit_requested || WindowShouldClose()) break;
        if (in.frame_pressed & BTN_DEBUG) overlay_on = !overlay_on;

        /* 2. fixed-step logic */
        int ticks = o.lockstep ? 1 : step_advance_ns(&step, frame ? snap_to_vsync(t0 - t_prev) : TICK_NS);
        t_prev = t0;
        for (int i = 0; i < ticks; i++) {
            InputFrame f;
            input_next_frame(&in, &f);
            uint32_t sb = options_script_buttons(&o, ctx.tick);
            f.pressed |= sb & ~script_prev;
            f.down |= sb;
            script_prev = sb;
            /* Scripted buttons can leave the way held ones do (COIN+START),
             * so a scripted run tests RetroPie's exit, not just --frames. */
            if (in.cfg.exit_combo && (sb & in.cfg.exit_combo) == in.cfg.exit_combo) in.exit_requested = 1;
            app_tick(&ctx, &f);
        }
        if (ctx.quit) break;
        int64_t t1 = perf_now_ns();

        /* 3. present into the play space */
        float dt = o.lockstep ? 1.0f / STEP_HZ : (float)(pf.frame_ms / 1000.0);
        if (dt <= 0.0f) dt = 1.0f / STEP_HZ;
        if (dt > 0.1f) dt = 0.1f;
        BeginDrawing();
        screen_begin_play((Color){ 14, 12, 16, 255 });
        app_present(&ctx, dt);
        screen_end_play();
        for (int s = 0; s < o.nshots; s++) {
            if (!o.shots[s].screen && o.shots[s].frame == frame) {
                if (screen_save_play(o.shots[s].path) == 0) fprintf(stderr, "shot: %s\n", o.shots[s].path);
                else fprintf(stderr, "shot: FAILED %s\n", o.shots[s].path);
            }
        }
        if (o.gpu_finish) gpustats_finish();
        int64_t t2 = perf_now_ns();

        /* 4. compose on the screen (nothing to do when a display plane scales) */
        screen_compose((float)ctx.time);
        if (overlay_on) overlay_draw(&ring, &last, &ctx, rss_kb, startup_ms);
        for (int s = 0; s < o.nshots; s++) {
            if (o.shots[s].screen && o.shots[s].frame == frame) {
                if (screen_save_screen(o.shots[s].path) == 0) fprintf(stderr, "shot: %s (screen)\n", o.shots[s].path);
                else fprintf(stderr, "shot: FAILED %s\n", o.shots[s].path);
            }
        }
        if (o.gpu_finish) gpustats_finish();
        int64_t t3 = perf_now_ns();

        /* 5. swap */
        EndDrawing();
        int64_t t4 = perf_now_ns();

        pf.logic_ms = perf_ms(t0, t1);
        pf.render_ms = perf_ms(t1, t2);
        pf.compose_ms = perf_ms(t2, t3);
        pf.swap_ms = perf_ms(t3, t4);
        pf.draw_calls = gpustats_take();
        pf.ticks = ticks;

        if (startup_ms < 0.0) {
            startup_ms = perf_process_age_ms();
            fprintf(stderr, "STARTUP: first %s frame on screen %.0f ms after exec (%.0f ms after main)\n",
                    app_state_name(ctx.state), startup_ms, perf_ms(t_main, perf_now_ns()));
            fprintf(stderr, "STARTUP: before main %.0f, options+probe %.0f, InitWindow %.0f, platform init %.0f, "
                    "modes init %.0f, first frame %.0f ms\n",
                    startup_ms - perf_ms(t_main, perf_now_ns()), perf_ms(t_main, t_before_init),
                    perf_ms(t_before_init, t_window), perf_ms(t_window, t_platform), perf_ms(t_platform, t_app),
                    perf_ms(t_app, perf_now_ns()));
        }
        if (frame % 30 == 0) rss_kb = perf_rss_kb();
        perf_ring_push(&ring, &pf);
        perf_csv_row(&pf, rss_kb);
        sum_frame_ms += pf.frame_ms;
        last = pf;
        frame++;
        if (o.frames && frame >= o.frames) break;
    }

    fprintf(stderr, "beese-poker: %llu frames, %llu ticks, mean frame %.3f ms, RSS %.1f MB, textures %.2f MB\n",
            (unsigned long long)frame, (unsigned long long)ctx.tick,
            frame > 1 ? sum_frame_ms / (double)(frame - 1) : 0.0, perf_rss_kb() / 1024.0,
            texreg_bytes() / (1024.0 * 1024.0));
    if (o.verbose) texreg_dump(stderr);

    perf_csv_close();
    app_shutdown(&ctx);
    screen_shutdown();
    joy_shutdown();
    CloseWindow();
    return 0;
}
