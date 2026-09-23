/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
#include "event.h"

void ev_push(EventQueue *q, uint16_t type, int a, int b, int v)
{
    GameEvent *e;
    /* A full queue drops the event rather than overwriting: presentation may
       miss a flourish, but game state is never affected by the queue. */
    if (q->n < 0 || q->n >= EV_QUEUE_CAP) return;
    e = &q->e[q->n++];
    e->type = type;
    e->a = (int16_t)a;
    e->b = (int16_t)b;
    e->v = (int32_t)v;
}
