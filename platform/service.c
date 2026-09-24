/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* service.c - APP_SERVICE: the operator's menu behind the hidden SERVICE
 * button (SPEC section 6), navigable with the arcade buttons alone:
 *
 *   HOLD 1 / UP                      previous line
 *   HOLD 2 / DOWN / BET ONE          next line
 *   HOLD 4 / LEFT                    value down
 *   HOLD 5 / RIGHT / BET MAX         value up
 *   DEAL / HOLD 3 / OK / START       choose / toggle / run
 *   CASH OUT / BACK / SERVICE        back (from a page), leave (from the list)
 *
 * On the input-test page every button is being tested, so only SERVICE, or
 * CASH OUT held for two seconds, leaves it.
 *
 * Pages: the settings list (credits, denomination, coin value, volumes,
 * music, hint default, double-up, difficulty, buy-in), EFFECTS (every
 * settings.effects toggle), STATISTICS, INPUT TEST, SOUND TEST (every SfxId,
 * one at a time or all in turn), LEARN PANEL (which encoder input is in
 * which panel position, see panel.h), SELF-TEST (selftest.h plus the save
 * directory write test, the audio device and the display mode), with a
 * PASS / FAIL summary. The save status is always on screen: a read-only save
 * directory shows as a red RUNNING FROM RAM warning. */
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "raylib.h"
#include "rlgl.h"
#include "ai/ai.h"
#include "audio.h"
#include "platform/app.h"
#include "platform/input.h"
#include "platform/joy_evdev.h"
#include "platform/panel.h"
#include "platform/perf.h"
#include "platform/placeholder_view.h"
#include "platform/save.h"
#include "platform/screen.h"
#include "platform/selftest.h"
#include "platform/service.h"
#include "platform/session.h"
#include "platform/texreg.h"

#define C_HONEY   ((Color){ 232, 170, 40, 255 })
#define C_CYAN    ((Color){ 40, 230, 255, 255 })
#define C_DIM     ((Color){ 130, 120, 110, 255 })
#define C_PANEL   ((Color){ 30, 26, 32, 255 })
#define C_RED     ((Color){ 240, 60, 60, 255 })
#define C_GREEN   ((Color){ 80, 230, 110, 255 })

enum { PG_MAIN, PG_EFFECTS, PG_STATS, PG_INPUT, PG_SOUND, PG_SELFTEST, PG_LEARN };

enum {
    IT_CREDITS, IT_ADD, IT_RESET, IT_DENOM, IT_COIN, IT_VMASTER, IT_VMUSIC, IT_VSFX, IT_MUSIC, IT_HINT,
    IT_DOUBLE, IT_DIFF, IT_BUYIN, IT_EFFECTS, IT_STATS, IT_LEARN, IT_INPUT, IT_SOUND, IT_SELFTEST, IT_RESET_STATS,
    IT_EXIT, IT_COUNT
};

static const char *const k_diff[AI_NDIFF] = { "EASY", "NORMAL", "HARD", "EXPERT" };
static const int k_add_amounts[] = { 10, 100, 1000, 10000 };
#define N_ADD ((int)(sizeof k_add_amounts / sizeof k_add_amounts[0]))

static struct {
    int page, sel, fx_sel, sfx_sel;
    int add_idx;
    int confirm;                /* item waiting for its second DEAL            */
    int cashout_held;           /* input test: ticks CASH OUT has been held    */
    /* sound test */
    int cycle, cycle_wait;      /* auto-cycling through every SfxId            */
    int play_req;               /* SfxId to play at the next present_update, -1 */
    /* self-test */
    pthread_t thr;
    atomic_int st_state;        /* 0 idle, 1 running, 2 logic done, 3 all done  */
    SelfTestReport report;
    double st_started;
} V = { .play_req = -1, .add_idx = 1, .sel = IT_ADD };

static void *selftest_thread(void *arg)
{
    (void)arg;
    selftest_run_logic(&V.report);
    atomic_store(&V.st_state, 2);
    return NULL;
}

static void start_selftest(void)
{
    if (atomic_load(&V.st_state) == 1) return;
    if (atomic_load(&V.st_state) >= 2) pthread_join(V.thr, NULL);
    memset(&V.report, 0, sizeof V.report);
    atomic_store(&V.st_state, 1);
    V.st_started = GetTime();
    if (pthread_create(&V.thr, NULL, selftest_thread, NULL) != 0) {
        selftest_run_logic(&V.report);          /* no thread: run it here */
        atomic_store(&V.st_state, 2);
        V.thr = pthread_self();
    }
}

/* The checks that need the platform, on the main thread. */
static void finish_selftest(void)
{
    SelfTestReport *r = &V.report;
    const SessionInfo *si = session_info();
    double ms = 0;
    char why[120] = "";
    int ok = save_probe_dir(si->save_dir, &ms, why, sizeof why) == 0;
    size_t dl = strlen(si->save_dir);
    const char *tail = dl > 36 ? si->save_dir + dl - 36 : si->save_dir;
    selftest_add(r, "save directory", ok && !si->ram_only, ms, "%s%s: %s%s", dl > 36 ? "..." : "", tail,
                 ok ? "write + fsync + read back ok" : why, si->ram_only ? " (RUNNING FROM RAM)" : "");
    AudioStats as;
    audio_get_stats(&as);
    if (si->audio_ok)
        selftest_add(r, "audio device", as.missing == 0, 0.0, "%d Hz, %d sounds + %d streams, %d missing",
                     as.device_rate, as.sounds_loaded, as.streams, as.missing);
    else
        selftest_add(r, "audio device", 0, 0.0, "no audio device (running silent)");
    const ScreenLayout *L = screen_layout();
    int gl = rlGetVersion();
    selftest_add(r, "display", IsWindowReady() && L->play.width >= PLAY_W, 0.0,
                 "%dx%d, play %dx%d at %d,%d (%.2fx, %s), %s", L->screen_w, L->screen_h, (int)L->play.width,
                 (int)L->play.height, (int)L->play.x, (int)L->play.y, L->scale, L->plane ? "display plane" : "GPU compose",
                 gl == RL_OPENGL_ES_20 ? "GLES 2" : gl == RL_OPENGL_ES_30 ? "GLES 3" : "OpenGL 3.3");
    fprintf(stderr, "SELFTEST: %s, %d checks, %d failed, %.0f ms\n", r->failures ? "FAIL" : "PASS", r->n, r->failures,
            (GetTime() - V.st_started) * 1000.0);
    for (int i = 0; i < r->n; i++)
        fprintf(stderr, "SELFTEST:   %-4s %-18s %s\n", r->item[i].pass ? "ok" : "FAIL", r->item[i].name, r->item[i].detail);
    atomic_store(&V.st_state, 3);
}

static int step_u16(uint16_t *v, const uint16_t *tab, int n, int d)
{
    int i = 0;
    while (i < n && tab[i] != *v) i++;
    if (i == n) i = 0;
    i += d;
    if (i < 0) i = 0;
    if (i >= n) i = n - 1;
    *v = tab[i];
    return 1;
}

static int step_u32(uint32_t *v, const uint32_t *tab, int n, int d)
{
    int i = 0;
    while (i < n && tab[i] != *v) i++;
    if (i == n) i = 0;
    i += d;
    if (i < 0) i = 0;
    if (i >= n) i = n - 1;
    *v = tab[i];
    return 1;
}

static void step_u8(uint8_t *v, int lo, int hi, int d)
{
    int x = *v + d;
    *v = (uint8_t)(x < lo ? lo : x > hi ? hi : x);
}

/* ---- LEARN PANEL: which encoder input is in which panel position ----------
 *
 * Asks for each position in turn (P1 top row left to right, bottom row,
 * SELECT, START, then P2) and records the first encoder button pressed. The
 * panel buttons are suspended meanwhile (panel_suspend), so the presses do
 * nothing else; the keyboard still works (Q / Backspace cancel). A position
 * nobody presses for LEARN_WAIT seconds is left unwired (a cabinet without
 * that button); on player 2's side a timeout skips the rest of the side.
 * Holding any button for 3 s cancels. Offered by itself on the first start
 * (autorun): there, no press at all for the first position means nobody is
 * there, and it quietly gives up without asking again. */
#define LEARN_WAIT   12.0f
#define LEARN_CANCEL  3.0f
#define LEARN_SHOW    5.0f

static struct {
    int       pending, autorun;       /* requested before the state switch      */
    int       active, step, finished;
    PanelWire got[PS_COUNT];
    float     step_t, msg_t, hold_t, done_t, flash_t;
    int       hold_j, hold_b;
    char      msg[96];
    int       saved;
} W;

void service_request_learn(AppCtx *ctx, int autorun)
{
    W.pending = 1;
    W.autorun = autorun;
    app_request(ctx, APP_SERVICE);
}

static void learn_start(int autorun)
{
    memset(W.got, 0, sizeof W.got);
    for (int s = 0; s < PS_COUNT; s++) W.got[s].button = -1;
    W.autorun = autorun;
    W.active = 1;
    W.step = 0;
    W.finished = 0;
    W.step_t = W.msg_t = W.hold_t = W.done_t = W.flash_t = 0;
    W.hold_j = W.hold_b = -1;
    W.msg[0] = '\0';
    W.saved = 0;
    V.page = PG_LEARN;
    panel_suspend(1);
}

/* Ends the wizard; the state switch happens in service_tick (W.finished). */
static void learn_stop(const char *why)
{
    if (!W.active) return;
    W.active = 0;
    panel_suspend(0);
    if (W.autorun) panel_wizard_offered();
    if (why) fprintf(stderr, "PANEL: learning %s\n", why);
    W.finished = 1;
}

static void learn_update(float dt)
{
    if (!W.active) return;
    W.flash_t += dt;
    if (W.msg_t > 0) W.msg_t -= dt;
    if (W.step >= PS_COUNT) {            /* the summary, then leave */
        W.done_t += dt;
        if (W.done_t >= LEARN_SHOW) learn_stop(NULL);
        return;
    }
    /* Holding a button cancels. */
    if (W.hold_b >= 0 && joy_button_down(W.hold_j, W.hold_b)) {
        W.hold_t += dt;
        if (W.hold_t >= LEARN_CANCEL) { learn_stop("cancelled (button held)"); return; }
    } else {
        W.hold_b = -1;
        W.hold_t = 0;
    }
    for (int j = 0; j < joy_count(); j++) {
        for (int b = 0; b < joy_buttons(j); b++) {
            if (!joy_button_pressed(j, b)) continue;
            W.hold_j = j;
            W.hold_b = b;
            W.hold_t = 0;
            int dup = -1;
            for (int s = 0; s < W.step; s++)
                if (W.got[s].button == b && W.got[s].joy == j) dup = s;
            if (dup >= 0) {
                snprintf(W.msg, sizeof W.msg, "THAT ONE IS ALREADY %s", panel_slot_name((PanelSlot)dup));
                W.msg_t = 2.0f;
                audio_play(SFX_ERROR, 0.6f, 1.0f, 0.0f);
                return;
            }
            W.got[W.step].joy = (int8_t)j;
            W.got[W.step].button = (int8_t)b;
            W.step++;
            W.step_t = 0;
            W.msg[0] = '\0';
            audio_play(SFX_SERVICE_BEEP, 0.8f, 1.0f + 0.04f * (float)W.step, 0.0f);
            if (W.step == PS_COUNT) {
                W.saved = panel_set_wiring(W.got) == 0;
                audio_play(SFX_MENU_SELECT, 1.0f, 1.0f, 0.0f);
            }
            return;
        }
    }
    W.step_t += dt;
    if (W.step_t >= LEARN_WAIT) {
        if (W.step == 0 && W.autorun) { learn_stop("not answered: nobody at the panel"); return; }
        snprintf(W.msg, sizeof W.msg, "NO PRESS: %s LEFT UNWIRED", panel_slot_name((PanelSlot)W.step));
        W.msg_t = 2.5f;
        /* Player 2's side: one timeout skips the rest of it. */
        int to = W.step >= PS_PER_SIDE ? PS_COUNT : W.step + 1;
        W.step = to;
        W.step_t = 0;
        if (W.step == PS_COUNT) W.saved = panel_set_wiring(W.got) == 0;
    }
}

/* The panel as a drawing: per side a stick, two rows of three buttons, and
 * SELECT / START. mark = the position to press (pulsing), -1 none; lit =
 * positions held now (bit per slot); labels = what each one does. */
static void draw_panel(float x, float y, float scale, int mark, uint32_t lit, int labels, double time)
{
    const float r = 26 * scale, gx = 78 * scale, gy = 74 * scale;
    for (int side = 0; side < 2; side++) {
        float ox = x + side * 520 * scale;
        DrawRectangleRounded((Rectangle){ ox, y, 480 * scale, 250 * scale }, 0.12f, 8, (Color){ 24, 20, 28, 255 });
        DrawRectangleRoundedLines((Rectangle){ ox, y, 480 * scale, 250 * scale }, 0.12f, 8, Fade(C_HONEY, 0.4f));
        DrawText(side ? "PLAYER 2" : "PLAYER 1", (int)(ox + 14 * scale), (int)(y + 8 * scale), (int)(18 * scale), C_DIM);
        /* the stick */
        float sx = ox + 70 * scale, sy = y + 110 * scale;
        DrawCircle((int)sx, (int)sy, 34 * scale, (Color){ 40, 34, 46, 255 });
        DrawCircle((int)sx, (int)sy, 16 * scale, (Color){ 200, 40, 50, 255 });
        for (int k = 0; k < PS_PER_SIDE; k++) {
            int s = side * PS_PER_SIDE + k;
            float cx, cy, rr = r;
            if (k < 6) {
                cx = ox + 190 * scale + (float)(k % 3) * gx;
                cy = y + 70 * scale + (float)(k / 3) * gy;
            } else {
                cx = ox + 210 * scale + (float)(k - 6) * 110 * scale;
                cy = y + 214 * scale;
                rr = 13 * scale;
            }
            int on = (lit >> s) & 1u;
            Color fill = on ? C_HONEY : (Color){ 52, 46, 58, 255 };
            if (s == mark) {
                float p = 0.5f + 0.5f * sinf((float)time * 8);
                DrawCircle((int)cx, (int)cy, rr + 10 * scale * p, Fade(C_CYAN, 0.35f));
                fill = on ? C_HONEY : Fade(C_CYAN, 0.55f + 0.4f * p);
            }
            DrawCircle((int)cx, (int)cy, rr, fill);
            DrawCircleLines((int)cx, (int)cy, rr, Fade(C_HONEY, 0.7f));
            const char *lab = labels ? panel_slot_label((PanelSlot)s) : "";
            if (k >= 6) lab = k == 6 ? "SELECT" : "START";
            int fs = (int)(14 * scale);
            int tw = MeasureText(lab, fs);
            float ly = k < 6 ? cy + rr + 4 * scale : cy - rr - 18 * scale;
            if (k < 3 && labels) ly = cy - rr - 18 * scale;
            DrawText(lab, (int)(cx - tw / 2), (int)ly, fs, on ? RAYWHITE : C_DIM);
        }
    }
}

static uint32_t slots_down(void)
{
    uint32_t m = 0;
    for (int s = 0; s < PS_COUNT; s++)
        if (panel_slot_down((PanelSlot)s)) m |= 1u << s;
    return m;
}

static void draw_learn(const AppCtx *ctx)
{
    (void)ctx;
    double t = W.flash_t;
    if (W.step < PS_COUNT) {
        char s[120];
        snprintf(s, sizeof s, "%s SIDE: PRESS THE", W.step < PS_PER_SIDE ? "PLAYER 1" : "PLAYER 2");
        DrawText(s, 40, 70, 30, RAYWHITE);
        const char *where = panel_slot_name((PanelSlot)W.step) + 3;     /* without "P1 " */
        DrawText(where, 40, 108, 44, C_CYAN);
        snprintf(s, sizeof s, "BUTTON  (%d of %d)", W.step + 1, PS_COUNT);
        DrawText(s, 40 + MeasureText(where, 44) + 20, 118, 30, RAYWHITE);
        /* Where it is, and each one learnt so far lit. */
        uint32_t got = 0;
        for (int i = 0; i < W.step; i++)
            if (W.got[i].button >= 0) got |= 1u << i;
        draw_panel(40, 190, 1.15f, W.step, got, 0, t);
        int left = (int)(LEARN_WAIT - W.step_t + 0.99f);
        snprintf(s, sizeof s, "No press in %d s: this position has no button. Hold any button %d s: cancel.", left,
                 (int)LEARN_CANCEL);
        DrawText(s, 40, 500, 20, C_DIM);
        if (W.hold_b >= 0 && W.hold_t > 0.5f) DrawText("CANCELLING...", 40, 530, 24, C_RED);
        if (W.msg_t > 0) DrawText(W.msg, 40, 560, 26, C_HONEY);
        DrawText("Keyboard: Q or Backspace cancels.", 40, 680, 20, C_DIM);
    } else {
        DrawText(W.saved ? "PANEL LEARNED AND SAVED" : "PANEL LEARNED (NOT SAVED: SAVE DIRECTORY NOT WRITABLE)", 40, 70, 34,
                 W.saved ? C_GREEN : C_RED);
        DrawText("Every button now does this (press one to check it):", 40, 116, 22, RAYWHITE);
        draw_panel(40, 170, 1.15f, -1, slots_down(), 1, t);
        if (W.msg_t > 0) DrawText(W.msg, 40, 500, 24, C_HONEY);
        char s[80];
        snprintf(s, sizeof s, "Back in %d s.  The CONTROLS page in the game menu shows this too.",
                 (int)(LEARN_SHOW - W.done_t + 0.99f));
        DrawText(s, 40, 560, 22, C_DIM);
    }
}

/* LEFT/RIGHT (d = -1/+1) or DEAL (d = 0) on a main-page line. */
static void main_action(AppCtx *ctx, int d)
{
    Settings *s = session_settings();
    int changed = 0;
    switch (V.sel) {
    case IT_ADD:
        if (d) V.add_idx = (V.add_idx + d + N_ADD) % N_ADD;
        else { session_add_credits(k_add_amounts[V.add_idx]); V.play_req = SFX_COIN_INSERT; }
        break;
    case IT_RESET:
    case IT_RESET_STATS:
        if (d) break;
        if (V.confirm == V.sel) {
            if (V.sel == IT_RESET) session_reset_credits();
            else session_reset_stats();
            V.confirm = -1;
            V.play_req = SFX_SERVICE_BEEP;
        } else {
            V.confirm = V.sel;
        }
        return;
    case IT_DENOM:   changed = step_u16(&s->denom_cents, settings_denoms, settings_ndenoms, d ? d : 1); break;
    case IT_COIN:    changed = step_u16(&s->coin_cents, settings_coins, settings_ncoins, d ? d : 1); break;
    case IT_VMASTER: step_u8(&s->vol_master, 0, 10, d ? d : 1); changed = 1; break;
    case IT_VMUSIC:  step_u8(&s->vol_music, 0, 10, d ? d : 1); changed = 1; break;
    case IT_VSFX:    step_u8(&s->vol_sfx, 0, 10, d ? d : 1); changed = 1; break;
    case IT_MUSIC:   s->music_on = !s->music_on; changed = 1; break;
    case IT_HINT:    s->hint_default = !s->hint_default; session_set_hint_on(s->hint_default); changed = 1; break;
    case IT_DOUBLE:  s->double_up = !s->double_up; changed = 1; break;
    case IT_DIFF:    step_u8(&s->difficulty, 0, AI_NDIFF - 1, d ? d : 1); changed = 1; break;
    case IT_BUYIN:   changed = step_u32(&s->holdem_buyin, settings_buyins, settings_nbuyins, d ? d : 1); break;
    case IT_EFFECTS: if (!d) V.page = PG_EFFECTS; break;
    case IT_STATS:   if (!d) V.page = PG_STATS; break;
    case IT_LEARN:   if (!d) learn_start(0); break;
    case IT_INPUT:   if (!d) { V.page = PG_INPUT; V.cashout_held = 0; } break;
    case IT_SOUND:   if (!d) { V.page = PG_SOUND; V.cycle = 0; } break;
    case IT_SELFTEST:if (!d) { V.page = PG_SELFTEST; start_selftest(); } break;
    case IT_EXIT:
        if (!d) {
            AppState back = ctx->prev;
            if (back == APP_SERVICE || back == APP_GPUTEST) back = APP_ATTRACT;
            app_request(ctx, back);
        }
        break;
    default: break;
    }
    if (changed) {
        session_settings_changed();
        if (V.sel == IT_VSFX || V.sel == IT_VMASTER) V.play_req = SFX_SERVICE_BEEP;
    }
}

static void service_enter(AppCtx *ctx, AppState from)
{
    (void)ctx; (void)from;
    V.page = PG_MAIN;
    V.confirm = -1;
    V.cycle = 0;
    if (W.pending) {
        W.pending = 0;
        learn_start(W.autorun);
    }
}

static void leave_service(AppCtx *ctx)
{
    AppState back = ctx->prev;
    if (back == APP_SERVICE || back == APP_GPUTEST) back = APP_ATTRACT;
    app_request(ctx, back);
}

static void service_tick(AppCtx *ctx, const InputFrame *in)
{
    uint32_t p = in->pressed;
    int up = (p & (BTN_UP | BTN_HOLD1)) != 0;
    int down = (p & (BTN_DOWN | BTN_HOLD2 | BTN_BET_ONE)) != 0;
    int dec = (p & (BTN_LEFT | BTN_HOLD4)) != 0;
    int inc = (p & (BTN_RIGHT | BTN_HOLD5 | BTN_BET_MAX)) != 0;
    int ok = (p & (BTN_DEAL | BTN_OK | BTN_HOLD3 | BTN_START)) != 0;
    int back = (p & (BTN_BACK | BTN_CASH_OUT | BTN_SERVICE)) != 0;

    if (atomic_load(&V.st_state) == 2) finish_selftest();

    if (W.finished) {
        W.finished = 0;
        if (W.autorun) { leave_service(ctx); return; }
        V.page = PG_MAIN;
    }

    switch (V.page) {
    case PG_MAIN:
        if (back) { leave_service(ctx); return; }
        if (up || down) {
            V.sel = (V.sel + (down ? 1 : IT_COUNT - 1)) % IT_COUNT;
            if (V.sel == IT_CREDITS) V.sel = down ? IT_ADD : IT_EXIT;
            V.confirm = -1;
        }
        if (dec) main_action(ctx, -1);
        else if (inc) main_action(ctx, +1);
        else if (ok) main_action(ctx, 0);
        break;
    case PG_EFFECTS:
        if (back) { V.page = PG_MAIN; break; }
        if (up) V.fx_sel = (V.fx_sel + FX_COUNT - 1) % FX_COUNT;
        if (down) V.fx_sel = (V.fx_sel + 1) % FX_COUNT;
        if (ok || dec || inc) {
            uint8_t *f = effects_field(&session_settings()->effects, V.fx_sel);
            *f = dec ? 0 : inc ? 1 : !*f;
            session_settings_changed();
        }
        break;
    case PG_INPUT:
        /* Every button is under test: only SERVICE, or CASH OUT held 2 s, leaves. */
        V.cashout_held = (in->down & BTN_CASH_OUT) ? V.cashout_held + 1 : 0;
        if ((p & BTN_SERVICE) || V.cashout_held >= 120) V.page = PG_MAIN;
        break;
    case PG_SOUND:
        if (back) { V.page = PG_MAIN; V.cycle = 0; break; }
        if (up) V.sfx_sel = (V.sfx_sel + SFX_COUNT - 1) % SFX_COUNT;
        if (down) V.sfx_sel = (V.sfx_sel + 1) % SFX_COUNT;
        if (ok) { V.play_req = V.sfx_sel; V.cycle = 0; }
        if (inc || dec) { V.cycle = !V.cycle; V.cycle_wait = 0; if (V.cycle) V.sfx_sel = SFX_COUNT - 1; }
        if (V.cycle && --V.cycle_wait <= 0) {
            V.sfx_sel = (V.sfx_sel + 1) % SFX_COUNT;
            V.play_req = V.sfx_sel;
            float d = audio_sfx_duration((SfxId)V.sfx_sel);
            V.cycle_wait = (int)((d > 0 ? d : 0.2f) * 60.0f) + 20;
            if (V.sfx_sel == SFX_COUNT - 1) V.cycle = 0;
        }
        break;
    case PG_SELFTEST:
        if (back) { V.page = PG_MAIN; break; }
        if (ok && atomic_load(&V.st_state) != 1) start_selftest();
        break;
    case PG_LEARN:
        /* The panel is suspended: only the keyboard reaches here. */
        if (back) learn_stop("cancelled");
        break;
    default:
        if (back) V.page = PG_MAIN;
        break;
    }
}

static void service_present_update(const AppCtx *ctx, const GameEvent *ev, int nev, float dt)
{
    placeholder_common_update(ctx->state, ev, nev, dt);
    if (V.page == PG_LEARN) learn_update(dt);
    if (V.play_req >= 0) {
        audio_play((SfxId)V.play_req, 1.0f, 1.0f, 0.0f);
        V.play_req = -1;
    }
    for (int i = 0; i < nev; i++)
        if (ev[i].type == APP_EV_BUTTON && V.page != PG_SOUND) {
            uint32_t bit = 1u << ev[i].a;
            if (bit & (BTN_UP | BTN_DOWN | BTN_HOLD1 | BTN_HOLD2 | BTN_BET_ONE)) audio_play(SFX_MENU_MOVE, 0.7f, 1.0f, 0.0f);
            else if (bit & ~(uint32_t)(BTN_COIN | BTN_DEBUG)) audio_play(SFX_SERVICE_BEEP, 0.7f, 1.0f, 0.0f);
        }
}

/* ---- drawing -------------------------------------------------------------- */

static void line(int x, int *y, Color c, const char *fmt, ...) __attribute__((format(printf, 4, 5)));
static void line(int x, int *y, Color c, const char *fmt, ...)
{
    char s[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s, sizeof s, fmt, ap);
    va_end(ap);
    DrawText(s, x, *y, 20, c);
    *y += 24;
}

static void money(char *out, size_t cap, long long cents)
{
    snprintf(out, cap, "$%lld.%02lld", cents / 100, cents % 100);
}

static void status_panel(const AppCtx *ctx)
{
    const SessionInfo *si = session_info();
    int x = 770, y = 70;
    DrawRectangle(750, 60, 510, 600, C_PANEL);
    line(x, &y, C_HONEY, "SAVE DATA");
    char dir[80];
    snprintf(dir, sizeof dir, "%.46s", si->save_dir);
    line(x, &y, RAYWHITE, "%s", dir);
    if (si->ram_only) {
        DrawRectangle(760, y - 2, 490, 76, Fade(C_RED, 0.25f));
        line(x, &y, C_RED, "WARNING: SAVE DIRECTORY NOT WRITABLE");
        line(x, &y, C_RED, "RUNNING FROM RAM - NOTHING IS KEPT");
        char e[80];
        snprintf(e, sizeof e, "%.42s", si->save_error);
        line(x, &y, C_RED, "%s", e);
    } else {
        line(x, &y, C_GREEN, "OK - %u saves, last %.1f ms, max %.1f ms", si->saves, si->save_last_ms, si->save_max_ms);
    }
    if (si->loaded_from == SAVE_SRC_BAK || si->loaded_from == SAVE_SRC_TMP || si->corrupt_files)
        line(x, &y, C_HONEY, "RECOVERED AT START-UP (%d damaged file%s)", si->corrupt_files,
             si->corrupt_files == 1 ? "" : "s");
    y += 10;
    line(x, &y, C_HONEY, "SYSTEM");
    line(x, &y, RAYWHITE, "audio %s", si->audio_ok ? "ok" : "NO DEVICE");
    const ScreenLayout *L = screen_layout();
    line(x, &y, RAYWHITE, "display %dx%d, play x%.2f %s", L->screen_w, L->screen_h, L->scale, L->plane ? "plane" : "gpu");
    line(x, &y, RAYWHITE, "RSS %.1f MB, textures %.2f MB", perf_rss_kb() / 1024.0, texreg_bytes() / 1048576.0);
    line(x, &y, RAYWHITE, "session seed %016llx", (unsigned long long)ctx->session_seed);
    line(x, &y, RAYWHITE, "start-up: session %.0f ms (audio %.0f)", si->init_ms, si->audio_ms);
    y += 10;
    line(x, &y, C_HONEY, "BUTTONS");
    line(x, &y, C_DIM, "HOLD1 / HOLD2: up / down");
    line(x, &y, C_DIM, "HOLD4 / HOLD5: value down / up");
    line(x, &y, C_DIM, "DEAL: choose       CASH OUT: back");
}

static void draw_main(const AppCtx *ctx)
{
    const Settings *s = session_settings();
    char v[64], a[32];
    int y = 64;
    for (int i = 0; i < IT_COUNT; i++) {
        const char *name = "";
        v[0] = '\0';
        switch (i) {
        case IT_CREDITS:
            name = "CREDITS";
            money(a, sizeof a, (long long)ctx->wallet.credits * s->denom_cents);
            snprintf(v, sizeof v, "%lld  (%s)", (long long)ctx->wallet.credits, a);
            break;
        case IT_ADD: name = "ADD CREDITS"; snprintf(v, sizeof v, "< +%d >  DEAL adds", k_add_amounts[V.add_idx]); break;
        case IT_RESET: name = "RESET CREDITS TO 0"; snprintf(v, sizeof v, "%s", V.confirm == IT_RESET ? "DEAL AGAIN TO CONFIRM" : ""); break;
        case IT_DENOM: name = "DENOMINATION"; money(v, sizeof v, s->denom_cents); break;
        case IT_COIN:
            name = "COIN VALUE";
            money(a, sizeof a, s->coin_cents);
            snprintf(v, sizeof v, "%s = %lld credits", a, (long long)settings_coin_credits(s));
            break;
        case IT_VMASTER: name = "MASTER VOLUME"; snprintf(v, sizeof v, "%d", s->vol_master); break;
        case IT_VMUSIC: name = "MUSIC VOLUME"; snprintf(v, sizeof v, "%d", s->vol_music); break;
        case IT_VSFX: name = "SOUND FX VOLUME"; snprintf(v, sizeof v, "%d", s->vol_sfx); break;
        case IT_MUSIC: name = "MUSIC"; snprintf(v, sizeof v, "%s", s->music_on ? "ON" : "OFF"); break;
        case IT_HINT: name = "STRATEGY HINT AT START"; snprintf(v, sizeof v, "%s", s->hint_default ? "ON" : "OFF"); break;
        case IT_DOUBLE: name = "DOUBLE UP"; snprintf(v, sizeof v, "%s", s->double_up ? "ON" : "OFF"); break;
        case IT_DIFF: name = "HOLD'EM OPPONENTS"; snprintf(v, sizeof v, "%s", k_diff[s->difficulty < AI_NDIFF ? s->difficulty : 1]); break;
        case IT_BUYIN: name = "HOLD'EM BUY-IN"; snprintf(v, sizeof v, "%u credits", (unsigned)s->holdem_buyin); break;
        case IT_EFFECTS: name = "EFFECTS  >"; break;
        case IT_STATS: name = "STATISTICS  >"; break;
        case IT_LEARN: name = "LEARN PANEL  >"; snprintf(v, sizeof v, "%s", panel_learned() ? "learned" : "not learned yet"); break;
        case IT_INPUT: name = "INPUT TEST  >"; break;
        case IT_SOUND: name = "SOUND TEST  >"; break;
        case IT_SELFTEST: name = "SELF-TEST  >"; break;
        case IT_RESET_STATS: name = "RESET STATISTICS"; snprintf(v, sizeof v, "%s", V.confirm == IT_RESET_STATS ? "DEAL AGAIN TO CONFIRM" : ""); break;
        case IT_EXIT: name = "EXIT SERVICE"; break;
        }
        int on = V.sel == i;
        if (on) DrawRectangle(20, y - 3, 720, 27, Fade(C_HONEY, 0.25f));
        DrawText(name, 30, y, 20, on ? RAYWHITE : (i == IT_CREDITS ? C_HONEY : C_DIM));
        DrawText(v, 360, y, 20, on ? C_CYAN : RAYWHITE);
        y += 30;
    }
    status_panel(ctx);
}

static void draw_effects(void)
{
    const EffectSettings *e = effects_get();
    int y = 80;
    DrawText("Every effect can be turned off here, for profiling on the cabinet.", 30, y, 20, C_DIM);
    y += 40;
    for (int i = 0; i < FX_COUNT; i++) {
        int on = V.fx_sel == i;
        uint8_t val = *effects_field((EffectSettings *)e, i);
        if (on) DrawRectangle(20, y - 3, 700, 27, Fade(C_HONEY, 0.25f));
        DrawText(effects_label(i), 30, y, 20, on ? RAYWHITE : C_DIM);
        DrawText(val ? "ON" : "OFF", 420, y, 20, val ? C_GREEN : C_RED);
        y += 30;
    }
    DrawText("HOLD1/HOLD2: choose   DEAL: toggle   HOLD4: off   HOLD5: on   CASH OUT: back", 30, 680, 20, C_DIM);
}

static void draw_stats(void)
{
    const Stats *st = session_stats();
    static const char *const vn[SAVE_VARIANTS] = { "JACKS OR BETTER", "BONUS POKER", "DEUCES WILD" };
    int y = 70, x = 40;
    line(x, &y, C_HONEY, "DRAW POKER                hands        bet       paid    return");
    for (int v = 0; v < SAVE_VARIANTS; v++) {
        double ret = st->draw_bet[v] ? 100.0 * (double)st->draw_paid[v] / (double)st->draw_bet[v] : 0.0;
        line(x, &y, RAYWHITE, "  %-18s %10llu %10llu %10llu   %6.2f%%", vn[v], (unsigned long long)st->draw_hands[v],
             (unsigned long long)st->draw_bet[v], (unsigned long long)st->draw_paid[v], ret);
    }
    line(x, &y, RAYWHITE, "  royal flushes %llu    four deuces %llu    biggest win %lld",
         (unsigned long long)st->royals, (unsigned long long)st->four_deuces, (long long)st->biggest_win);
    line(x, &y, RAYWHITE, "  double-up rounds %llu, won %llu", (unsigned long long)st->dbl_played,
         (unsigned long long)st->dbl_won);
    y += 16;
    line(x, &y, C_HONEY, "TEXAS HOLD'EM");
    line(x, &y, RAYWHITE, "  sit-and-gos %llu   won %llu   hands %llu", (unsigned long long)st->holdem_games,
         (unsigned long long)st->holdem_wins, (unsigned long long)st->holdem_hands);
    line(x, &y, RAYWHITE, "  places 1st %llu  2nd %llu  3rd %llu  4th %llu  5th %llu  6th %llu",
         (unsigned long long)st->holdem_places[0], (unsigned long long)st->holdem_places[1],
         (unsigned long long)st->holdem_places[2], (unsigned long long)st->holdem_places[3],
         (unsigned long long)st->holdem_places[4], (unsigned long long)st->holdem_places[5]);
    line(x, &y, RAYWHITE, "  buy-ins %llu credits, prizes %llu credits", (unsigned long long)st->holdem_buyins,
         (unsigned long long)st->holdem_prizes);
    y += 16;
    line(x, &y, C_HONEY, "CREDITS");
    line(x, &y, RAYWHITE, "  coins in %llu   added in service %llu   resets %llu", (unsigned long long)st->coins_in,
         (unsigned long long)st->credits_added, (unsigned long long)st->credit_resets);
    DrawText("CASH OUT: back", 30, 680, 20, C_DIM);
}

static void draw_input(const AppCtx *ctx)
{
    const int n = INPUT_NBUTTONS, gap = 6, w = 180, h = 44;
    DrawText("Press every button: it lights while held. SERVICE (or CASH OUT held 2 s) leaves.", 30, 70, 20, C_DIM);
    for (int i = 0; i < n; i++) {
        int x = 30 + (i % 6) * (w + gap), y = 110 + (i / 6) * (h + gap);
        uint32_t bit = 1u << i;
        Color fill = C_PANEL, txt = C_DIM;
        if (ctx->input.down & bit) { fill = C_HONEY; txt = BLACK; }
        if (ctx->input.pressed & bit) { fill = RAYWHITE; txt = BLACK; }
        DrawRectangle(x, y, w, h, fill);
        DrawRectangleLines(x, y, w, h, Fade(C_HONEY, 0.6f));
        const char *name = input_button_name(i);
        int tw = MeasureText(name, 20);
        DrawText(name, x + (w - tw) / 2, y + 12, 20, txt);
    }
    int y = 330;
    char buf[200];
    snprintf(buf, sizeof buf, "slider %+d   touch %s %d,%d", ctx->input.slider, ctx->input.touch ? "DOWN" : "up",
             ctx->input.touch_x, ctx->input.touch_y);
    DrawText(buf, 30, y, 20, RAYWHITE);
    y += 34;
    snprintf(buf, sizeof buf, "joysticks: %d  (numbers as in EmulationStation's es_input.cfg: JOYn_Bk)", joy_count());
    DrawText(buf, 30, y, 20, C_HONEY);
    y += 28;
    for (int j = 0; j < joy_count() && j < 6; j++) {
        char held[100] = "";
        int p = 0;
        for (int b = 0; b < joy_buttons(j) && p < 90; b++)
            if (joy_button_down(j, b)) p += snprintf(held + p, sizeof held - (size_t)p, " B%d", b);
        snprintf(buf, sizeof buf, "JOY%d %.24s  axis0 %+.2f axis1 %+.2f hat %d  down:%s", j, joy_name(j), joy_axis(j, 0),
                 joy_axis(j, 1), joy_hat(j, 0), held);
        DrawText(buf, 30, y, 20, C_CYAN);
        y += 26;
    }
    /* The panel by position, lit while held, with what each button does. */
    draw_panel(40, 456, 0.82f, -1, slots_down(), 1, ctx->time);
    if (ctx->input.touch) DrawCircleLines(ctx->input.touch_x, ctx->input.touch_y, 18, C_CYAN);
}

static void draw_sound(void)
{
    DrawText("DEAL: play   HOLD4/HOLD5: play ALL in turn (on/off)   CASH OUT: back", 30, 64, 20, C_DIM);
    int cols = 3, rows = (SFX_COUNT + cols - 1) / cols;
    for (int i = 0; i < SFX_COUNT; i++) {
        int x = 30 + (i / rows) * 410, y = 100 + (i % rows) * 36;
        int on = V.sfx_sel == i;
        if (on) DrawRectangle(x - 6, y - 4, 400, 30, Fade(C_HONEY, 0.3f));
        char s[80];
        snprintf(s, sizeof s, "%2d %-16s %.2f s%s", i, audio_sfx_name((SfxId)i), audio_sfx_duration((SfxId)i),
                 audio_is_playing((SfxId)i) ? "  <" : "");
        DrawText(s, x, y, 20, on ? RAYWHITE : C_DIM);
    }
    if (V.cycle) DrawText("PLAYING ALL...", 30, 680, 20, C_CYAN);
    if (!session_info()->audio_ok) DrawText("NO AUDIO DEVICE", 900, 680, 20, C_RED);
}

static void draw_selftest(void)
{
    int st = atomic_load(&V.st_state);
    const SelfTestReport *r = &V.report;
    if (st == 0) { DrawText("DEAL: run the self-test", 30, 80, 20, C_DIM); return; }
    if (st < 3) {
        DrawText("RUNNING...", 30, 80, 30, C_CYAN);
        return;
    }
    int y = 70;
    for (int i = 0; i < r->n; i++) {
        const SelfTestItem *it = &r->item[i];
        DrawText(it->pass ? "PASS" : "FAIL", 30, y, 20, it->pass ? C_GREEN : C_RED);
        DrawText(it->name, 100, y, 20, RAYWHITE);
        char d[140];
        snprintf(d, sizeof d, "%s%s", it->detail, "");
        DrawText(d, 330, y, 20, C_DIM);
        y += 30;
    }
    y += 20;
    char s[80];
    snprintf(s, sizeof s, "%s  -  %d CHECKS, %d FAILED", r->failures ? "FAIL" : "PASS", r->n, r->failures);
    DrawText(s, 30, y, 40, r->failures ? C_RED : C_GREEN);
    DrawText("DEAL: run again   CASH OUT: back", 30, 680, 20, C_DIM);
}

static void service_present_draw(const AppCtx *ctx)
{
    static const char *const titles[] = { "SERVICE", "SERVICE - EFFECTS", "SERVICE - STATISTICS", "SERVICE - INPUT TEST",
                                          "SERVICE - SOUND TEST", "SERVICE - SELF-TEST", "SERVICE - LEARN PANEL" };
    DrawRectangle(0, 0, PLAY_W, PLAY_H, (Color){ 10, 12, 20, 255 });
    DrawRectangle(0, 0, PLAY_W, 44, (Color){ 40, 30, 10, 255 });
    DrawText(titles[V.page], 20, 10, 30, C_HONEY);
    const SessionInfo *si = session_info();
    if (si->ram_only && ((int)(ctx->time * 2) % 2 == 0)) DrawText("RUNNING FROM RAM", 960, 12, 24, C_RED);
    switch (V.page) {
    case PG_MAIN: draw_main(ctx); break;
    case PG_EFFECTS: draw_effects(); break;
    case PG_STATS: draw_stats(); break;
    case PG_INPUT: draw_input(ctx); break;
    case PG_SOUND: draw_sound(); break;
    case PG_SELFTEST: draw_selftest(); break;
    case PG_LEARN: draw_learn(ctx); break;
    }
}

static void service_shutdown(void)
{
    if (W.active) panel_suspend(0);
    int st = atomic_load(&V.st_state);
    if (st >= 1 && !pthread_equal(V.thr, pthread_self())) pthread_join(V.thr, NULL);
    atomic_store(&V.st_state, 0);
}

const AppMode mode_service = {
    .name = "service",
    .enter = service_enter,
    .tick = service_tick,
    .present_update = service_present_update,
    .present_draw = service_present_draw,
    .shutdown = service_shutdown,
};
