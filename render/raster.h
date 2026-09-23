/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* raster.h - the CPU vector rasteriser that draws every generated image at
 * start-up (cards, sprites, glows, the background and the side art).
 *
 * Shapes are signed distance functions (SDFs) evaluated per pixel over the
 * shape's bounding box. Coverage comes from the distance itself
 * (alpha = clamp(0.5 - d, 0, 1) with d in pixels), which gives the same
 * edge quality as 4x4 supersampling at a sixteenth of the work, and stays
 * exact under rotation and scale. Everything is painted with the
 * premultiplied "over" operator into RGBA8 canvases, which may be views into
 * a larger atlas page (stride), so many threads can paint disjoint parts of
 * one page at once. Nothing here touches raylib or the GPU.
 *
 * Coordinates: canvas pixels, y down, pixel centres at +0.5. A shape is
 * described in its own local units and placed with an affine transform. */
#ifndef BPL_RENDER_RASTER_H
#define BPL_RENDER_RASTER_H

#include <stdint.h>

typedef struct { float r, g, b, a; } RCol;          /* straight alpha, 0..1 */

typedef struct {
    uint8_t *px;         /* RGBA8, premultiplied alpha */
    int      w, h;
    int      stride;     /* in pixels */
} Canvas;

/* local -> canvas: X = ax*x + bx*y + tx, Y = ay*x + by*y + ty */
typedef struct { float ax, bx, tx, ay, by, ty; } Xf;

Xf   xf_identity(void);
Xf   xf_make(float x, float y, float scale, float rot_rad);   /* scale, rotate, then translate */
Xf   xf_scale2(float x, float y, float sx, float sy, float rot_rad);
Xf   xf_mul(Xf outer, Xf inner);                               /* outer(inner(p)) */
Xf   xf_rot180_about(float cx, float cy);                      /* point reflection */

typedef enum {
    SH_CIRCLE,      /* p0,p1 centre, p2 radius                                  */
    SH_ELLIPSE,     /* p0,p1 centre, p2,p3 radii                                */
    SH_RBOX,        /* p0,p1 centre, p2,p3 half size, p4 corner radius          */
    SH_POLY,        /* pts[npts]                                                */
    SH_SEG,         /* p0,p1 -> p2,p3, p4 half width (a capsule)                */
    SH_RING,        /* p0,p1 centre, p2 radius, p3 half width                   */
    SH_HEART,       /* p0,p1 centre, p2 size (height ~2*size), point down; p3 != 0: point up */
    SH_HEX,         /* p0,p1 centre, p2 radius (flat top when p3 = 0, pointy when 1) */
    SH_ARC,         /* p0,p1 centre, p2 radius, p3 half width, p4 mid angle, p5 half aperture */
    SH_TRI          /* p0..p5 the three corners                                 */
} ShapeType;

typedef enum {
    OP_UNION,       /* min(d, s)                          */
    OP_SUB,         /* max(d, -s): cut this shape away   */
    OP_INTER,       /* max(d, s)                          */
    OP_SMOOTH       /* smooth union, p7 = blend radius    */
} ShapeOp;

typedef struct {
    uint8_t type, op;
    float   p[8];
    const float *pts;    /* SH_POLY: x0,y0,x1,y1,... */
    int     npts;
} Shape;

/* Distance in local units to the union/cut chain of n shapes (first op ignored). */
float shape_sdf(const Shape *s, int n, float x, float y);

typedef enum { PAINT_SOLID, PAINT_LINEAR, PAINT_RADIAL, PAINT_FN } PaintType;

typedef RCol (*PaintFn)(float lx, float ly, float d_px, const void *user);

typedef struct {
    uint8_t type;
    RCol    c0, c1;
    float   x0, y0, x1, y1;   /* LINEAR: c0 at (x0,y0) .. c1 at (x1,y1); RADIAL: centre x0,y0, radius x1 */
    PaintFn fn;               /* FN: colour at a local point (d_px = pixel distance to the edge) */
    const void *user;
} Paint;

Paint paint_solid(RCol c);
Paint paint_linear(RCol c0, float x0, float y0, RCol c1, float x1, float y1);
Paint paint_radial(RCol c0, float cx, float cy, RCol c1, float r);
Paint paint_fn(PaintFn fn, const void *user);

typedef enum { BL_OVER, BL_ADD, BL_ERASE } BlendOp;

typedef struct {
    float   outline;     /* > 0: stroke of this width (pixels) centred on the edge instead of a fill */
    float   offset;      /* grow (+) or shrink (-) the shape by this many pixels                   */
    float   feather;     /* extra edge softness in pixels (0 = crisp)                               */
    float   glow;        /* > 0: an exponential halo of this falloff (pixels) outside the shape      */
    float   opacity;     /* multiplies everything; 0 is treated as 1                                */
    uint8_t blend;       /* BlendOp */
} FillOpt;

/* Paints the shape chain. opt may be NULL (crisp fill, over). */
void cv_fill(Canvas *cv, const Xf *xf, const Shape *s, int n, const Paint *paint, const FillOpt *opt);

/* A rectangle of solid paint with no shape (fast path for backgrounds). */
void cv_clear(Canvas *cv, RCol c);
void cv_fill_rect(Canvas *cv, int x, int y, int w, int h, const Paint *paint);

/* Paints an 8-bit coverage bitmap (a font glyph) through xf (which maps the
 * bitmap's pixel space to the canvas), bilinear, tinted by paint. */
void cv_blit_alpha(Canvas *cv, const Xf *xf, const uint8_t *alpha, int aw, int ah, const Paint *paint,
                   float opacity);

/* Separable box blur, 3 passes (close to a gaussian of sigma ~ radius),
 * on all four channels, zero outside the canvas. Allocates a float copy of
 * the canvas for the duration (start-up only). */
void cv_blur(Canvas *cv, int radius);

/* 2x2 box downsample of src into dst (dst is src/2). */
void cv_downsample2(const Canvas *src, Canvas *dst);

/* Colour helpers. */
RCol rc(float r, float g, float b, float a);
RCol rc_hex(uint32_t rgb, float a);            /* 0xRRGGBB */
RCol rc_mix(RCol a, RCol b, float t);
RCol rc_scale(RCol a, float k);                /* rgb * k */

/* Deterministic hash noise for procedural texture (brushed metal, grain). */
float rnoise_hash(int x, int y, uint32_t seed);          /* 0..1 */
float rnoise_value(float x, float y, uint32_t seed);     /* smooth 0..1 */

/* Honeycomb helper: distance (local units) from p to the nearest hex cell
 * edge, for a hex grid of cell radius r; also returns the cell id. */
float hex_grid_dist(float x, float y, float r, int *cell_q, int *cell_r);

#endif
