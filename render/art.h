/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* art.h - the game's shared vector motifs, painted with raster.h at start-up:
 * the four suit pips, the bee mascot, brushed brass and honeycomb fills, and
 * the house palette. All original designs. */
#ifndef BPL_RENDER_ART_H
#define BPL_RENDER_ART_H

#include "render/raster.h"

/* House palette ("neon honey lounge"), straight alpha. */
#define ART_CHARCOAL  0x0E0C10
#define ART_FELT      0x1A1320
#define ART_PLUM      0x2A0F2E
#define ART_HONEY     0xE8AA28
#define ART_GOLD      0xFFD25A
#define ART_AMBER     0xFF8010
#define ART_MAGENTA   0xFF28C8
#define ART_CYAN      0x28E6FF
#define ART_IVORY     0xF8F1E2
#define ART_RUBY      0xD01A48
#define ART_INK       0x1E1A24
#define ART_BROWN     0x2B1D14

/* Suit pip, point-symmetric unit design (half height 1), painted centred at
 * (cx, cy) with half height `size` px, rotated by rot. top/bottom = gradient. */
void art_pip(Canvas *cv, int suit, float cx, float cy, float size, float rot, RCol top, RCol bottom,
             const FillOpt *opt);
/* The suit's ink colours (card red / card black), top and bottom of the gradient. */
void art_suit_colors(int suit, RCol *top, RCol *bottom);

/* The bee mascot, facing left, about 2 units wide; scale = px per unit.
 * alpha fades it; dark_outline adds the sticker-style outline. */
void art_bee(Canvas *cv, float cx, float cy, float scale, float rot, float alpha, int dark_outline);

/* Paints for cv_fill. */
typedef struct { float angle; uint32_t seed; float light; } BrassParams;   /* brushed brass */
RCol art_brass(float lx, float ly, float d_px, const void *user);

typedef struct { float r; RCol base, line, cell; float line_w; uint32_t seed; float cell_prob; } HoneyParams;
RCol art_honeycomb(float lx, float ly, float d_px, const void *user);

#endif
