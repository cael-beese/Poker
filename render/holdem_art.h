/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* holdem_art.h - the Hold'em table's generated art, painted at start-up
 * with the rest of the atlas (render_init): the felt table backdrop, casino
 * chips per denomination, the dealer puck, and the avatar portraits of the
 * bee-lounge cast. All original, all procedural (raster.h SDFs).
 *
 * Avatars are painted in layers so the presentation can animate them from
 * public information only (the TELL event, idle blinks, wins, busts):
 *   back   dark disc + brass frame (the portrait's own backdrop)
 *   wings  shared translucent wing, drawn left and right (flutter)
 *   antenna shared stalk, rotated about its base (fidget)
 *   front  body, head, outfit, mouth - everything but the eyes
 *   eye / pupil  shared, placed by the rig (glance, blink)
 *   over   the eye-region overlay: lids, lashes (drawn after the eyes)
 *   brow   shared, rotated by tension
 * Sprites are premultiplied; draw them with gfx_col(). */
#ifndef BPL_RENDER_HOLDEM_ART_H
#define BPL_RENDER_HOLDEM_ART_H

#include "raylib.h"
#include "render/atlas.h"

/* Chip denominations, smallest first. */
enum { HCHIP_1, HCHIP_5, HCHIP_25, HCHIP_100, HCHIP_500, HCHIP_1000, HCHIP_5000, HCHIP_N };
extern const int hart_chip_value[HCHIP_N];

/* Baked chip: 44 x 36, face ellipse centred at (22, 15), 6 px edge below. */
#define HCHIP_W 44
#define HCHIP_H 36
#define HCHIP_THICK 6

/* The cast. One avatar per seat name; the human's seat is its own badge. */
enum { AV_YOU, AV_BUZZ, AV_HONEY, AV_STINGER, AV_DRONE, AV_QUEENIE, AV_BEE, AV_N };
enum { AVL_BACK, AVL_FRONT, AVL_OVER, AVL_N };
enum { HPART_WING, HPART_ANTENNA, HPART_EYE, HPART_PUPIL, HPART_BROW, HPART_PUCK, HPART_N };

#define HAV_SIZE 128          /* avatar layers are 128 x 128 (over: see rig) */

typedef struct {
    int   bee;                /* 0: no eyes, antennae or wings (the human badge) */
    float eye_x[2], eye_y;    /* eye centres, avatar px                        */
    float eye_rx, eye_ry;     /* eye white half size                           */
    float pupil_r;
    float ant_x[2], ant_y;    /* antenna bases                                 */
    float ant_rot[2];         /* resting angles (radians, 0 = straight up)     */
    Color ant_tint;
    float brow_y;             /* brow centre line                              */
    Color brow_col;
    float over_x, over_y;     /* where the over layer's top-left goes          */
    int   has_over;
    Color theme;              /* the character's neon colour (rings, tags)     */
} AvatarRig;

/* Start-up (render.c). */
void holdem_art_declare(void);
int  holdem_art_job_count(void);
void holdem_art_paint_job(int i);
void holdem_art_finish(void);
void holdem_art_shutdown(void);

const Spr       *hart_chip(int denom);
const Spr       *hart_avatar(int who, int layer);    /* NULL when a layer is absent */
const Spr       *hart_part(int part);
const AvatarRig *hart_rig(int who);
/* The 1280x720 table backdrop (opaque), or 0 before start-up. */
unsigned         hart_table_tex(void);

/* Table geometry the backdrop was painted with (the presentation's layout
 * follows it): a stadium centred at (HT_CX, HT_CY). */
#define HT_CX 640.0f
#define HT_CY 332.0f
#define HT_HX 560.0f          /* half width, outer edge of the rail   */
#define HT_HY 228.0f          /* half height                          */
#define HT_RAIL 34.0f         /* rail width (brass + leather + brass) */
#define HT_NEON 46.0f         /* the neon inlay, inside the outer edge */
#define HT_BETLINE 116.0f     /* the betting line                      */

#endif
