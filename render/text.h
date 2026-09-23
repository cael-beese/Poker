/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* text.h - fonts rasterised into the atlas at start-up, and text drawing.
 *
 * Four OFL fonts (assets/fonts, see CREDITS.md), each baked at the sizes the
 * game uses, as bitmaps in the shared atlas: text is quads from the same
 * texture as every sprite, so it batches with them (raylib's DrawText uses a
 * texture of its own per font, which splits the batch at every string).
 * Display faces also get a glow set: each glyph at half resolution, padded
 * and blurred, drawn additively under the glyph - neon without a bloom pass.
 *
 * Drawing at another size scales the nearest baked size (bilinear). Only
 * printable ASCII is baked; Bungee has capitals only and maps a-z to A-Z. */
#ifndef BPL_RENDER_TEXT_H
#define BPL_RENDER_TEXT_H

#include "raylib.h"
#include "render/gfx.h"

typedef enum {
    FONT_UI_S,      /* Barlow Condensed SemiBold 20 - small labels          */
    FONT_UI_M,      /* Barlow Condensed SemiBold 28 - paytable, UI           */
    FONT_UI_L,      /* Barlow Condensed SemiBold 44 - large UI               */
    FONT_DISP_S,    /* Bungee 26 - buttons, meters                           */
    FONT_DISP_M,    /* Bungee 52 - banners, credit meters          (glow)    */
    FONT_DISP_L,    /* Bungee 104 - JACKPOT / BIG WIN               (glow)    */
    FONT_NEON_M,    /* Tilt Neon 36 - neon labels                   (glow)    */
    FONT_NEON_L,    /* Tilt Neon 64 - neon signs                    (glow)    */
    FONT_SCRIPT,    /* Neonderthaw 84 - the marquee's script        (glow)    */
    FONT_COUNT
} FontId;

typedef enum { ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT } TextAlign;

/* Optional per-glyph animation: called for every visible glyph with its
 * index in the string; may move, scale, fade or recolour it. */
typedef void (*GlyphFn)(int i, float *dx, float *dy, float *scale, float *alpha, Color *tint, void *user);

typedef struct {
    Color     color;          /* face colour                                     */
    Color     color2;         /* bottom colour for a vertical gradient (a = 0: none) */
    Color     glow;           /* neon glow colour (needs a glow font)             */
    float     glow_k;         /* glow intensity, 0 = none (1 = strong)            */
    Color     shadow;         /* soft dark halo behind (glow font) or offset copy */
    float     shadow_k;       /* 0 = none                                         */
    Color     outline;        /* crisp outline (8 offset copies)                  */
    float     outline_px;     /* 0 = none                                         */
    float     spacing;        /* extra px between glyphs                           */
    TextAlign align;
    float     opacity;        /* 0 treated as 1                                   */
    GlyphFn   fn;
    void     *user;
} TextStyle;

/* Start-up: declared by render_init (see render.c). */
int  text_load(void);                /* reads the TTFs; 0 ok                  */
void text_raster_job(int i);         /* i in 0..FONT_COUNT-1, any thread       */
void text_declare(void);             /* atlas items, after all raster jobs     */
void text_paint_job(int i);          /* paints font i into the atlas            */
void text_finish(void);              /* after atlas_upload: sprites; frees CPU  */
void text_shutdown(void);
int  text_have_font(FontId f);       /* 0 when its TTF was missing (raylib's default font is used) */

/* Card index glyphs for the card painter: an 8-bit coverage bitmap of one
 * character of Bungee at a pixel size baked for cards. */
typedef struct { const unsigned char *px; int w, h; float ox, oy, adv; } GlyphBitmap;
int  text_card_glyph(int size_class, char ch, GlyphBitmap *out);   /* size_class 0 = large, 1 = medium */

float text_width(FontId f, const char *s, float size, float spacing);
float text_line_height(FontId f, float size);
float text_base_size(FontId f);

/* x,y = top-left of the line box (or its centre/right edge by align). */
void text_draw(FontId f, const char *s, float x, float y, float size, Color c);
void text_draw_ex(FontId f, const char *s, float x, float y, float size, const TextStyle *st);
/* Neon tube text centred at (cx, cy): glow + pastel core. power 0..1 dims
 * the whole sign (flicker), glow_k scales the halo. */
void text_neon(FontId f, const char *s, float cx, float cy, float size, Color tube, float power, float glow_k);
/* Gold-leaf banner text centred at (cx, cy): dark halo, gradient face, highlight. */
void text_gold(FontId f, const char *s, float cx, float cy, float size, float opacity, float glow_k);

#endif
