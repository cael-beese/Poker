/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* holdem_present.c - see holdem_present.h.
 *
 * How it follows the game. Everything that HAPPENS arrives as an event and
 * starts an animation (a card flies, chips slide, a hand turns over). What
 * the table SHOWS at rest is the const game state minus whatever is still
 * in the air: a seat's bet spot shows `bet - chips flying to it`, the pot
 * `pot - chips flying to it`, a stack `stack - chips flying to it`. So the
 * picture always converges on the true state when the motion ends, whatever
 * the frame rate or however many ticks a frame ran, and a missed event can
 * only cost an animation, never a wrong number.
 *
 * Honesty: opponents' hole cards are drawn face down until an EV_HOLDEM_SHOW
 * event says what they are; their tells are driven only by the value in
 * EV_HOLDEM_TELL; nothing reads the deck. Equity during an all-in run-out
 * uses only tabled cards. Cosmetic randomness is the renderer's own stream. */
#include "render/holdem_present.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raylib.h"
#include "rlgl.h"
#include "engine/eval.h"
#include "platform/fx_settings.h"
#include "platform/screen.h"
#include "render/holdem_art.h"
#include "render/render.h"
#if defined(BPL_HAVE_AUDIO)
#include "audio.h"
#endif

#define PI_F 3.14159265f
#define NSEAT HOLDEM_SEATS

/* ---- layout (1280 x 720) --------------------------------------------------- */

typedef struct {
    float ax, ay;       /* avatar centre                                   */
    int   flip;         /* nameplate to the left of the avatar             */
    float cx, cy;       /* hole cards                                      */
    float bx, by;       /* bet spot                                        */
    float dx, dy;       /* dealer button spot                              */
    float sx, sy;       /* tabled hole cards                               */
} SeatPos;

static const SeatPos k_pos[NSEAT] = {
    { 300, 614, 0, 646, 584, 640, 448, 540, 470, 646, 584 },
    { 124, 468, 0, 252, 392, 360, 408, 346, 466, 262, 382 },
    { 166, 192, 0, 300, 282, 404, 262, 386, 192, 300, 298 },
    { 546, 62, 0, 640, 152, 520, 188, 770, 126, 834, 62 },
    { 1114, 192, 1, 980, 282, 876, 262, 894, 192, 980, 298 },
    { 1156, 468, 1, 1028, 392, 920, 408, 934, 468, 1018, 382 },
};

#define AV_D 92.0f              /* avatar diameter                      */
#define PLATE_W 164.0f
#define PLATE_H 56.0f
#define BOARD_Y 330.0f
#define BOARD_DX 86.0f
#define BOARD_SC 0.64f
#define POT_X 640.0f
#define POT_Y 236.0f
#define HOLE_SC 0.36f           /* opponents' cards, face down          */
#define SHOW_SC 0.52f           /* opponents' cards, tabled             */
#define HERO_SC 0.50f           /* seat 0's cards (CARD_L)              */
#define BAR_Y 652.0f

static float board_x(int i) { return 640.0f + (float)(i - 2) * BOARD_DX; }

static Rectangle plate_rect(int s)
{
    const SeatPos *p = &k_pos[s];
    float x = p->flip ? p->ax - 30 - PLATE_W : p->ax + 26;
    return (Rectangle){ x, p->ay - PLATE_H * 0.5f, PLATE_W, PLATE_H };
}

static Vector2 plate_centre(int s)
{
    Rectangle r = plate_rect(s);
    return (Vector2){ r.x + r.width * 0.5f, r.y + r.height * 0.5f };
}

/* Side pots spread left and right of the main pot. */
static Vector2 pot_slot(int i, int n)
{
    static const float dx[NSEAT] = { 0, 175, -175, 345, -345, 470 };
    if (n <= 1) return (Vector2){ POT_X, POT_Y };
    return (Vector2){ POT_X + dx[i < NSEAT ? i : NSEAT - 1], POT_Y };
}

/* ---- cosmetic state ------------------------------------------------------------- */

typedef struct {
    int   on;
    Card  c;
    float x0, y0, r0, s0, x1, y1, r1, s1;
    float t, dur, delay, arc;
    int   moving;
    float flip, flip_to, ft, fdur, fdelay;
    int   flipping;
    float alpha;
    int   leaving;
    float x, y, rot, sc;
    float highlight, dim;
    int   ev;               /* bits this update: 1 landed, 2 flip started, 4 flip done, 8 gone */
} CardFx;

enum { FL_BET, FL_POT, FL_STACK };

typedef struct {
    int     on, kind, seat, pot, started, award;
    int64_t amt;
    float   x0, y0, x1, y1, t, dur, delay, arc;
} Flight;

#define NFLIGHT 64
#define NFLOAT 12
#define NBANNER 6

typedef struct {
    float blink, blink_t, next_blink;
    float gx, gy, tgx, tgy, next_gaze;
    float ant, bob, flap, chip;
    float tension;
    float react;
    float hurt;
} AvFx;

typedef struct {
    int   avatar;
    char  name[16];
    char  badge[12];
    int   dealt;                /* hole cards dealt this hand            */
    int   shown, folded, mucked;
    float tell;
    int   has_tell;
    float think_t;
    char  act[28];
    Color act_col;
    float act_t;                /* seconds since the tag appeared (< 0: none) */
    int   allin;
    char  hand[40];
    float hand_t;
    int   winner;
    int   best_valid;
    Card  best[5];
    float eq;
    int   eq_valid;
    int   out, place;
    float out_t;
    AvFx  av;
} SeatFx;

typedef struct {
    int   on;
    char  text[40], sub[40];
    float x, y, size, t, dur;
    int   rays;
    Color col;
    int   neon;
} Banner;

typedef struct {
    int   on;
    char  text[24];
    float x, y, t, dur;
    Color col;
} Floater;

static struct {
    int inited;
    const HoldemGame *game;
    int demo, phase;
    uint32_t hand_no;
    SeatFx seat[NSEAT];
    CardFx hole[NSEAT][2];
    CardFx board[5];
    int    nboard;
    Flight fl[NFLIGHT];
    int64_t infl_bet[NSEAT], infl_stack[NSEAT], infl_pot;
    int64_t spot[NSEAT];         /* chips at each bet spot, landed or flying */
    int    npots;
    HoldemPot pots[HOLDEM_MAX_POTS];
    int64_t awarded[HOLDEM_MAX_POTS];
    int    split_on;
    float  split_t;
    float  btn_x, btn_y;
    Tween  btn_tx, btn_ty;
    int    btn_seat;
    int    to_act;
    float  turn_t;
    int    runout;
    float  runout_t;
    float  clear_timer;
    int    cleared;
    Banner banner[NBANNER];
    Floater fl_text[NFLOAT];
    FxClock clock;
    Shake shake;
    BulbRing bulbs;
    float  bulbs_k;
    Celebration cel;
    float  rail_mode_t;
    int    rail_mode;           /* 0 calm, 1 your turn, 2 all-in, 3 celebration */
    UiButton btn[8];
    float  bar_k;               /* action bar lit 0..1                     */
    Spring slider;
    int64_t amount;
    float  amount_bump;
    float  level_flash;
    float  result_t;
    int    result_delay;
    float  over_t;
    float  lobby_t;
    float  time;
    int    snd;
    int    last_game_over;
    int    game_over_winner;
    int    fresh;               /* just reset: the next sync is expected */
    HoldemPresentStats st;
} P;

/* ---- helpers ------------------------------------------------------------------- */

static float pan_of(int s)
{
    if (s < 0 || s >= NSEAT) return 0;
    return clampf((k_pos[s].ax - 640.0f) / 640.0f * 0.6f / 0.8f, -0.6f, 0.6f);
}

static void sfx(int id, float vol, float pitch, float pan)
{
#if defined(BPL_HAVE_AUDIO)
    if (P.snd) audio_play((SfxId)id, vol, pitch, pan);
#else
    (void)id; (void)vol; (void)pitch; (void)pan;
#endif
}

/* A bounded string copy that always terminates.  Used where snprintf("%s")
   used to be: truncating a label to its box is intended, and GCC -O3 on the
   Pi (-Wformat-truncation) rejects the snprintf form under -Werror. */
static void copy_str(char *dst, size_t n, const char *src)
{
    if (!n) return;
    size_t l = strlen(src);
    if (l >= n) l = n - 1;
    memcpy(dst, src, l);
    dst[l] = 0;
}

static const char *fmt_chips(int64_t v, char *buf, size_t n)
{
    char tmp[32];
    int neg = v < 0;
    unsigned long long u = (unsigned long long)(neg ? -v : v);
    int len = snprintf(tmp, sizeof tmp, "%llu", u);
    char out[48];
    int o = 0;
    if (neg) out[o++] = '-';
    for (int i = 0; i < len; i++) {
        out[o++] = tmp[i];
        int left = len - 1 - i;
        if (left > 0 && left % 3 == 0) out[o++] = ',';
    }
    out[o] = '\0';
    copy_str(buf, n, out);
    return buf;
}

static const char *place_str(int p)
{
    static const char *const s[] = { "-", "1ST", "2ND", "3RD", "4TH", "5TH", "6TH" };
    return p >= 0 && p <= 6 ? s[p] : "?";
}

static TextStyle style(Color c, TextAlign a)
{
    TextStyle t;
    memset(&t, 0, sizeof t);
    t.color = c;
    t.align = a;
    return t;
}

/* Text centred vertically on cy. */
static void label(FontId f, const char *s, float x, float cy, float size, Color c, TextAlign a, float shadow)
{
    TextStyle t = style(c, a);
    if (shadow > 0) { t.shadow = BLACK; t.shadow_k = shadow; }
    text_draw_ex(f, s, x, cy - size * 0.56f, size, &t);
}

static void gold_label(FontId f, const char *s, float x, float cy, float size, TextAlign a, float glow)
{
    TextStyle t = style((Color){ 255, 244, 200, 255 }, a);
    t.color2 = (Color){ 240, 160, 40, 255 };
    t.shadow = BLACK;
    t.shadow_k = 0.8f;
    if (glow > 0) { t.glow = UI_AMBER; t.glow_k = glow; }
    text_draw_ex(f, s, x, cy - size * 0.56f, size, &t);
}

static Color fade(Color c, float a)
{
    return (Color){ c.r, c.g, c.b, (unsigned char)(clampf(a, 0, 1) * c.a) };
}

static int avatar_for(const char *name, int seat)
{
    static const char *const names[AV_N] = { "YOU", "BUZZ", "HONEY", "STINGER", "DRONE", "QUEENIE", "BEE" };
    if (name) {
        for (int a = 0; a < AV_N; a++) {
            size_t n = strlen(names[a]);
            if (strncmp(name, names[a], n) == 0 && (name[n] == '\0' || name[n] == ' ')) return a;
        }
    }
    return seat == 0 ? AV_YOU : AV_BUZZ + (seat - 1) % 5;
}

/* "BUZZ (ROCK)" -> name "BUZZ", badge "ROCK". */
static void split_name(const char *full, char *name, size_t nn, char *badge, size_t nb)
{
    name[0] = badge[0] = '\0';
    if (!full) return;
    const char *p = strstr(full, " (");
    if (!p) { copy_str(name, nn, full); return; }
    size_t len = (size_t)(p - full);
    if (len >= nn) len = nn - 1;
    memcpy(name, full, len);
    name[len] = '\0';
    const char *q = strchr(p + 2, ')');
    size_t bl = q ? (size_t)(q - (p + 2)) : strlen(p + 2);
    if (bl >= nb) bl = nb - 1;
    memcpy(badge, p + 2, bl);
    badge[bl] = '\0';
}

static Color badge_color(const char *b)
{
    if (strcmp(b, "ROCK") == 0) return (Color){ 150, 170, 200, 255 };
    if (strcmp(b, "MANIAC") == 0) return (Color){ 255, 70, 90, 255 };
    if (strcmp(b, "SHARK") == 0) return UI_CYAN;
    if (strcmp(b, "FISH") == 0) return (Color){ 120, 230, 120, 255 };
    return UI_HONEY;
}

/* ---- hand names ------------------------------------------------------------------ */

static const char *const k_rank1[13] = { "TWO", "THREE", "FOUR", "FIVE", "SIX", "SEVEN", "EIGHT", "NINE",
                                         "TEN", "JACK", "QUEEN", "KING", "ACE" };
static const char *const k_rankn[13] = { "TWOS", "THREES", "FOURS", "FIVES", "SIXES", "SEVENS", "EIGHTS",
                                         "NINES", "TENS", "JACKS", "QUEENS", "KINGS", "ACES" };

static void hand_desc(const Card best[5], int rank, char *out, size_t n)
{
    int cnt[13] = { 0 }, hi = 0;
    for (int i = 0; i < 5; i++) {
        cnt[card_rank(best[i])]++;
        if (card_rank(best[i]) > hi) hi = card_rank(best[i]);
    }
    int quad = -1, trip = -1, p1 = -1, p2 = -1;
    for (int r = 12; r >= 0; r--) {
        if (cnt[r] == 4) quad = r;
        else if (cnt[r] == 3) trip = r;
        else if (cnt[r] == 2) { if (p1 < 0) p1 = r; else p2 = r; }
    }
    int wheel = cnt[12] && cnt[0] && cnt[1] && cnt[2] && cnt[3];
    int shi = wheel ? 3 : hi;
    switch (eval_category(rank)) {
    case HC_STRAIGHT_FLUSH:
        if (eval_is_royal(rank)) snprintf(out, n, "ROYAL FLUSH");
        else snprintf(out, n, "STRAIGHT FLUSH, %s HIGH", k_rank1[shi]);
        break;
    case HC_QUADS: snprintf(out, n, "FOUR %s", k_rankn[quad < 0 ? hi : quad]); break;
    case HC_FULL_HOUSE:
        snprintf(out, n, "%s FULL OF %s", k_rankn[trip < 0 ? hi : trip], k_rankn[p1 < 0 ? 0 : p1]);
        break;
    case HC_FLUSH: snprintf(out, n, "FLUSH, %s HIGH", k_rank1[hi]); break;
    case HC_STRAIGHT: snprintf(out, n, "STRAIGHT, %s HIGH", k_rank1[shi]); break;
    case HC_TRIPS: snprintf(out, n, "THREE %s", k_rankn[trip < 0 ? hi : trip]); break;
    case HC_TWO_PAIR: snprintf(out, n, "%s AND %s", k_rankn[p1 < 0 ? 0 : p1], k_rankn[p2 < 0 ? 0 : p2]); break;
    case HC_PAIR: snprintf(out, n, "PAIR OF %s", k_rankn[p1 < 0 ? hi : p1]); break;
    default: snprintf(out, n, "%s HIGH", k_rank1[hi]); break;
    }
}

/* ---- cards ------------------------------------------------------------------------- */

static float anim_k(void) { return g_effects.card_anim ? 1.0f : 0.0f; }

static void cf_place(CardFx *c, Card card, float x, float y, float rot, float sc, int face)
{
    memset(c, 0, sizeof *c);
    c->on = 1;
    c->c = card;
    c->x = c->x1 = x;
    c->y = c->y1 = y;
    c->rot = c->r1 = rot;
    c->sc = c->s1 = sc;
    c->flip = c->flip_to = face ? 1.0f : 0.0f;
    c->alpha = 1;
}

static void cf_deal(CardFx *c, Card card, Vector2 from, float x, float y, float rot, float sc, float delay,
                    float dur, float arc, int face, float flip_after, float flip_dur)
{
    cf_place(c, card, from.x, from.y, rot - 0.9f, sc * 0.6f, 0);
    c->x0 = from.x; c->y0 = from.y; c->r0 = rot - 0.9f; c->s0 = sc * 0.6f;
    c->x1 = x; c->y1 = y; c->r1 = rot; c->s1 = sc;
    c->delay = delay * anim_k();
    c->dur = fmaxf(0.001f, dur * anim_k());
    c->arc = arc;
    c->t = 0;
    c->moving = 1;
    c->alpha = 0;           /* appears when it leaves the shoe */
    if (face) {
        c->flip_to = 1;
        c->fdelay = c->delay + c->dur + flip_after * anim_k();
        c->fdur = fmaxf(0.001f, flip_dur * anim_k());
        c->ft = 0;
        c->flipping = 1;
    }
}

static void cf_move(CardFx *c, float x, float y, float rot, float sc, float dur, float delay)
{
    c->x0 = c->x; c->y0 = c->y; c->r0 = c->rot; c->s0 = c->sc;
    c->x1 = x; c->y1 = y; c->r1 = rot; c->s1 = sc;
    c->t = 0;
    c->delay = delay * anim_k();
    c->dur = fmaxf(0.001f, dur * anim_k());
    c->arc = 0;
    c->moving = 1;
}

static void cf_flip(CardFx *c, Card card, float delay, float dur)
{
    if (card != CARD_NONE) c->c = card;
    c->flip_to = 1;
    c->fdelay = delay * anim_k();
    c->fdur = fmaxf(0.001f, dur * anim_k());
    c->ft = 0;
    c->flipping = 1;
}

/* Slide away (fold, muck, the end of the hand) and fade out. */
static void cf_leave(CardFx *c, float tx, float ty, float delay)
{
    if (!c->on || c->leaving) return;
    cf_move(c, tx, ty, c->rot + 0.6f, c->sc * 0.7f, 0.42f, delay);
    c->leaving = 1;
}

static void cf_update(CardFx *c, float dt)
{
    c->ev = 0;
    if (!c->on) return;
    if (c->moving) {
        c->t += dt;
        float t = c->t - c->delay;
        if (t >= 0) {
            if (c->alpha <= 0 && !c->leaving) c->alpha = 1;
            float k = clampf(t / c->dur, 0, 1);
            float e = ease(c->leaving ? EASE_IN_QUAD : EASE_OUT_CUBIC, k);
            Vec2f q;
            if (c->arc != 0) {
                Vec2f a = { c->x0, c->y0 }, b = { c->x1, c->y1 };
                Vec2f m = { (a.x + b.x) * 0.5f, fminf(a.y, b.y) - c->arc };
                q = bezier2(a, m, b, e);
            } else {
                q = (Vec2f){ lerpf(c->x0, c->x1, e), lerpf(c->y0, c->y1, e) };
            }
            c->x = q.x;
            c->y = q.y;
            c->rot = lerpf(c->r0, c->r1, ease(c->leaving ? EASE_IN_QUAD : EASE_OUT_BACK, k));
            c->sc = lerpf(c->s0, c->s1, ease(EASE_OUT_QUART, k));
            if (c->leaving) c->alpha = 1 - ease(EASE_IN_QUAD, k);
            if (k >= 1) {
                c->moving = 0;
                if (c->leaving) { c->on = 0; c->ev |= 8; return; }
                c->ev |= 1;
            }
        }
    }
    if (c->flipping) {
        c->ft += dt;
        float t = c->ft - c->fdelay;
        if (t >= 0) {
            if (c->flip == 0 && c->flip_to > 0) c->ev |= 2;
            float k = clampf(t / c->fdur, 0, 1);
            c->flip = card_flip_curve(k);
            if (k >= 1) { c->flipping = 0; c->flip = c->flip_to; c->ev |= 4; }
        }
    }
}

static void cf_draw(const CardFx *c, CardSize size, int back, float glow)
{
    if (!c->on || c->alpha <= 0.01f) return;
    CardPose p;
    memset(&p, 0, sizeof p);
    p.x = c->x;
    p.y = c->y;
    p.rot = c->rot;
    p.scale = c->sc;
    p.flip = c->flip;
    p.alpha = c->alpha;
    p.back = back;
    p.highlight = c->highlight;
    p.dim = c->dim;
    p.glow = glow;
    if (c->flipping && c->flip > 0) p.lift = 10.0f * c->sc * sinf(c->flip * PI_F);
    card_draw(c->c, size, &p);
}

/* ---- chips --------------------------------------------------------------------------- */

typedef struct { int denom, n; } ChipCol;

/* Splits an amount into columns of chips. The top denomination is the
 * largest that gives at least two chips, so piles look like piles; the
 * total is capped by colouring up. Returns the column count. */
static int chip_columns(int64_t amount, ChipCol *cols, int maxcols, int maxchips, int per_col)
{
    if (amount <= 0) return 0;
    int top = 0;
    for (int d = HCHIP_N - 1; d >= 0; d--)
        if (amount / hart_chip_value[d] >= 2) { top = d; break; }
    for (;;) {
        int64_t left = amount;
        int total = 0, nc = 0;
        for (int d = top; d >= 0 && nc < maxcols; d--) {
            int64_t k = left / hart_chip_value[d];
            if (d == top && k > (int64_t)maxchips * 4) k = (int64_t)maxchips * 4;
            left -= k * hart_chip_value[d];
            total += (int)k;
            while (k > 0 && nc < maxcols) {
                int take = (int)(k > per_col ? per_col : k);
                cols[nc].denom = d;
                cols[nc].n = take;
                nc++;
                k -= take;
            }
        }
        if ((total <= maxchips && nc < maxcols) || top >= HCHIP_N - 1) return nc;
        top++;
    }
}

/* A pile of chips standing on (x, y) (the bottom centre). */
static void draw_pile(int64_t amount, float x, float y, float scale, float alpha, int maxcols)
{
    ChipCol cols[8];
    if (maxcols > 8) maxcols = 8;
    int nc = chip_columns(amount, cols, maxcols, 34, 8);
    if (nc == 0) return;
    float w = HCHIP_W * scale, h = HCHIP_H * scale, t = HCHIP_THICK * scale;
    float gap = w * 0.86f;
    int back = nc > 4 ? nc / 2 : 0;        /* a second row behind for big piles */
    PCol c = gfx_cola(WHITE, alpha);
    float sw = (float)(nc - back > back ? nc - back : back) * gap + w * 0.6f;
    gfx_spr_rot(sprite(SPR_GLOW), x, y - t, sw * 1.1f, h * 0.9f, 0, gfx_cola(BLACK, 0.55f * alpha));
    for (int row = 0; row < 2; row++) {
        int from = row == 0 ? 0 : back, to = row == 0 ? back : nc;
        int n = to - from;
        if (n <= 0) continue;
        float ry = y - (row == 0 && back ? 12 * scale : 0);
        float rx = x - (float)(n - 1) * gap * 0.5f + (row == 0 ? gap * 0.5f : 0);
        for (int i = from; i < to; i++) {
            float cx = rx + (float)(i - from) * gap;
            const Spr *s = hart_chip(cols[i].denom);
            for (int k = 0; k < cols[i].n; k++) {
                float jx = (float)(((i * 7 + k * 3) % 5) - 2) * 0.35f * scale;
                gfx_spr(s, cx - w * 0.5f + jx, ry - h + 2 * scale - (float)k * t, w, h, c);
            }
        }
    }
}

/* ---- flights --------------------------------------------------------------------------- */

static Flight *flight_new(void)
{
    for (int i = 0; i < NFLIGHT; i++)
        if (!P.fl[i].on) { memset(&P.fl[i], 0, sizeof P.fl[i]); return &P.fl[i]; }
    return NULL;
}

static void flight_land(Flight *f)
{
    f->on = 0;
    switch (f->kind) {
    case FL_BET: P.infl_bet[f->seat] -= f->amt; break;
    case FL_POT: P.infl_pot -= f->amt; break;
    default: P.infl_stack[f->seat] -= f->amt; break;
    }
    float pan = pan_of(f->seat);
    if (f->kind == FL_POT) sfx(SFX_CHIP_POT, 0.55f, 1.0f, pan);
    else if (f->kind == FL_BET) sfx(f->amt >= 100 ? SFX_CHIP_STACK : SFX_CHIP_SINGLE, 0.75f, 1.0f, pan);
    else sfx(f->award ? SFX_CHIP_POT : SFX_CHIP_SINGLE, 0.8f, 1.0f, pan);
    if (f->award) {
        for (int i = 0; i < NFLOAT; i++) {
            Floater *t = &P.fl_text[i];
            if (t->on) continue;
            t->on = 1;
            char b[24];
            snprintf(t->text, sizeof t->text, "+%s", fmt_chips(f->amt, b, sizeof b));
            Vector2 pc = plate_centre(f->seat);
            t->x = pc.x;
            t->y = pc.y - 40;
            t->t = 0;
            t->dur = 1.8f;
            t->col = UI_GOLD;
            break;
        }
        P.seat[f->seat].av.react = 1;
        pfx_sparkle(f->x1, f->y1, 60, 8);
    }
}

/* Chips from (x0,y0) to (x1,y1); the destination's display holds them back
 * until they land. */
static Flight *fly(int kind, int seat, int pot, int64_t amt, float x0, float y0, float x1, float y1, float delay,
                float dur)
{
    if (amt <= 0) return NULL;
    switch (kind) {
    case FL_BET: P.infl_bet[seat] += amt; break;
    case FL_POT: P.infl_pot += amt; break;
    default: P.infl_stack[seat] += amt; break;
    }
    Flight *f = flight_new();
    if (!f) {       /* pool full: land at once */
        Flight tmp = { 1, kind, seat, pot, 1, 0, amt, x0, y0, x1, y1, 0, 0, 0, 0 };
        flight_land(&tmp);
        return NULL;
    }
    f->on = 1;
    f->kind = kind;
    f->seat = seat;
    f->pot = pot;
    f->amt = amt;
    f->x0 = x0; f->y0 = y0; f->x1 = x1; f->y1 = y1;
    f->delay = g_effects.chip_anim ? delay : 0;
    f->dur = g_effects.chip_anim ? dur : 0;
    f->arc = 18 + 0.06f * sqrtf((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
    return f;
}

static void flights_update(float dt)
{
    int live = 0;
    for (int i = 0; i < NFLIGHT; i++) {
        Flight *f = &P.fl[i];
        if (!f->on) continue;
        f->t += dt;
        if (f->t >= f->delay + f->dur) flight_land(f);
        else live++;
    }
    P.st.flights_live = live;
    if (live > P.st.flights_peak) P.st.flights_peak = live;
}

static void flights_draw(void)
{
    for (int i = 0; i < NFLIGHT; i++) {
        const Flight *f = &P.fl[i];
        if (!f->on || f->t < f->delay) continue;
        float k = clampf((f->t - f->delay) / fmaxf(f->dur, 0.001f), 0, 1);
        /* Slight overshoot into the spot, then it settles. */
        float e = ease(EASE_OUT_BACK, k);
        Vec2f a = { f->x0, f->y0 }, b = { f->x1, f->y1 };
        Vec2f m = { (a.x + b.x) * 0.5f, fminf(a.y, b.y) - f->arc };
        Vec2f q = bezier2(a, m, b, clampf(e, 0, 1));
        if (e > 1) {
            q.x += (b.x - a.x) * (e - 1) * 0.6f;
            q.y += (b.y - a.y) * (e - 1) * 0.6f;
        }
        /* The stack grows or shrinks to the size it has where it lands. */
        float dst = f->kind == FL_POT ? 0.9f : f->kind == FL_BET ? 0.7f : 0.6f;
        float sc = lerpf(0.7f, dst, k) + 0.12f * sinf(k * PI_F);
        draw_pile(f->amt, q.x, q.y + 10, sc, 1, 3);
    }
}

/* ---- banners and floaters -------------------------------------------------------------- */

static void banner(const char *text, const char *sub, float x, float y, float size, float dur, int rays, Color col,
                   int neon)
{
    Banner *b = NULL;
    for (int i = 0; i < NBANNER; i++) if (!P.banner[i].on) { b = &P.banner[i]; break; }
    if (!b) b = &P.banner[0];
    memset(b, 0, sizeof *b);
    b->on = 1;
    copy_str(b->text, sizeof b->text, text);
    copy_str(b->sub, sizeof b->sub, sub ? sub : "");
    b->x = x; b->y = y; b->size = size; b->dur = dur; b->rays = rays; b->col = col; b->neon = neon;
}

static void banners_draw(double time)
{
    for (int i = 0; i < NBANNER; i++) {
        const Banner *b = &P.banner[i];
        if (!b->on) continue;
        float ap = clampf(b->t / 0.35f, 0, 1), van = clampf((b->t - (b->dur - 0.35f)) / 0.35f, 0, 1);
        if (b->neon) {
            float a = ease(EASE_OUT_BACK, ap) * (1 - van);
            if (a > 0.01f) {
                float w = text_width(FONT_NEON_M, b->text, b->size, 0);
                gfx_nine(sprite_nine(NINE_RRECT), b->x - w * 0.5f - 26, b->y - b->size * 0.75f, w + 52, b->size * 1.5f, 16,
                         gfx_cola((Color){ 10, 6, 12, 255 }, 0.8f * a));
                text_neon(FONT_NEON_M, b->text, b->x, b->y, b->size * (0.8f + 0.2f * a), b->col, a, 1.0f);
            }
        } else {
            ui_banner(b->text, b->x, b->y, b->size, ap, van, b->rays, time);
        }
        if (b->sub[0]) {
            float a = clampf((b->t - 0.2f) / 0.3f, 0, 1) * (1 - van);
            if (a > 0.01f) text_neon(FONT_NEON_M, b->sub, b->x, b->y + b->size * 0.95f, 34, UI_CYAN, a, 1.0f);
        }
    }
}

static void floaters_draw(void)
{
    for (int i = 0; i < NFLOAT; i++) {
        const Floater *t = &P.fl_text[i];
        if (!t->on) continue;
        float k = t->t / t->dur;
        float a = k < 0.15f ? k / 0.15f : 1 - clampf((k - 0.6f) / 0.4f, 0, 1);
        float y = t->y - 36 * ease(EASE_OUT_CUBIC, clampf(k, 0, 1));
        TextStyle s = style((Color){ 255, 246, 196, 255 }, ALIGN_CENTER);
        s.color2 = (Color){ 240, 150, 30, 255 };
        s.glow = t->col;
        s.glow_k = 0.8f;
        s.shadow = BLACK;
        s.shadow_k = 0.9f;
        s.opacity = a;
        text_draw_ex(FONT_DISP_M, t->text, t->x, y - 18, 34, &s);
    }
}

/* ---- equity during a run-out (tabled cards only) ----------------------------------------- */

static void compute_equity(const HoldemGame *g)
{
    int seats[NSEAT], n = 0;
    for (int s = 0; s < NSEAT; s++) {
        P.seat[s].eq_valid = 0;
        if (!holdem_seat_live(g, s)) continue;
        if (!g->seat[s].shown) return;          /* someone's hand is still hidden */
        seats[n++] = s;
    }
    if (n < 2) return;
    uint8_t used[52] = { 0 };
    Card board[5];
    /* Only the board cards already turned on the table: the game knows the
       next card a moment before it lands, and the odds must not tell. */
    int nb = 0;
    while (nb < g->nboard && nb < 5 && P.board[nb].on && P.board[nb].flip >= 1 && !P.board[nb].flipping) nb++;
    for (int i = 0; i < nb; i++) { board[i] = g->board[i]; used[board[i]] = 1; }
    for (int i = 0; i < n; i++) { used[g->seat[seats[i]].hole[0]] = 1; used[g->seat[seats[i]].hole[1]] = 1; }
    Card deck[52];
    int nd = 0;
    for (int c = 0; c < 52; c++) if (!used[c]) deck[nd++] = (Card)c;
    int need = 5 - nb;
    double win[NSEAT] = { 0 };
    long trials = 0;
    Card c7[7];
    Rng r;
    rng_seed(&r, rng_next(fx_rng()));
    long max_trials = need <= 2 ? 0 : 1500;
    /* Exhaustive for the turn and river, sampled before the flop. */
    for (int a = 0; need == 0 ? a < 1 : a < nd; a++) {
        for (int b = need >= 2 ? a + 1 : 0; need >= 2 ? b < nd : b < 1; b++) {
            if (need > 2) break;
            Card run[5];
            int k = 0;
            for (int i = 0; i < nb; i++) run[k++] = board[i];
            if (need >= 1) run[k++] = deck[a];
            if (need >= 2) run[k++] = deck[b];
            int best = 1 << 30, nbest = 0, rk[NSEAT];
            for (int i = 0; i < n; i++) {
                c7[0] = g->seat[seats[i]].hole[0];
                c7[1] = g->seat[seats[i]].hole[1];
                memcpy(c7 + 2, run, 5);
                rk[i] = eval7(c7);
                if (rk[i] < best) { best = rk[i]; nbest = 1; }
                else if (rk[i] == best) nbest++;
            }
            for (int i = 0; i < n; i++) if (rk[i] == best) win[i] += 1.0 / nbest;
            trials++;
        }
        if (need > 2) break;
    }
    for (long t = 0; t < max_trials; t++) {
        Card run[5];
        for (int i = 0; i < nb; i++) run[i] = board[i];
        Card pool[52];
        memcpy(pool, deck, (size_t)nd * sizeof(Card));
        for (int i = 0; i < need; i++) {
            int j = i + (int)rng_below(&r, (uint32_t)(nd - i));
            Card tmp = pool[i]; pool[i] = pool[j]; pool[j] = tmp;
            run[nb + i] = pool[i];
        }
        int best = 1 << 30, nbest = 0, rk[NSEAT];
        for (int i = 0; i < n; i++) {
            c7[0] = g->seat[seats[i]].hole[0];
            c7[1] = g->seat[seats[i]].hole[1];
            memcpy(c7 + 2, run, 5);
            rk[i] = eval7(c7);
            if (rk[i] < best) { best = rk[i]; nbest = 1; }
            else if (rk[i] == best) nbest++;
        }
        for (int i = 0; i < n; i++) if (rk[i] == best) win[i] += 1.0 / nbest;
        trials++;
    }
    if (trials <= 0) return;
    for (int i = 0; i < n; i++) {
        P.seat[seats[i]].eq = (float)(win[i] / (double)trials);
        P.seat[seats[i]].eq_valid = 1;
    }
}

/* The five cards a tabled hand plays, once the board is complete. */
static void compute_best(const HoldemGame *g, int s)
{
    SeatFx *f = &P.seat[s];
    f->best_valid = 0;
    if (g->nboard < 5) return;
    if (s != 0 && !g->seat[s].shown) return;
    Card c7[7] = { g->seat[s].hole[0], g->seat[s].hole[1] };
    memcpy(c7 + 2, g->board, 5);
    int rank = eval_best(c7, 7, f->best);
    if (rank <= 0) return;
    f->best_valid = 1;
    hand_desc(f->best, rank, f->hand, sizeof f->hand);
    f->hand_t = 0;
}

static int in_best(const SeatFx *f, Card c)
{
    if (!f->best_valid) return 0;
    for (int i = 0; i < 5; i++) if (f->best[i] == c) return 1;
    return 0;
}

/* ---- the reset and the sync fallback ------------------------------------------------------ */

static void hole_rest(int s, int k, Card c, int face, float *x, float *y, float *rot, float *sc)
{
    const SeatPos *p = &k_pos[s];
    if (s == 0) {
        *x = p->cx + (k ? 48.0f : -48.0f);
        *y = p->cy;
        *rot = k ? 0.06f : -0.06f;
        *sc = HERO_SC;
        return;
    }
    (void)c;
    if (face) {
        /* Tabled: bigger, where the whole table can read them. */
        *x = p->sx + (k ? 34.0f : -34.0f);
        *y = p->sy;
        *rot = k ? 0.05f : -0.05f;
        *sc = SHOW_SC;
    } else {
        *x = p->cx + (k ? 12.0f : -12.0f);
        *y = p->cy;
        *rot = k ? 0.14f : -0.12f;
        *sc = HOLE_SC;
    }
}

static void reset_hand_fx(void)
{
    for (int s = 0; s < NSEAT; s++) {
        SeatFx *f = &P.seat[s];
        f->dealt = f->shown = f->folded = f->mucked = 0;
        f->has_tell = 0;
        f->act[0] = '\0';
        f->act_t = -1;
        f->allin = 0;
        f->hand[0] = '\0';
        f->winner = 0;
        f->best_valid = 0;
        f->eq_valid = 0;
        P.hole[s][0].on = P.hole[s][1].on = 0;
    }
    for (int i = 0; i < 5; i++) P.board[i].on = 0;
    memset(P.spot, 0, sizeof P.spot);
    P.nboard = 0;
    P.npots = 0;
    memset(P.awarded, 0, sizeof P.awarded);
    P.split_on = 0;
    P.split_t = 0;
    P.runout = 0;
    P.clear_timer = -1;
    P.cleared = 0;
    if (P.rail_mode != 3) P.rail_mode = 0;
}

static void reset_all(const HoldemViewInfo *v)
{
    P.game = v->game;
    P.demo = v->demo;
    P.fresh = 1;
    for (int i = 0; i < NFLIGHT; i++) P.fl[i].on = 0;
    memset(P.infl_bet, 0, sizeof P.infl_bet);
    memset(P.infl_stack, 0, sizeof P.infl_stack);
    P.infl_pot = 0;
    for (int i = 0; i < NBANNER; i++) P.banner[i].on = 0;
    for (int i = 0; i < NFLOAT; i++) P.fl_text[i].on = 0;
    reset_hand_fx();
    for (int s = 0; s < NSEAT; s++) {
        SeatFx *f = &P.seat[s];
        f->out = 0;
        f->place = 0;
        f->out_t = 0;
        memset(&f->av, 0, sizeof f->av);
        f->av.next_blink = fx_randf(0.5f, 3.0f);
        f->av.next_gaze = fx_randf(0.3f, 2.0f);
    }
    P.to_act = -1;
    P.btn_seat = -1;
    P.hand_no = 0;
    P.last_game_over = 0;
    P.result_t = 0;
    P.over_t = 0;
    P.rail_mode = 0;
    if (P.cel.active) celeb_skip(&P.cel);
}

/* The view should show what the game holds; if an event was missed (or the
 * view came in mid-hand), put the pieces at rest where they belong. Only
 * public cards are used: seat 0's own, tabled hands and the board. */
static void sync_to_game(const HoldemGame *g)
{
    if (!g) return;
    int dealing_done = g->phase > HP_DEAL && g->phase <= HP_HAND_END;
    for (int s = 0; s < NSEAT; s++) {
        const HoldemSeat *st = &g->seat[s];
        SeatFx *f = &P.seat[s];
        if (st->empty) continue;
        int want = st->in_hand && !st->folded && !st->mucked && dealing_done && !P.cleared;
        for (int k = 0; k < 2; k++) {
            CardFx *c = &P.hole[s][k];
            if (want && !c->on && f->dealt >= 2) continue;          /* it left on purpose */
            if (want && !c->on) {
                int face = s == 0 || st->shown;
                float x, y, r, sc;
                hole_rest(s, k, st->hole[k], face, &x, &y, &r, &sc);
                cf_place(c, face ? st->hole[k] : CARD_NONE, x, y, r, sc, face);
                P.st.desyncs += !P.fresh;
            }
            if (want && c->on && st->shown && c->flip_to < 1 && !c->flipping) {
                cf_flip(c, st->hole[k], 0, 0.3f);
                float x, y, r, sc;
                hole_rest(s, k, st->hole[k], 1, &x, &y, &r, &sc);
                cf_move(c, x, y, r, sc, 0.3f, 0);
            }
        }
        if (dealing_done) f->dealt = 2;
        f->folded = st->folded;
        if (!st->in_game && st->place > 0 && !f->out) { f->out = 1; f->place = st->place; f->out_t = 10; }
    }
    if (!P.cleared) {
        for (int i = P.nboard; i < g->nboard && i < 5; i++) {
            cf_place(&P.board[i], g->board[i], board_x(i), BOARD_Y, 0, BOARD_SC, 1);
            P.st.desyncs += !P.fresh;
        }
        if (g->nboard > P.nboard) P.nboard = g->nboard;
    }
}

/* ---- events ------------------------------------------------------------------------------- */

static Vector2 shoe_pos(void)
{
    return (Vector2){ P.btn_x, P.btn_y };
}

static void set_act(int s, const char *txt, Color c)
{
    SeatFx *f = &P.seat[s];
    snprintf(f->act, sizeof f->act, "%.27s", txt);
    f->act_col = c;
    f->act_t = 0;
}

static void human_celebrate(const HoldemViewInfo *v, const HoldemGame *g, int64_t won)
{
    int64_t total = 0;
    for (int s = 0; s < NSEAT; s++) total += g->seat[s].stack;
    int64_t bb = holdem_level(g).bb;
    WinTier tier = WIN_SMALL;
    const char *title = NULL;
    if (won * 100 >= total * 40) { tier = WIN_JACKPOT; title = "MONSTER POT"; }
    else if (won * 100 >= total * 22) { tier = WIN_BIG; title = "BIG POT"; }
    else if (won >= 12 * bb) { tier = WIN_MEDIUM; title = "YOU WIN"; }
    if (tier == WIN_JACKPOT && !g_effects.takeover) tier = WIN_BIG;
    Vector2 focus = { 640, v->demo ? 470.0f : 520.0f };
    celeb_start(&P.cel, tier, (long long)won, title, focus);
    celeb_set_label(&P.cel, "POT");
    P.st.celebrations++;
}

static void on_event(const HoldemViewInfo *v, const HoldemGame *g, const GameEvent *e)
{
    int s = e->a;
    int ok = s >= 0 && s < NSEAT;
    char b1[32], b2[64];
    P.st.events_seen++;
    switch (e->type) {
    case EV_HOLDEM_HAND_START: {
        /* The last hand's cards, if they are still out, go now. */
        reset_hand_fx();
        P.hand_no = (uint32_t)e->a;
        P.st.hands_seen++;
        int bs = e->b >= 0 && e->b < NSEAT ? e->b : 0;
        if (P.btn_seat < 0) {
            P.btn_x = k_pos[bs].dx;
            P.btn_y = k_pos[bs].dy;
            tw_start(&P.btn_tx, P.btn_x, P.btn_x, 0.01f, 0, EASE_OUT_CUBIC);
            tw_start(&P.btn_ty, P.btn_y, P.btn_y, 0.01f, 0, EASE_OUT_CUBIC);
        } else if (bs != P.btn_seat) {
            tw_start(&P.btn_tx, P.btn_x, k_pos[bs].dx, 0.6f * anim_k() + 0.01f, 0, EASE_INOUT_CUBIC);
            tw_start(&P.btn_ty, P.btn_y, k_pos[bs].dy, 0.6f * anim_k() + 0.01f, 0, EASE_INOUT_CUBIC);
        }
        P.btn_seat = bs;
        sfx(SFX_CARD_SHUFFLE, 0.45f, 1.0f, 0);
        break;
    }
    case EV_HOLDEM_LEVEL_UP: {
        HoldemLevel lv = holdem_level(g);
        char sb[16], bb[16];
        fmt_chips(lv.sb, sb, sizeof sb);
        fmt_chips(lv.bb, bb, sizeof bb);
        if (lv.ante) snprintf(b2, sizeof b2, "%s / %s  ANTE %d", sb, bb, (int)lv.ante);
        else snprintf(b2, sizeof b2, "%s / %s", sb, bb);
        banner("BLINDS UP", b2, 640, 168, 60, 2.8f, 1, UI_GOLD, 0);
        P.level_flash = 1;
        sfx(SFX_BLINDS_UP, 1.0f, 1.0f, 0);
        break;
    }
    case EV_HOLDEM_ANTE:
        if (ok) {
            Vector2 pc = plate_centre(s);
            fly(FL_POT, s, 0, e->v, pc.x, pc.y, POT_X, POT_Y, 0.03f * s, 0.45f);
        }
        break;
    case EV_HOLDEM_POST_SB:
    case EV_HOLDEM_POST_BB:
        if (ok) {
            Vector2 pc = plate_centre(s);
            fly(FL_BET, s, 0, e->v, pc.x, pc.y, k_pos[s].bx, k_pos[s].by, 0, 0.38f);
            P.spot[s] += e->v;
            snprintf(b2, sizeof b2, "%s %s", e->type == EV_HOLDEM_POST_SB ? "SB" : "BB", fmt_chips(e->v, b1, sizeof b1));
            set_act(s, b2, e->type == EV_HOLDEM_POST_SB ? UI_CYAN : UI_MAGENTA);
            if (e->b) P.seat[s].allin = 1;
        }
        break;
    case EV_HOLDEM_DEAL_HOLE:
        if (ok && e->b >= 0 && e->b < 2) {
            int face = s == 0 && e->v >= 0;
            float x, y, r, sc;
            hole_rest(s, e->b, CARD_NONE, 0, &x, &y, &r, &sc);
            cf_deal(&P.hole[s][e->b], face ? (Card)e->v : CARD_NONE, shoe_pos(), x, y, r, sc, 0, 0.30f, 50, face,
                    e->b == 1 ? 0.12f : 0.35f, 0.26f);
            P.seat[s].dealt = e->b + 1;
        }
        break;
    case EV_HOLDEM_TURN:
        if (ok) {
            P.to_act = s;
            P.turn_t = 0;
            P.seat[s].has_tell = 0;
            P.seat[s].think_t = 0;
            if (e->b) {
                sfx(SFX_YOUR_TURN, 0.8f, 1.0f, 0);
                if (P.rail_mode == 0) P.rail_mode = 1;
            }
        }
        break;
    case EV_HOLDEM_TELL:
        if (ok) {
            P.seat[s].tell = clampf((float)e->v / 1000.0f, 0, 1);
            P.seat[s].has_tell = 1;
        }
        break;
    case EV_HOLDEM_AMOUNT: {
        HoldemLegal L;
        holdem_legal(g, &L);
        float k = L.max_to > L.min_to ? (float)(e->v - L.min_to) / (float)(L.max_to - L.min_to) : 1.0f;
        P.amount_bump = 1;
        sfx(SFX_BET_ONE, 0.6f, 0.9f + 0.6f * clampf(k, 0, 1), 0);
        break;
    }
    case EV_HOLDEM_ACTION:
        if (ok) {
            SeatFx *f = &P.seat[s];
            f->has_tell = 0;
            if (P.to_act == s) P.to_act = -1;
            if (P.rail_mode == 1) P.rail_mode = 0;
            float pan = pan_of(s);
            Vector2 pc = plate_centre(s);
            int allin = g->seat[s].allin && e->b != ACT_FOLD && e->b != ACT_CHECK;
            switch (e->b) {
            case ACT_FOLD:
                set_act(s, "FOLD", (Color){ 150, 140, 150, 255 });
                f->folded = 1;
                for (int k = 0; k < 2; k++) cf_leave(&P.hole[s][k], POT_X + (k ? 10.0f : -10.0f), BOARD_Y - 10, 0.04f * k);
                sfx(SFX_FOLD, 0.8f, 1.0f, pan);
                break;
            case ACT_CHECK:
                set_act(s, "CHECK", (Color){ 120, 220, 255, 255 });
                sfx(SFX_CHECK, 0.9f, 1.0f, pan);
                break;
            default: {
                /* The chips added: the new total minus what was already out. */
                int64_t add = (int64_t)e->v - P.spot[s];
                if (add > 0) fly(FL_BET, s, 0, add, pc.x, pc.y, k_pos[s].bx, k_pos[s].by, 0, 0.38f);
                P.spot[s] = e->v;
                const char *w = e->b == ACT_CALL ? "CALL" : e->b == ACT_BET ? "BET" : e->b == ACT_RAISE ? "RAISE TO" : "ALL-IN";
                snprintf(b2, sizeof b2, "%s %s", w, fmt_chips(e->v, b1, sizeof b1));
                if (allin) snprintf(b2, sizeof b2, "ALL-IN %s", fmt_chips(e->v, b1, sizeof b1));
                set_act(s, b2, allin ? UI_MAGENTA : e->b == ACT_CALL ? (Color){ 120, 220, 255, 255 } : UI_GOLD);
                break;
            }
            }
            if (allin && !f->allin) {
                f->allin = 1;
                Vector2 bp = { k_pos[s].bx, k_pos[s].by };
                banner("ALL-IN", NULL, bp.x, bp.y - 34, 44, 1.6f, 0, UI_GOLD, 0);
                sfx(SFX_ALL_IN, 1.0f, 1.0f, pan);
                shake_add(&P.shake, 0.22f);
                pfx_burst(PK_SPARK, pc.x, pc.y, 26, 360, -PI_F / 2, PI_F, UI_MAGENTA);
                P.rail_mode = 2;
            }
            f->av.react = 0.35f;
        }
        break;
    case EV_HOLDEM_UNCALLED:
        if (ok) {
            Vector2 pc = plate_centre(s);
            P.spot[s] -= e->v;
            if (P.spot[s] < 0) P.spot[s] = 0;
            fly(FL_STACK, s, 0, e->v, k_pos[s].bx, k_pos[s].by, pc.x, pc.y, 0, 0.4f);
        }
        break;
    case EV_HOLDEM_BETS_TO_POT: {
        /* Every bet spot slides into the middle, one after another, from
           the left of the button round. */
        int n = 0;
        for (int t = 0; t < NSEAT; t++) {
            int st = ((P.btn_seat < 0 ? 0 : P.btn_seat) + 1 + t) % NSEAT;
            if (P.spot[st] <= 0) continue;
            fly(FL_POT, st, 0, P.spot[st], k_pos[st].bx, k_pos[st].by, POT_X, POT_Y, 0.07f * (float)n, 0.42f);
            P.spot[st] = 0;
            n++;
        }
        break;
    }
    case EV_HOLDEM_POT:
        if (e->a >= 0 && e->a < HOLDEM_MAX_POTS) {
            if (e->a == 0) P.npots = 0;
            P.pots[e->a].amount = e->v;
            P.pots[e->a].eligible = (uint8_t)e->b;
            if (e->a + 1 > P.npots) P.npots = e->a + 1;
            P.split_on = 0;
            P.split_t = 0;
        }
        break;
    case EV_HOLDEM_BOARD:
        if (e->a >= 0 && e->a < 5) {
            int i = e->a;
            float hold = 0.10f, fdur = 0.28f;
            if (P.runout && i >= 3) { hold = i == 4 ? 0.55f : 0.30f; fdur = i == 4 ? 0.6f : 0.4f; }
            cf_deal(&P.board[i], (Card)e->v, shoe_pos(), board_x(i), BOARD_Y, fx_randf(-0.02f, 0.02f), BOARD_SC,
                    0, 0.30f, 70, 1, hold, fdur);
            if (i + 1 > P.nboard) P.nboard = i + 1;
        }
        break;
    case EV_HOLDEM_STREET:
        for (int t = 0; t < NSEAT; t++)
            if (!P.seat[t].folded && !P.seat[t].allin) P.seat[t].act_t = -1;
        break;
    case EV_HOLDEM_RUNOUT:
        P.runout = 1;
        P.runout_t = 0;
        banner("ALL IN", "RUNNING IT OUT", 640, 438, 42, 2.4f, 0, UI_MAGENTA, 1);
        sfx(SFX_REVEAL, 0.9f, 1.0f, 0);
        P.rail_mode = 2;
        break;
    case EV_HOLDEM_SHOW:
        if (ok) {
            Card c0 = (Card)(e->v & 0xFF), c1 = (Card)((e->v >> 8) & 0xFF);
            SeatFx *f = &P.seat[s];
            f->shown = 1;
            for (int k = 0; k < 2; k++) {
                CardFx *c = &P.hole[s][k];
                Card cc = k ? c1 : c0;
                float x, y, r, sc;
                hole_rest(s, k, cc, 1, &x, &y, &r, &sc);
                if (!c->on) cf_place(c, cc, x, y, r, sc, 0);
                if (s != 0 || c->flip < 1) cf_flip(c, cc, 0.06f * k, 0.32f);
                cf_move(c, x, y, r, sc, 0.32f, 0);
            }
            sfx(SFX_CARD_FLIP, 0.9f, 1.0f, pan_of(s));
            if (e->b == HOLDEM_SHOW_SHOWDOWN) clock_hitpause(&P.clock, 2);
            compute_best(g, s);
            if (P.runout) compute_equity(g);
        }
        break;
    case EV_HOLDEM_HAND_RANK:
        if (ok) compute_best(g, s);
        break;
    case EV_HOLDEM_MUCK:
        if (ok) {
            P.seat[s].mucked = 1;
            if (!e->b) {
                set_act(s, "MUCK", (Color){ 150, 140, 150, 255 });
                for (int k = 0; k < 2; k++) cf_leave(&P.hole[s][k], POT_X, BOARD_Y - 10, 0.05f * k);
                sfx(SFX_FOLD, 0.6f, 1.0f, pan_of(s));
            }
        }
        break;
    case EV_HOLDEM_SHOW_PROMPT:
        break;
    case EV_HOLDEM_AWARD:
        if (ok && e->b >= 0 && e->b < HOLDEM_MAX_POTS) {
            Vector2 from = { POT_X, POT_Y };
            if (P.split_on && P.npots > 1) from = pot_slot(e->b, P.npots);
            P.awarded[e->b] += e->v;
            Vector2 pc = plate_centre(s);
            Flight *fa = fly(FL_STACK, s, e->b, e->v, from.x, from.y - 10, pc.x, pc.y, 0.05f, 0.62f);
            if (fa) fa->award = 1;
            SeatFx *f = &P.seat[s];
            f->winner = 1;
            if (!f->best_valid) compute_best(g, s);
        }
        break;
    case EV_HOLDEM_HAND_END:
        P.clear_timer = 1.1f;
        if (P.rail_mode == 2) P.rail_mode = 0;
        if (g->won[0] > 0 && !v->demo) {
            human_celebrate(v, g, g->won[0]);
        } else if (v->demo) {
            /* Attract: big pots get the coin burst and banner (silent). */
            int64_t total = 0;
            int best = -1;
            for (int t = 0; t < NSEAT; t++) {
                total += g->seat[t].stack;
                if (best < 0 || g->won[t] > g->won[best]) best = t;
            }
            if (best >= 0 && g->won[best] * 100 >= total * 25) {
                char nm[40];
                snprintf(nm, sizeof nm, "%s WINS", P.seat[best].name[0] ? P.seat[best].name : "SEAT");
                celeb_start(&P.cel, WIN_MEDIUM, (long long)g->won[best], nm, (Vector2){ 640, 470 });
                P.st.celebrations++;
            }
        }
        break;
    case EV_HOLDEM_ELIMINATED:
        if (ok) {
            SeatFx *f = &P.seat[s];
            f->out = 1;
            f->place = e->b;
            f->out_t = 0;
            f->av.hurt = 1;
            Vector2 c = { k_pos[s].ax, k_pos[s].ay };
            pfx_burst(PK_SPARK, c.x, c.y, 34, 420, -PI_F / 2, PI_F * 2, (Color){ 255, 80, 60, 255 });
            pfx_burst(PK_DROP, c.x, c.y - 20, 14, 220, -PI_F / 2, PI_F * 0.8f, (Color){ 0 });
            shake_add(&P.shake, s == 0 ? 0.5f : 0.25f);
            sfx(SFX_BUST, s == 0 && !v->demo ? 1.0f : 0.6f, 1.0f, pan_of(s));
        }
        break;
    case EV_HOLDEM_HUMAN_OUT:
        break;
    case EV_HOLDEM_GAME_OVER:
        P.last_game_over = 1;
        P.game_over_winner = e->a;
        P.over_t = 0;
        if (e->a >= 0 && e->a < NSEAT) {
            P.seat[e->a].av.react = 1;
            if (e->a == 0 && !v->demo) {
                if (P.cel.active) celeb_skip(&P.cel);
                celeb_start(&P.cel, g_effects.takeover ? WIN_JACKPOT : WIN_BIG, (long long)(v->won > 0 ? v->won : v->prize[1]),
                            "CHAMPION", (Vector2){ 640, 400 });
                celeb_set_label(&P.cel, "PRIZE CREDITS");
                P.st.celebrations++;
            } else {
                /* An AI took it: a banner and confetti at its seat, no stinger. */
                char nm[40];
                snprintf(nm, sizeof nm, "%s WINS", P.seat[e->a].name[0] ? P.seat[e->a].name : "SEAT");
                banner(nm, "SIT & GO CHAMPION", 640, 190, 56, 3.0f, 1, UI_GOLD, 0);
                pfx_confetti_burst(k_pos[e->a].ax, k_pos[e->a].ay, 0.6f);
                sfx(SFX_REVEAL, 0.8f, 1.0f, 0);
            }
        }
        break;
    default: break;
    }
}

/* ---- celebration sounds --------------------------------------------------------------- */

static void on_cue(void *user, CelebCue cue, float a)
{
    (void)user;
#if defined(BPL_HAVE_AUDIO)
    switch (cue) {
    case CUE_CHIME: sfx(SFX_WIN_SMALL, 1, 1, 0); break;
    case CUE_COINS: sfx(SFX_WIN_MEDIUM, 1, 1, 0); break;
    case CUE_BIG: sfx(SFX_WIN_BIG, 1, 1, 0); break;
    case CUE_JACKPOT: sfx(SFX_WIN_JACKPOT, 1, 1, 0); break;
    case CUE_REVEAL: sfx(SFX_REVEAL, 1, 1, 0); break;
    case CUE_TICK: sfx(SFX_CREDIT_TICK, 0.45f, a, 0); break;
    case CUE_COUNT_END: sfx(SFX_CREDIT_END, 0.9f, 1, 0); break;
    case CUE_SKIP: if (P.snd) audio_stop(SFX_WIN_JACKPOT); break;
    default: break;
    }
#else
    (void)cue; (void)a;
#endif
}

static void init_once(void)
{
    if (P.inited) return;
    memset(&P, 0, sizeof P);
    clock_init(&P.clock);
    shake_init(&P.shake);
    bulbs_init(&P.bulbs, (Rectangle){ 10, 10, PLAY_W - 20, PLAY_H - 20 }, 34);
    celeb_init(&P.cel, &P.shake, &P.clock, &P.bulbs, on_cue, NULL);
    P.btn_seat = -1;
    P.to_act = -1;
    P.clear_timer = -1;
    for (int s = 0; s < NSEAT; s++) {
        P.seat[s].act_t = -1;
        P.seat[s].av.next_blink = 1.0f + 0.4f * (float)s;
        P.seat[s].av.blink_t = -1;
        P.seat[s].av.next_gaze = 0.5f + 0.3f * (float)s;
    }
    P.inited = 1;
}

/* ---- the action bar's buttons ------------------------------------------------------------ */

enum { B_FOLD, B_CALL, B_HALF, B_3Q, B_POT, B_ALLIN, B_DEAL, B_N };

typedef struct {
    UiButtonState st[B_N];
    char label[B_N][32];
    const char *caption[B_N];
    Color col[B_N];
    int mode;               /* 0 waiting, 1 your turn, 2 show prompt, 3 busted */
    HoldemLegal L;
} BarState;

static void bar_state(const HoldemGame *g, BarState *b)
{
    static const char *const caps[B_N] = { "HOLD 1", "HOLD 2", "HOLD 3", "HOLD 4", "HOLD 5", "BET MAX", "DEAL" };
    char n1[24];
    memset(b, 0, sizeof *b);
    for (int i = 0; i < B_N; i++) {
        b->caption[i] = caps[i];
        b->st[i] = BTN_STATE_OFF;
    }
    b->col[B_FOLD] = (Color){ 255, 70, 90, 255 };
    b->col[B_CALL] = UI_CYAN;
    b->col[B_HALF] = b->col[B_3Q] = b->col[B_POT] = UI_AMBER;
    b->col[B_ALLIN] = UI_MAGENTA;
    b->col[B_DEAL] = UI_GOLD;
    snprintf(b->label[B_FOLD], 32, "FOLD");
    snprintf(b->label[B_CALL], 32, "CHECK");
    snprintf(b->label[B_HALF], 32, "1/2 POT");
    snprintf(b->label[B_3Q], 32, "3/4 POT");
    snprintf(b->label[B_POT], 32, "POT");
    snprintf(b->label[B_ALLIN], 32, "ALL-IN");
    snprintf(b->label[B_DEAL], 32, "BET");
    if (!g) return;
    if (g->prompt) {
        b->mode = 2;
        snprintf(b->label[B_FOLD], 32, "MUCK");
        snprintf(b->label[B_CALL], 32, "SHOW");
        snprintf(b->label[B_DEAL], 32, "SHOW HAND");
        b->st[B_FOLD] = BTN_STATE_ON;
        b->st[B_CALL] = BTN_STATE_ON;
        b->st[B_DEAL] = BTN_STATE_LIT;
        return;
    }
    if (g->phase == HP_BUSTED) {
        b->mode = 3;
        snprintf(b->label[B_DEAL], 32, "WATCH ON");
        b->st[B_DEAL] = BTN_STATE_LIT;
        return;
    }
    if (!g->seat[0].in_game && g->seat[0].place > 0 && !g->cfg.seat0_ai) {
        b->mode = 4;            /* out, watching the rest */
        return;
    }
    if (!holdem_is_human_turn(g)) return;
    b->mode = 1;
    holdem_legal(g, &b->L);
    const HoldemLegal *L = &b->L;
    if (L->can_fold) b->st[B_FOLD] = BTN_STATE_ON;
    b->st[B_CALL] = BTN_STATE_ON;
    if (L->can_call) {
        if (L->to_call >= g->seat[0].stack) snprintf(b->label[B_CALL], 32, "CALL ALL-IN");
        else snprintf(b->label[B_CALL], 32, "CALL %s", fmt_chips(L->to_call, n1, sizeof n1));
    }
    if (L->can_bet || L->can_raise) {
        int64_t sel = g->sel_amount;
        static const int pct[3] = { 50, 75, 100 };
        for (int k = 0; k < 3; k++) {
            int64_t q = holdem_quick_bet(g, pct[k]);
            b->st[B_HALF + k] = q == sel ? BTN_STATE_LIT : BTN_STATE_ON;
        }
        b->st[B_ALLIN] = sel >= L->max_to ? BTN_STATE_LIT : BTN_STATE_ON;
        b->st[B_DEAL] = BTN_STATE_LIT;
        if (sel >= L->max_to) snprintf(b->label[B_DEAL], 32, "ALL-IN %s", fmt_chips(sel, n1, sizeof n1));
        else snprintf(b->label[B_DEAL], 32, "%s %s", L->can_bet ? "BET" : "RAISE TO", fmt_chips(sel, n1, sizeof n1));
    }
}

static void bar_rects(Rectangle r[B_N])
{
    static const float w[B_N] = { 150, 200, 124, 124, 124, 150, 300 };
    float total = 0;
    for (int i = 0; i < B_N; i++) total += w[i];
    total += 10.0f * (B_N - 1);
    float x = (PLAY_W - total) * 0.5f;
    for (int i = 0; i < B_N; i++) {
        r[i] = (Rectangle){ x, BAR_Y + 4, w[i], 58 };
        x += w[i] + 10;
    }
}

/* Same-frame feedback: the pressed look and a click, the frame the button
 * is read. The game's own reaction (the action, its sound) arrives as
 * events in the same frame. */
static void handle_presses(const HoldemViewInfo *v, const HoldemGame *g, float dt)
{
    BarState b;
    bar_state(g, &b);
    for (int i = 0; i < B_N; i++) ui_button_update(&P.btn[i], b.st[i], dt);
    if (b.mode == 1 && (b.L.can_bet || b.L.can_raise)) {
        /* The slider follows the selected amount with a little overshoot. */
        float target = b.L.max_to > b.L.min_to ? (float)(g->sel_amount - b.L.min_to) / (float)(b.L.max_to - b.L.min_to) : 1;
        spring_update(&P.slider, clampf(target, 0, 1), 22, 0.55f, dt);
    }
    if (v->demo || v->phase != HV_PLAYING || !g || v->confirm_leave) return;
    static const uint32_t map[B_N] = { BTN_HOLD1, BTN_HOLD2, BTN_HOLD3, BTN_HOLD4, BTN_HOLD5, BTN_BET_MAX,
                                       BTN_DEAL | BTN_OK };
    uint32_t pr = v->pressed;
    for (int i = 0; i < B_N; i++) {
        if (!(pr & map[i])) continue;
        ui_button_press(&P.btn[i]);
        sfx(b.st[i] == BTN_STATE_OFF ? SFX_ERROR : SFX_BUTTON, b.st[i] == BTN_STATE_OFF ? 0.35f : 0.7f, 1.0f, 0);
    }
    if (pr & (BTN_BET_ONE | BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT)) P.amount_bump = 1;
    if (pr & BTN_CASH_OUT) sfx(SFX_BUTTON, 0.6f, 0.9f, 0);
}

/* ---- avatars: idle life and tells -------------------------------------------------------- */

static void update_avatar(const HoldemGame *g, int s, float dt)
{
    SeatFx *f = &P.seat[s];
    AvFx *a = &f->av;
    int thinking = g && g->phase == HP_TURN && g->to_act == s && !(s == 0 && !g->cfg.seat0_ai);
    if (thinking) f->think_t += dt;
    /* Tension: only the TELL value the table announced; before it arrives
       (the first 0.4 s of a turn) a neutral "pondering" level. */
    float target = thinking ? (f->has_tell ? f->tell : 0.3f) : 0.08f;
    if (f->out) target = 0;
    a->tension += (target - a->tension) * clampf(dt * (thinking ? 5.0f : 2.0f), 0, 1);
    float t = a->tension;
    a->ant += dt * (1.1f + 7.5f * t * t) * 2 * PI_F;
    a->bob += dt * (0.3f + 1.3f * t) * 2 * PI_F;
    a->flap += dt * (t > 0.45f ? 26.0f : 3.0f);
    a->chip += dt * (0.9f + 6.0f * t) * 2 * PI_F;
    a->react = fmaxf(0, a->react - dt * 0.9f);
    a->hurt = fmaxf(0, a->hurt - dt * 0.8f);
    /* Blinks: a little quicker when tense. */
    a->next_blink -= dt;
    if (a->blink_t >= 0 && a->blink_t < 0.16f) {
        a->blink_t += dt;
        a->blink = sinf(clampf(a->blink_t / 0.16f, 0, 1) * PI_F);
    } else {
        a->blink = 0;
        a->blink_t = -1;
    }
    if (a->next_blink <= 0) {
        a->blink_t = 0;
        a->next_blink = t > 0.6f ? fx_randf(0.6f, 1.8f) : fx_randf(2.2f, 5.0f);
    }
    /* Gaze: glances at the cards, darting more with tension; otherwise it
       follows whoever is to act. */
    a->next_gaze -= dt;
    if (a->next_gaze <= 0) {
        if (thinking) {
            float mag = 1.2f + 2.6f * t;
            if (fx_randf(0, 1) < 0.45f - 0.25f * t) { a->tgx = 0; a->tgy = 2.2f; }
            else { a->tgx = fx_randf(-mag, mag); a->tgy = fx_randf(-mag * 0.6f, mag * 0.8f); }
            a->next_gaze = lerpf(1.6f, 0.28f, t) * fx_randf(0.7f, 1.3f);
        } else if (g && g->phase == HP_TURN && g->to_act >= 0 && g->to_act != s) {
            float dx = k_pos[g->to_act].ax - k_pos[s].ax, dy = k_pos[g->to_act].ay - k_pos[s].ay;
            float l = sqrtf(dx * dx + dy * dy);
            if (l > 1) { a->tgx = dx / l * 2.4f; a->tgy = dy / l * 2.0f; }
            a->next_gaze = fx_randf(0.8f, 2.0f);
        } else {
            a->tgx = fx_randf(-1.5f, 1.5f);
            a->tgy = fx_randf(-0.5f, 1.8f);
            a->next_gaze = fx_randf(1.5f, 3.5f);
        }
    }
    float k = clampf(dt * 16, 0, 1);
    a->gx += (a->tgx - a->gx) * k;
    a->gy += (a->tgy - a->gy) * k;
}

static Color scale_col(Color c, float k)
{
    return (Color){ (unsigned char)(c.r * k), (unsigned char)(c.g * k), (unsigned char)(c.b * k), c.a };
}

static void draw_avatar(int who, const AvFx *a, float cx, float cy, float size, float bright, float alpha)
{
    const AvatarRig *rg = hart_rig(who);
    const float k = size / (float)HAV_SIZE;
    float t = a ? a->tension : 0;
    float bob = a ? sinf(a->bob) * (0.6f + 2.2f * t) : 0;
    float kk = k * (1.0f + (a ? 0.08f * sinf(a->react * PI_F) : 0));
    unsigned char lv = (unsigned char)(255 * clampf(bright, 0, 1));
    Color tint = { lv, lv, lv, 255 };
    if (a && a->hurt > 0)
        tint = (Color){ lv, (unsigned char)(lv * (1 - 0.6f * a->hurt)), (unsigned char)(lv * (1 - 0.6f * a->hurt)), 255 };
    PCol col = gfx_cola(tint, alpha);
    const Spr *back = hart_avatar(who, AVL_BACK), *front = hart_avatar(who, AVL_FRONT), *over = hart_avatar(who, AVL_OVER);
    float x0 = cx - 64 * kk, y0 = cy - 64 * kk + bob;
    if (back) gfx_spr(back, x0, y0 - bob, 128 * kk, 128 * kk, col);
#define AX(px) (x0 + (px) * kk)
#define AY(py) (y0 + (py) * kk)
    if (rg->bee) {
        /* Wings behind the shoulders: a lazy flutter, a buzz when tense. */
        const Spr *w = hart_part(HPART_WING);
        float amp = 0.04f + 0.22f * fmaxf(0, t - 0.4f);
        float fl = a ? sinf(a->flap) * amp : 0;
        Spr m = *w;
        float tu = m.u0;
        m.u0 = m.u1;
        m.u1 = tu;
        PCol wc = gfx_cola(tint, alpha * 0.75f);
        gfx_spr_rot(w, AX(16), AY(76), 32 * kk, 45 * kk, -0.95f - fl, wc);
        gfx_spr_rot(&m, AX(112), AY(76), 32 * kk, 45 * kk, 0.95f + fl, wc);
        /* Antennae, rotated about their bases: the fidget. */
        const Spr *an = hart_part(HPART_ANTENNA);
        float wig = (0.05f + 0.40f * t) * (a ? sinf(a->ant) : 0);
        Color at = scale_col(rg->ant_tint, bright);
        for (int i = 0; i < 2; i++) {
            float r = rg->ant_rot[i] + (i ? -wig : wig * 0.8f);
            float bx = AX(rg->ant_x[i]), by = AY(rg->ant_y);
            float d = 21 * kk;
            gfx_spr_rot(an, bx + d * sinf(r), by - d * cosf(r), 18 * kk, 46 * kk, r, gfx_cola(at, alpha));
        }
    }
    if (front) gfx_spr(front, x0, y0, 128 * kk, 128 * kk, col);
    if (rg->bee) {
        const Spr *eye = hart_part(HPART_EYE), *pup = hart_part(HPART_PUPIL);
        float bl = a ? a->blink : 0;
        float ew = rg->eye_rx / 8.3f * 20 * kk, eh = rg->eye_ry / 10.3f * 24 * kk * (1 - 0.92f * bl);
        float gx = a ? clampf(a->gx, -3.2f, 3.2f) : 0, gy = a ? clampf(a->gy, -2.5f, 2.6f) : 0;
        for (int i = 0; i < 2; i++) {
            float ex = AX(rg->eye_x[i]), ey = AY(rg->eye_y);
            gfx_spr(eye, ex - ew * 0.5f, ey - eh * 0.5f, ew, eh, col);
            float pw = rg->pupil_r / 4.8f * 12 * kk, ph = pw * (1 - 0.92f * bl);
            gfx_spr(pup, ex + gx * kk - pw * 0.5f, ey + gy * kk * (1 - bl) - ph * 0.5f, pw, ph, col);
        }
        if (over) gfx_spr(over, AX(rg->over_x), AY(rg->over_y), 80 * kk, 36 * kk, col);
        /* Brows: knit and lowered with tension. */
        const Spr *br = hart_part(HPART_BROW);
        float ang = 0.05f + 0.34f * t, drop = 1.6f * t;
        Color bc = scale_col(rg->brow_col, bright);
        gfx_spr_rot(br, AX(rg->eye_x[0]), AY(rg->brow_y + drop), 22 * kk, 7 * kk, ang, gfx_cola(bc, alpha));
        gfx_spr_rot(br, AX(rg->eye_x[1]), AY(rg->brow_y + drop), 22 * kk, 7 * kk, -ang, gfx_cola(bc, alpha));
        /* A bead of sweat on a very tense turn. */
        if (t > 0.72f && a) {
            float st = fmodf(a->bob * 0.15f, 1.0f);
            float sa = clampf((t - 0.72f) / 0.15f, 0, 1) * (1 - st);
            gfx_spr_rot(sprite(SPR_DROP), AX(97), AY(40 + 14 * st), 10 * kk, 14 * kk, 0,
                        gfx_cola((Color){ 170, 230, 255, 255 }, 0.8f * sa * alpha));
        }
    }
#undef AX
#undef AY
}

/* The chip-shuffle tell: two chips riffling beside a thinking player. */
static void draw_chip_riffle(int s, float t, float phase)
{
    const SeatPos *p = &k_pos[s];
    float x = p->ax + (p->flip ? -34.0f : 34.0f), y = p->ay + 44;
    float h = 1.5f + 7.0f * t;
    for (int i = 0; i < 2; i++) {
        float ph = phase + (float)i * PI_F;
        float lift = fmaxf(0, sinf(ph)) * h;
        const Spr *c = hart_chip(i ? HCHIP_100 : HCHIP_25);
        gfx_spr(c, x - 13 + (float)i * 5.0f, y - 21 - lift - (float)i * 3.0f, 26, 21, gfx_col(WHITE));
    }
}

/* ---- update ---------------------------------------------------------------------------------- */

static void clear_table(void)
{
    Vector2 sh = shoe_pos();
    for (int s = 0; s < NSEAT; s++)
        for (int k = 0; k < 2; k++) cf_leave(&P.hole[s][k], sh.x, sh.y, 0.03f * (float)(s + k));
    for (int i = 0; i < 5; i++) cf_leave(&P.board[i], sh.x, sh.y, 0.04f * (float)i);
    P.cleared = 1;
}

void holdem_present_update(const HoldemViewInfo *v, const GameEvent *ev, int nev, float dt)
{
    init_once();
    const HoldemGame *g = v->phase == HV_LOBBY ? NULL : v->game;
    P.snd = !v->demo;
    P.phase = v->phase;
    if (g != P.game || v->demo != P.demo) reset_all(v);
    P.game = g;
    P.demo = v->demo;
    for (int s = 0; s < NSEAT; s++) {
        SeatFx *f = &P.seat[s];
        split_name(v->seat_name[s], f->name, sizeof f->name, f->badge, sizeof f->badge);
        f->avatar = avatar_for(f->name, s);
    }
    float sdt = clock_step(&P.clock, dt);
    P.time += sdt;
    shake_update(&P.shake, dt);

    if (g) {
        for (int i = 0; i < nev; i++) {
            const GameEvent *e = &ev[i];
            if (e->type < EV_HOLDEM_HAND_START || e->type >= EV_HOLDEM_LAST_) continue;
            /* A new sit-and-go in the same game struct (attract restarts). */
            if (e->type == EV_HOLDEM_HAND_START && e->a == 1 && P.hand_no > 1) reset_all(v);
            on_event(v, g, e);
        }
    }
    handle_presses(v, g, dt);
    if (P.cel.active && (v->pressed & ~(uint32_t)(BTN_DEBUG | BTN_COIN)) && !v->demo) celeb_skip(&P.cel);
    /* Measuring (development and the cabinet perf pass): BPL_HOLDEM_TAKEOVER=1
       plays the big-pot takeover over the live table again and again - the
       heaviest Hold'em scene - without touching the game. */
    static int takeover_env = -1;
    if (takeover_env < 0) takeover_env = getenv("BPL_HOLDEM_TAKEOVER") != NULL;
    if (takeover_env && g && !P.cel.active && P.time > 3.0f) {
        celeb_start(&P.cel, WIN_JACKPOT, 4500, "MONSTER POT", (Vector2){ 640, 520 });
        celeb_set_label(&P.cel, "POT");
    }
    celeb_update(&P.cel, sdt, dt);
    pfx_update(sdt);
    int big = P.cel.active && (P.cel.tier == WIN_BIG || P.cel.tier == WIN_JACKPOT);
    P.bulbs_k += ((big ? 1.0f : 0.0f) - P.bulbs_k) * clampf(dt * 3, 0, 1);
    if (P.bulbs_k > 0.01f) bulbs_update(&P.bulbs, dt);
    if (P.cel.active && P.cel.tier >= WIN_BIG) P.rail_mode = 3;
    else if (P.rail_mode == 3) P.rail_mode = 0;

    /* Cards. */
    int landed = 0, flipped = 0;
    float land_pan = 0;
    for (int s = 0; s < NSEAT; s++)
        for (int k = 0; k < 2; k++) {
            CardFx *c = &P.hole[s][k];
            cf_update(c, sdt);
            if (c->ev & 1) { landed++; land_pan = pan_of(s); }
            if ((c->ev & 2) && s == 0) flipped++;
        }
    for (int i = 0; i < 5; i++) {
        CardFx *c = &P.board[i];
        cf_update(c, sdt);
        if (c->ev & 1) { landed++; land_pan = 0; }
        if (c->ev & 2) flipped++;
        if ((c->ev & 4) && P.runout && i >= 3) {
            /* The key cards of a run-out land with a hit. */
            clock_hitpause(&P.clock, i == 4 ? 3 : 2);
            shake_add(&P.shake, i == 4 ? 0.3f : 0.15f);
            if (i == 4) sfx(SFX_REVEAL, 0.8f, 1.0f, 0);
        }
        if ((c->ev & 4) && P.runout && g) compute_equity(g);
    }
    if (landed) sfx(SFX_CARD_DEAL, 0.7f, 1.0f, land_pan);
    if (flipped) sfx(SFX_CARD_FLIP, 0.7f, 1.0f, 0);
    int ncards = 0;
    for (int s = 0; s < NSEAT; s++) ncards += P.hole[s][0].on + P.hole[s][1].on;
    for (int i = 0; i < 5; i++) ncards += P.board[i].on;
    P.st.cards_live = ncards;

    flights_update(sdt);
    if (P.npots > 1 && !P.split_on && P.infl_pot <= 0) { P.split_on = 1; P.split_t = 0; }
    if (P.split_on) P.split_t += sdt;
    if (P.clear_timer > 0) {
        P.clear_timer -= sdt;
        if (P.clear_timer <= 0 && !P.last_game_over) clear_table();
    }
    tw_update(&P.btn_tx, sdt);
    tw_update(&P.btn_ty, sdt);
    if (P.btn_seat >= 0) { P.btn_x = tw_value(&P.btn_tx); P.btn_y = tw_value(&P.btn_ty); }
    for (int i = 0; i < NBANNER; i++) {
        Banner *b = &P.banner[i];
        if (b->on && (b->t += dt) >= b->dur) b->on = 0;
    }
    for (int i = 0; i < NFLOAT; i++) {
        Floater *t = &P.fl_text[i];
        if (t->on && (t->t += dt) >= t->dur) t->on = 0;
    }
    for (int s = 0; s < NSEAT; s++) {
        SeatFx *f = &P.seat[s];
        if (f->act_t >= 0) f->act_t += dt;
        f->hand_t += dt;
        f->out_t += dt;
        update_avatar(g, s, sdt);
    }
    P.turn_t += dt;
    P.runout_t += dt;
    P.amount_bump = fmaxf(0, P.amount_bump - dt * 4);
    P.level_flash = fmaxf(0, P.level_flash - dt * 0.6f);
    P.rail_mode_t += dt;
    if (P.last_game_over) P.over_t += dt;
    /* The result panel waits for the winner's moment: the takeover, or the
       banner for an AI champion. */
    if (v->phase == HV_RESULT && (!P.cel.active || P.cel.skipping) && (P.over_t > 2.6f || !P.last_game_over)) P.result_t += dt;
    if (v->phase != HV_RESULT) P.result_t = 0;
    if (v->phase == HV_LOBBY) P.lobby_t += dt;

    if (g) {
        sync_to_game(g);
        /* Bet spots: with nothing in the air they must match the table. */
        int any = 0;
        for (int i = 0; i < NFLIGHT; i++) any |= P.fl[i].on;
        if (!any) {
            for (int s = 0; s < NSEAT; s++)
                if (P.spot[s] != g->seat[s].bet) { P.spot[s] = g->seat[s].bet; P.st.desyncs += !P.fresh; }
            memset(P.infl_bet, 0, sizeof P.infl_bet);
            memset(P.infl_stack, 0, sizeof P.infl_stack);
            P.infl_pot = 0;
        }
    }
    P.fresh = 0;
}

const HoldemPresentStats *holdem_present_stats(void) { return &P.st; }

/* ---- drawing: small pieces ------------------------------------------------------------------ */

static void pill(float cx, float cy, const char *s, FontId f, float size, Color text, Color edge, float alpha, float scale)
{
    if (alpha <= 0.01f || scale <= 0.01f) return;
    float w = (text_width(f, s, size, 0) + 22) * scale, h = (size + 10) * scale;
    gfx_nine(sprite_nine(NINE_RRECT), cx - w * 0.5f, cy - h * 0.5f, w, h, 14 * scale, gfx_cola((Color){ 12, 8, 14, 255 }, 0.82f * alpha));
    if (edge.a) gfx_nine(sprite_nine(NINE_RRECT_LINE), cx - w * 0.5f, cy - h * 0.5f, w, h, 14 * scale, gfx_cola(edge, alpha));
    TextStyle t = style(text, ALIGN_CENTER);
    t.opacity = alpha;
    text_draw_ex(f, s, cx, cy - size * scale * 0.56f, size * scale, &t);
}

static void draw_backdrop(double time, float dim)
{
    unsigned tex = hart_table_tex();
    if (!tex) { ui_background(time, dim); return; }
    /* With bloom on the table rides on the bloom composite (no pass of its
       own); otherwise it covers the play space with blending off. */
    if (!post_backdrop(tex, clampf(dim, 0, 1))) {
        unsigned char v = (unsigned char)(255 * clampf(dim, 0, 1));
        Spr s = { 0, 0, 1, 1, PLAY_W, PLAY_H, tex };
        gfx_flush();
        rlDisableColorBlend();
        gfx_spr(&s, 0, 0, PLAY_W, PLAY_H, (PCol){ v, v, v, 255 });
        gfx_flush();
        rlEnableColorBlend();
    }
}

/* A point on the neon inlay, s in [0, rail_len). */
#define RAIL_R (HT_HY - HT_NEON)
#define RAIL_HALF (HT_HX - HT_HY)
static float rail_len(void) { return 4 * RAIL_HALF + 2 * PI_F * RAIL_R; }

static Vector2 rail_point(float s)
{
    float L = rail_len();
    s = fmodf(s, L);
    if (s < 0) s += L;
    float lx = HT_CX - RAIL_HALF, rx = HT_CX + RAIL_HALF;
    if (s < 2 * RAIL_HALF) return (Vector2){ lx + s, HT_CY - RAIL_R };
    s -= 2 * RAIL_HALF;
    if (s < PI_F * RAIL_R) {
        float a = -PI_F / 2 + s / RAIL_R;
        return (Vector2){ rx + RAIL_R * cosf(a), HT_CY + RAIL_R * sinf(a) };
    }
    s -= PI_F * RAIL_R;
    if (s < 2 * RAIL_HALF) return (Vector2){ rx - s, HT_CY + RAIL_R };
    s -= 2 * RAIL_HALF;
    float a = PI_F / 2 + s / RAIL_R;
    return (Vector2){ lx + RAIL_R * cosf(a), HT_CY + RAIL_R * sinf(a) };
}

static Color hue(float h)
{
    h = fmodf(h, 1.0f) * 6;
    float x = 1 - fabsf(fmodf(h, 2) - 1);
    float r = 0, g = 0, b = 0;
    if (h < 1) { r = 1; g = x; } else if (h < 2) { r = x; g = 1; } else if (h < 3) { g = 1; b = x; }
    else if (h < 4) { g = x; b = 1; } else if (h < 5) { r = x; b = 1; } else { r = 1; b = x; }
    return (Color){ (unsigned char)(255 * r), (unsigned char)(255 * g), (unsigned char)(255 * b), 255 };
}

/* The table's neon inlay, lit: a slow shimmer; honey pulses on your turn;
 * a magenta chase during an all-in; a rainbow run in a big celebration. */
static void draw_rail_neon(double time)
{
    const int N = 110;
    float L = rail_len(), t = (float)time;
    const Spr *gl = sprite(SPR_GLOW);
    for (int i = 0; i < N; i++) {
        float s0 = L * (float)i / N, s1 = L * (float)(i + 1) / N;
        Vector2 a = rail_point(s0), b = rail_point(s1);
        float u = (float)i / N;
        Color c = UI_MAGENTA;
        float k;
        switch (P.rail_mode) {
        case 1: c = UI_HONEY; k = 0.22f + 0.16f * (0.5f + 0.5f * sinf(t * 4.0f)); break;
        case 2: k = 0.14f + 0.5f * powf(0.5f + 0.5f * sinf(u * 2 * PI_F * 6 - t * 9.0f), 6); break;
        case 3: c = hue(u * 2 - t * 0.5f); k = 0.3f + 0.35f * powf(0.5f + 0.5f * sinf(u * 2 * PI_F * 8 - t * 12.0f), 4); break;
        default: k = 0.16f + 0.10f * (0.5f + 0.5f * sinf(u * 2 * PI_F * 2 - t * 0.9f)); break;
        }
        gfx_streak(gl, a, b, 13, gfx_add(c, k));
    }
}

static void draw_pots(const HoldemGame *g)
{
    char b[32], s[48];
    int64_t middle = g->pot - P.infl_pot;
    if (middle < 0) middle = 0;
    int64_t total = holdem_pot_total(g);
    if (!P.split_on || P.npots <= 1) {
        if (middle > 0) draw_pile(middle, POT_X, POT_Y + 4, 0.9f, 1, 6);
        if (total > 0) {
            snprintf(s, sizeof s, "POT %s", fmt_chips(total, b, sizeof b));
            gfx_spr_rot(sprite(SPR_GLOW), POT_X, POT_Y + 26, 220, 60, 0, gfx_add(UI_AMBER, 0.22f));
            pill(POT_X, POT_Y + 26, s, FONT_DISP_S, 24, (Color){ 255, 230, 160, 255 }, UI_GOLD, 1, 1);
        }
        return;
    }
    float e = ease(EASE_OUT_BACK, clampf(P.split_t / 0.55f, 0, 1));
    int64_t sum = 0;
    for (int i = 0; i < P.npots; i++) sum += P.pots[i].amount - P.awarded[i];
    float k = sum > 0 && P.infl_pot > 0 ? (float)(sum - P.infl_pot) / (float)sum : 1.0f;
    for (int i = 0; i < P.npots; i++) {
        int64_t amt = (int64_t)((float)(P.pots[i].amount - P.awarded[i]) * clampf(k, 0, 1));
        Vector2 q = pot_slot(i, P.npots);
        float x = lerpf(POT_X, q.x, e), y = lerpf(POT_Y, q.y, e);
        if (amt > 0) draw_pile(amt, x, y + 4, 0.72f, 1, 4);
        if (P.pots[i].amount - P.awarded[i] <= 0) continue;
        if (i == 0) snprintf(s, sizeof s, "MAIN %s", fmt_chips(P.pots[i].amount - P.awarded[i], b, sizeof b));
        else snprintf(s, sizeof s, "SIDE %d  %s", i, fmt_chips(P.pots[i].amount - P.awarded[i], b, sizeof b));
        /* The label carries who can win it: little faces after the amount. */
        int n = 0;
        for (int st = 0; st < NSEAT; st++) n += (P.pots[i].eligible >> st) & 1;
        Color tc = i == 0 ? (Color){ 255, 226, 150, 255 } : (Color){ 150, 240, 255, 255 };
        Color ec = i == 0 ? (Color){ 180, 130, 50, 255 } : (Color){ 40, 160, 190, 255 };
        float tw = text_width(FONT_UI_M, s, 18, 0), fw = (float)n * 19.0f;
        float w = tw + fw + 28, h = 26, lx = x - w * 0.5f, ly = y + 24 - h * 0.5f;
        gfx_nine(sprite_nine(NINE_RRECT), lx, ly, w, h, 12, gfx_cola((Color){ 12, 8, 14, 255 }, 0.86f));
        gfx_nine(sprite_nine(NINE_RRECT_LINE), lx, ly, w, h, 12, gfx_col(ec));
        label(FONT_UI_M, s, lx + 11, y + 24, 18, tc, ALIGN_LEFT, 0);
        float fx = lx + 17 + tw;
        for (int st = 0; st < NSEAT; st++) {
            if (!((P.pots[i].eligible >> st) & 1)) continue;
            const Spr *face = hart_avatar(P.seat[st].avatar, AVL_FRONT);
            const Spr *bk = hart_avatar(P.seat[st].avatar, AVL_BACK);
            if (bk) gfx_spr(bk, fx, y + 24 - 9, 18, 18, gfx_col(WHITE));
            if (face) gfx_spr(face, fx, y + 24 - 9, 18, 18, gfx_col(WHITE));
            fx += 19;
        }
    }
}

static void draw_bets(const HoldemGame *g)
{
    char b[32];
    (void)g;
    for (int s = 0; s < NSEAT; s++) {
        int64_t amt = P.spot[s] - P.infl_bet[s];
        if (amt <= 0) continue;
        const SeatPos *p = &k_pos[s];
        draw_pile(amt, p->bx, p->by + 6, 0.7f, 1, 3);
        pill(p->bx, p->by + 22, fmt_chips(amt, b, sizeof b), FONT_UI_M, 18, (Color){ 255, 236, 190, 255 }, (Color){ 0 }, 0.95f, 1);
    }
}

static void draw_dealer_button(void)
{
    if (P.btn_seat < 0) return;
    gfx_spr_rot(sprite(SPR_GLOW), P.btn_x, P.btn_y + 6, 50, 26, 0, gfx_cola(BLACK, 0.5f));
    gfx_spr(hart_part(HPART_PUCK), P.btn_x - 20, P.btn_y - 17, 40, 34, gfx_col(WHITE));
    label(FONT_DISP_S, "D", P.btn_x, P.btn_y - 4, 17, (Color){ 40, 30, 20, 255 }, ALIGN_CENTER, 0);
}

/* The river is on the table and turned: hand names and highlights may show
 * (the game knows the river a moment before the card lands and flips). */
static int board_shown(void)
{
    for (int i = 0; i < 5; i++)
        if (!P.board[i].on || P.board[i].flip < 1 || P.board[i].moving) return 0;
    return 1;
}

static int showdown_highlight(void)
{
    if (!board_shown()) return 0;
    for (int s = 0; s < NSEAT; s++) if (P.seat[s].winner && P.seat[s].best_valid) return 1;
    return 0;
}

static int winners_use(Card c)
{
    for (int s = 0; s < NSEAT; s++) if (P.seat[s].winner && in_best(&P.seat[s], c)) return 1;
    return 0;
}

static void draw_board(double time)
{
    int hl = showdown_highlight();
    float pulse = 0.55f + 0.45f * sinf((float)time * 6);
    for (int i = 0; i < 5; i++) {
        if (P.board[i].on) continue;
        float w = CARD_M_W * BOARD_SC, h = CARD_M_H * BOARD_SC;
        gfx_nine(sprite_nine(NINE_RRECT_LINE), board_x(i) - w * 0.5f, BOARD_Y - h * 0.5f, w, h, 10, gfx_add(UI_HONEY, 0.14f));
    }
    for (int i = 0; i < 5; i++) {
        CardFx c = P.board[i];
        if (!c.on) continue;
        if (hl && c.flip >= 1) {
            if (winners_use(c.c)) c.highlight = pulse;
            else c.dim = 0.6f;
        }
        cf_draw(&c, CARD_M, 0, 0);
    }
}

static void draw_holes(const HoldemViewInfo *v, const HoldemGame *g, double time)
{
    int hl = showdown_highlight();
    float pulse = 0.55f + 0.45f * sinf((float)time * 6);
    for (int pass = 0; pass < 2; pass++)
        for (int s = 0; s < NSEAT; s++) {
            if ((s == 0) != (pass == 1)) continue;
            const SeatFx *f = &P.seat[s];
            for (int k = 0; k < 2; k++) {
                CardFx c = P.hole[s][k];
                if (!c.on) continue;
                if (hl && c.flip >= 1) {
                    if (f->winner && in_best(f, c.c)) c.highlight = pulse;
                    else c.dim = f->winner ? 0.3f : 0.55f;
                }
                float glow = 0;
                if (s == 0 && !v->demo && g->phase == HP_TURN && g->to_act == 0) glow = 0.35f + 0.15f * sinf((float)time * 4);
                cf_draw(&c, s == 0 ? CARD_L : CARD_M, s & 1, glow);
            }
        }
}

static void draw_seat(const HoldemViewInfo *v, const HoldemGame *g, int s, double time)
{
    const HoldemSeat *st = &g->seat[s];
    if (st->empty) return;
    SeatFx *f = &P.seat[s];
    const SeatPos *p = &k_pos[s];
    const AvatarRig *rg = hart_rig(f->avatar);
    int out = f->out || (!st->in_game && st->place > 0);
    int acting = g->phase == HP_TURN && g->to_act == s;
    int human = s == 0 && !v->demo;
    float bright = out ? 0.38f : (st->folded || (!st->in_hand && g->phase != HP_HAND_START) ? 0.6f : 1.0f);
    float t = (float)time;
    char b[32];
    Rectangle r = plate_rect(s);

    /* The nameplate. */
    if (acting) {
        gfx_nine(sprite_nine(NINE_GLOW), r.x - 24, r.y - 24, r.width + 48, r.height + 48, 40,
                 gfx_add(human ? UI_GOLD : rg->theme, 0.5f + 0.25f * sinf(t * 6)));
    }
    gfx_nine_vgrad(sprite_nine(NINE_RRECT), r.x, r.y, r.width, r.height, 14, gfx_cola((Color){ 40, 28, 44, 255 }, 0.94f),
                   gfx_cola((Color){ 12, 8, 14, 255 }, 0.94f));
    Color line = acting ? (human ? UI_GOLD : rg->theme) : scale_col((Color){ 196, 146, 62, 255 }, 0.55f + 0.45f * bright);
    gfx_nine(sprite_nine(NINE_RRECT_LINE), r.x, r.y, r.width, r.height, 14, gfx_col(line));
    float tx = p->flip ? r.x + 14 : r.x + 32;
    const char *nm = f->name[0] ? f->name : "SEAT";
    label(FONT_UI_M, nm, tx, r.y + 17, 23, scale_col((Color){ 248, 241, 226, 255 }, 0.5f + 0.5f * bright), ALIGN_LEFT, 0);
    int64_t stack = st->stack - P.infl_stack[s];
    if (stack < 0) stack = 0;
    if (out) {
        snprintf(b, sizeof b, "OUT  %s", place_str(f->place ? f->place : st->place));
        label(FONT_DISP_S, b, tx, r.y + 40, 19, (Color){ 200, 90, 90, 255 }, ALIGN_LEFT, 0);
    } else if (st->allin && st->in_hand && !st->folded && stack == 0) {
        label(FONT_DISP_S, "ALL-IN", tx, r.y + 40, 20, UI_MAGENTA, ALIGN_LEFT, 0);
    } else {
        TextStyle ts = style((Color){ 255, 236, 170, 255 }, ALIGN_LEFT);
        ts.color2 = (Color){ 235, 160, 40, 255 };
        ts.opacity = 0.5f + 0.5f * bright;
        text_draw_ex(FONT_DISP_S, fmt_chips(stack, b, sizeof b), tx, r.y + 40 - 21 * 0.56f, 21, &ts);
    }
    /* Badges on the plate's top edge: personality, blinds. */
    float bx = p->flip ? r.x + 18 : r.x + r.width - 18;
    if (f->badge[0] && !out) {
        Color bc = badge_color(f->badge);
        float w = text_width(FONT_UI_S, f->badge, 14, 1) + 12;
        float x = p->flip ? r.x + 10 : r.x + r.width - 10 - w;
        gfx_nine(sprite_nine(NINE_RRECT), x, r.y - 9, w, 17, 8, gfx_cola((Color){ 14, 10, 16, 255 }, 0.95f));
        gfx_nine(sprite_nine(NINE_RRECT_LINE), x, r.y - 9, w, 17, 8, gfx_cola(bc, 0.9f));
        TextStyle ts = style(bc, ALIGN_CENTER);
        ts.spacing = 1;
        text_draw_ex(FONT_UI_S, f->badge, x + w * 0.5f, r.y - 9.5f, 14, &ts);
        bx = p->flip ? x + w + 18 : x - 18;
    }
    if (st->in_hand && !out && g->phase != HP_HAND_START) {
        const char *bl = s == g->bb_seat ? "BB" : s == g->sb_seat ? "SB" : NULL;
        if (bl) {
            Color bc = s == g->bb_seat ? UI_MAGENTA : UI_CYAN;
            gfx_spr_rot(sprite(SPR_GLOW), bx, r.y, 34, 34, 0, gfx_add(bc, 0.35f));
            gfx_nine(sprite_nine(NINE_RRECT), bx - 15, r.y - 9, 30, 17, 8, gfx_cola((Color){ 14, 10, 16, 255 }, 0.95f));
            gfx_nine(sprite_nine(NINE_RRECT_LINE), bx - 15, r.y - 9, 30, 17, 8, gfx_col(bc));
            text_draw_ex(FONT_UI_S, bl, bx, r.y - 9.5f, 14, &(TextStyle){ .color = bc, .align = ALIGN_CENTER });
        }
    }

    /* The avatar, its timer ring and its tells. */
    float react = f->av.react;
    if (f->winner && !out) {
        float k = 0.5f + 0.5f * sinf(t * 5);
        gfx_spr_rot(sprite(SPR_GLOW), p->ax, p->ay, AV_D * 1.9f, AV_D * 1.9f, 0, gfx_add(UI_GOLD, 0.35f + 0.25f * k));
    }
    draw_avatar(f->avatar, &f->av, p->ax, p->ay, AV_D, bright, 1);
    if (acting) {
        Color rc = human ? UI_GOLD : rg->theme;
        float pulse = 0.6f + 0.4f * sinf(t * 5);
        gfx_spr_rot(sprite(SPR_RING), p->ax, p->ay, AV_D * 1.5f, AV_D * 1.5f, 0, gfx_add(rc, 0.8f * pulse));
        /* A comet runs round the ring while the seat thinks. */
        float a0 = t * (human ? 2.2f : 4.2f);
        for (int i = 0; i < 9; i++) {
            float a = a0 - (float)i * 0.13f;
            float rr = AV_D * 0.585f;
            gfx_spr_rot(sprite(SPR_GLOW), p->ax + rr * cosf(a), p->ay + rr * sinf(a), 22 - (float)i, 22 - (float)i, 0,
                        gfx_add(rc, 0.9f * (1 - (float)i / 9.0f)));
        }
    }
    if (acting && !human && rg->bee) draw_chip_riffle(s, f->av.tension, f->av.chip);
    if (out) {
        float k = ease(EASE_OUT_BACK, clampf(f->out_t / 0.35f, 0, 1));
        float sc = 2.0f - k;
        float a = clampf(f->out_t / 0.2f, 0, 1);
        snprintf(b, sizeof b, "OUT %s", place_str(f->place ? f->place : st->place));
        pill(p->ax, p->ay + 26, b, FONT_DISP_S, 18, (Color){ 255, 230, 230, 255 }, (Color){ 230, 50, 60, 255 }, a, sc * 0.9f);
    }
    (void)react;
}

static void draw_tags(const HoldemViewInfo *v, const HoldemGame *g)
{
    char b[48];
    (void)v;
    for (int s = 0; s < NSEAT; s++) {
        const SeatFx *f = &P.seat[s];
        const SeatPos *p = &k_pos[s];
        if (g->seat[s].empty) continue;
        Rectangle r = plate_rect(s);
        /* The last action, under the nameplate (above it for seat 0). */
        if (f->act_t >= 0 && f->act[0]) {
            float sc = ease(EASE_OUT_BACK, clampf(f->act_t / 0.22f, 0, 1));
            float a = f->act_t > 3.0f ? fmaxf(0.6f, 1 - (f->act_t - 3.0f)) : 1;
            float y = s == 0 ? r.y - 16 : r.y + r.height + 14;
            if (s == 3) y = r.y + r.height + 12;
            pill(r.x + r.width * 0.5f, y, f->act, FONT_UI_M, 20, WHITE, f->act_col, a, sc);
        }
        /* The hand's name at the showdown, and the equity in a run-out. */
        int tabled = s == 0 || f->shown;
        if (!tabled || !(P.hole[s][0].on || P.hole[s][1].on)) continue;
        float hy = s == 0 ? p->cy - 92 : p->sy + 58, hx = s == 0 ? p->cx : p->sx;
        if (f->hand[0] && f->best_valid && board_shown() && (f->shown || s == 0) &&
            (g->phase >= HP_SHOWDOWN || f->winner || P.last_game_over)) {
            float a = clampf(f->hand_t / 0.3f, 0, 1);
            if (f->winner) {
                float w = text_width(FONT_UI_M, f->hand, 22, 0) + 30;
                gfx_nine(sprite_nine(NINE_GLOW), hx - w * 0.5f - 24, hy - 17 - 24, w + 48, 34 + 48, 40, gfx_add(UI_GOLD, 0.6f * a));
                pill(hx, hy, f->hand, FONT_UI_M, 22, (Color){ 255, 236, 170, 255 }, UI_GOLD, a, 1);
            } else if (f->shown || s == 0) {
                pill(hx, hy, f->hand, FONT_UI_M, 19, (Color){ 220, 214, 204, 255 }, (Color){ 120, 110, 120, 255 }, 0.9f * a, 1);
            }
        } else if (f->eq_valid && P.runout && !board_shown()) {
            snprintf(b, sizeof b, "%d%%", (int)lrintf(f->eq * 100));
            Color c = f->eq >= 0.5f ? (Color){ 120, 255, 160, 255 } : f->eq >= 0.2f ? UI_GOLD : (Color){ 255, 120, 120, 255 };
            pill(hx, hy, b, FONT_DISP_S, 20, c, c, 1, 1);
        }
    }
}

/* ---- HUD --------------------------------------------------------------------------------- */

static void draw_hud(const HoldemViewInfo *v, const HoldemGame *g)
{
    char b[64], n1[16], n2[16];
    HoldemLevel lv = holdem_level(g);
    Rectangle l = { 14, 10, 268, 70 };
    ui_panel(l, UI_HONEY, 0.18f + 0.8f * P.level_flash, 1);
    snprintf(b, sizeof b, "BLINDS %s / %s", fmt_chips(lv.sb, n1, sizeof n1), fmt_chips(lv.bb, n2, sizeof n2));
    gold_label(FONT_DISP_S, b, l.x + 14, l.y + 20, 20, ALIGN_LEFT, 0.2f + 0.6f * P.level_flash);
    int hpl = g->cfg.hands_per_level > 0 ? g->cfg.hands_per_level : 1;
    int into = g->hand_no > 0 ? (g->hand_no - 1) % hpl + 1 : 0;
    int left = hpl - into;
    int last = g->level >= g->cfg.nlevels - 1;
    if (lv.ante) snprintf(b, sizeof b, "ANTE %d   LEVEL %d", (int)lv.ante, g->level + 1);
    else snprintf(b, sizeof b, "LEVEL %d   HAND %d", g->level + 1, g->hand_no);
    label(FONT_UI_M, b, l.x + 14, l.y + 42, 19, (Color){ 230, 214, 190, 255 }, ALIGN_LEFT, 0);
    if (!last) {
        snprintf(b, sizeof b, left <= 0 ? "BLINDS UP NEXT HAND" : "UP IN %d", left + (left <= 0 ? 0 : 0));
        if (left > 0) snprintf(b, sizeof b, "NEXT LEVEL IN %d HAND%s", left, left == 1 ? "" : "S");
        label(FONT_UI_S, b, l.x + 14, l.y + 60, 15, left <= 1 ? UI_AMBER : (Color){ 170, 160, 150, 255 }, ALIGN_LEFT, 0);
        float k = clampf((float)into / (float)hpl, 0, 1);
        gfx_rect(l.x + 170, l.y + 56, 84, 6, gfx_cola((Color){ 60, 40, 30, 255 }, 0.9f));
        gfx_rect(l.x + 170, l.y + 56, 84 * k, 6, gfx_col(left <= 1 ? UI_AMBER : UI_HONEY));
    }
    Rectangle r = { PLAY_W - 14 - 268, 10, 268, 70 };
    ui_panel(r, UI_CYAN, 0.18f, 1);
    if (!v->demo) {
        snprintf(b, sizeof b, "CREDITS %s", fmt_chips(v->credits, n1, sizeof n1));
        gold_label(FONT_DISP_S, b, r.x + r.width - 14, r.y + 20, 20, ALIGN_RIGHT, 0.2f);
        char p1[16], p2[16], p3[16];
        snprintf(b, sizeof b, "PRIZES  1ST %s  2ND %s  3RD %s", fmt_chips(v->prize[1], p1, sizeof p1),
                 fmt_chips(v->prize[2], p2, sizeof p2), fmt_chips(v->prize[3], p3, sizeof p3));
        label(FONT_UI_S, b, r.x + r.width - 14, r.y + 42, 16, (Color){ 150, 230, 255, 255 }, ALIGN_RIGHT, 0);
    } else {
        gold_label(FONT_DISP_S, "SIT & GO DEMO", r.x + r.width - 14, r.y + 20, 20, ALIGN_RIGHT, 0.2f);
    }
    snprintf(b, sizeof b, g->players_left == 1 ? "%d PLAYER LEFT" : "%d PLAYERS LEFT", g->players_left);
    label(FONT_UI_S, b, r.x + r.width - 14, r.y + 60, 15, (Color){ 170, 160, 150, 255 }, ALIGN_RIGHT, 0);
}

/* ---- the action bar ------------------------------------------------------------------------ */

static void draw_action_bar(const HoldemViewInfo *v, const HoldemGame *g, double time)
{
    BarState b;
    Rectangle r[B_N];
    char s[64], n1[16], n2[16];
    bar_state(g, &b);
    bar_rects(r);
    gfx_rect_vgrad(0, BAR_Y - 8, PLAY_W, PLAY_H - BAR_Y + 8, gfx_cola((Color){ 8, 5, 10, 255 }, 0.2f),
                   gfx_cola((Color){ 8, 5, 10, 255 }, 0.92f));
    gfx_rect_hgrad(0, BAR_Y - 8, PLAY_W * 0.5f, 2, gfx_add(UI_MAGENTA, 0), gfx_add(UI_MAGENTA, 0.8f));
    gfx_rect_hgrad(PLAY_W * 0.5f, BAR_Y - 8, PLAY_W * 0.5f, 2, gfx_add(UI_MAGENTA, 0.8f), gfx_add(UI_MAGENTA, 0));
    for (int i = 0; i < B_N; i++) {
        ui_button_draw(&P.btn[i], r[i], "", b.col[i], b.st[i], time);
        float p = ease(EASE_OUT_QUAD, P.btn[i].press);
        float cx = r[i].x + r[i].width * 0.5f, dy = 2 * p;
        Color cap = b.st[i] == BTN_STATE_OFF ? (Color){ 110, 100, 110, 255 } : fade(b.col[i], 0.95f);
        /* The caption names the panel button; the icon beside it shows where it is. */
        static const uint32_t keys[B_N] = { BTN_HOLD1, BTN_HOLD2, BTN_HOLD3, BTN_HOLD4, BTN_HOLD5, BTN_BET_MAX, BTN_DEAL };
        const float gh = 15, gw = ui_panel_glyph_w(gh), cw = text_width(FONT_UI_S, b.caption[i], 14, 0);
        float x0 = cx - (gw + 6 + cw) * 0.5f;
        ui_panel_glyph(x0, r[i].y + 5 + dy, gh, keys[i], b.st[i] == BTN_STATE_OFF ? (Color){ 150, 140, 150, 255 } : b.col[i],
                       b.st[i] == BTN_STATE_OFF ? 0.6f : 1.0f);
        label(FONT_UI_S, b.caption[i], x0 + gw + 6 + cw * 0.5f, r[i].y + 13 + dy, 14, cap, ALIGN_CENTER, 0);
        TextStyle ts = style(b.st[i] == BTN_STATE_OFF ? (Color){ 120, 112, 120, 255 } : (Color){ 255, 250, 240, 255 }, ALIGN_CENTER);
        ts.shadow = BLACK;
        ts.shadow_k = 0.8f;
        float size = i == B_DEAL ? 22 : 19;
        if (text_width(FONT_DISP_S, b.label[i], size, 0) > r[i].width - 14) size *= (r[i].width - 14) / text_width(FONT_DISP_S, b.label[i], size, 0);
        text_draw_ex(FONT_DISP_S, b.label[i], cx, r[i].y + 36 + dy - size * 0.56f, size, &ts);
    }
    /* The bet panel: the amount, the slider, the step button. */
    Rectangle pr = { 792, 574, 474, 66 };
    if (b.mode == 1 && (b.L.can_bet || b.L.can_raise)) {
        ui_panel(pr, UI_GOLD, 0.3f + 0.5f * P.amount_bump, 1);
        int allin = g->sel_amount >= b.L.max_to;
        label(FONT_UI_S, allin ? "ALL-IN" : b.L.can_bet ? "BET" : "RAISE TO", pr.x + 16, pr.y + 16, 15, (Color){ 230, 200, 150, 255 },
              ALIGN_LEFT, 0);
        TextStyle ts = style((Color){ 255, 246, 196, 255 }, ALIGN_LEFT);
        ts.color2 = (Color){ 240, 150, 30, 255 };
        ts.glow = UI_AMBER;
        ts.glow_k = 0.4f + 0.6f * P.amount_bump;
        ts.shadow = BLACK;
        ts.shadow_k = 0.8f;
        float sz = 34 * (1 + 0.08f * P.amount_bump);
        text_draw_ex(FONT_DISP_M, fmt_chips(g->sel_amount, n1, sizeof n1), pr.x + 16, pr.y + 44 - sz * 0.56f, sz, &ts);
        float x0 = pr.x + 200, x1 = pr.x + pr.width - 22, y = pr.y + 26;
        gfx_nine(sprite_nine(NINE_RRECT), x0 - 4, y - 5, x1 - x0 + 8, 10, 5, gfx_cola((Color){ 40, 26, 30, 255 }, 1));
        float k = clampf(P.slider.x, 0, 1.05f);
        gfx_rect_hgrad(x0, y - 2, (x1 - x0) * clampf(k, 0, 1), 4, gfx_col(UI_AMBER), gfx_col(allin ? UI_MAGENTA : UI_GOLD));
        float tx = lerpf(x0, x1, k);
        gfx_spr_rot(sprite(SPR_GLOW), tx, y, 44, 44, 0, gfx_add(allin ? UI_MAGENTA : UI_GOLD, 0.7f + 0.5f * P.amount_bump));
        gfx_spr(hart_chip(allin ? HCHIP_500 : HCHIP_1000), tx - 14, y - 12, 28, 23, gfx_col(WHITE));
        snprintf(s, sizeof s, "MIN %s", fmt_chips(b.L.min_to, n1, sizeof n1));
        label(FONT_UI_S, s, x0, pr.y + 50, 14, (Color){ 170, 160, 150, 255 }, ALIGN_LEFT, 0);
        snprintf(s, sizeof s, "MAX %s", fmt_chips(b.L.max_to, n2, sizeof n2));
        label(FONT_UI_S, s, x1, pr.y + 50, 14, (Color){ 170, 160, 150, 255 }, ALIGN_RIGHT, 0);
        snprintf(s, sizeof s, "BET ONE  +%s", fmt_chips(holdem_level(g).bb, n1, sizeof n1));
        label(FONT_UI_S, s, (x0 + x1) * 0.5f, pr.y + 50, 14, UI_AMBER, ALIGN_CENTER, 0);
    } else if (b.mode == 1) {
        label(FONT_NEON_M, "YOUR TURN", pr.x + pr.width * 0.5f, pr.y + 33, 30, UI_GOLD, ALIGN_CENTER, 0.8f);
    } else if (b.mode == 2) {
        ui_panel(pr, UI_CYAN, 0.4f, 1);
        label(FONT_DISP_S, "SHOW YOUR HAND?", pr.x + 18, pr.y + 22, 22, UI_CYAN, ALIGN_LEFT, 0.6f);
        int left = g->cfg.human_show_ticks - g->prompt_ticks;
        snprintf(s, sizeof s, "MUCKS IN %d", left > 0 ? (left + 59) / 60 : 0);
        label(FONT_UI_M, s, pr.x + 18, pr.y + 48, 18, (Color){ 200, 190, 180, 255 }, ALIGN_LEFT, 0);
        label(FONT_UI_M, "DEAL / HOLD 2: SHOW    HOLD 1: MUCK", pr.x + pr.width - 16, pr.y + 48, 17, (Color){ 230, 220, 200, 255 },
              ALIGN_RIGHT, 0);
    } else if (b.mode == 3) {
        ui_panel(pr, (Color){ 230, 60, 80, 255 }, 0.3f, 1);
        int pl = g->seat[0].place;
        snprintf(s, sizeof s, "YOU FINISHED %s", place_str(pl));
        label(FONT_DISP_S, s, pr.x + 18, pr.y + 22, 22, (Color){ 255, 200, 200, 255 }, ALIGN_LEFT, 0.6f);
        if (v->won > 0) snprintf(s, sizeof s, "PRIZE %s CREDITS PAID", fmt_chips(v->won, n1, sizeof n1));
        else snprintf(s, sizeof s, "NO PRIZE THIS TIME");
        label(FONT_UI_M, s, pr.x + 18, pr.y + 48, 18, UI_GOLD, ALIGN_LEFT, 0);
        label(FONT_UI_M, "DEAL: WATCH   CASH OUT: LEAVE", pr.x + pr.width - 16, pr.y + 48, 17, (Color){ 230, 220, 200, 255 },
              ALIGN_RIGHT, 0);
    } else if (b.mode == 4) {
        ui_panel(pr, (Color){ 150, 140, 160, 255 }, 0.15f, 0.9f);
        snprintf(s, sizeof s, "YOU FINISHED %s  -  WATCHING", place_str(g->seat[0].place));
        label(FONT_DISP_S, s, pr.x + 18, pr.y + 22, 20, (Color){ 230, 220, 230, 255 }, ALIGN_LEFT, 0.6f);
        if (v->won > 0) snprintf(s, sizeof s, "PRIZE %s CREDITS PAID", fmt_chips(v->won, n1, sizeof n1));
        else snprintf(s, sizeof s, "NO PRIZE THIS TIME");
        label(FONT_UI_M, s, pr.x + 18, pr.y + 48, 18, UI_GOLD, ALIGN_LEFT, 0);
        label(FONT_UI_M, "CASH OUT: LEAVE", pr.x + pr.width - 16, pr.y + 48, 17, (Color){ 230, 220, 200, 255 }, ALIGN_RIGHT, 0);
    } else if (g->phase == HP_TURN && g->to_act > 0 && g->to_act < NSEAT) {
        snprintf(s, sizeof s, "%s IS THINKING", P.seat[g->to_act].name);
        label(FONT_UI_M, s, pr.x + pr.width * 0.5f, pr.y + 36, 20, (Color){ 160, 150, 160, 255 }, ALIGN_CENTER, 0);
    }
    label(FONT_UI_S, "CASH OUT: LEAVE TABLE", 18, BAR_Y - 18, 14, (Color){ 130, 120, 130, 255 }, ALIGN_LEFT, 0);
}

/* ---- screens --------------------------------------------------------------------------------- */

static void draw_demo_overlay(double time)
{
    float t = (float)time;
    gfx_rect_vgrad(0, BAR_Y - 4, PLAY_W, PLAY_H - BAR_Y + 4, gfx_cola(BLACK, 0.0f), gfx_cola(BLACK, 0.85f));
    float p = 0.6f + 0.4f * sinf(t * 4.0f);
    text_neon(FONT_NEON_L, "PRESS DEAL", 640, 684, 54, UI_CYAN, 0.75f + 0.25f * p, 1.0f);
    label(FONT_UI_M, "DEMO  -  TEXAS HOLD'EM SIT & GO", 24, 690, 20, (Color){ 255, 170, 220, 255 }, ALIGN_LEFT, 0.6f);
    label(FONT_UI_M, "FREE PLAY  -  REAL SHUFFLES, REAL ODDS", PLAY_W - 24, 690, 20, (Color){ 200, 190, 170, 255 }, ALIGN_RIGHT,
          0.6f);
}

static void draw_lobby(const HoldemViewInfo *v, double time)
{
    char b[80], n1[16];
    float t = (float)time;
    Rectangle r = { 170, 40, 940, 600 };
    gfx_nine(sprite_nine(NINE_SHADOW), r.x - 30, r.y - 20, r.width + 60, r.height + 60, 44, gfx_cola(BLACK, 0.7f));
    ui_panel(r, UI_MAGENTA, 0.45f, 0.96f);
    text_neon(FONT_NEON_L, "TEXAS HOLD'EM", 640, 96, 62, UI_GOLD, 0.95f + 0.05f * sinf(t * 1.3f), 1.0f);
    label(FONT_UI_M, "NO-LIMIT  -  6-MAX SIT & GO  -  YOU AGAINST FIVE AI PLAYERS", 640, 146, 22, (Color){ 230, 210, 190, 255 },
          ALIGN_CENTER, 0.6f);
    static const int cast[5] = { AV_BUZZ, AV_HONEY, AV_STINGER, AV_DRONE, AV_QUEENIE };
    static const char *const names[5] = { "BUZZ", "HONEY", "STINGER", "DRONE", "QUEENIE" };
    for (int i = 0; i < 5; i++) {
        float x = 640 + (float)(i - 2) * 168, y = 246 + 4 * sinf(t * 1.6f + (float)i);
        gfx_spr_rot(sprite(SPR_GLOW), x, y, 170, 170, 0, gfx_add(hart_rig(cast[i])->theme, 0.18f));
        draw_avatar(cast[i], &P.seat[i + 1].av, x, y, 112, 1, 1);
        label(FONT_DISP_S, names[i], x, 322, 20, (Color){ 248, 241, 226, 255 }, ALIGN_CENTER, 0.7f);
    }
    snprintf(b, sizeof b, "OPPONENTS: %s", v->difficulty ? v->difficulty : "NORMAL");
    label(FONT_UI_M, b, 640, 358, 20, UI_CYAN, ALIGN_CENTER, 0.6f);
    snprintf(b, sizeof b, "BUY-IN  %s CREDITS", fmt_chips(v->buyin, n1, sizeof n1));
    gold_label(FONT_DISP_M, b, 640, 408, 40, ALIGN_CENTER, 0.5f);
    for (int p = 1; p <= 3; p++) {
        char pz[16];
        snprintf(b, sizeof b, "%s  %s", place_str(p), fmt_chips(v->prize[p], pz, sizeof pz));
        pill(640 + (float)(p - 2) * 190, 462, b, FONT_DISP_S, 22, p == 1 ? UI_GOLD : (Color){ 240, 230, 220, 255 },
             p == 1 ? UI_GOLD : (Color){ 150, 120, 90, 255 }, 1, 1);
    }
    label(FONT_UI_M, "1,500 CHIPS EACH  -  BLINDS RISE EVERY 10 HANDS  -  TOP THREE PAID IN CREDITS", 640, 506, 19,
          (Color){ 190, 180, 170, 255 }, ALIGN_CENTER, 0);
    float p = 0.6f + 0.4f * sinf(t * 4.0f);
    text_neon(FONT_NEON_L, "PRESS DEAL TO BUY IN", 640, 562, 46, UI_MAGENTA, 0.7f + 0.3f * p, 1.0f);
    snprintf(b, sizeof b, "CREDITS %s", fmt_chips(v->credits, n1, sizeof n1));
    gold_label(FONT_DISP_S, b, 200, 612, 20, ALIGN_LEFT, 0.2f);
    label(FONT_UI_M, "CASH OUT: MENU", 1080, 612, 18, (Color){ 170, 160, 150, 255 }, ALIGN_RIGHT, 0);
}

static void draw_result(const HoldemViewInfo *v, const HoldemGame *g, double time)
{
    char b[80], n1[16];
    float a = clampf(P.result_t / 0.4f, 0, 1);
    if (a <= 0.01f) return;
    float e = ease(EASE_OUT_BACK, a);
    gfx_rect(0, 0, PLAY_W, PLAY_H, gfx_cola(BLACK, 0.68f * a));
    Rectangle r = { 300, 70 + 30 * (1 - e), 680, 560 };
    gfx_nine(sprite_nine(NINE_RRECT), r.x, r.y, r.width, r.height, 16, gfx_cola((Color){ 10, 6, 12, 255 }, 0.9f * a));
    ui_panel(r, v->place == 1 ? UI_GOLD : UI_MAGENTA, 0.5f * a, a);
    if (v->place == 1) text_neon(FONT_NEON_L, "CHAMPION!", 640, r.y + 52, 60, UI_GOLD, a, 1);
    else {
        snprintf(b, sizeof b, v->place > 0 ? "YOU FINISHED %s" : "SIT & GO OVER", place_str(v->place));
        text_neon(FONT_NEON_L, b, 640, r.y + 52, 50, UI_MAGENTA, a, 1);
    }
    label(FONT_UI_M, "FINAL STANDINGS", 640, r.y + 102, 20, fade((Color){ 200, 190, 180, 255 }, a), ALIGN_CENTER, 0);
    for (int p = 1; p <= NSEAT; p++) {
        int s = -1;
        for (int i = 0; i < NSEAT; i++) if (!g->seat[i].empty && g->seat[i].place == p) s = i;
        if (s < 0) continue;
        float y = r.y + 136 + (float)(p - 1) * 52;
        float ra = clampf((P.result_t - 0.3f - 0.08f * (float)p) / 0.25f, 0, 1) * a;
        if (ra <= 0.01f) continue;
        int me = s == 0 && !v->demo;
        if (me) gfx_nine(sprite_nine(NINE_RRECT), r.x + 30, y - 23, r.width - 60, 46, 12, gfx_add(UI_GOLD, 0.18f * ra));
        label(FONT_DISP_S, place_str(p), r.x + 56, y, 22, fade(p == 1 ? UI_GOLD : (Color){ 230, 220, 210, 255 }, ra), ALIGN_LEFT, 0);
        const Spr *bk = hart_avatar(P.seat[s].avatar, AVL_BACK), *fr = hart_avatar(P.seat[s].avatar, AVL_FRONT);
        if (bk) gfx_spr(bk, r.x + 130, y - 20, 40, 40, gfx_cola(WHITE, ra));
        if (fr) gfx_spr(fr, r.x + 130, y - 20, 40, 40, gfx_cola(WHITE, ra));
        label(FONT_UI_M, P.seat[s].name[0] ? P.seat[s].name : "SEAT", r.x + 184, y, 24, fade(me ? UI_GOLD : UI_IVORY, ra), ALIGN_LEFT, 0);
        if (p <= 3) snprintf(b, sizeof b, "%s CREDITS", fmt_chips(v->prize[p], n1, sizeof n1));
        else snprintf(b, sizeof b, "-");
        label(FONT_DISP_S, b, r.x + r.width - 50, y, 20, fade(p <= 3 ? (Color){ 255, 226, 150, 255 } : (Color){ 140, 130, 130, 255 }, ra),
              ALIGN_RIGHT, 0);
    }
    float pa = 0.6f + 0.4f * sinf((float)time * 4);
    if (v->won > 0) {
        snprintf(b, sizeof b, "YOUR PRIZE  %s CREDITS", fmt_chips(v->won, n1, sizeof n1));
        gold_label(FONT_DISP_S, b, 640, r.y + r.height - 70, 24, ALIGN_CENTER, 0.5f * a);
    }
    label(FONT_UI_M, "DEAL: PLAY AGAIN        CASH OUT: MENU", 640, r.y + r.height - 32, 22, fade(UI_CYAN, a * pa), ALIGN_CENTER, 0.6f);
}

static void draw_confirm(const HoldemViewInfo *v, const HoldemGame *g)
{
    char b[80], n1[16];
    int place = g->players_left;
    gfx_rect(0, 0, PLAY_W, PLAY_H, gfx_cola(BLACK, 0.55f));
    Rectangle r = { 290, 220, 700, 250 };
    ui_panel(r, UI_MAGENTA, 0.6f, 1);
    text_neon(FONT_NEON_L, "LEAVE THE TABLE?", 640, 272, 50, UI_MAGENTA, 1, 1);
    snprintf(b, sizeof b, "YOU WOULD FINISH %s  -  PRIZE %s", place_str(place),
             fmt_chips(place >= 1 && place <= 3 ? v->prize[place] : 0, n1, sizeof n1));
    label(FONT_UI_M, b, 640, 340, 26, UI_IVORY, ALIGN_CENTER, 0);
    label(FONT_DISP_S, "CASH OUT: LEAVE        ANY OTHER BUTTON: STAY", 640, 410, 20, UI_GOLD, ALIGN_CENTER, 0.5f);
}

/* ---- the frame ------------------------------------------------------------------------------ */

static double g_fill_sum, g_fill_max;
static int g_fill_n, g_fill_log = -1;

void holdem_present_view(const HoldemViewInfo *v, double time)
{
    init_once();
    const HoldemGame *g = v->phase == HV_LOBBY ? NULL : v->game;
    float dim = celeb_dim(&P.cel);
    double t = time;
    render_begin();
    draw_backdrop(t, g ? dim : 0.5f);
    if (!g) {
        draw_lobby(v, t);
        if (v->message) pill(640, 690, v->message, FONT_UI_M, 22, UI_AMBER, UI_AMBER, 1, 1);
    } else {
        float dx, dy, deg;
        shake_offset(&P.shake, &dx, &dy, &deg);
        gfx_push_offset(dx, dy, deg, PLAY_W * 0.5f, PLAY_H * 0.5f);
        gfx_set_tint(dim, dim, dim);
        draw_rail_neon(t);
        draw_pots(g);
        draw_bets(g);
        draw_dealer_button();
        draw_board(t);
        for (int s = 0; s < NSEAT; s++) draw_seat(v, g, s, t);
        draw_holes(v, g, t);
        flights_draw();
        draw_tags(v, g);
        floaters_draw();
        gfx_set_tint(1, 1, 1);
        celeb_draw_back(&P.cel, t);
        pfx_draw();
        celeb_draw_front(&P.cel, t);
        gfx_pop_offset();
        banners_draw(t);
        gfx_set_tint(dim, dim, dim);
        draw_hud(v, g);
        gfx_set_tint(1, 1, 1);
        if (!v->demo && v->phase == HV_PLAYING) draw_action_bar(v, g, t);
        if (v->demo) draw_demo_overlay(t);
        if (P.bulbs_k > 0.01f) bulbs_draw(&P.bulbs);
        if (v->phase == HV_RESULT) draw_result(v, g, t);
        if (v->confirm_leave) draw_confirm(v, g);
        if (v->message) pill(640, 500, v->message, FONT_UI_M, 22, UI_AMBER, UI_AMBER, 1, 1);
    }
    render_end();
    /* Development: BPL_HOLDEM_FILL=1 logs blended fill (Mpx per frame). */
    if (g_fill_log < 0) g_fill_log = getenv("BPL_HOLDEM_FILL") != NULL;
    if (g_fill_log) {
        double f = gfx_fill_take();
        g_fill_sum += f;
        if (f > g_fill_max) g_fill_max = f;
        if (++g_fill_n == 120) {
            fprintf(stderr, "HOLDEM FILL: mean %.2f Mpx, max %.2f Mpx per frame (celebration %d, particles %d)\n",
                    g_fill_sum / 120 / 1e6, g_fill_max / 1e6, P.cel.active ? (int)P.cel.tier : 0, pfx_count());
            g_fill_sum = g_fill_max = 0;
            g_fill_n = 0;
        }
    }
}

const HoldemViewFns holdem_present_fns = { holdem_present_update, holdem_present_view };
