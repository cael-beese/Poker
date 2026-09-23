/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* draw_view.h - the Draw Poker presentation (SPEC 3A, milestone 4): the
 * video poker screen, the main menu and the Draw part of attract mode.
 *
 * These implement platform/placeholder_view.h's seam: the modes own the game
 * and the flow and hand over a read-only *ViewInfo once per frame. The views
 * react to the frame's events (games/draw/draw_game.h 100-120, app 300+),
 * keep only cosmetic state (animations, particles, count-up meters, their
 * own Rng through fx.h), never write game state and allocate nothing after
 * start-up. Sound goes out from here, in reaction to events (audio/).
 *
 * The *_update functions make no GL calls, so a test can drive them
 * headless; the *_draw functions draw into the 1280x720 play space. */
#ifndef BPL_RENDER_DRAW_VIEW_H
#define BPL_RENDER_DRAW_VIEW_H

#include "raylib.h"
#include "platform/placeholder_view.h"

/* The table platform/app_modes.c installs when render/ is built. */
extern const DrawScreenViews draw_views_render;

/* ---- the Draw screen ---------------------------------------------------- */
void draw_view_reset(void);          /* forget all cosmetic state (no GL)   */
void draw_view_update(const DrawViewInfo *v, const GameEvent *ev, int nev, float dt);
void draw_view_draw(const DrawViewInfo *v, double time);

/* ---- menu and attract (lounge_view.c) ----------------------------------- */
void menu_view_update(const MenuViewInfo *v, const GameEvent *ev, int nev, float dt);
void menu_view_draw(const MenuViewInfo *v, double time);
void attract_view_update(const AttractViewInfo *v, const GameEvent *ev, int nev, float dt);
void attract_view_draw(const AttractViewInfo *v, double time);

/* ---- shared pieces -------------------------------------------------------- */

/* The full paytable: every paid category of the variant against all five
 * bet columns. bet_pos is the lit column (1..5, fractional while it slides);
 * pre_cat / pre_k light the dealt hand's row (hold phase); win_cat / win_k
 * flash the winning row. Row heights fit the variant's row count. */
void dv_paytable(Rectangle r, int variant, float bet_pos, int pre_cat, float pre_k, int win_cat, float win_k,
                 double time);

/* A sound effect, unless the view is silent (attract demo) or there is no
 * audio library. pan -1..1. */
void dv_sfx(int sfx_id, float vol, float pitch, float pan);

/* Fill accounting: call once per frame after render_end(); takes the frame's
 * gfx_fill_take() and, with BPL_FILL_LOG=1, prints mean / max Mpx per scene
 * every 300 frames. */
enum { DV_FILL_PLAY, DV_FILL_CELEB, DV_FILL_JACKPOT, DV_FILL_MENU, DV_FILL_TITLE, DV_FILL_SCENES };
void dv_fill_account(int scene);

/* Test / diagnostics: the view's idea of the screen. */
typedef struct {
    int      slots_face_up;     /* bit i: card i is shown face up          */
    uint8_t  held_shown;        /* bit i: HELD shown (> half way in)       */
    int      celebrating;       /* a WinTier while a celebration runs      */
    int      double_panel;      /* the double-up panel is (partly) visible */
    long long credits_shown, win_shown;
    int      particles;
} DrawViewProbe;
void draw_view_probe(DrawViewProbe *out);

#endif
