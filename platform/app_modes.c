/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* app_modes.c - which AppMode serves which app state.
 *
 * This is the one place a later milestone edits to plug its mode in: declare
 * your `extern const AppMode mode_xxx;` and replace the placeholder in its
 * slot. The placeholders live in modes_placeholder.c. */
#include "platform/app.h"

extern const AppMode mode_attract_placeholder;
extern const AppMode mode_menu_placeholder;
extern const AppMode mode_draw_placeholder;
extern const AppMode mode_holdem_placeholder;
extern const AppMode mode_service_placeholder;
extern const AppMode mode_gputest;

const AppMode *const app_modes[APP_NSTATES] = {
    [APP_ATTRACT] = &mode_attract_placeholder,
    [APP_MENU]    = &mode_menu_placeholder,
    [APP_DRAW]    = &mode_draw_placeholder,     /* games/draw + render/: replace here */
    [APP_HOLDEM]  = &mode_holdem_placeholder,   /* games/holdem + ai/ + render/: replace here */
    [APP_SERVICE] = &mode_service_placeholder,
    [APP_GPUTEST] = &mode_gputest,
};
