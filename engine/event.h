/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* engine/event.h - what happened, for presentation and audio to react to.
 *
 * PLACEHOLDER copied verbatim from docs/CONTRACT.md section 2 by the platform
 * branch so it can build before the engine branch merges. The engine agent
 * owns this file; at merge the engine's version wins. */
#ifndef BPL_ENGINE_EVENT_H
#define BPL_ENGINE_EVENT_H

#include <stdint.h>

typedef struct { uint16_t type; int16_t a, b; int32_t v; } GameEvent;
typedef struct { GameEvent e[256]; int n; } EventQueue;   /* cleared each tick by the app */
void ev_push(EventQueue *q, uint16_t type, int a, int b, int v);   /* drops if full */
/* event type numbers: engine 0-99, draw 100-199, holdem 200-299, app 300-399 */

#endif
