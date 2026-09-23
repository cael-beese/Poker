/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* engine/event.c - PLACEHOLDER implementation of ev_push() so the platform
 * branch links before the engine branch merges. The engine agent owns this
 * file; at merge the engine's version wins. */
#include "engine/event.h"

void ev_push(EventQueue *q, uint16_t type, int a, int b, int v)
{
    /* A full queue drops the event rather than overwriting, as the contract says. */
    if (q->n >= (int)(sizeof q->e / sizeof q->e[0])) return;
    GameEvent *e = &q->e[q->n++];
    e->type = type;
    e->a = (int16_t)a;
    e->b = (int16_t)b;
    e->v = (int32_t)v;
}
