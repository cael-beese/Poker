/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* placeholder_draw.c - the stand-in Draw Poker screen and its sounds.
 * See placeholder_view.h: reads a const DrawGame, reacts to its events,
 * changes nothing. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "raylib.h"
#include "audio.h"
#include "platform/placeholder_view.h"
#include "platform/screen.h"

#define C_HONEY   ((Color){ 232, 170, 40, 255 })
#define C_AMBER   ((Color){ 255, 128, 16, 255 })
#define C_MAGENTA ((Color){ 255, 40, 200, 255 })
#define C_CYAN    ((Color){ 40, 230, 255, 255 })
#define C_DIM     ((Color){ 130, 120, 110, 255 })
#define C_PANEL   ((Color){ 30, 26, 32, 255 })
#define C_RED     ((Color){ 220, 40, 50, 255 })

#define CARD_W 144.0f
#define CARD_H 196.0f
#define CARD_GAP 26.0f
#define CARD_Y 298.0f
#define CARD_X0 ((PLAY_W - (5 * CARD_W + 4 * CARD_GAP)) / 2)

/* Cosmetic state only. */
static struct {
    float win_flash;        /* seconds left of the winning-row flash           */
    float deny;             /* seconds left of the "not enough credits" line    */
    float dbl_linger;       /* keep the double-up panel up after a loss         */
    int   dbl_shown;        /* the double-up card has been turned               */
    int   last_tier;
} P;

static void text_c(const char *s, int cx, int y, int size, Color c)
{
    int w = MeasureText(s, size);
    DrawText(s, cx - w / 2, y, size, c);
}

void draw_placeholder_update(const DrawViewInfo *v, const GameEvent *ev, int nev, float dt)
{
    P.win_flash = P.win_flash > dt ? P.win_flash - dt : 0.0f;
    P.deny = P.deny > dt ? P.deny - dt : 0.0f;
    P.dbl_linger = P.dbl_linger > dt ? P.dbl_linger - dt : 0.0f;
    int snd = !v->demo;
    for (int i = 0; i < nev; i++) {
        const GameEvent *e = &ev[i];
        float pan = (e->a - 2) * 0.25f;
        switch (e->type) {
        case EV_DRAW_VARIANT:    if (snd) audio_play(SFX_MENU_MOVE, 1.0f, 1.0f, 0.0f); break;
        case EV_DRAW_BET:
            if (snd) {
                if (e->b) audio_play(SFX_BET_MAX, 1.0f, 1.0f, 0.0f);
                else audio_play(SFX_BET_ONE, 1.0f, 1.0f + 0.06f * (float)(e->a - 1), 0.0f);
            }
            break;
        case EV_DRAW_DENIED:
            P.deny = 2.0f;
            if (snd) audio_play(SFX_ERROR, 1.0f, 1.0f, 0.0f);
            break;
        case EV_DRAW_HAND_START: P.win_flash = 0.0f; P.dbl_linger = 0.0f; P.dbl_shown = 0; break;
        case EV_DRAW_DEAL_CARD:
        case EV_DRAW_DRAW_CARD:  if (snd) audio_play(SFX_CARD_DEAL, 0.9f, 1.0f, pan); break;
        case EV_DRAW_FLIP_CARD:  if (snd) audio_play(SFX_CARD_FLIP, 0.8f, 1.0f, pan); break;
        case EV_DRAW_HOLD:       if (snd) audio_play(e->b ? SFX_HOLD_ON : SFX_HOLD_OFF, 1.0f, 1.0f, pan); break;
        case EV_DRAW_DISCARD:    if (snd && e->a == 0) audio_play(SFX_CARD_SLIDE, 0.6f, 1.0f, 0.0f); break;
        case EV_DRAW_WIN: {
            static const SfxId tier_sfx[] = { SFX_WIN_SMALL, SFX_WIN_SMALL, SFX_WIN_MEDIUM, SFX_WIN_BIG, SFX_WIN_JACKPOT };
            P.last_tier = e->b;
            P.win_flash = e->b >= DT_JACKPOT ? 8.0f : 3.0f;
            if (snd && e->b >= 0 && e->b <= DT_JACKPOT) audio_play(tier_sfx[e->b], 1.0f, 1.0f, 0.0f);
            break;
        }
        case EV_DRAW_DOUBLE_START: P.dbl_shown = 0; if (snd) audio_play(SFX_CARD_SLIDE, 1.0f, 1.0f, 0.0f); break;
        case EV_DRAW_DOUBLE_GUESS: if (snd) audio_play(SFX_BUTTON, 1.0f, 1.0f, 0.0f); break;
        case EV_DRAW_DOUBLE_CARD:  P.dbl_shown = 1; if (snd) audio_play(SFX_CARD_FLIP, 1.0f, 1.0f, 0.0f); break;
        case EV_DRAW_DOUBLE_WIN:   if (snd) audio_play(SFX_DOUBLE_WIN, 1.0f, 1.0f, 0.0f); break;
        case EV_DRAW_DOUBLE_LOSE:  P.dbl_linger = 2.5f; if (snd) audio_play(SFX_DOUBLE_LOSE, 1.0f, 1.0f, 0.0f); break;
        case EV_DRAW_COLLECT:      if (snd && e->v > 0) audio_play(SFX_CREDIT_END, 1.0f, 1.0f, 0.0f); break;
        default: break;
        }
    }
}

static void hold_string(uint8_t mask, const Card *cards, char *out, size_t cap)
{
    static const char *const ranks[13] = { "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K", "A" };
    static const char suits[4] = { 'c', 'd', 'h', 's' };
    size_t n = 0;
    out[0] = '\0';
    if (!mask) { snprintf(out, cap, "NOTHING - DRAW FIVE"); return; }
    for (int i = 0; i < 5; i++) {
        if (!(mask >> i & 1) || cards[i] == CARD_NONE) continue;
        int w = snprintf(out + n, cap - n, "%s%s%c", n ? " " : "", ranks[card_rank(cards[i])], suits[card_suit(cards[i])]);
        if (w < 0 || (size_t)w >= cap - n) break;
        n += (size_t)w;
    }
}

static void double_panel(const DrawGame *g, double time)
{
    Rectangle r = { 300, CARD_Y - 4, 680, CARD_H + 24 };
    DrawRectangleRounded(r, 0.05f, 6, (Color){ 16, 10, 24, 245 });
    DrawRectangleRoundedLinesEx(r, 0.05f, 6, 3, C_MAGENTA);
    char s[96];
    snprintf(s, sizeof s, "DOUBLE UP  -  ROUND %d", g->dbl_round);
    DrawText(s, (int)r.x + 20, (int)r.y + 16, 24, C_MAGENTA);
    snprintf(s, sizeof s, "STAKE %d  ->  %d", g->meter, g->meter * 2);
    if (g->state == DS_OFFER) snprintf(s, sizeof s, "WON  %d", g->meter);
    if (g->state == DS_IDLE) snprintf(s, sizeof s, "LOST  %d", g->last.win << (g->last.dbl_won < 12 ? g->last.dbl_won : 0));
    DrawText(s, (int)r.x + 20, (int)r.y + 52, 24, C_HONEY);
    int shown = P.dbl_shown || g->state == DS_OFFER || g->state == DS_IDLE;
    Card c = g->dbl_card;
    if (g->state == DS_DOUBLE) c = card_make(0, 0), shown = 0;
    ph_card(r.x + r.width - 190, r.y + 18, CARD_W, CARD_H, c, shown, 1.0f);
    if (g->state == DS_DOUBLE && fmod(time * 2.0, 1.0) < 0.7) {
        DrawText("RED  ?  BLACK", (int)r.x + 20, (int)r.y + 110, 36, RAYWHITE);
    } else if (g->state == DS_DOUBLE_REVEAL) {
        DrawText(g->dbl_guess == DRAW_RED ? "YOU SAID RED" : "YOU SAID BLACK", (int)r.x + 20, (int)r.y + 110, 30, RAYWHITE);
    }
    /* Earlier double-up cards of this hand. */
    for (int i = 0; i + 1 < g->dbl_round && i < 8; i++)
        ph_card(r.x + 20 + i * 44.0f, r.y + 160, 40, 56, g->dbl_hist[i], 1, 1.0f);
}

void draw_placeholder_view(const DrawViewInfo *v, double time)
{
    const DrawGame *g = v->game;
    char s[160];
    DrawRectangleGradientV(0, 0, PLAY_W, PLAY_H, (Color){ 14, 22, 30, 255 }, (Color){ 6, 8, 12, 255 });

    /* Title and paytable. */
    text_c(draw_variant_name(g->variant), PLAY_W / 2, 12, 30, C_HONEY);
    if (!v->demo) DrawText("BEESE'S POKER LOUNGE", 20, 18, 20, C_DIM);
    if (v->hint_on && !v->demo) DrawText("HINT ON", PLAY_W - 110, 18, 20, C_CYAN);
    int lit = DC_NONE, flash = 0;
    if (g->hand_no > 0) {
        if (g->state == DS_HOLD) lit = g->dealt_cat;
        else if (g->final_cat != DC_NONE && g->win > 0 && g->state != DS_DEALING) { lit = g->final_cat; flash = P.win_flash > 0; }
    }
    ph_paytable(g->variant, g->bet, lit, flash, 190, 50, 900, time);

    /* The five cards. */
    int dbl = g->state == DS_DOUBLE || g->state == DS_DOUBLE_REVEAL || (g->state == DS_OFFER && g->dbl_round > 0) ||
              (g->state == DS_IDLE && P.dbl_linger > 0 && g->dbl_round > 0);
    uint8_t hint_mask = (v->hint_on && v->hint && g->state == DS_HOLD) ? v->hint->best : 0;
    for (int i = 0; i < 5; i++) {
        float x = CARD_X0 + i * (CARD_W + CARD_GAP);
        int up = (g->face_up >> i) & 1;
        Card c = g->cards[i];
        if (g->state == DS_DRAWING && ((g->replaced >> i) & 1) && !up) c = card_make(0, 0);   /* the back */
        ph_card(x, CARD_Y, CARD_W, CARD_H, c, up, dbl ? 0.35f : 1.0f);
        if (hint_mask >> i & 1)
            DrawRectangleRoundedLinesEx((Rectangle){ x - 6, CARD_Y - 6, CARD_W + 12, CARD_H + 12 }, 0.08f, 6, 4,
                                        Fade(C_CYAN, 0.6f + 0.4f * (float)sin(time * 5.0)));
        int held = (g->held >> i) & 1;
        if (held && g->in_hand && !dbl) {
            DrawRectangle((int)x, (int)(CARD_Y + CARD_H + 6), (int)CARD_W, 28, C_HONEY);
            text_c("HELD", (int)(x + CARD_W / 2), (int)(CARD_Y + CARD_H + 10), 20, BLACK);
        } else if (!v->demo && g->state == DS_HOLD) {
            char k[12];
            snprintf(k, sizeof k, "HOLD %d", i + 1);
            text_c(k, (int)(x + CARD_W / 2), (int)(CARD_Y + CARD_H + 10), 20, C_DIM);
        }
    }
    if (dbl) double_panel(g, time);

    /* The hint line. */
    if (hint_mask || (v->hint_on && v->hint && g->state == DS_HOLD)) {
        char h[64];
        hold_string(v->hint->best, g->cards, h, sizeof h);
        snprintf(s, sizeof s, "HINT: HOLD %s   (EXPECTED RETURN %.2f CREDITS)", h, v->hint->best_ev);
        text_c(s, PLAY_W / 2, 536, 20, C_CYAN);
    }

    /* The message line. */
    const char *msg = NULL;
    Color mc = RAYWHITE;
    if (v->message) { msg = v->message; mc = C_AMBER; }
    else if (P.deny > 0) { msg = "NOT ENOUGH CREDITS - INSERT COIN"; mc = C_RED; }
    else if (g->state == DS_OFFER) {
        snprintf(s, sizeof s, "%s  WIN %d   -   HOLD 1: DOUBLE UP   HOLD 5: TAKE WIN", draw_cat_name(g->final_cat), g->meter);
        if (g->dbl_round > 0) snprintf(s, sizeof s, "DOUBLED!  WIN %d   -   HOLD 1: DOUBLE AGAIN   HOLD 5: TAKE WIN", g->meter);
        msg = s; mc = C_HONEY;
    } else if (g->state == DS_DOUBLE) {
        msg = "HOLD 1-2: RED     HOLD 4-5: BLACK     HOLD 3: TAKE WIN"; mc = C_MAGENTA;
    } else if (g->state == DS_HOLD) {
        msg = g->dealt_cat != DC_NONE ? draw_cat_name(g->dealt_cat) : "CHOOSE CARDS TO HOLD, THEN DRAW";
        mc = g->dealt_cat != DC_NONE ? C_HONEY : RAYWHITE;
    } else if (g->state == DS_IDLE && g->hand_no > 0) {
        if (g->last.paid > 0) { snprintf(s, sizeof s, "%s  -  PAID %d", draw_cat_name(g->last.cat), g->last.paid); msg = s; mc = C_HONEY; }
        else if (g->last.win > 0) { snprintf(s, sizeof s, "DOUBLE UP LOST"); msg = s; mc = C_RED; }
        else msg = v->demo ? "" : "GAME OVER  -  PRESS DEAL";
    } else if (g->state == DS_IDLE) {
        msg = v->demo ? "" : "PRESS DEAL OR BET MAX";
    }
    if (msg) text_c(msg, PLAY_W / 2, 568, 30, mc);

    if (v->demo) return;

    /* Meters. */
    ph_credits_line(v->credits, v->denom_cents, s, sizeof s);
    DrawText(s, 30, 616, 30, C_HONEY);
    snprintf(s, sizeof s, "BET %d", g->bet);
    text_c(s, PLAY_W / 2 + 80, 616, 30, RAYWHITE);
    snprintf(s, sizeof s, "WIN %d", g->state == DS_OFFER || g->state == DS_DOUBLE || g->state == DS_DOUBLE_REVEAL
                                        ? g->meter : (g->state == DS_IDLE && g->hand_no > 0 ? g->last.paid : 0));
    int tw = MeasureText(s, 30);
    DrawText(s, PLAY_W - 30 - tw, 616, 30, C_MAGENTA);

    /* What the buttons do now. */
    const char *legend = "BET ONE: BET   BET MAX: BET 5 + DEAL   DEAL: DEAL   CASH OUT: MENU   UP/DOWN: GAME";
    if (g->state == DS_HOLD) legend = "HOLD 1-5: HOLD   DEAL: DRAW   BET ONE: HINT ON/OFF";
    else if (g->state == DS_OFFER) legend = "HOLD 1: DOUBLE   HOLD 5 / CASH OUT: TAKE WIN   DEAL: TAKE WIN + DEAL";
    else if (g->state == DS_DOUBLE) legend = "HOLD 1-2: RED   HOLD 4-5: BLACK   HOLD 3 / CASH OUT: TAKE WIN";
    else if (g->state == DS_DEALING || g->state == DS_DRAWING || g->state == DS_DOUBLE_REVEAL) legend = "";
    text_c(legend, PLAY_W / 2, 684, 20, C_DIM);
}
