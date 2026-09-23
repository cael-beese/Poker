/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
#ifndef BPL_ENGINE_EVENT_H
#define BPL_ENGINE_EVENT_H

#include <stdint.h>

/* engine/event.h - what happened, for presentation and audio to react to.
   Event type numbers: engine 0-99, draw 100-199, holdem 200-299, app 300-399.
   The meaning of a, b and v is defined by each event type. */
typedef struct { uint16_t type; int16_t a, b; int32_t v; } GameEvent;
typedef struct { GameEvent e[256]; int n; } EventQueue;   /* cleared each tick by the app */

#define EV_QUEUE_CAP 256

enum {
  EV_NONE = 0,          /* never pushed; a zeroed GameEvent reads as "nothing" */
  EV_ENGINE_FIRST = 0, EV_ENGINE_LAST = 99,
  EV_DRAW_FIRST = 100,  EV_DRAW_LAST = 199,
  EV_HOLDEM_FIRST = 200, EV_HOLDEM_LAST = 299,
  EV_APP_FIRST = 300,   EV_APP_LAST = 399
};

void ev_push(EventQueue *q, uint16_t type, int a, int b, int v);   /* drops if full */

static inline void ev_clear(EventQueue *q) { q->n = 0; }

#endif
