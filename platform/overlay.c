/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* overlay.c - see overlay.h. */
#include "platform/overlay.h"

#include <stdio.h>

#include "raylib.h"
#include "rlgl.h"
#include "platform/screen.h"
#include "platform/fx_settings.h"
#include "platform/texreg.h"

void overlay_draw(const PerfRing *ring, const PerfFrame *last, const AppCtx *ctx, long rss_kb,
                  double startup_ms)
{
    double avg, max, logic, render, compose, swap;
    perf_ring_stats(ring, &avg, &max, &logic, &render, &compose, &swap);
    const ScreenLayout *L = screen_layout();
    int k = GetScreenHeight() / 720;
    if (k < 1) k = 1;
    int size = 20 * k, lh = 24 * k, x = 12 * k, y = 12 * k;

    char lines[9][160];
    int n = 0;
    snprintf(lines[n++], sizeof lines[0], "FPS %.1f   frame %.2f ms (avg %.2f, max %.2f)",
             avg > 0 ? 1000.0 / avg : 0.0, last->frame_ms, avg, max);
    snprintf(lines[n++], sizeof lines[0], "logic %.2f  render %.2f  compose %.2f  swap %.2f ms",
             logic, render, compose, swap);
    snprintf(lines[n++], sizeof lines[0], "draw calls %u   ticks/frame %d", last->draw_calls, last->ticks);
    snprintf(lines[n++], sizeof lines[0], "RSS %.1f MB   textures %.2f MB (%d)", rss_kb / 1024.0,
             texreg_bytes() / (1024.0 * 1024.0), texreg_count());
    snprintf(lines[n++], sizeof lines[0], "%s  tick %llu  seed %016llx", app_state_name(ctx->state),
             (unsigned long long)ctx->tick, (unsigned long long)ctx->session_seed);
    snprintf(lines[n++], sizeof lines[0], "%dx%d  play %.2fx %s %s  GL %s  start %.0f ms", L->screen_w, L->screen_h,
             L->scale, L->point ? "point" : "bilinear", L->plane ? "plane" : "gpu",
             rlGetVersion() == RL_OPENGL_ES_20 ? "ES2" : rlGetVersion() == RL_OPENGL_ES_30 ? "ES3" : "3.3",
             startup_ms);
    /* Effect toggles: upper case = on, lower case = off. */
    int p = snprintf(lines[n], sizeof lines[0], "fx");
    for (int i = 0; i < EFFECT_COUNT && p < (int)sizeof lines[0] - 24; i++) {
        char name[24];
        snprintf(name, sizeof name, "%s", fx_settings_name(i));
        for (char *c = name; *c; c++)
            if (*fx_settings_ptr(&g_effects, i) && *c >= 'a' && *c <= 'z') *c = (char)(*c - 32);
        p += snprintf(lines[n] + p, sizeof lines[0] - (size_t)p, " %s", name);
    }
    n++;

    int w = 0;
    for (int i = 0; i < n; i++) {
        int tw = MeasureText(lines[i], size);
        if (tw > w) w = tw;
    }
    DrawRectangle(x - 6 * k, y - 6 * k, w + 12 * k, n * lh + 8 * k, (Color){ 0, 0, 0, 190 });
    for (int i = 0; i < n; i++)
        DrawText(lines[i], x, y + i * lh, size, i == 0 ? (Color){ 255, 210, 90, 255 } : RAYWHITE);
}
