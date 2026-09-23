/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* modes_placeholder.c - stand-in screens for every app state, so the state
 * machine, input and composition can be seen working before the real modes
 * exist. Each is replaced in app_modes.c by the milestone that owns it.
 *
 * Every placeholder shows the logical-button panel along the bottom: a
 * button lights while it is down and flashes white on the frame it is
 * pressed, which is how "every press is visible in the same frame" can be
 * checked on the cabinet. */
#include <math.h>
#include <stdio.h>

#include "raylib.h"
#include "platform/app.h"
#include "platform/input.h"
#include "platform/joy_evdev.h"
#include "platform/perf.h"
#include "platform/screen.h"
#include "platform/texreg.h"

static const Color C_HONEY = { 232, 170, 40, 255 };
static const Color C_AMBER = { 255, 128, 16, 255 };
static const Color C_MAGENTA = { 255, 40, 200, 255 };
static const Color C_CYAN = { 40, 230, 255, 255 };
static const Color C_DIM = { 120, 110, 100, 255 };
static const Color C_PANEL = { 30, 26, 32, 255 };

static void text_centered(const char *s, int y, int size, Color c)
{
    int w = MeasureText(s, size);
    DrawText(s, (PLAY_W - w) / 2, y, size, c);
}

/* Neon text: a few offset translucent copies under a bright core. */
static void neon_text(const char *s, int y, int size, Color glow, float pulse)
{
    int w = MeasureText(s, size), x = (PLAY_W - w) / 2;
    for (int i = 3; i >= 1; i--) {
        Color g = Fade(glow, (0.10f + 0.08f * pulse) * (float)(4 - i));
        DrawText(s, x - i, y, size, g);
        DrawText(s, x + i, y, size, g);
        DrawText(s, x, y - i, size, g);
        DrawText(s, x, y + i, size, g);
    }
    DrawText(s, x, y, size, ColorBrightness(glow, 0.55f + 0.3f * pulse));
}

static void backdrop(const char *title, const char *subtitle)
{
    DrawRectangleGradientV(0, 0, PLAY_W, PLAY_H, (Color){ 22, 18, 26, 255 }, (Color){ 8, 7, 10, 255 });
    DrawRectangle(0, 0, PLAY_W, 4, C_HONEY);
    DrawRectangle(0, PLAY_H - 4, PLAY_W, 4, C_HONEY);
    if (title) text_centered(title, 40, 40, C_HONEY);
    if (subtitle) text_centered(subtitle, 88, 20, C_DIM);
}

static void button_panel(const AppCtx *ctx)
{
    const int n = INPUT_NBUTTONS, gap = 4, y = PLAY_H - 58, h = 40;
    const int w = (PLAY_W - 40 - gap * (n - 1)) / n;
    for (int i = 0; i < n; i++) {
        int x = 20 + i * (w + gap);
        uint32_t bit = 1u << i;
        Color fill = C_PANEL, txt = C_DIM;
        if (ctx->input.down & bit) { fill = C_HONEY; txt = BLACK; }
        if (ctx->input.pressed & bit) { fill = RAYWHITE; txt = BLACK; }
        DrawRectangle(x, y, w, h, fill);
        DrawRectangleLines(x, y, w, h, Fade(C_HONEY, 0.6f));
        const char *name = input_button_name(i);
        int size = 10, tw = MeasureText(name, size);
        DrawText(name, x + (w - tw) / 2, y + (h - size) / 2, size, txt);
    }
    char buf[96];
    snprintf(buf, sizeof buf, "CREDITS %lld   tick %llu", (long long)ctx->wallet.credits,
             (unsigned long long)ctx->tick);
    DrawText(buf, 20, PLAY_H - 84, 20, C_HONEY);
    if (ctx->input.touch) DrawCircleLines(ctx->input.touch_x, ctx->input.touch_y, 18, C_CYAN);
}

/* ---- ATTRACT ------------------------------------------------------------- */

static void attract_tick(AppCtx *ctx, const InputFrame *in)
{
    if (in->pressed & (BTN_DEAL | BTN_START | BTN_OK | BTN_COIN | BTN_BET_ONE | BTN_BET_MAX))
        app_request(ctx, APP_MENU);
}

static void attract_draw(const AppCtx *ctx)
{
    float t = (float)ctx->time;
    float pulse = 0.5f + 0.5f * sinf(t * 3.0f);
    backdrop(NULL, NULL);
    /* Honeycomb wash behind the title. */
    for (int row = 0; row < 9; row++)
        for (int col = 0; col < 16; col++) {
            float x = 60.0f + col * 80.0f + (row & 1) * 40.0f, y = 40.0f + row * 70.0f;
            float a = 0.05f + 0.05f * sinf(t * 1.3f + col * 0.5f + row * 0.7f);
            DrawPolyLinesEx((Vector2){ x, y }, 6, 44, 30, 2, Fade(C_HONEY, a));
        }
    neon_text("BEESE'S", 150, 60, C_MAGENTA, pulse);
    neon_text("POKER LOUNGE", 220, 90, C_HONEY, pulse);
    text_centered("placeholder attract screen - platform milestone", 340, 20, C_DIM);
    if (((int)(t * 2.0f)) % 2 == 0) neon_text("PRESS DEAL", 430, 50, C_CYAN, 1.0f);
    button_panel(ctx);
}

const AppMode mode_attract_placeholder = {
    .name = "attract (placeholder)", .tick = attract_tick, .present_draw = attract_draw,
};

/* ---- MENU ---------------------------------------------------------------- */

static struct { int sel; uint32_t idle; } g_menu;

static void menu_enter(AppCtx *ctx, AppState from)
{
    (void)ctx; (void)from;
    g_menu.idle = 0;
}

static void menu_tick(AppCtx *ctx, const InputFrame *in)
{
    g_menu.idle = in->down ? 0 : g_menu.idle + 1;
    if (in->pressed & (BTN_UP | BTN_LEFT)) g_menu.sel = (g_menu.sel + 2) % 3;
    if (in->pressed & (BTN_DOWN | BTN_RIGHT)) g_menu.sel = (g_menu.sel + 1) % 3;
    if (in->pressed & BTN_HOLD1) g_menu.sel = 0;
    if (in->pressed & BTN_HOLD2) g_menu.sel = 1;
    if (in->pressed & BTN_HOLD3) g_menu.sel = 2;
    if (in->pressed & (BTN_OK | BTN_DEAL | BTN_START))
        app_request(ctx, g_menu.sel == 2 ? APP_RENDERTEST : g_menu.sel ? APP_HOLDEM : APP_DRAW);
    if (in->pressed & BTN_BACK) app_request(ctx, APP_ATTRACT);
    /* SPEC: back to attract after 60 s without input. */
    if (g_menu.idle >= 60u * 60u) app_request(ctx, APP_ATTRACT);
}

static void menu_draw(const AppCtx *ctx)
{
    backdrop("CHOOSE YOUR GAME", "placeholder menu - UP/DOWN or HOLD1/HOLD2/HOLD3, then DEAL");
    const char *items[3] = { "DRAW POKER", "TEXAS HOLD'EM", "RENDER TEST" };
    for (int i = 0; i < 3; i++) {
        Rectangle r = { 70.0f + i * 390.0f, 220, 360, 260 };
        int on = g_menu.sel == i;
        DrawRectangleRec(r, on ? Fade(C_HONEY, 0.18f) : C_PANEL);
        DrawRectangleLinesEx(r, on ? 4 : 2, on ? C_HONEY : C_DIM);
        int w = MeasureText(items[i], 40);
        DrawText(items[i], (int)(r.x + (r.width - w) / 2), (int)(r.y + 110), 40, on ? C_HONEY : C_DIM);
    }
    button_panel(ctx);
}

const AppMode mode_menu_placeholder = {
    .name = "menu (placeholder)", .enter = menu_enter, .tick = menu_tick, .present_draw = menu_draw,
};

/* ---- DRAW / HOLDEM ------------------------------------------------------- */

static struct { uint8_t held[5]; } g_draw;

static void draw_tick(AppCtx *ctx, const InputFrame *in)
{
    for (int i = 0; i < 5; i++)
        if (in->pressed & (BTN_HOLD1 << i)) g_draw.held[i] ^= 1;
    if (in->pressed & (BTN_CASH_OUT | BTN_BACK)) app_request(ctx, APP_MENU);
}

static void draw_draw(const AppCtx *ctx)
{
    backdrop("DRAW POKER", "placeholder - games/draw + render/ plug in via platform/app_modes.c");
    for (int i = 0; i < 5; i++) {
        Rectangle r = { 150.0f + i * 200.0f, 200, 170, 240 };
        DrawRectangleRounded(r, 0.08f, 6, (Color){ 240, 236, 226, 255 });
        DrawRectangleRoundedLinesEx(r, 0.08f, 6, 3, C_AMBER);
        if (g_draw.held[i]) {
            DrawRectangle((int)r.x, (int)(r.y + r.height + 14), (int)r.width, 36, C_HONEY);
            int w = MeasureText("HELD", 30);
            DrawText("HELD", (int)(r.x + (r.width - w) / 2), (int)(r.y + r.height + 17), 30, BLACK);
        }
    }
    text_centered("HOLD1-5 toggle   CASH OUT / BACK = menu", 540, 20, C_DIM);
    button_panel(ctx);
}

const AppMode mode_draw_placeholder = {
    .name = "draw (placeholder)", .tick = draw_tick, .present_draw = draw_draw,
};

static void holdem_tick(AppCtx *ctx, const InputFrame *in)
{
    if (in->pressed & (BTN_CASH_OUT | BTN_BACK)) app_request(ctx, APP_MENU);
}

static void holdem_draw(const AppCtx *ctx)
{
    backdrop("TEXAS HOLD'EM", "placeholder - games/holdem + ai/ + render/ plug in via platform/app_modes.c");
    DrawEllipse(PLAY_W / 2, 360, 470, 200, (Color){ 20, 70, 50, 255 });
    DrawEllipseLines(PLAY_W / 2, 360, 470, 200, C_HONEY);
    for (int s = 0; s < 6; s++) {
        float a = (float)s / 6.0f * 2.0f * PI + PI / 2;
        DrawCircle((int)(PLAY_W / 2 + cosf(a) * 520), (int)(360 + sinf(a) * 250), 36, C_PANEL);
        DrawCircleLines((int)(PLAY_W / 2 + cosf(a) * 520), (int)(360 + sinf(a) * 250), 36, C_HONEY);
    }
    if (ctx->input.slider) {
        char buf[48];
        snprintf(buf, sizeof buf, "slider %d", ctx->input.slider);
        text_centered(buf, 350, 20, C_CYAN);
    }
    button_panel(ctx);
}

const AppMode mode_holdem_placeholder = {
    .name = "holdem (placeholder)", .tick = holdem_tick, .present_draw = holdem_draw,
};

/* ---- SERVICE ------------------------------------------------------------- */

static void service_tick(AppCtx *ctx, const InputFrame *in)
{
    if (in->pressed & (BTN_BACK | BTN_SERVICE)) {
        AppState back = ctx->prev;
        if (back == APP_SERVICE || back == APP_GPUTEST) back = APP_ATTRACT;
        app_request(ctx, back);
    }
    /* Placeholder credit handling, so the wallet path is exercised. */
    if (in->pressed & BTN_COIN) ctx->wallet.credits += 100;
}

static void service_draw(const AppCtx *ctx)
{
    backdrop("SERVICE", "placeholder - BACK or SERVICE returns; COIN adds 100 credits");
    const ScreenLayout *L = screen_layout();
    char buf[160];
    int y = 140;
    snprintf(buf, sizeof buf, "session seed   %016llx", (unsigned long long)ctx->session_seed);
    DrawText(buf, 80, y, 20, RAYWHITE); y += 30;
    snprintf(buf, sizeof buf, "screen         %dx%d, play %dx%d at %d,%d (%.2fx %s, %s)", L->screen_w, L->screen_h,
             (int)L->play.width, (int)L->play.height, (int)L->play.x, (int)L->play.y, L->scale,
             L->integer ? "integer" : "fractional", L->point ? "point" : "bilinear");
    DrawText(buf, 80, y, 20, RAYWHITE); y += 30;
    snprintf(buf, sizeof buf, "memory         RSS %.1f MB, textures %.2f MB in %d", perf_rss_kb() / 1024.0,
             texreg_bytes() / (1024.0 * 1024.0), texreg_count());
    DrawText(buf, 80, y, 20, RAYWHITE); y += 30;
    snprintf(buf, sizeof buf, "joysticks      %d", joy_count());
    DrawText(buf, 80, y, 20, RAYWHITE); y += 30;
    for (int j = 0; j < joy_count() && j < 6; j++) {
        char held[80] = "";
        int p = 0;
        for (int b = 0; b < joy_buttons(j) && p < 70; b++)
            if (joy_button_down(j, b)) p += snprintf(held + p, sizeof held - (size_t)p, " B%d", b);
        snprintf(buf, sizeof buf, "  JOY%d  %s  (%d buttons, %d axes)  axis0 %+.2f axis1 %+.2f  down:%s", j,
                 joy_name(j), joy_buttons(j), joy_axes(j), joy_axis(j, 0), joy_axis(j, 1), held);
        DrawText(buf, 80, y, 20, C_CYAN); y += 28;
    }
    button_panel(ctx);
}

const AppMode mode_service_placeholder = {
    .name = "service (placeholder)", .tick = service_tick, .present_draw = service_draw,
};
