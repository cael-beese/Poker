/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* app_modes.c - which AppMode serves which app state.
 *
 * This is the one place a later milestone edits to plug its mode in: declare
 * your `extern const AppMode mode_xxx;` and replace the placeholder in its
 * slot. The placeholders live in modes_placeholder.c.
 *
 * This file is compiled into the executable, not into bpl_platform, so the
 * table can name modes from libraries that themselves depend on
 * bpl_platform (render/, games/) without a circular static-library link. */
#include "platform/app.h"
#include "platform/placeholder_view.h"

extern const AppMode mode_attract_placeholder;
extern const AppMode mode_menu_placeholder;
extern const AppMode mode_draw_placeholder;
extern const AppMode mode_holdem_placeholder;
extern const AppMode mode_service_placeholder;
extern const AppMode mode_gputest;
#if defined(BPL_HAVE_RENDER)
extern const AppMode mode_rendertest;
#define RENDERTEST_MODE (&mode_rendertest)
#else
#define RENDERTEST_MODE (&mode_gputest)
#endif

/* The real modes (systems milestone): game flow in mode_*.c / service.c,
 * presentation behind platform/placeholder_view.h. */
extern const AppMode mode_attract;
extern const AppMode mode_menu;
extern const AppMode mode_draw;
extern const AppMode mode_holdem;
extern const AppMode mode_service;

/* The Hold'em table's presentation (render/holdem_present.c), used by both
 * the Hold'em mode and the Hold'em part of attract (placeholder_view.h). */
#if defined(BPL_HAVE_RENDER)
extern const HoldemViewFns holdem_present_fns;
const HoldemViewFns *const app_holdem_view = &holdem_present_fns;
#else
const HoldemViewFns *const app_holdem_view = NULL;
#endif

const AppMode *const app_modes[APP_NSTATES] = {
    [APP_ATTRACT] = &mode_attract,
    [APP_MENU]    = &mode_menu,
    [APP_DRAW]    = &mode_draw,      /* presentation: placeholder_draw.c   */
    [APP_HOLDEM]  = &mode_holdem,    /* presentation: render/holdem_present.c via app_holdem_view */
    [APP_SERVICE] = &mode_service,
    [APP_GPUTEST] = &mode_gputest,
    [APP_RENDERTEST] = RENDERTEST_MODE,   /* render/rendertest.c */
};
