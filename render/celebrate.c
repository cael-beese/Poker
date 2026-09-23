/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* celebrate.c - see celebrate.h. */
#include "render/celebrate.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "platform/screen.h"
#include "render/particles.h"
#include "render/sprites.h"

#define PI_F 3.14159265f

static const float k_dur[] = { 0, 1.2f, 2.4f, 4.6f, 7.0f };
static const float k_out = 0.8f;

static void cue(Celebration *c, CelebCue q, float a)
{
    if (c->cue) c->cue(c->user, q, a);
}

static void on_tick(void *user, float pitch) { cue(user, CUE_TICK, pitch); }
static void on_end(void *user) { cue(user, CUE_COUNT_END, 0); }

void celeb_init(Celebration *c, Shake *shake, FxClock *clock, BulbRing *bulbs, CelebCueFn fn, void *user)
{
    memset(c, 0, sizeof *c);
    c->shake = shake;
    c->clock = clock;
    c->bulbs = bulbs;
    c->cue = fn;
    c->user = user;
    c->dim = 1;
}

void celeb_start(Celebration *c, WinTier tier, long long amount, const char *title, Vector2 focus)
{
    if (tier == WIN_NONE) return;
    c->tier = tier;
    c->active = 1;
    c->skipping = 0;
    c->t = 0;
    c->out = 0;
    c->dur = k_dur[tier];
    c->amount = amount;
    c->focus = focus;
    c->step = 0;
    c->next_fx = 0;
    const char *def = tier == WIN_JACKPOT ? "JACKPOT" : tier == WIN_BIG ? "BIG WIN" : "WINNER";
    snprintf(c->title, sizeof c->title, "%s", title ? title : def);
    meter_set(&c->meter, 0);
    c->meter.on_tick = on_tick;
    c->meter.on_end = on_end;
    c->meter.user = c;

    switch (tier) {
    case WIN_SMALL:
        pfx_sparkle(focus.x, focus.y, 150, 16);
        cue(c, CUE_CHIME, 0);
        break;
    case WIN_MEDIUM:
        pfx_coin_burst(focus.x, focus.y, 0.5f);
        if (c->bulbs) bulbs_set(c->bulbs, BULBS_ALTERNATE, 6);
        cue(c, CUE_COINS, 0);
        break;
    case WIN_BIG:
        if (c->clock) clock_hitpause(c->clock, 2);
        if (c->shake) shake_add(c->shake, 0.55f);
        if (c->bulbs) bulbs_set(c->bulbs, BULBS_CHASE, 16);
        pfx_coin_burst(focus.x, focus.y, 0.8f);
        pfx_honey_fountain(120, PLAY_H + 10, 2.4f, 0.6f);
        pfx_honey_fountain(PLAY_W - 120, PLAY_H + 10, 2.4f, 0.6f);
        flash_fire(&c->flash, 0.35f, 0.25f);
        cue(c, CUE_BIG, 0);
        break;
    case WIN_JACKPOT:
        if (c->clock) {
            clock_hitpause(c->clock, 3);
            clock_slowmo(c->clock, 0.3f, 0.5f, 1.3f);
        }
        if (c->shake) shake_add(c->shake, 0.85f);
        if (c->bulbs) bulbs_set(c->bulbs, BULBS_FLASH, 10);
        flash_fire(&c->flash, 0.9f, 0.4f);
        pfx_coin_burst(focus.x, focus.y, 1.0f);
        cue(c, CUE_REVEAL, 0);
        break;
    default: break;
    }
}

static void begin_out(Celebration *c)
{
    pfx_stop_emitters();
    if (c->t < c->dur - k_out) c->t = c->dur - k_out;
}

void celeb_skip(Celebration *c)
{
    if (!c->active || c->skipping) return;
    c->skipping = 1;
    meter_skip(&c->meter);
    if (c->clock) clock_cancel(c->clock);
    cue(c, CUE_SKIP, 0);
    pfx_stop_emitters();
    /* A quick 0.35 s fade instead of the normal wind-down. */
    c->dur = c->t + 0.35f;
}

void celeb_update(Celebration *c, float dt, float real_dt)
{
    flash_update(&c->flash, real_dt);
    meter_update(&c->meter, dt);
    if (!c->active) {
        c->dim += (1 - c->dim) * fminf(1, real_dt * 4);
        c->spot = fmaxf(0, c->spot - real_dt * 2);
        return;
    }
    c->t += real_dt;
    float t = c->t;
    float out_start = c->dur - (c->skipping ? 0.35f : k_out);
    c->out = t > out_start ? fminf(1, (t - out_start) / (c->dur - out_start)) : 0;

    float dim_target = 1, spot_target = 0;
    if (c->tier == WIN_JACKPOT) {
        if (c->step == 0 && t >= 0.25f) {
            c->step = 1;
            cue(c, CUE_JACKPOT, 0);
            if (c->bulbs) bulbs_set(c->bulbs, BULBS_CHASE, 22);
            pfx_fireworks(5.4f, 1.0f);
            pfx_coin_shower(5.2f, 1.0f);
            pfx_honey_fountain(90, PLAY_H + 10, 4.6f, 1.0f);
            pfx_honey_fountain(PLAY_W - 90, PLAY_H + 10, 4.6f, 1.0f);
            pfx_confetti_burst(PLAY_W * 0.5f, PLAY_H * 0.42f, 1.0f);
        }
        if (c->step == 1 && t >= 1.0f) {
            c->step = 2;
            meter_count(&c->meter, (double)c->amount, 4.2f);
        }
        if (c->step == 2 && t >= 2.7f) {
            c->step = 3;
            pfx_confetti_burst(PLAY_W * 0.25f, PLAY_H * 0.35f, 0.7f);
            if (c->shake) shake_add(c->shake, 0.3f);
        }
        if (c->step == 3 && t >= 4.3f) {
            c->step = 4;
            pfx_confetti_burst(PLAY_W * 0.75f, PLAY_H * 0.35f, 0.7f);
            if (c->shake) shake_add(c->shake, 0.3f);
        }
        if (!c->skipping && c->step >= 1 && t < out_start) { dim_target = 0.42f; spot_target = 1; }
    } else if (c->tier == WIN_BIG) {
        if (c->step == 0 && t >= 0.4f) {
            c->step = 1;
            meter_count(&c->meter, (double)c->amount, 2.6f);
        }
        if (!c->skipping && t < out_start) dim_target = 0.8f;
    }
    if (t >= out_start && c->step < 99) {
        c->step = 99;
        begin_out(c);
        if (c->bulbs) bulbs_set(c->bulbs, BULBS_IDLE, 8);
    }
    c->dim += (dim_target - c->dim) * fminf(1, real_dt * 5);
    c->spot += (spot_target - c->spot) * fminf(1, real_dt * 3);
    if (t >= c->dur) {
        c->active = 0;
        if (c->meter.running) meter_skip(&c->meter);
        cue(c, CUE_DONE, 0);
    }
}

float celeb_dim(const Celebration *c) { return c->dim; }

static void beam(Vector2 apex, float angle, float len, float width, Color col, float k)
{
    float dx = sinf(angle), dy = cosf(angle);
    float nx = dy, ny = -dx;
    Vector2 p[4] = {
        { apex.x - nx * width * 0.5f, apex.y - ny * width * 0.5f },
        { apex.x + dx * len - nx * width * 0.5f, apex.y + dy * len - ny * width * 0.5f },
        { apex.x + dx * len + nx * width * 0.5f, apex.y + dy * len + ny * width * 0.5f },
        { apex.x + nx * width * 0.5f, apex.y + ny * width * 0.5f },
    };
    gfx_quad(sprite(SPR_BEAM), p, gfx_add(col, k));
    /* The pool of light where it lands. */
    float fx = apex.x + dx * len * 0.92f, fy = apex.y + dy * len * 0.92f;
    gfx_spr_rot(sprite(SPR_GLOW), fx, fy, width * 1.1f, width * 0.45f, 0, gfx_add(col, 0.5f * k));
}

void celeb_draw_back(const Celebration *c, double time)
{
    if (c->spot <= 0.01f) return;
    float t = (float)time;
    float k = 0.55f * c->spot;
    beam((Vector2){ 250, -30 }, 0.45f * sinf(t * 1.1f) - 0.25f, 860, 520, (Color){ 255, 210, 120, 255 }, k);
    beam((Vector2){ PLAY_W - 250, -30 }, 0.45f * sinf(t * 1.1f + 2.0f) + 0.25f, 860, 520, (Color){ 255, 90, 220, 255 }, k);
}

void celeb_draw_front(const Celebration *c, double time)
{
    if (c->active || c->flash.v > 0) {
        float lt = c->t;
        switch (c->tier) {
        case WIN_SMALL: {
            float k = clampf(lt / 0.9f, 0, 1);
            float s = 120 + 260 * ease(EASE_OUT_CUBIC, k);
            gfx_spr_rot(sprite(SPR_RING), c->focus.x, c->focus.y, s, s * 0.8f, 0, gfx_add(UI_GOLD, 1.2f * (1 - k)));
            break;
        }
        case WIN_MEDIUM: {
            float ap = clampf(lt / 0.5f, 0, 1);
            ui_banner(c->title, c->focus.x, c->focus.y - 175, 56, ap, c->out, 0, time);
            break;
        }
        case WIN_BIG: {
            float ap = clampf(lt / 0.6f, 0, 1);
            ui_banner(c->title, PLAY_W * 0.5f, 250, 104, ap, c->out, 1, time);
            if (lt > 0.35f) {
                float mo = clampf((lt - 0.35f) / 0.3f, 0, 1) * (1 - c->out);
                if (mo > 0.02f) {
                    Rectangle r = { PLAY_W * 0.5f - 180, 330 + 30 * (1 - ease(EASE_OUT_BACK, mo)), 360, 104 };
                    meter_draw(&c->meter, r, "YOU WIN", UI_GOLD);
                }
            }
            break;
        }
        case WIN_JACKPOT: {
            float ap = clampf((lt - 0.25f) / 0.7f, 0, 1);
            ui_banner(c->title, PLAY_W * 0.5f, 240, 120, ap, c->out, 1, time);
            if (lt > 0.9f) {
                float mo = clampf((lt - 0.9f) / 0.35f, 0, 1) * (1 - c->out);
                if (mo > 0.02f) {
                    Rectangle r = { PLAY_W * 0.5f - 210, 340 + 40 * (1 - ease(EASE_OUT_BACK, mo)), 420, 120 };
                    meter_draw(&c->meter, r, "JACKPOT PAYS", UI_MAGENTA);
                }
            }
            break;
        }
        default: break;
        }
    }
    if (c->flash.v > 0.01f) gfx_rect(0, 0, PLAY_W, PLAY_H, gfx_add((Color){ 255, 244, 220, 255 }, c->flash.v));
}
