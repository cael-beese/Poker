/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* gfx.h - batched sprite drawing for everything the renderer shows.
 *
 * Why not raylib's DrawTexturePro / DrawRectangle / DrawText: each of those
 * picks its own texture (the shapes texture, the font texture) and primitive
 * mode, and every change of either is a new GL draw call. Here everything is
 * a textured quad from the atlas, so a whole frame is a handful of draw
 * calls (see render_stats()).
 *
 * The atlas is premultiplied, and drawing runs with premultiplied blending
 * (ONE, ONE_MINUS_SRC_ALPHA) between gfx_begin() and gfx_end(). That makes
 * additive light and ordinary alpha the same blend mode: a colour with
 * alpha 0 adds (glows, sparks, neon), alpha 1 covers. So:
 *   gfx_col(c)          an ordinary colour, c.a = opacity (premultiplied here)
 *   gfx_add(c, k)       additive light of colour c at intensity k
 * and never switch blend modes to mix the two.
 *
 * A global tint (gfx_set_tint) multiplies every colour: the celebration uses
 * it to dim the game layer under a jackpot without an extra full-screen pass.
 * Colours passed in are straight (non-premultiplied) raylib Colors. */
#ifndef BPL_RENDER_GFX_H
#define BPL_RENDER_GFX_H

#include "raylib.h"
#include "render/atlas.h"

/* Premultiplied colours (what the quad functions take). */
typedef struct { unsigned char r, g, b, a; } PCol;

PCol gfx_col(Color c);                      /* straight -> premultiplied        */
PCol gfx_cola(Color c, float alpha);        /* with opacity scaled by alpha     */
PCol gfx_add(Color c, float intensity);     /* additive light (alpha 0)         */
PCol gfx_mixp(PCol a, PCol b, float t);

void gfx_begin(void);                       /* premultiplied blending, tint 1   */
void gfx_end(void);                         /* flush, back to raylib's alpha    */
void gfx_set_tint(float r, float g, float b);
void gfx_flush(void);

/* Quads. Corners in order top-left, bottom-left, bottom-right, top-right. */
void gfx_quad(const Spr *s, const Vector2 p[4], PCol c);
void gfx_quad4(const Spr *s, const Vector2 p[4], const PCol c[4]);
/* Axis-aligned. */
void gfx_spr(const Spr *s, float x, float y, float w, float h, PCol c);
/* Centred, rotated (radians), scaled independently. */
void gfx_spr_rot(const Spr *s, float cx, float cy, float w, float h, float rot, PCol c);
/* Vertical gradient, top and bottom colours. */
void gfx_spr_vgrad(const Spr *s, float x, float y, float w, float h, PCol top, PCol bottom);
/* A stretched streak from a to b, width w (sparks, beams). */
void gfx_streak(const Spr *s, Vector2 a, Vector2 b, float w, PCol c);

/* Solid rectangles through the atlas' white texel. */
void gfx_rect(float x, float y, float w, float h, PCol c);
void gfx_rect_vgrad(float x, float y, float w, float h, PCol top, PCol bottom);
void gfx_rect_hgrad(float x, float y, float w, float h, PCol left, PCol right);

/* Nine-slice: a sprite whose corners of size `corner` px stay unscaled. */
typedef struct { Spr s; float corner; int hollow; } Nine;
/* hollow = the image's centre is empty (outlines, glows) or always covered
 * (shadows under their object): its centre cell is not drawn, which saves
 * the fill of the whole interior - on the Pi's fill-bound GPU that matters. */
void gfx_nine(const Nine *n, float x, float y, float w, float h, float corner_px, PCol c);
/* The same with a vertical gradient from top to bottom colour. */
void gfx_nine_vgrad(const Nine *n, float x, float y, float w, float h, float corner_px, PCol top, PCol bottom);

/* The white texel, set by the renderer after the atlas upload. */
void gfx_set_white(Spr white);
const Spr *gfx_white(void);

/* Pixels covered by quads since the last take (fill-rate accounting: the
 * Pi's GPU costs ~2.2 ms per million blended pixels). Passes drawn outside
 * gfx (bloom) add their share with gfx_fill_add. */
double gfx_fill_take(void);
void   gfx_fill_add(double px);

/* Screen-space offset for everything drawn until popped (screen shake);
 * a matrix on rlgl's stack, so it does not break the batch. */
void gfx_push_offset(float dx, float dy, float rot_deg, float cx, float cy);
void gfx_pop_offset(void);

#endif
