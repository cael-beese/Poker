/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* placeholder_view.h - the stand-in presentation of every screen: plain
 * raylib shapes and text, plus a direct event -> sound mapping, so the game
 * is playable (and audible) before render/ exists.
 *
 * THIS IS THE SEAM FOR THE PRESENTATION AGENTS. The modes (mode_*.c) own the
 * game state and the flow; they hand these functions a read-only *ViewInfo
 * (const game state + the few app facts a screen needs) once per frame:
 *
 *   xxx_placeholder_update(info, events, nevents, dt)   cosmetic state, audio
 *   xxx_placeholder_view(info, time)                    draw, 1280x720 space
 *
 * To replace a screen, implement the same two calls in render/ (or change
 * the one call site in the mode's present_update / present_draw). They must
 * not change game state and hold no game logic: everything they show is in
 * the info struct or the const game it points to, and everything that
 * happened arrives as an event (games/draw/draw_game.h, games/holdem/holdem.h,
 * platform/app.h, platform/session.h).
 *
 * Files: placeholder_ui.c (shared helpers, cards, menu, attract, sounds of
 * app events and music), placeholder_draw.c, placeholder_holdem.c. */
#ifndef BPL_PLATFORM_PLACEHOLDER_VIEW_H
#define BPL_PLATFORM_PLACEHOLDER_VIEW_H

#include <stdint.h>

#include "engine/card.h"
#include "engine/event.h"
#include "games/draw/draw_game.h"
#include "games/draw/draw_hint.h"
#include "games/holdem/holdem.h"

/* ---- Draw Poker ---------------------------------------------------------- */

typedef struct {
    const DrawGame *game;
    int64_t     credits;
    int         denom_cents;
    int         hint_on;
    const DrawHint *hint;       /* the hint for the cards on the table, or NULL */
    int         demo;           /* attract-mode demo: no meters, no sounds      */
    const char *message;        /* a transient line (e.g. "FINISH THE HAND"), or NULL */
} DrawViewInfo;

void draw_placeholder_update(const DrawViewInfo *v, const GameEvent *ev, int nev, float dt);
void draw_placeholder_view(const DrawViewInfo *v, double time);

/* ---- Hold'em --------------------------------------------------------------- */

enum { HV_LOBBY, HV_PLAYING, HV_RESULT };

typedef struct {
    const HoldemGame *game;     /* NULL before the first sit-and-go              */
    int         phase;          /* HV_*                                           */
    int64_t     credits;
    int         denom_cents;
    int64_t     buyin;
    int64_t     prize[4];       /* [1..3] prize by place                          */
    const char *difficulty;     /* "NORMAL"                                        */
    const char *seat_name[HOLDEM_SEATS];
    int         place;          /* the player's finishing place (result), 0 if none */
    int64_t     won;            /* credits paid for it                            */
    int         confirm_leave;  /* the "leave the table?" prompt is open          */
    int         demo;
    const char *message;
} HoldemViewInfo;

void holdem_placeholder_update(const HoldemViewInfo *v, const GameEvent *ev, int nev, float dt);
void holdem_placeholder_view(const HoldemViewInfo *v, double time);

/* ---- menu and attract ------------------------------------------------------ */

typedef struct {
    int         sel;            /* MENU_GAME_* highlighted                         */
    int         hint_on;
    int64_t     credits;
    int         denom_cents;
    int64_t     coin_credits;
    int64_t     buyin;
    const char *message;
} MenuViewInfo;

void menu_placeholder_update(const MenuViewInfo *v, const GameEvent *ev, int nev, float dt);
void menu_placeholder_view(const MenuViewInfo *v, double time);

enum { ATTRACT_TITLE, ATTRACT_DRAW, ATTRACT_HOLDEM };

typedef struct {
    int     phase;              /* ATTRACT_*                                       */
    int     variant;            /* Draw variant being demonstrated / shown          */
    int64_t credits;
} AttractViewInfo;

/* The title card (phase TITLE), or the banner drawn over a demo game. */
void attract_placeholder_view(const AttractViewInfo *v, double time);

/* ---- which presentation serves the Draw, menu and attract screens ----------- */

/* The modes call the Draw / menu / attract views through this table rather
 * than by name, so bpl_platform never links against render/ (which depends
 * on it). The executable picks the table in platform/app_modes.c, as it
 * picks the modes: the renderer's (render/draw_view.h) when render/ is
 * built, else draw_views_placeholder. Any pointer may be NULL. */
typedef struct {
    void (*draw_update)(const DrawViewInfo *v, const GameEvent *ev, int nev, float dt);
    void (*draw_view)(const DrawViewInfo *v, double time);
    void (*menu_update)(const MenuViewInfo *v, const GameEvent *ev, int nev, float dt);
    void (*menu_view)(const MenuViewInfo *v, double time);
    /* Attract: the title card's cosmetic state, then (after any demo game's
       own view) the title card or the banner over the demo. */
    void (*attract_update)(const AttractViewInfo *v, const GameEvent *ev, int nev, float dt);
    void (*attract_view)(const AttractViewInfo *v, double time);
} DrawScreenViews;

extern const DrawScreenViews draw_views_placeholder;
extern const DrawScreenViews *const app_draw_views;      /* platform/app_modes.c */

/* ---- shared ------------------------------------------------------------------ */

/* Sounds for app / session events (coins, state changes) and the music for
 * the current app state; every mode's present_update calls it. */
void placeholder_common_update(int app_state, const GameEvent *ev, int nev, float dt);

/* Helpers the screens share. */
void ph_card(float x, float y, float w, float h, Card c, int face_up, float alpha);
void ph_text_centered(const char *s, int cx, int y, int size, uint32_t rgba);
void ph_paytable(int variant, int bet, int lit_cat, int flash, float x, float y, float w, double time);
void ph_credits_line(int64_t credits, int denom_cents, char *out, int cap);

#endif
