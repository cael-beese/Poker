/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* atlas.h - the texture atlas every generated image lives in.
 *
 * Building happens once at start-up, in phases:
 *   1. atlas_declare(w, h) for every image (sizes known up front);
 *   2. atlas_pack(): shelf-packs them into as few pages as possible (one
 *      4096-wide page when the GPU allows it) and allocates the CPU pages;
 *   3. painters fill atlas_canvas(id) - any thread, regions never overlap;
 *   4. atlas_upload(): one texture per page through texreg, CPU copies freed.
 * Afterwards atlas_spr(id) gives the sprite (page + UVs) to draw with gfx.h.
 * Images are premultiplied RGBA with 2 px of transparent padding around each,
 * so bilinear sampling never bleeds a neighbour in. */
#ifndef BPL_RENDER_ATLAS_H
#define BPL_RENDER_ATLAS_H

#include <stddef.h>
#include "render/raster.h"

#define ATLAS_MAX_ITEMS 2600
#define ATLAS_MAX_PAGES 4
#define ATLAS_PAD 2

typedef struct {
    float u0, v0, u1, v1;    /* texture coordinates of the image */
    float w, h;              /* size in pixels                   */
    unsigned tex;            /* GL texture id of its page         */
} Spr;

void   atlas_reset(void);
int    atlas_declare(int w, int h);           /* -> id, or -1 when full */
int    atlas_pack(int max_tex_size);          /* 0 ok */
Canvas atlas_canvas(int id);
void   atlas_upload(void);                    /* main thread, GL */
void   atlas_shutdown(void);

Spr    atlas_spr(int id);
/* A sprite for a sub-rectangle of an image (e.g. one cell of a strip), and a
 * sprite shrunk to the centre of a pixel (for stretching a flat colour). */
Spr    atlas_sub(int id, float x, float y, float w, float h);
Spr    atlas_texel(int id, float x, float y);

int    atlas_pages(void);
void   atlas_page_size(int page, int *w, int *h);
size_t atlas_bytes(void);
int    atlas_items(void);

#endif
