/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* render.h - the renderer: start-up generation and the frame wrapper.
 *
 * Presentation code includes this one header and gets the whole kit:
 *   gfx.h        batched quads from the atlas (premultiplied: gfx_col / gfx_add)
 *   text.h       baked fonts, neon and gold text
 *   cards.h      the procedural deck, CardPose drawing, deal/flip motion
 *   particles.h  pooled coins, sparks, droplets, confetti, fireworks
 *   tween.h      easing, tweens, springs, arcs
 *   fx.h         shake, hit-pause, slow motion, flashes, cosmetic Rng
 *   ui.h         background, panels, buttons, marquee, bulbs, meters, banners
 *   celebrate.h  win-tier celebrations
 *   post.h       bloom
 *
 * A present_draw looks like this (see rendertest.c for a full one):
 *
 *     render_begin();                         // bloom target + premultiplied blending
 *     ui_background(time, celeb_dim(&cel));
 *     gfx_push_offset(shake dx, dy, deg, 640, 360);
 *     gfx_set_tint(dim, dim, dim);            // the game layer, dimmed under a jackpot
 *     ... cards, paytable, buttons ...
 *     gfx_set_tint(1, 1, 1);
 *     celeb_draw_back(&cel, time);            // spotlights
 *     pfx_draw();
 *     celeb_draw_front(&cel, time);           // banner, meter, flash
 *     gfx_pop_offset();
 *     bulbs_draw(&bulbs); marquee_draw(&mq, 640, 70, 0.9f);
 *     render_end();                           // bloom passes + composite
 *
 * render_init() is idempotent: every mode that presents calls it from its
 * init; the work happens once. */
#ifndef BPL_RENDER_RENDER_H
#define BPL_RENDER_RENDER_H

#include <stddef.h>
#include <stdint.h>

#include "render/atlas.h"
#include "render/cards.h"
#include "render/celebrate.h"
#include "render/fx.h"
#include "render/gfx.h"
#include "render/particles.h"
#include "render/post.h"
#include "render/sprites.h"
#include "render/text.h"
#include "render/tween.h"
#include "render/ui.h"

typedef struct {
    double total_ms;        /* render_init wall time                          */
    double load_ms;         /* reading the TTF files                          */
    double raster_ms;       /* glyph rasterisation (parallel)                  */
    double paint_ms;        /* cards, sprites, glyph copies, backgrounds (parallel) */
    double upload_ms;       /* texture uploads                                 */
    double post_ms;         /* shader compiles and render targets              */
    int    threads;
    int    max_tex;         /* GL_MAX_TEXTURE_SIZE                             */
    int    pages, page_w, page_h, items;
    size_t atlas_bytes;
    int    fonts_ok;
    int    bloom_ok;
} RenderStats;

int  render_init(uint64_t seed);        /* 0 ok; safe to call more than once */
void render_shutdown(void);
const RenderStats *render_stats(void);

/* assets/<dir>/<file>, looked up next to the working directory, then
 * relative to the executable (the build directory -> ../../assets). */
void render_asset_path(char *out, size_t n, const char *dir, const char *file);

void render_begin(void);
void render_end(void);

#endif
