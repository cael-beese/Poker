/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* sprites.h - the generated effect and UI sprites in the atlas.
 *
 * Every image is premultiplied white-or-coloured art meant to be tinted:
 * draw with gfx_col() to cover, gfx_add() to glow. Painted at start-up. */
#ifndef BPL_RENDER_SPRITES_H
#define BPL_RENDER_SPRITES_H

#include "render/gfx.h"

typedef enum {
    SPR_WHITE,          /* 4x4 white; gfx_rect uses its centre texel       */
    SPR_GLOW,           /* soft round light, 64x64                          */
    SPR_SPARK,          /* hot streak with a white core, 64x16              */
    SPR_STAR,           /* four-point twinkle, 48x48                        */
    SPR_COIN0,          /* gold coin, 8 spin frames 40x40 ... */
    SPR_COIN7 = SPR_COIN0 + 7,
    SPR_DROP,           /* honey droplet, 24x32                              */
    SPR_CONFETTI,       /* white paper chip, 16x10 (tint it)                 */
    SPR_BULB_ON,        /* lit bulb, 28x28                                   */
    SPR_BULB_OFF,       /* unlit bulb glass, 28x28                           */
    SPR_BEAM,           /* spotlight beam, apex at the top centre, 128x512  */
    SPR_RAYS,           /* sunburst, 256x256                                 */
    SPR_RING,           /* thin glowing ring, 128x128                        */
    SPR_BEE,            /* the mascot, 128x112                               */
    SPR_HEX_GLOW,       /* soft hexagon light, 96x96                         */
    SPR_PIP_C, SPR_PIP_D, SPR_PIP_H, SPR_PIP_S,   /* suit pips 48x48, in card colours */
    SPR_PIP_NEON_C, SPR_PIP_NEON_D, SPR_PIP_NEON_H, SPR_PIP_NEON_S,   /* white outline pips with glow, 64x64 */
    SPR_COUNT
} SpriteId;

typedef enum {
    NINE_RRECT,         /* filled rounded rectangle (radius 14 at corner 16)   */
    NINE_RRECT_LINE,    /* its 3 px outline                                    */
    NINE_GLOW,          /* glow around a rounded rectangle; corner 40, edge at 24 in */
    NINE_SHADOW,        /* soft filled shadow; corner 44, edge at 24 in         */
    NINE_PANEL,         /* glass panel: vertical sheen and a lit top edge       */
    NINE_COUNT
} NineId;

void sprites_declare(void);
int  sprites_job_count(void);
void sprites_paint_job(int i);
void sprites_finish(void);

const Spr  *sprite(SpriteId id);
const Nine *sprite_nine(NineId id);

#endif
