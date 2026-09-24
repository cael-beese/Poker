/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* ui.h - the neon UI kit for the presentation modes.
 *
 * Widgets keep their own small animation state in a struct the caller owns
 * (no heap, no globals), updated from present_update and drawn from
 * present_draw. Button feedback: call ui_button_press() in present_update
 * on the frame the button's input is pressed (ctx->input.pressed) - the
 * pressed look is drawn in that same frame. */
#ifndef BPL_RENDER_UI_H
#define BPL_RENDER_UI_H

#include <stdint.h>

#include "raylib.h"
#include "render/gfx.h"
#include "render/text.h"
#include "render/tween.h"

/* House colours (straight alpha). */
#define UI_HONEY    ((Color){ 232, 170, 40, 255 })
#define UI_GOLD     ((Color){ 255, 210, 90, 255 })
#define UI_AMBER    ((Color){ 255, 128, 16, 255 })
#define UI_MAGENTA  ((Color){ 255, 40, 200, 255 })
#define UI_CYAN     ((Color){ 40, 230, 255, 255 })
#define UI_IVORY    ((Color){ 248, 241, 226, 255 })
#define UI_INK      ((Color){ 14, 12, 16, 255 })

/* ---- background --------------------------------------------------------- */
/* The play-space backdrop: dark plum honeycomb with a warm centre, painted
 * at start-up into its own 1280x720 texture, plus a few breathing hex
 * lights. dim multiplies it (the jackpot takeover darkens the room). */
void ui_background(double time, float dim);

/* ---- panels and buttons ------------------------------------------------- */
/* Dark glass panel with a neon edge. glow_k 0..1. */
void ui_panel(Rectangle r, Color neon, float glow_k, float alpha);

typedef enum { BTN_STATE_OFF, BTN_STATE_ON, BTN_STATE_LIT } UiButtonState;   /* disabled, available, calling for a press */
typedef struct {
    float press;     /* 1 on the press frame, decays   */
    float lit;       /* eased towards the lit state     */
} UiButton;

void ui_button_press(UiButton *b);
void ui_button_update(UiButton *b, UiButtonState st, float dt);
void ui_button_draw(const UiButton *b, Rectangle r, const char *label, Color neon, UiButtonState st, double time);
/* The same, with a small drawing of the panel at the left that lights where
 * logical button(s) key are (panel.h), so a player can see which panel
 * button it is. key 0 = none. */
void ui_button_draw_key(const UiButton *b, Rectangle r, const char *label, Color neon, UiButtonState st, double time,
                        uint32_t key);

/* ---- the panel icon ------------------------------------------------------ */
/* The cabinet panel in miniature (per side two rows of three buttons, then
 * SELECT and START), h px high at x,y (top left), with every position that
 * drives logical button(s) btn lit in c. Returns its width. */
float ui_panel_glyph(float x, float y, float h, uint32_t btn, Color c, float alpha);
float ui_panel_glyph_w(float h);

/* ---- the neon marquee sign ---------------------------------------------- */
typedef struct {
    float t;
    float pow[24];          /* per letter power 0..1                */
    float flick_t[24];      /* seconds into a flicker, <0 = none     */
    int   flick_pat[24];
    float next;             /* seconds to the next flicker           */
    float intro;            /* 0..1 power-up sweep at start          */
    int   cue;              /* a flicker started (for the buzz SFX)  */
} Marquee;

void marquee_init(Marquee *m, int intro);
void marquee_update(Marquee *m, float dt);
/* The title sign "Beese's Poker Lounge", centred at (cx, cy); scale 1 = 820 px wide. */
void marquee_draw(const Marquee *m, float cx, float cy, float scale);
int  marquee_take_cue(Marquee *m);   /* 1 if a flicker started since the last call */

/* ---- chasing bulbs around the play area --------------------------------- */
typedef enum { BULBS_IDLE, BULBS_CHASE, BULBS_ALTERNATE, BULBS_FLASH, BULBS_SPARKLE, BULBS_OFF } BulbPattern;
#define BULBS_MAX 160
typedef struct {
    BulbPattern pattern;
    float speed;            /* steps per second                 */
    float t;
    int   n;
    Vector2 pos[BULBS_MAX];
    float b[BULBS_MAX];     /* brightness with filament lag     */
    Color color;
} BulbRing;

/* Bulbs every `spacing` px on a rectangle inset from the play-space edge. */
void bulbs_init(BulbRing *r, Rectangle rect, float spacing);
void bulbs_set(BulbRing *r, BulbPattern p, float speed);
void bulbs_update(BulbRing *r, float dt);
void bulbs_draw(const BulbRing *r);

/* ---- count-up meter ----------------------------------------------------- */
typedef void (*MeterTickFn)(void *user, float pitch);
typedef void (*MeterEndFn)(void *user);
typedef struct {
    double shown, from, to;
    float  t, dur;
    int    running;
    int    ticks_done, ticks_total;
    float  bump;                    /* scale kick on each tick / at the end */
    MeterTickFn on_tick;            /* audio hook: pitch rises 1 -> 2       */
    MeterEndFn  on_end;
    void  *user;
} Meter;

void meter_set(Meter *m, double v);
void meter_count(Meter *m, double to, float seconds);
void meter_skip(Meter *m);
void meter_update(Meter *m, float dt);
/* A neon credit window: label above, value in gold digits. */
void meter_draw(const Meter *m, Rectangle r, const char *label, Color neon);

/* ---- banners and tags --------------------------------------------------- */
/* Gold banner text with optional sunburst; appear 0..1 (zooms in with an
 * overshoot), vanish 0..1 (shrinks out). */
void ui_banner(const char *text, float cx, float cy, float size, float appear, float vanish, int rays, double time);
/* The neon "HELD" tag under a card. on 0..1 fades/pops it. */
void ui_held_tag(float cx, float cy, float on, double time);

/* ---- side art ----------------------------------------------------------- */
/* Installs the generated side art for the ultrawide margins. */
void ui_side_art_install(void);

/* Start-up (render.c). */
void ui_declare(void);
int  ui_job_count(void);
void ui_paint_job(int i);
void ui_finish(void);
void ui_shutdown(void);

#endif
