/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* mode_menu.c - APP_MENU: choose the game (Draw Poker variant or Hold'em),
 * the strategy hint and the CONTROLS page, with the arcade buttons alone:
 *   HOLD 1-4          pick Jacks or Better / Bonus / Deuces Wild / Hold'em
 *   BET ONE, stick    move the choice (the fifth choice is CONTROLS)
 *   HOLD 5            strategy hint on / off
 *   DEAL, BET MAX, OK, START   play the chosen game (or open CONTROLS)
 *   BACK              attract;  60 s without input: attract
 * The CONTROLS page (panel.h) draws the whole panel with what every button
 * does and lights the one pressed; CASH OUT goes back. It opens by itself the
 * first time a person reaches the menu. */
#include "platform/app.h"
#include "platform/panel.h"
#include "platform/placeholder_view.h"
#include "platform/session.h"

#define MENU_NSEL (MENU_NGAMES + 1)

static struct {
    int sel;
    int controls;
} M;

static void menu_enter(AppCtx *ctx, AppState from)
{
    (void)ctx;
    M.sel = session_menu_game();
    M.controls = 0;
    if (from == APP_ATTRACT && panel_controls_due()) {
        M.controls = 1;
        panel_controls_shown();
    }
}

static void menu_tick(AppCtx *ctx, const InputFrame *in)
{
    uint32_t p = in->pressed;
    if (M.controls) {
        /* Every other button only lights up on the page. */
        if (p & (BTN_CASH_OUT | BTN_BACK)) M.controls = 0;
        else if (session_idle_ticks() >= SESSION_IDLE_ATTRACT_TICKS) app_request(ctx, APP_ATTRACT);
        return;
    }
    for (int i = 0; i < MENU_NGAMES; i++)
        if (p & (BTN_HOLD1 << i)) M.sel = i;
    if (p & (BTN_DOWN | BTN_RIGHT | BTN_BET_ONE)) M.sel = (M.sel + 1) % MENU_NSEL;
    if (p & (BTN_UP | BTN_LEFT)) M.sel = (M.sel + MENU_NSEL - 1) % MENU_NSEL;
    if (p & BTN_HOLD5) session_set_hint_on(!session_hint_on());
    if (M.sel < MENU_NGAMES) session_set_menu_game(M.sel);
    if (p & (BTN_DEAL | BTN_BET_MAX | BTN_OK | BTN_START)) {
        if (M.sel == MENU_SEL_CONTROLS) M.controls = 1;
        else app_request(ctx, M.sel == MENU_GAME_HOLDEM ? APP_HOLDEM : APP_DRAW);
    } else if (p & BTN_BACK)
        app_request(ctx, APP_ATTRACT);
    else if (session_idle_ticks() >= SESSION_IDLE_ATTRACT_TICKS)
        app_request(ctx, APP_ATTRACT);
}

static void fill(const AppCtx *ctx, MenuViewInfo *v)
{
    const Settings *s = session_settings();
    v->sel = M.sel;
    v->controls = M.controls;
    v->down = ctx->input.down;
    v->hint_on = session_hint_on();
    v->credits = ctx->wallet.credits;
    v->denom_cents = s->denom_cents;
    v->coin_credits = settings_coin_credits(s);
    v->buyin = s->holdem_buyin;
    v->message = NULL;
}

static void menu_present_update(const AppCtx *ctx, const GameEvent *ev, int nev, float dt)
{
    MenuViewInfo v;
    fill(ctx, &v);
    placeholder_common_update(ctx->state, ev, nev, dt);
    if (app_draw_views->menu_update) app_draw_views->menu_update(&v, ev, nev, dt);
}

static void menu_present_draw(const AppCtx *ctx)
{
    MenuViewInfo v;
    fill(ctx, &v);
    if (app_draw_views->menu_view) app_draw_views->menu_view(&v, ctx->time);
}

const AppMode mode_menu = {
    .name = "menu",
    .enter = menu_enter,
    .tick = menu_tick,
    .present_update = menu_present_update,
    .present_draw = menu_present_draw,
};
