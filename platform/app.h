/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* app.h - the application state machine and the interface every mode plugs into.
 *
 * States: ATTRACT -> MENU -> DRAW | HOLDEM, SERVICE from anywhere (the
 * SERVICE button), plus GPUTEST (the platform's GPU smoke test, --mode gputest).
 * Each state is served by one AppMode, a table of function pointers. The table
 * of modes is platform/app_modes.c: to plug a real mode in, define an AppMode
 * in your module and point its slot there at it, replacing the placeholder.
 *
 * Call order, per rendered frame (see platform/main.c):
 *   input sampled -> 0..n ticks -> present_update once -> present_draw once
 *   tick:           60 Hz, deterministic game logic; the ONLY code that changes
 *                   game state. Push events to ctx->events. Switch states with
 *                   app_request(), never by calling another mode.
 *   present_update: once per frame with every event pushed since the last
 *                   frame (from all ticks of this frame) and the frame's dt.
 *                   Cosmetic state only.
 *   present_draw:   once per frame, inside the 1280x720 play space (already
 *                   cleared to charcoal). Reads state, changes nothing.
 * A mode keeps its state in its own file-static storage (no heap per frame);
 * game state follows the flat-struct rule of CONTRACT.md section 3. */
#ifndef BPL_PLATFORM_APP_H
#define BPL_PLATFORM_APP_H

#include <stdint.h>
#include "engine/event.h"
#include "engine/input.h"
#include "engine/wallet.h"

typedef enum {
    APP_ATTRACT, APP_MENU, APP_DRAW, APP_HOLDEM, APP_SERVICE, APP_GPUTEST, APP_NSTATES
} AppState;

/* App event types (the 300-399 range of CONTRACT.md). */
enum {
    APP_EV_STATE = 300,     /* a = from state, b = to state */
    APP_EV_BUTTON = 301,    /* a = logical button bit index pressed (for UI feedback) */
};

typedef struct AppCtx {
    Wallet     wallet;          /* owned by the app, passed to games               */
    EventQueue events;          /* this tick's events; cleared before every tick   */
    uint64_t   session_seed;    /* --seed, else OS entropy; derive per-mode streams */
    uint64_t   tick;            /* ticks run so far                                 */
    AppState   state, prev;     /* current state; the one before it                 */
    InputFrame input;           /* last tick's input, 'pressed' merged over the frame
                                   (presentation may use it for button feedback)    */
    double     time;            /* presentation seconds (frame dt summed)           */
    int        quit;            /* set to leave the program cleanly                 */
    /* private to app.c */
    AppState   request;
    int        has_request;
} AppCtx;

typedef struct AppMode {
    const char *name;
    void (*init)(AppCtx *ctx);                        /* once, after the window exists: build resources */
    void (*enter)(AppCtx *ctx, AppState from);        /* every time the app switches to this state       */
    void (*leave)(AppCtx *ctx, AppState to);          /* before switching away                           */
    void (*tick)(AppCtx *ctx, const InputFrame *in);  /* 60 Hz, deterministic                            */
    void (*present_update)(const AppCtx *ctx, const GameEvent *ev, int nev, float dt);
    void (*present_draw)(const AppCtx *ctx);          /* inside the 1280x720 play space                  */
    void (*shutdown)(void);                           /* once, before the window closes                  */
} AppMode;                                            /* any pointer may be NULL                         */

/* The mode table, one per state (platform/app_modes.c). */
extern const AppMode *const app_modes[APP_NSTATES];

void app_init(AppCtx *ctx, uint64_t seed, AppState first);
void app_tick(AppCtx *ctx, const InputFrame *in);
void app_present(AppCtx *ctx, float dt);          /* present_update + present_draw of the current state */
void app_shutdown(AppCtx *ctx);

void app_request(AppCtx *ctx, AppState to);       /* switch after this tick */
const char *app_state_name(AppState s);
int  app_state_from_name(const char *name);       /* -1 if unknown */

#endif
