/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* holdem_present.h - the Hold'em table's presentation: the 6-max sit-and-go
 * table, its motion, sound and celebrations, the lobby and result screens,
 * and the table part of attract mode.
 *
 * It plugs into the platform through platform/placeholder_view.h
 * (holdem_present_fns, named by platform/app_modes.c). It reads the const
 * HoldemGame, reacts to the table's events (games/holdem/holdem.h), keeps
 * only cosmetic state, and never writes game state. Cosmetic randomness
 * comes from the renderer's own stream (fx_rng). No allocation after
 * start-up. */
#ifndef BPL_RENDER_HOLDEM_PRESENT_H
#define BPL_RENDER_HOLDEM_PRESENT_H

#include "platform/placeholder_view.h"

void holdem_present_update(const HoldemViewInfo *v, const GameEvent *ev, int nev, float dt);
void holdem_present_view(const HoldemViewInfo *v, double time);

extern const HoldemViewFns holdem_present_fns;

/* Statistics for tests and measurement (fill per frame is gfx_fill_take's). */
typedef struct {
    int    flights_live, cards_live, flights_peak;
    long   events_seen, hands_seen, celebrations;
    int    desyncs;            /* times the view had to snap to the game state */
} HoldemPresentStats;
const HoldemPresentStats *holdem_present_stats(void);

#endif
