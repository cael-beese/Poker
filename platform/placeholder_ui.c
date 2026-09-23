/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* placeholder_ui.c - shared placeholder drawing (cards, paytable, meters),
 * the menu and attract screens, and the app-level sounds and music.
 * See placeholder_view.h: presentation only, replaceable as a whole. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "raylib.h"
#include "audio.h"
#include "platform/app.h"
#include "platform/placeholder_view.h"
#include "platform/screen.h"
#include "platform/session.h"

#define C_HONEY   ((Color){ 232, 170, 40, 255 })
#define C_AMBER   ((Color){ 255, 128, 16, 255 })
#define C_MAGENTA ((Color){ 255, 40, 200, 255 })
#define C_CYAN    ((Color){ 40, 230, 255, 255 })
#define C_DIM     ((Color){ 130, 120, 110, 255 })
#define C_PANEL   ((Color){ 30, 26, 32, 255 })
#define C_RED     ((Color){ 210, 30, 40, 255 })
#define C_INK     ((Color){ 25, 25, 30, 255 })

void ph_text_centered(const char *s, int cx, int y, int size, uint32_t rgba)
{
    int w = MeasureText(s, size);
    DrawText(s, cx - w / 2, y, size, GetColor(rgba));
}

static void text_c(const char *s, int cx, int y, int size, Color c)
{
    int w = MeasureText(s, size);
    DrawText(s, cx - w / 2, y, size, c);
}

static void neon(const char *s, int cx, int y, int size, Color glow, float pulse)
{
    int w = MeasureText(s, size), x = cx - w / 2;
    for (int i = 3; i >= 1; i--) {
        Color g = Fade(glow, (0.10f + 0.08f * pulse) * (float)(4 - i));
        DrawText(s, x - i, y, size, g);
        DrawText(s, x + i, y, size, g);
        DrawText(s, x, y - i, size, g);
        DrawText(s, x, y + i, size, g);
    }
    DrawText(s, x, y, size, ColorBrightness(glow, 0.55f + 0.3f * pulse));
}

void ph_credits_line(int64_t credits, int denom_cents, char *out, int cap)
{
    long long cents = (long long)credits * (denom_cents > 0 ? denom_cents : 1);
    snprintf(out, (size_t)cap, "CREDITS %lld  ($%lld.%02lld)", (long long)credits, cents / 100, cents % 100);
}

/* ---- cards --------------------------------------------------------------- */

static void suit_shape(int suit, float cx, float cy, float s, Color c)
{
    switch (suit) {
    case SUIT_H:
        DrawCircleV((Vector2){ cx - s * 0.25f, cy - s * 0.12f }, s * 0.27f, c);
        DrawCircleV((Vector2){ cx + s * 0.25f, cy - s * 0.12f }, s * 0.27f, c);
        DrawTriangle((Vector2){ cx - s * 0.52f, cy - s * 0.02f }, (Vector2){ cx, cy + s * 0.5f },
                     (Vector2){ cx + s * 0.52f, cy - s * 0.02f }, c);
        break;
    case SUIT_D:
        DrawTriangle((Vector2){ cx, cy - s * 0.55f }, (Vector2){ cx - s * 0.4f, cy }, (Vector2){ cx + s * 0.4f, cy }, c);
        DrawTriangle((Vector2){ cx - s * 0.4f, cy }, (Vector2){ cx, cy + s * 0.55f }, (Vector2){ cx + s * 0.4f, cy }, c);
        break;
    case SUIT_C:
        DrawCircleV((Vector2){ cx, cy - s * 0.25f }, s * 0.22f, c);
        DrawCircleV((Vector2){ cx - s * 0.25f, cy + s * 0.08f }, s * 0.22f, c);
        DrawCircleV((Vector2){ cx + s * 0.25f, cy + s * 0.08f }, s * 0.22f, c);
        DrawTriangle((Vector2){ cx, cy }, (Vector2){ cx - s * 0.18f, cy + s * 0.5f }, (Vector2){ cx + s * 0.18f, cy + s * 0.5f }, c);
        break;
    default: /* spade */
        DrawCircleV((Vector2){ cx - s * 0.23f, cy + s * 0.1f }, s * 0.25f, c);
        DrawCircleV((Vector2){ cx + s * 0.23f, cy + s * 0.1f }, s * 0.25f, c);
        DrawTriangle((Vector2){ cx, cy - s * 0.5f }, (Vector2){ cx - s * 0.47f, cy + s * 0.05f },
                     (Vector2){ cx + s * 0.47f, cy + s * 0.05f }, c);
        DrawTriangle((Vector2){ cx, cy + s * 0.1f }, (Vector2){ cx - s * 0.18f, cy + s * 0.55f },
                     (Vector2){ cx + s * 0.18f, cy + s * 0.55f }, c);
        break;
    }
}

void ph_card(float x, float y, float w, float h, Card c, int face_up, float alpha)
{
    Rectangle r = { x, y, w, h };
    if (c == CARD_NONE) {
        DrawRectangleRoundedLinesEx(r, 0.08f, 6, 2, Fade(C_DIM, 0.5f * alpha));
        return;
    }
    if (!face_up) {
        DrawRectangleRounded(r, 0.08f, 6, Fade((Color){ 70, 20, 60, 255 }, alpha));
        Rectangle in = { x + w * 0.08f, y + w * 0.08f, w * 0.84f, h - w * 0.16f };
        DrawRectangleRoundedLinesEx(in, 0.08f, 6, 2, Fade(C_HONEY, alpha));
        for (int i = 0; i < 3; i++)
            DrawPolyLinesEx((Vector2){ x + w / 2, y + h * (0.3f + 0.2f * i) }, 6, w * 0.16f, 30, 2, Fade(C_HONEY, 0.7f * alpha));
        return;
    }
    static const char *const ranks[13] = { "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K", "A" };
    int rk = card_rank(c), st = card_suit(c);
    Color ink = (st == SUIT_H || st == SUIT_D) ? C_RED : C_INK;
    DrawRectangleRounded(r, 0.08f, 6, Fade((Color){ 246, 242, 232, 255 }, alpha));
    DrawRectangleRoundedLinesEx(r, 0.08f, 6, 2, Fade(C_AMBER, alpha));
    int fs = (int)(h * 0.2f);
    if (fs < 10) fs = 10;
    DrawText(ranks[rk], (int)(x + w * 0.08f), (int)(y + h * 0.04f), fs, Fade(ink, alpha));
    suit_shape(st, x + w * 0.08f + fs * 0.3f, y + h * 0.06f + fs * 1.35f, fs * 0.7f, Fade(ink, alpha));
    suit_shape(st, x + w * 0.58f, y + h * 0.6f, w * 0.5f, Fade(ink, alpha));
    if (rk >= 9 && rk <= 11) {   /* court cards: a letter in a frame, placeholder art */
        DrawRectangleLinesEx((Rectangle){ x + w * 0.3f, y + h * 0.25f, w * 0.55f, h * 0.65f }, 2, Fade(C_HONEY, alpha));
    }
}

/* ---- paytable -------------------------------------------------------------- */

void ph_paytable(int variant, int bet, int lit_cat, int flash, float x, float y, float w, double time)
{
    const DrawPaytable *pt = draw_paytable(variant);
    if (!pt) return;
    const int rh = 20, fs = 20;
    float colw = w * 0.11f, name_w = w - 5 * colw;
    float h = (float)(pt->rows * rh + 10);
    DrawRectangleRec((Rectangle){ x, y, w, h }, (Color){ 12, 10, 40, 230 });
    DrawRectangleLinesEx((Rectangle){ x, y, w, h }, 2, C_HONEY);
    if (bet >= 1 && bet <= 5)
        DrawRectangleRec((Rectangle){ x + name_w + (bet - 1) * colw, y + 2, colw, h - 4 }, Fade(C_RED, 0.55f));
    for (int i = 0; i < pt->rows; i++) {
        int cat = pt->row_cat[i];
        float ry = y + 5 + (float)(i * rh);
        int on = cat == lit_cat && lit_cat != DC_NONE;
        if (on && (!flash || fmod(time * 4.0, 1.0) < 0.6))
            DrawRectangleRec((Rectangle){ x + 2, ry - 1, w - 4, (float)rh }, Fade(C_HONEY, 0.35f));
        Color tc = on ? RAYWHITE : C_HONEY;
        DrawText(draw_cat_name(cat), (int)(x + 12), (int)ry, fs, tc);
        for (int b = 1; b <= 5; b++) {
            char s[16];
            snprintf(s, sizeof s, "%d", draw_pay(variant, cat, b));
            int tw = MeasureText(s, fs);
            DrawText(s, (int)(x + name_w + b * colw - tw - 10), (int)ry, fs, tc);
        }
    }
}

/* ---- sounds of app events, music by state ------------------------------------ */

void placeholder_common_update(int app_state, const GameEvent *ev, int nev, float dt)
{
    static int music_state = -1, music_on = -1, audio_seen = 0;
    int want_on = session_settings()->music_on;
    int audio_now = session_info()->audio_ok;
    if (audio_now != audio_seen) {      /* audio arrived after the first frame: start the music */
        audio_seen = audio_now;
        music_state = -1;
    }
    if (app_state != music_state || want_on != music_on) {
        MusicId m = MUSIC_NONE;
        if (app_state == APP_ATTRACT) m = MUSIC_ATTRACT;
        else if (app_state == APP_MENU || app_state == APP_DRAW || app_state == APP_HOLDEM) m = MUSIC_LOUNGE;
        /* The lounge loop carries on from menu into the games. */
        int same = (music_state == APP_MENU || music_state == APP_DRAW || music_state == APP_HOLDEM) &&
                   m == MUSIC_LOUNGE && want_on == music_on;
        if (!same) audio_music(m, m != MUSIC_NONE && want_on);
        audio_ambience(want_on && (app_state == APP_DRAW || app_state == APP_HOLDEM));
        music_state = app_state;
        music_on = want_on;
    }
    for (int i = 0; i < nev; i++) {
        const GameEvent *e = &ev[i];
        if (e->type == APP_EV_COIN) audio_play(SFX_COIN_INSERT, 1.0f, 1.0f, 0.0f);
        else if (e->type == APP_EV_STATE && e->b != APP_SERVICE && e->a != APP_SERVICE)
            audio_play(SFX_WHOOSH, 0.6f, 1.0f, 0.0f);
    }
    audio_update(dt);
}

/* ---- menu -------------------------------------------------------------------- */

static const char *const k_menu_names[MENU_NGAMES] = {
    "JACKS OR BETTER", "BONUS POKER", "DEUCES WILD", "TEXAS HOLD'EM"
};
static const char *const k_menu_sub[MENU_NGAMES] = {
    "9/6  -  99.54%", "8/5  -  99.17%", "FULL PAY  -  100.76%", "6-MAX SIT & GO"
};

void menu_placeholder_update(const MenuViewInfo *v, const GameEvent *ev, int nev, float dt)
{
    (void)v; (void)dt;
    for (int i = 0; i < nev; i++) {
        const GameEvent *e = &ev[i];
        if (e->type != APP_EV_BUTTON) continue;
        uint32_t bit = 1u << e->a;
        if (bit & (BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT | BTN_BET_ONE | BTN_HOLD1 | BTN_HOLD2 | BTN_HOLD3 | BTN_HOLD4))
            audio_play(SFX_MENU_MOVE, 0.8f, 1.0f, 0.0f);
        else if (bit & BTN_HOLD5) audio_play(SFX_HOLD_ON, 0.8f, 1.0f, 0.0f);
        else if (bit & (BTN_DEAL | BTN_OK | BTN_START | BTN_BET_MAX)) audio_play(SFX_MENU_SELECT, 1.0f, 1.0f, 0.0f);
    }
}

void menu_placeholder_view(const MenuViewInfo *v, double time)
{
    float pulse = 0.5f + 0.5f * (float)sin(time * 3.0);
    DrawRectangleGradientV(0, 0, PLAY_W, PLAY_H, (Color){ 22, 18, 26, 255 }, (Color){ 8, 7, 10, 255 });
    neon("BEESE'S POKER LOUNGE", PLAY_W / 2, 26, 50, C_HONEY, pulse);
    text_c("CHOOSE YOUR GAME", PLAY_W / 2, 92, 20, C_DIM);
    for (int i = 0; i < MENU_NGAMES; i++) {
        Rectangle r = { 60.0f + i * 295.0f, 140, 275, 250 };
        int on = v->sel == i;
        DrawRectangleRounded(r, 0.06f, 6, on ? Fade(C_HONEY, 0.22f) : C_PANEL);
        DrawRectangleRoundedLinesEx(r, 0.06f, 6, on ? 4.0f : 2.0f, on ? C_HONEY : C_DIM);
        char key[16];
        snprintf(key, sizeof key, "HOLD %d", i + 1);
        text_c(key, (int)(r.x + r.width / 2), (int)r.y + 14, 20, on ? C_CYAN : C_DIM);
        text_c(k_menu_names[i], (int)(r.x + r.width / 2), (int)r.y + 80, i == MENU_GAME_HOLDEM ? 28 : 26,
               on ? RAYWHITE : C_HONEY);
        text_c(k_menu_sub[i], (int)(r.x + r.width / 2), (int)r.y + 130, 20, C_DIM);
        if (i == MENU_GAME_HOLDEM) {
            char s[64];
            snprintf(s, sizeof s, "BUY-IN %lld", (long long)v->buyin);
            text_c(s, (int)(r.x + r.width / 2), (int)r.y + 170, 20, C_DIM);
        } else {
            text_c("BET 1-5, BET MAX 5", (int)(r.x + r.width / 2), (int)r.y + 170, 20, C_DIM);
        }
    }
    Rectangle h = { 60, 410, 1160, 60 };
    DrawRectangleRounded(h, 0.2f, 6, C_PANEL);
    DrawRectangleRoundedLinesEx(h, 0.2f, 6, 2, v->hint_on ? C_CYAN : C_DIM);
    char s[160];
    snprintf(s, sizeof s, "HOLD 5:  STRATEGY HINT  %s", v->hint_on ? "ON" : "OFF");
    text_c(s, PLAY_W / 2, 428, 26, v->hint_on ? C_CYAN : C_DIM);

    if (((int)(time * 2.0)) % 2 == 0) neon("PRESS DEAL TO PLAY", PLAY_W / 2, 500, 40, C_MAGENTA, 1.0f);
    ph_credits_line(v->credits, v->denom_cents, s, sizeof s);
    text_c(s, PLAY_W / 2, 570, 30, C_HONEY);
    if (v->credits <= 0) {
        snprintf(s, sizeof s, "INSERT COIN  -  %lld CREDITS PER COIN", (long long)v->coin_credits);
        text_c(s, PLAY_W / 2, 610, 20, C_CYAN);
    }
    if (v->message) text_c(v->message, PLAY_W / 2, 640, 24, C_AMBER);
    text_c("HOLD 1-4 / BET ONE: CHOOSE    DEAL: PLAY    HOLD 5: HINT    COIN: CREDITS    COIN+START: EXIT",
           PLAY_W / 2, 690, 20, C_DIM);
}

/* ---- attract ------------------------------------------------------------------- */

void attract_placeholder_view(const AttractViewInfo *v, double time)
{
    float pulse = 0.5f + 0.5f * (float)sin(time * 3.0);
    int blink = ((int)(time * 2.0)) % 2 == 0;
    if (v->phase == ATTRACT_TITLE) {
        DrawRectangleGradientV(0, 0, PLAY_W, PLAY_H, (Color){ 22, 18, 26, 255 }, (Color){ 8, 7, 10, 255 });
        for (int row = 0; row < 9; row++)
            for (int col = 0; col < 16; col++) {
                float x = 60.0f + col * 80.0f + (row & 1) * 40.0f, y = 40.0f + row * 70.0f;
                float a = 0.05f + 0.05f * sinf((float)time * 1.3f + col * 0.5f + row * 0.7f);
                DrawPolyLinesEx((Vector2){ x, y }, 6, 44, 30, 2, Fade(C_HONEY, a));
            }
        neon("BEESE'S", PLAY_W / 2, 40, 60, C_MAGENTA, pulse);
        neon("POKER LOUNGE", PLAY_W / 2, 105, 90, C_HONEY, pulse);
        ph_paytable(v->variant, 5, DC_NONE, 0, 290, 225, 700, time);
        text_c(draw_variant_name(v->variant), PLAY_W / 2, 200, 20, C_CYAN);
        if (blink) neon("PRESS DEAL", PLAY_W / 2, 480, 50, C_CYAN, 1.0f);
        text_c("JACKS OR BETTER  *  BONUS POKER  *  DEUCES WILD  *  TEXAS HOLD'EM", PLAY_W / 2, 560, 20, C_DIM);
        text_c("FREE PLAY - FOR AMUSEMENT ONLY - REAL SHUFFLES, REAL ODDS", PLAY_W / 2, 600, 20, C_DIM);
        char s[96];
        snprintf(s, sizeof s, "CREDITS %lld", (long long)v->credits);
        text_c(s, PLAY_W / 2, 660, 30, C_HONEY);
        return;
    }
    /* Banner over a demo game. */
    DrawRectangle(0, 640, PLAY_W, 76, Fade(BLACK, 0.7f));
    if (blink) neon("PRESS DEAL", PLAY_W / 2, 652, 50, C_CYAN, 1.0f);
    else neon(v->phase == ATTRACT_DRAW ? "DEMO - DRAW POKER" : "DEMO - TEXAS HOLD'EM", PLAY_W / 2, 658, 40,
              C_MAGENTA, pulse);
}
