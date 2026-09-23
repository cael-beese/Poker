/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* app.c - see app.h. */
#include "platform/app.h"
#include "platform/session.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

static const char *const g_state_names[APP_NSTATES] = {
    "ATTRACT", "MENU", "DRAW", "HOLDEM", "SERVICE", "GPUTEST", "RENDERTEST"
};

/* Events of every tick since the last rendered frame, for present_update.
 * Sized for 4 ticks of a full queue, the most a frame can run. */
static GameEvent g_frame_ev[1024];
static int g_nframe_ev;
static uint32_t g_frame_pressed;

const char *app_state_name(AppState s)
{
    return (s >= 0 && s < APP_NSTATES) ? g_state_names[s] : "?";
}

int app_state_from_name(const char *name)
{
    for (int i = 0; i < APP_NSTATES; i++)
        if (strcasecmp(name, g_state_names[i]) == 0) return i;
    return -1;
}

void app_request(AppCtx *ctx, AppState to)
{
    ctx->request = to;
    ctx->has_request = 1;
}

void app_init(AppCtx *ctx, uint64_t seed, AppState first)
{
    memset(ctx, 0, sizeof *ctx);
    ctx->session_seed = seed;
    /* Credits, settings and stats come from the save file (session.h). */
    session_init(ctx);
    for (int s = 0; s < APP_NSTATES; s++) {
        /* A mode may serve several states; initialise it once. */
        int seen = 0;
        for (int t = 0; t < s; t++) if (app_modes[t] == app_modes[s]) seen = 1;
        if (!seen && app_modes[s] && app_modes[s]->init) app_modes[s]->init(ctx);
    }
    ctx->state = ctx->prev = first;
    if (app_modes[first] && app_modes[first]->enter) app_modes[first]->enter(ctx, first);
}

static void switch_state(AppCtx *ctx, AppState to)
{
    AppState from = ctx->state;
    if (to == from) return;
    if (app_modes[from] && app_modes[from]->leave) app_modes[from]->leave(ctx, to);
    ctx->prev = from;
    ctx->state = to;
    ev_push(&ctx->events, APP_EV_STATE, (int)from, (int)to, 0);
    if (app_modes[to] && app_modes[to]->enter) app_modes[to]->enter(ctx, from);
}

void app_tick(AppCtx *ctx, const InputFrame *in)
{
    ctx->events.n = 0;
    ctx->has_request = 0;

    for (int b = 0; b < 19; b++)          /* BTN_HOLD1 .. BTN_DEBUG */
        if (in->pressed & (1u << b)) ev_push(&ctx->events, APP_EV_BUTTON, b, 0, 0);
    session_before_tick(ctx, in);      /* idle timer, coins */

    /* The service button works from every state except the service menu
     * itself and the GPU / render tests (measurements, not games). */
    if ((in->pressed & BTN_SERVICE) && ctx->state != APP_SERVICE && ctx->state != APP_GPUTEST &&
        ctx->state != APP_RENDERTEST)
        app_request(ctx, APP_SERVICE);
    else if (app_modes[ctx->state] && app_modes[ctx->state]->tick)
        app_modes[ctx->state]->tick(ctx, in);

    if (ctx->has_request) switch_state(ctx, ctx->request);
    ctx->has_request = 0;
    session_after_tick(ctx);           /* saves at the end of a tick, never mid-way */

    g_frame_pressed |= in->pressed;
    ctx->input = *in;
    ctx->input.pressed = g_frame_pressed;

    int room = (int)(sizeof g_frame_ev / sizeof g_frame_ev[0]) - g_nframe_ev;
    int n = ctx->events.n < room ? ctx->events.n : room;
    memcpy(&g_frame_ev[g_nframe_ev], ctx->events.e, sizeof(GameEvent) * (size_t)n);
    g_nframe_ev += n;
    ctx->tick++;
}

void app_present(AppCtx *ctx, float dt)
{
    const AppMode *m = app_modes[ctx->state];
    ctx->time += dt;
    if (m && m->present_update) m->present_update(ctx, g_frame_ev, g_nframe_ev, dt);
    if (m && m->present_draw) m->present_draw(ctx);
    g_nframe_ev = 0;
    g_frame_pressed = 0;
    /* A frame that runs no tick must not show the previous frame's presses again. */
    ctx->input.pressed = 0;
}

void app_shutdown(AppCtx *ctx)
{
    for (int s = 0; s < APP_NSTATES; s++) {
        int seen = 0;
        for (int t = 0; t < s; t++) if (app_modes[t] == app_modes[s]) seen = 1;
        if (!seen && app_modes[s] && app_modes[s]->shutdown) app_modes[s]->shutdown();
    }
    session_shutdown(ctx);             /* the last save, after the modes settle up */
}
