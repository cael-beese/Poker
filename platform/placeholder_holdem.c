/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* placeholder_holdem.c - the stand-in Hold'em table, lobby and result
 * screens, and their sounds. See placeholder_view.h: reads a const
 * HoldemGame, reacts to its events, changes nothing. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "raylib.h"
#include "audio.h"
#include "engine/eval.h"
#include "platform/placeholder_view.h"
#include "platform/screen.h"

#define C_HONEY   ((Color){ 232, 170, 40, 255 })
#define C_AMBER   ((Color){ 255, 128, 16, 255 })
#define C_MAGENTA ((Color){ 255, 40, 200, 255 })
#define C_CYAN    ((Color){ 40, 230, 255, 255 })
#define C_DIM     ((Color){ 130, 120, 110, 255 })
#define C_PANEL   ((Color){ 30, 26, 32, 255 })
#define C_RED     ((Color){ 220, 40, 50, 255 })
#define C_FELT    ((Color){ 18, 70, 52, 255 })

/* Seat centres; seat numbers increase clockwise as seen on screen. */
static const Vector2 k_seat[HOLDEM_SEATS] = {
    { 640, 548 }, { 215, 452 }, { 215, 168 }, { 640, 92 }, { 1065, 168 }, { 1065, 452 }
};

static struct {
    char  act[HOLDEM_SEATS][24];    /* last action text per seat            */
    char  hand[HOLDEM_SEATS][24];   /* hand name at showdown                */
    int64_t won[HOLDEM_SEATS];      /* chips won this hand                  */
    int   dealt[HOLDEM_SEATS];      /* hole cards dealt so far this hand    */
    char  news[80];                 /* the last table-wide event            */
    float news_t;
} P;

static void text_c(const char *s, int cx, int y, int size, Color c)
{
    int w = MeasureText(s, size);
    DrawText(s, cx - w / 2, y, size, c);
}

static const char *place_str(int p)
{
    static const char *const s[] = { "-", "1ST", "2ND", "3RD", "4TH", "5TH", "6TH" };
    return p >= 0 && p <= 6 ? s[p] : "?";
}

void holdem_placeholder_update(const HoldemViewInfo *v, const GameEvent *ev, int nev, float dt)
{
    int snd = !v->demo;
    P.news_t = P.news_t > dt ? P.news_t - dt : 0.0f;
    for (int i = 0; i < nev; i++) {
        const GameEvent *e = &ev[i];
        int s = e->a;
        int okseat = s >= 0 && s < HOLDEM_SEATS;
        float pan = okseat ? (k_seat[s].x - 640.0f) / 640.0f : 0.0f;
        switch (e->type) {
        case EV_HOLDEM_HAND_START:
            memset(P.act, 0, sizeof P.act);
            memset(P.hand, 0, sizeof P.hand);
            memset(P.won, 0, sizeof P.won);
            memset(P.dealt, 0, sizeof P.dealt);
            if (snd) audio_play(SFX_CARD_SHUFFLE, 0.5f, 1.0f, 0.0f);
            break;
        case EV_HOLDEM_LEVEL_UP:
            snprintf(P.news, sizeof P.news, "BLINDS UP  -  BIG BLIND %d", e->v);
            P.news_t = 4.0f;
            if (snd) audio_play(SFX_BLINDS_UP, 1.0f, 1.0f, 0.0f);
            break;
        case EV_HOLDEM_ANTE:
        case EV_HOLDEM_POST_SB:
        case EV_HOLDEM_POST_BB:
            if (okseat) snprintf(P.act[s], sizeof P.act[s], "%s %d",
                                 e->type == EV_HOLDEM_ANTE ? "ANTE" : e->type == EV_HOLDEM_POST_SB ? "SB" : "BB", e->v);
            if (snd) audio_play(SFX_CHIP_SINGLE, 0.5f, 1.0f, pan);
            break;
        case EV_HOLDEM_DEAL_HOLE:
            if (okseat && P.dealt[s] < 2) P.dealt[s]++;
            if (snd) audio_play(SFX_CARD_DEAL, 0.5f, 1.0f, pan);
            break;
        case EV_HOLDEM_TURN:
            if (snd && e->b) audio_play(SFX_YOUR_TURN, 0.8f, 1.0f, 0.0f);
            break;
        case EV_HOLDEM_AMOUNT:
            if (snd) audio_play(SFX_BUTTON, 0.7f, 1.0f, 0.0f);
            break;
        case EV_HOLDEM_ACTION: {
            static const char *const names[] = { "FOLD", "CHECK", "CALL", "BET", "RAISE TO", "ALL IN", "SB", "BB" };
            int a = e->b;
            if (okseat && a >= 0 && a <= ACT_POST_BB) {
                if (a == ACT_FOLD || a == ACT_CHECK) snprintf(P.act[s], sizeof P.act[s], "%s", names[a]);
                else snprintf(P.act[s], sizeof P.act[s], "%s %d", names[a], e->v);
            }
            if (snd) {
                SfxId x = a == ACT_FOLD ? SFX_FOLD : a == ACT_CHECK ? SFX_CHECK : a == ACT_CALL ? SFX_CHIP_SINGLE
                        : a == ACT_ALLIN ? SFX_ALL_IN : SFX_CHIP_STACK;
                audio_play(x, 1.0f, 1.0f, pan);
            }
            break;
        }
        case EV_HOLDEM_BETS_TO_POT: if (snd) audio_play(SFX_CHIP_POT, 0.8f, 1.0f, 0.0f); break;
        case EV_HOLDEM_STREET:
            for (int k = 0; k < HOLDEM_SEATS; k++)
                if (strcmp(P.act[k], "FOLD") != 0 && strncmp(P.act[k], "ALL", 3) != 0) P.act[k][0] = '\0';
            break;
        case EV_HOLDEM_BOARD: if (snd) audio_play(SFX_CARD_FLIP, 0.9f, 1.0f, 0.0f); break;
        case EV_HOLDEM_RUNOUT:
            snprintf(P.news, sizeof P.news, "ALL IN  -  RUNNING IT OUT");
            P.news_t = 3.0f;
            if (snd) audio_play(SFX_REVEAL, 1.0f, 1.0f, 0.0f);
            break;
        case EV_HOLDEM_SHOW: if (snd) audio_play(SFX_CARD_FLIP, 1.0f, 1.0f, pan); break;
        case EV_HOLDEM_HAND_RANK:
            if (okseat && e->v > 0) snprintf(P.hand[s], sizeof P.hand[s], "%s", eval_category_name(eval_category(e->v)));
            break;
        case EV_HOLDEM_MUCK: if (okseat) snprintf(P.act[s], sizeof P.act[s], "MUCK"); break;
        case EV_HOLDEM_AWARD:
            if (okseat) P.won[s] += e->v;
            if (snd) audio_play(s == 0 && !v->demo ? SFX_WIN_SMALL : SFX_CHIP_POT, 1.0f, 1.0f, pan);
            break;
        case EV_HOLDEM_ELIMINATED:
            if (okseat) snprintf(P.act[s], sizeof P.act[s], "OUT - %s", place_str(e->b));
            break;
        case EV_HOLDEM_HUMAN_OUT: if (snd) audio_play(SFX_BUST, 1.0f, 1.0f, 0.0f); break;
        case EV_HOLDEM_GAME_OVER:
            if (snd && e->a == 0) audio_play(SFX_WIN_JACKPOT, 1.0f, 1.0f, 0.0f);
            break;
        default: break;
        }
    }
}

static void seat_panel(const HoldemViewInfo *v, const HoldemGame *g, int s, double time)
{
    const HoldemSeat *st = &g->seat[s];
    Vector2 c = k_seat[s];
    if (st->empty) return;
    Rectangle r = { c.x - 95, c.y - 34, 190, 68 };
    int acting = g->to_act == s && (g->phase == HP_TURN);
    Color edge = acting ? C_CYAN : (s == 0 && !v->demo ? C_HONEY : C_DIM);
    DrawRectangleRounded(r, 0.2f, 6, st->in_game ? C_PANEL : Fade(C_PANEL, 0.4f));
    DrawRectangleRoundedLinesEx(r, 0.2f, 6, acting ? 3.0f + (float)sin(time * 8.0) : 2.0f, edge);
    char s1[64];
    snprintf(s1, sizeof s1, "%s", v->seat_name[s] ? v->seat_name[s] : "SEAT");
    DrawText(s1, (int)r.x + 10, (int)r.y + 6, 20, s == 0 && !v->demo ? C_HONEY : RAYWHITE);
    if (st->in_game || st->stack > 0) snprintf(s1, sizeof s1, "%lld", (long long)st->stack);
    else snprintf(s1, sizeof s1, "OUT %s", place_str(st->place));
    DrawText(s1, (int)r.x + 10, (int)r.y + 30, 24, st->allin ? C_MAGENTA : C_HONEY);
    if (P.act[s][0]) {
        int w = MeasureText(P.act[s], 20);
        DrawText(P.act[s], (int)(r.x + r.width - w - 8), (int)r.y + 36, 20, C_CYAN);
    }
    if (P.hand[s][0]) text_c(P.hand[s], (int)c.x, (int)(r.y + r.height + 2), 20, C_AMBER);
    if (P.won[s] > 0) {
        snprintf(s1, sizeof s1, "WINS %lld", (long long)P.won[s]);
        text_c(s1, (int)c.x, (int)(r.y - 22), 20, C_HONEY);
    }

    /* Hole cards, beside the panel on the table side. */
    int dealt = (g->phase > HP_DEAL) ? 2 : (g->phase == HP_DEAL ? P.dealt[s] : 0);
    if (g->phase == HP_HAND_START) dealt = 0;
    if (st->in_hand && !st->folded && !st->mucked && dealt > 0) {
        float cw = 46, ch = 64;
        float x = c.x - cw - 2, y = r.y - ch - 4;
        if (s == 1 || s == 2) { x = r.x + r.width + 8; y = c.y - ch / 2; }
        if (s == 4 || s == 5) { x = r.x - 2 * cw - 14; y = c.y - ch / 2; }
        if (s == 3) { x = r.x + r.width + 8; y = c.y - ch / 2; }
        int up = st->shown || (s == 0);
        for (int k = 0; k < dealt; k++) ph_card(x + k * (cw + 4), y, cw, ch, st->hole[k], up, 1.0f);
    }
    /* Chips in front this street. */
    if (st->bet > 0) {
        Vector2 m = { c.x + (640 - c.x) * 0.38f, c.y + (300 - c.y) * 0.38f };
        DrawCircleV(m, 12, C_MAGENTA);
        DrawCircleLinesV(m, 12, RAYWHITE);
        snprintf(s1, sizeof s1, "%lld", (long long)st->bet);
        DrawText(s1, (int)m.x + 16, (int)m.y - 10, 20, RAYWHITE);
    }
    if (g->button == s) {
        Vector2 b = { c.x + (640 - c.x) * 0.3f - 34, c.y + (300 - c.y) * 0.3f };
        DrawCircleV(b, 13, RAYWHITE);
        text_c("D", (int)b.x, (int)b.y - 9, 20, BLACK);
    }
}

static void action_bar(const HoldemViewInfo *v, const HoldemGame *g)
{
    char s[200];
    Rectangle bar = { 20, 626, 1240, 88 };
    DrawRectangleRounded(bar, 0.2f, 6, (Color){ 10, 10, 14, 235 });
    if (g->prompt) {
        text_c("SHOW YOUR HAND?", PLAY_W / 2, 636, 30, C_HONEY);
        text_c("DEAL / HOLD 2: SHOW       HOLD 1: MUCK", PLAY_W / 2, 676, 24, RAYWHITE);
        return;
    }
    if (g->phase == HP_BUSTED) {
        snprintf(s, sizeof s, "YOU FINISHED %s", place_str(g->seat[0].place));
        text_c(s, PLAY_W / 2, 636, 30, C_HONEY);
        text_c("DEAL: WATCH THE REST       CASH OUT: LEAVE THE TABLE", PLAY_W / 2, 676, 24, RAYWHITE);
        return;
    }
    if (!holdem_is_human_turn(g)) {
        const char *who = g->to_act >= 0 && g->to_act < HOLDEM_SEATS && g->phase == HP_TURN ? v->seat_name[g->to_act] : NULL;
        if (who) snprintf(s, sizeof s, "%s IS THINKING...", who);
        else snprintf(s, sizeof s, " ");
        text_c(s, PLAY_W / 2, 650, 24, C_DIM);
        text_c("CASH OUT: LEAVE THE TABLE", PLAY_W / 2, 684, 20, C_DIM);
        return;
    }
    HoldemLegal L;
    holdem_legal(g, &L);
    int n = 0;
    char items[8][48];
    if (L.can_fold && !L.can_check) snprintf(items[n++], 48, "HOLD1 FOLD");
    if (L.can_check) snprintf(items[n++], 48, "HOLD2 CHECK");
    else if (L.can_call) snprintf(items[n++], 48, "HOLD2 CALL %lld", (long long)L.to_call);
    if (L.can_bet || L.can_raise) {
        snprintf(items[n++], 48, "HOLD3 1/2 POT");
        snprintf(items[n++], 48, "HOLD4 3/4 POT");
        snprintf(items[n++], 48, "HOLD5 POT");
        snprintf(items[n++], 48, "BET MAX ALL-IN");
        snprintf(items[n++], 48, "BET ONE +BB");
    }
    float x = 36;
    for (int i = 0; i < n; i++) {
        int w = MeasureText(items[i], 20);
        DrawRectangleRounded((Rectangle){ x - 6, 636, (float)w + 12, 30 }, 0.3f, 6, C_PANEL);
        DrawText(items[i], (int)x, 641, 20, RAYWHITE);
        x += (float)w + 26;
    }
    if (L.can_bet || L.can_raise) {
        int allin = g->sel_amount >= L.max_to;
        snprintf(s, sizeof s, "DEAL: %s %lld%s", L.can_bet ? "BET" : "RAISE TO", (long long)g->sel_amount,
                 allin ? " (ALL IN)" : "");
        text_c(s, PLAY_W / 2, 678, 28, C_CYAN);
    } else {
        text_c("YOUR TURN", PLAY_W / 2, 678, 28, C_CYAN);
    }
}

static void lobby(const HoldemViewInfo *v, double time)
{
    char s[160];
    text_c("TEXAS HOLD'EM  -  SIT & GO", PLAY_W / 2, 60, 50, C_HONEY);
    text_c("YOU AGAINST FIVE AI PLAYERS  -  1500 CHIPS EACH  -  BLINDS RISE EVERY 10 HANDS", PLAY_W / 2, 130, 20, C_DIM);
    snprintf(s, sizeof s, "BUY-IN  %lld CREDITS", (long long)v->buyin);
    text_c(s, PLAY_W / 2, 200, 40, RAYWHITE);
    for (int p = 1; p <= 3; p++) {
        snprintf(s, sizeof s, "%s PLACE  PAYS  %lld", place_str(p), (long long)v->prize[p]);
        text_c(s, PLAY_W / 2, 260 + p * 40, 30, p == 1 ? C_HONEY : RAYWHITE);
    }
    snprintf(s, sizeof s, "OPPONENTS: %s", v->difficulty ? v->difficulty : "");
    text_c(s, PLAY_W / 2, 440, 24, C_CYAN);
    if (((int)(time * 2.0)) % 2 == 0) text_c("DEAL: BUY IN", PLAY_W / 2, 500, 40, C_MAGENTA);
    text_c("CASH OUT: BACK TO THE MENU", PLAY_W / 2, 556, 20, C_DIM);
}

static void result(const HoldemViewInfo *v, double time)
{
    char s[160];
    (void)time;
    DrawRectangle(240, 180, 800, 300, (Color){ 10, 10, 14, 235 });
    DrawRectangleLinesEx((Rectangle){ 240, 180, 800, 300 }, 3, C_HONEY);
    if (v->place > 0) snprintf(s, sizeof s, "YOU FINISHED %s", place_str(v->place));
    else snprintf(s, sizeof s, "GAME OVER");
    text_c(s, PLAY_W / 2, 220, 50, v->place == 1 ? C_HONEY : RAYWHITE);
    snprintf(s, sizeof s, v->won > 0 ? "PRIZE  %lld CREDITS" : "NO PRIZE THIS TIME", (long long)v->won);
    text_c(s, PLAY_W / 2, 300, 36, v->won > 0 ? C_MAGENTA : C_DIM);
    text_c("DEAL: PLAY AGAIN        CASH OUT: MENU", PLAY_W / 2, 400, 28, RAYWHITE);
}

void holdem_placeholder_view(const HoldemViewInfo *v, double time)
{
    const HoldemGame *g = v->game;
    char s[200];
    DrawRectangleGradientV(0, 0, PLAY_W, PLAY_H, (Color){ 14, 12, 18, 255 }, (Color){ 6, 6, 8, 255 });
    if (v->phase == HV_LOBBY || !g) {
        lobby(v, time);
    } else {
        DrawEllipse(640, 310, 470, 205, C_FELT);
        DrawEllipseLines(640, 310, 470, 205, C_HONEY);
        DrawEllipseLines(640, 310, 462, 198, Fade(C_HONEY, 0.4f));
        for (int i = 0; i < 5; i++) {
            float x = 640 - 2.5f * 68 + i * 68.0f;
            ph_card(x, 236, 62, 86, i < g->nboard ? g->board[i] : CARD_NONE, 1, 1.0f);
        }
        snprintf(s, sizeof s, "POT %lld", (long long)holdem_pot_total(g));
        text_c(s, 640, 336, 30, C_HONEY);
        for (int i = 0; i < HOLDEM_SEATS; i++) seat_panel(v, g, i, time);

        HoldemLevel lv = holdem_level(g);
        snprintf(s, sizeof s, "HAND %d   BLINDS %d/%d%s", g->hand_no, lv.sb, lv.bb, lv.ante ? "" : "");
        if (lv.ante) snprintf(s, sizeof s, "HAND %d   BLINDS %d/%d  ANTE %d", g->hand_no, lv.sb, lv.bb, lv.ante);
        DrawText(s, 16, 12, 20, C_DIM);
        snprintf(s, sizeof s, "PLAYERS LEFT %d", g->players_left);
        DrawText(s, 16, 36, 20, C_DIM);
        if (!v->demo) {
            ph_credits_line(v->credits, v->denom_cents, s, sizeof s);
            int w = MeasureText(s, 20);
            DrawText(s, PLAY_W - 16 - w, 12, 20, C_HONEY);
            snprintf(s, sizeof s, "PRIZES %lld / %lld / %lld", (long long)v->prize[1], (long long)v->prize[2],
                     (long long)v->prize[3]);
            w = MeasureText(s, 20);
            DrawText(s, PLAY_W - 16 - w, 36, 20, C_DIM);
        }
        if (P.news_t > 0) text_c(P.news, 640, 380, 24, C_MAGENTA);
        if (g->phase == HP_GAME_OVER && g->winner >= 0 && g->winner < HOLDEM_SEATS) {
            snprintf(s, sizeof s, "%s WINS THE SIT & GO", v->seat_name[g->winner]);
            text_c(s, 640, 410, 30, C_HONEY);
        }
        if (!v->demo && v->phase == HV_PLAYING) action_bar(v, g);
        if (v->phase == HV_RESULT) result(v, time);
    }
    if (v->confirm_leave && g) {
        int place = g->players_left;
        DrawRectangle(200, 200, 880, 260, (Color){ 10, 10, 14, 245 });
        DrawRectangleLinesEx((Rectangle){ 200, 200, 880, 260 }, 3, C_MAGENTA);
        text_c("LEAVE THE TABLE?", PLAY_W / 2, 230, 44, C_MAGENTA);
        snprintf(s, sizeof s, "YOU WOULD FINISH %s - PRIZE %lld", place_str(place),
                 (long long)(place >= 1 && place <= 3 ? v->prize[place] : 0));
        text_c(s, PLAY_W / 2, 300, 28, RAYWHITE);
        text_c("CASH OUT: LEAVE          DEAL: KEEP PLAYING", PLAY_W / 2, 380, 28, C_HONEY);
    }
    if (v->message) text_c(v->message, PLAY_W / 2, 600, 24, C_AMBER);
}
