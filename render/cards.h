/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* cards.h - the procedural deck and how cards move.
 *
 * All 52 faces and two backs are painted at start-up (render_init) in two
 * sizes, straight into the atlas: vector-drawn with analytic anti-aliasing,
 * so each size is crisp at 1:1.
 *   CARD_L 200 x 280  the Draw Poker hand (1:1), Hold'em hole cards (scaled)
 *   CARD_M 120 x 168  the Hold'em board and opponents; scales down to ~60 px
 * Art: ivory stock with a faint honeycomb, a brushed-brass edge, Bungee
 * indices, original court figures (King with sceptre, Queen with a flower,
 * Jack with a honey dipper) on a dark honeycomb panel, and a honeycomb back
 * with the bee mascot in a brass medallion. All original.
 *
 * Drawing goes through a CardPose (position, flip, lift, glow...), so the
 * presentation animates a pose and the card look stays in one place. */
#ifndef BPL_RENDER_CARDS_H
#define BPL_RENDER_CARDS_H

#include "raylib.h"
#include "engine/card.h"
#include "render/gfx.h"
#include "render/tween.h"

typedef enum { CARD_L, CARD_M, CARD_NSIZES } CardSize;

#define CARD_L_W 200
#define CARD_L_H 280
#define CARD_M_W 120
#define CARD_M_H 168
#define CARD_BACKS 2          /* back variants: 0 magenta, 1 cyan */
#define CARD_SHEEN_FRAMES 16

float card_w(CardSize s);
float card_h(CardSize s);

/* Start-up (render.c). */
void cards_declare(void);
int  cards_job_count(void);
void cards_paint_job(int i);     /* any thread */
void cards_finish(void);         /* after the atlas upload */

const Spr *card_face_spr(Card c, CardSize s);
const Spr *card_back_spr(CardSize s, int variant);

/* How a card is shown this frame. Zero-initialise, then set what you need:
 * a zeroed pose is a face-down card at (0,0) - set flip = 1 for face up. */
typedef struct {
    float x, y;            /* centre, play-space px                             */
    float scale;           /* 0 is treated as 1 (native size of the CardSize)   */
    float rot;             /* radians                                           */
    float flip;            /* 0 = back up .. 1 = face up (fake-3D turn between) */
    float lift;            /* px the card floats up (hold), shadow grows        */
    float glow;            /* 0..1 neon glow frame (held)                       */
    Color glow_color;      /* default honey                                     */
    float highlight;       /* 0..1 winning-card pulse (white-hot frame + sheen) */
    float dim;             /* 0..1 darken (a card that did not win)             */
    float sheen;           /* -1/0 none, else 0..1 position of a specular band   */
    float sheen_k;         /* its intensity                                     */
    float alpha;           /* 0 is treated as 1                                 */
    int   back;            /* back variant                                      */
    int   no_shadow;
} CardPose;

void card_draw(Card c, CardSize s, const CardPose *p);

/* ---- motion ------------------------------------------------------------- */

/* A deal: the card leaves the shoe face down, flies an eased arc to its
 * spot while turning to its resting angle, lands with a small settle, and
 * optionally flips face up after landing. */
typedef struct {
    Vec2f from, ctrl, to;
    float t, delay, dur;
    float rot_from, rot_to;
    float scale_from;
    float flip_at, flip_dur;   /* flip starts flip_at s after landing; dur 0 = no flip */
    int   state;               /* 0 idle, 1 waiting, 2 flying, 3 landed, 4 flipping, 5 done */
    int   events;              /* bit 0: left the shoe, bit 1: landed, bit 2: flip started, bit 3: flip done (this update) */
} CardMotion;

void card_motion_deal(CardMotion *m, Vec2f shoe, Vec2f to, float delay, float dur, float arc_px,
                      float rot_from, float rot_to, float flip_after, float flip_dur);
/* Advances and writes x, y, rot, scale, flip into pose. Returns m->events. */
int  card_motion_update(CardMotion *m, float dt, CardPose *pose);

/* A flip in place (draw: cards turning over), 0 -> 1 with a lift at the middle. */
float card_flip_curve(float t);            /* eased flip progress for t in 0..1 */

/* Idle shimmer for held cards: a slow specular band that crosses now and
 * then. seed staggers cards. Writes sheen / sheen_k (none if the shimmer
 * effect is off). */
void card_shimmer(CardPose *p, double time, int seed);

#endif
