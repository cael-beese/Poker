/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* raster.c - see raster.h. */
#include "render/raster.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---- transforms --------------------------------------------------------- */

Xf xf_identity(void) { return (Xf){ 1, 0, 0, 0, 1, 0 }; }

Xf xf_make(float x, float y, float scale, float rot)
{
    return xf_scale2(x, y, scale, scale, rot);
}

Xf xf_scale2(float x, float y, float sx, float sy, float rot)
{
    float c = cosf(rot), s = sinf(rot);
    return (Xf){ c * sx, -s * sy, x, s * sx, c * sy, y };
}

Xf xf_mul(Xf o, Xf i)
{
    Xf r;
    r.ax = o.ax * i.ax + o.bx * i.ay;
    r.bx = o.ax * i.bx + o.bx * i.by;
    r.tx = o.ax * i.tx + o.bx * i.ty + o.tx;
    r.ay = o.ay * i.ax + o.by * i.ay;
    r.by = o.ay * i.bx + o.by * i.by;
    r.ty = o.ay * i.tx + o.by * i.ty + o.ty;
    return r;
}

Xf xf_rot180_about(float cx, float cy) { return (Xf){ -1, 0, 2 * cx, 0, -1, 2 * cy }; }

static int xf_invert(const Xf *m, Xf *out, float *scale)
{
    float det = m->ax * m->by - m->bx * m->ay;
    if (fabsf(det) < 1e-12f) return -1;
    float id = 1.0f / det;
    out->ax = m->by * id;
    out->bx = -m->bx * id;
    out->ay = -m->ay * id;
    out->by = m->ax * id;
    out->tx = -(out->ax * m->tx + out->bx * m->ty);
    out->ty = -(out->ay * m->tx + out->by * m->ty);
    *scale = sqrtf(fabsf(det));
    return 0;
}

/* ---- colours and noise -------------------------------------------------- */

RCol rc(float r, float g, float b, float a) { return (RCol){ r, g, b, a }; }

RCol rc_hex(uint32_t rgb, float a)
{
    return (RCol){ ((rgb >> 16) & 255) / 255.0f, ((rgb >> 8) & 255) / 255.0f, (rgb & 255) / 255.0f, a };
}

RCol rc_mix(RCol a, RCol b, float t)
{
    return (RCol){ a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t };
}

RCol rc_scale(RCol a, float k) { return (RCol){ a.r * k, a.g * k, a.b * k, a.a }; }

static uint32_t hash3(uint32_t x, uint32_t y, uint32_t s)
{
    uint32_t h = x * 0x8da6b343u ^ y * 0xd8163841u ^ s * 0xcb1ab31fu;
    h ^= h >> 15; h *= 0x2c1b3c6du;
    h ^= h >> 12; h *= 0x297a2d39u;
    h ^= h >> 15;
    return h;
}

float rnoise_hash(int x, int y, uint32_t seed) { return (float)(hash3((uint32_t)x, (uint32_t)y, seed) & 0xffffff) / 16777215.0f; }

float rnoise_value(float x, float y, uint32_t seed)
{
    float fx = floorf(x), fy = floorf(y);
    int ix = (int)fx, iy = (int)fy;
    float tx = x - fx, ty = y - fy;
    tx = tx * tx * (3 - 2 * tx);
    ty = ty * ty * (3 - 2 * ty);
    float a = rnoise_hash(ix, iy, seed), b = rnoise_hash(ix + 1, iy, seed);
    float c = rnoise_hash(ix, iy + 1, seed), d = rnoise_hash(ix + 1, iy + 1, seed);
    return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * ty;
}

float hex_grid_dist(float x, float y, float r, int *cq, int *cr)
{
    /* Pointy-top hex grid: two offset rectangular lattices, take the nearer centre. */
    const float w = 1.7320508f * r, h = 3.0f * r;
    float ax = fmodf(x, w); if (ax < 0) ax += w;
    float ay = fmodf(y, h); if (ay < 0) ay += h;
    ax -= w * 0.5f; ay -= h * 0.5f;
    float bx = fmodf(x - w * 0.5f, w); if (bx < 0) bx += w;
    float by = fmodf(y - h * 0.5f, h); if (by < 0) by += h;
    bx -= w * 0.5f; by -= h * 0.5f;
    float gx, gy;
    int use_a = ax * ax + ay * ay < bx * bx + by * by;
    if (use_a) { gx = ax; gy = ay; } else { gx = bx; gy = by; }
    if (cq) {
        float cx = x - gx, cy = y - gy;
        *cq = (int)floorf(cx / (w * 0.5f) + 0.5f);
        *cr = (int)floorf(cy / (h * 0.5f) + 0.5f);
    }
    /* Distance to the hex edge (pointy top): inradius r*sqrt(3)/2. */
    float px = fabsf(gx), py = fabsf(gy);
    float d = fmaxf(px, px * 0.5f + py * 0.8660254f);
    return r * 0.8660254f - d;
}

/* ---- SDFs --------------------------------------------------------------- */

static float sd_poly(const float *v, int n, float px, float py)
{
    float dx = px - v[0], dy = py - v[1];
    float d = dx * dx + dy * dy, s = 1.0f;
    for (int i = 0, j = n - 1; i < n; j = i, i++) {
        float ex = v[2 * j] - v[2 * i], ey = v[2 * j + 1] - v[2 * i + 1];
        float wx = px - v[2 * i], wy = py - v[2 * i + 1];
        float t = (wx * ex + wy * ey) / (ex * ex + ey * ey);
        t = t < 0 ? 0 : t > 1 ? 1 : t;
        float bx = wx - ex * t, by = wy - ey * t;
        float dd = bx * bx + by * by;
        if (dd < d) d = dd;
        int c1 = py >= v[2 * i + 1], c2 = py < v[2 * j + 1], c3 = ex * wy > ey * wx;
        if ((c1 && c2 && c3) || (!c1 && !c2 && !c3)) s = -s;
    }
    return s * sqrtf(d);
}

static float sd_heart(float x, float y)
{
    /* Inigo Quilez's heart, y up, point at the origin, ~1.1 tall. */
    x = fabsf(x);
    if (y + x > 1.0f) {
        float dx = x - 0.25f, dy = y - 0.75f;
        return sqrtf(dx * dx + dy * dy) - 0.35355339f;
    }
    float ax = x, ay = y - 1.0f;
    float m = 0.5f * fmaxf(x + y, 0.0f);
    float bx = x - m, by = y - m;
    float d = fminf(ax * ax + ay * ay, bx * bx + by * by);
    return sqrtf(d) * (x - y > 0 ? 1.0f : -1.0f);
}

static float sd_one(const Shape *s, float x, float y)
{
    const float *p = s->p;
    switch (s->type) {
    case SH_CIRCLE: {
        float dx = x - p[0], dy = y - p[1];
        return sqrtf(dx * dx + dy * dy) - p[2];
    }
    case SH_ELLIPSE: {
        /* Not exact: scaled distance, good near the edge, which is what AA needs. */
        float dx = (x - p[0]) / p[2], dy = (y - p[1]) / p[3];
        float k = sqrtf(dx * dx + dy * dy);
        if (k < 1e-6f) return -fminf(p[2], p[3]);
        /* Gradient-corrected: (k-1)/|grad k| */
        float gx = dx / (p[2] * k), gy = dy / (p[3] * k);
        return (k - 1.0f) / sqrtf(gx * gx + gy * gy);
    }
    case SH_RBOX: {
        float qx = fabsf(x - p[0]) - p[2] + p[4], qy = fabsf(y - p[1]) - p[3] + p[4];
        float mx = fmaxf(qx, 0), my = fmaxf(qy, 0);
        return sqrtf(mx * mx + my * my) + fminf(fmaxf(qx, qy), 0.0f) - p[4];
    }
    case SH_POLY: return sd_poly(s->pts, s->npts, x, y);
    case SH_TRI: return sd_poly(p, 3, x, y);
    case SH_SEG: {
        float ax = x - p[0], ay = y - p[1], bx = p[2] - p[0], by = p[3] - p[1];
        float l = bx * bx + by * by;
        float t = l > 0 ? (ax * bx + ay * by) / l : 0;
        t = t < 0 ? 0 : t > 1 ? 1 : t;
        float dx = ax - bx * t, dy = ay - by * t;
        return sqrtf(dx * dx + dy * dy) - p[4];
    }
    case SH_RING: {
        float dx = x - p[0], dy = y - p[1];
        return fabsf(sqrtf(dx * dx + dy * dy) - p[2]) - p[3];
    }
    case SH_HEART: {
        /* Local: centre p0,p1, total height ~2*size, point down (canvas y down). */
        float k = p[2] / 0.55f;
        float lx = (x - p[0]) / k, ly = (p[3] != 0 ? (y - p[1]) : -(y - p[1])) / k + 0.52f;
        return sd_heart(lx, ly) * k;
    }
    case SH_HEX: {
        /* Inigo Quilez's hexagon: flat top and bottom, corners left and right;
         * p2 is the corner radius, the edges sit at the inradius. */
        float qx = fabsf(x - p[0]), qy = fabsf(y - p[1]);
        if (p[3] != 0) { float t = qx; qx = qy; qy = t; }
        const float kx = -0.8660254f, ky = 0.5f, kz = 0.57735027f;
        float r = p[2] * 0.8660254f;
        float dot = kx * qx + ky * qy;
        if (dot < 0) { qx -= 2 * dot * kx; qy -= 2 * dot * ky; }
        float cl = qx < -kz * r ? -kz * r : qx > kz * r ? kz * r : qx;
        qx -= cl; qy -= r;
        return sqrtf(qx * qx + qy * qy) * (qy > 0 ? 1.0f : -1.0f);
    }
    case SH_ARC: {
        /* Ring segment around angle p4 (radians, 0 = +x, y down), half aperture p5. */
        float dx = x - p[0], dy = y - p[1];
        float a = atan2f(dy, dx) - p[4];
        while (a > 3.14159265f) a -= 6.2831853f;
        while (a < -3.14159265f) a += 6.2831853f;
        if (fabsf(a) <= p[5]) return fabsf(sqrtf(dx * dx + dy * dy) - p[2]) - p[3];
        float ea = p[4] + (a > 0 ? p[5] : -p[5]);
        float ex = p[0] + cosf(ea) * p[2], ey = p[1] + sinf(ea) * p[2];
        float ddx = x - ex, ddy = y - ey;
        return sqrtf(ddx * ddx + ddy * ddy) - p[3];
    }
    }
    return 1e9f;
}

float shape_sdf(const Shape *s, int n, float x, float y)
{
    float d = sd_one(&s[0], x, y);
    for (int i = 1; i < n; i++) {
        float e = sd_one(&s[i], x, y);
        switch (s[i].op) {
        case OP_SUB: d = fmaxf(d, -e); break;
        case OP_INTER: d = fmaxf(d, e); break;
        case OP_SMOOTH: {
            float k = s[i].p[7] > 0 ? s[i].p[7] : 0.1f;
            float h = 0.5f + 0.5f * (e - d) / k;
            h = h < 0 ? 0 : h > 1 ? 1 : h;
            d = e + (d - e) * h - k * h * (1 - h);
            break;
        }
        default: d = fminf(d, e); break;
        }
    }
    return d;
}

/* Conservative local bounding box of the chain (union of the non-cut shapes). */
static void shape_bounds(const Shape *s, int n, float *x0, float *y0, float *x1, float *y1)
{
    float a = 1e9f, b = 1e9f, c = -1e9f, d = -1e9f;
    for (int i = 0; i < n; i++) {
        if (i > 0 && (s[i].op == OP_SUB || s[i].op == OP_INTER)) continue;
        const float *p = s[i].p;
        float l, t, r, bt;
        switch (s[i].type) {
        case SH_CIRCLE: l = p[0] - p[2]; r = p[0] + p[2]; t = p[1] - p[2]; bt = p[1] + p[2]; break;
        case SH_ELLIPSE: l = p[0] - p[2]; r = p[0] + p[2]; t = p[1] - p[3]; bt = p[1] + p[3]; break;
        case SH_RBOX: l = p[0] - p[2]; r = p[0] + p[2]; t = p[1] - p[3]; bt = p[1] + p[3]; break;
        case SH_RING: case SH_ARC:
            l = p[0] - p[2] - p[3]; r = p[0] + p[2] + p[3]; t = p[1] - p[2] - p[3]; bt = p[1] + p[2] + p[3]; break;
        case SH_HEART: l = p[0] - p[2] * 1.3f; r = p[0] + p[2] * 1.3f; t = p[1] - p[2] * 1.3f; bt = p[1] + p[2] * 1.3f; break;
        case SH_HEX: l = p[0] - p[2]; r = p[0] + p[2]; t = p[1] - p[2]; bt = p[1] + p[2]; break;
        case SH_SEG:
            l = fminf(p[0], p[2]) - p[4]; r = fmaxf(p[0], p[2]) + p[4];
            t = fminf(p[1], p[3]) - p[4]; bt = fmaxf(p[1], p[3]) + p[4]; break;
        case SH_TRI:
            l = fminf(p[0], fminf(p[2], p[4])); r = fmaxf(p[0], fmaxf(p[2], p[4]));
            t = fminf(p[1], fminf(p[3], p[5])); bt = fmaxf(p[1], fmaxf(p[3], p[5])); break;
        default: {
            l = t = 1e9f; r = bt = -1e9f;
            for (int k = 0; k < s[i].npts; k++) {
                l = fminf(l, s[i].pts[2 * k]); r = fmaxf(r, s[i].pts[2 * k]);
                t = fminf(t, s[i].pts[2 * k + 1]); bt = fmaxf(bt, s[i].pts[2 * k + 1]);
            }
        }
        }
        if (s[i].op == OP_SMOOTH) { l -= p[7]; t -= p[7]; r += p[7]; bt += p[7]; }
        a = fminf(a, l); b = fminf(b, t); c = fmaxf(c, r); d = fmaxf(d, bt);
    }
    *x0 = a; *y0 = b; *x1 = c; *y1 = d;
}

/* ---- painting ----------------------------------------------------------- */

Paint paint_solid(RCol c) { Paint p; memset(&p, 0, sizeof p); p.type = PAINT_SOLID; p.c0 = c; return p; }

Paint paint_linear(RCol c0, float x0, float y0, RCol c1, float x1, float y1)
{
    Paint p; memset(&p, 0, sizeof p);
    p.type = PAINT_LINEAR; p.c0 = c0; p.c1 = c1; p.x0 = x0; p.y0 = y0; p.x1 = x1; p.y1 = y1;
    return p;
}

Paint paint_radial(RCol c0, float cx, float cy, RCol c1, float r)
{
    Paint p; memset(&p, 0, sizeof p);
    p.type = PAINT_RADIAL; p.c0 = c0; p.c1 = c1; p.x0 = cx; p.y0 = cy; p.x1 = r;
    return p;
}

Paint paint_fn(PaintFn fn, const void *user)
{
    Paint p; memset(&p, 0, sizeof p);
    p.type = PAINT_FN; p.fn = fn; p.user = user;
    return p;
}

static inline RCol paint_at(const Paint *p, float lx, float ly, float d)
{
    switch (p->type) {
    case PAINT_LINEAR: {
        float dx = p->x1 - p->x0, dy = p->y1 - p->y0;
        float l = dx * dx + dy * dy;
        float t = l > 0 ? ((lx - p->x0) * dx + (ly - p->y0) * dy) / l : 0;
        t = t < 0 ? 0 : t > 1 ? 1 : t;
        return rc_mix(p->c0, p->c1, t);
    }
    case PAINT_RADIAL: {
        float dx = lx - p->x0, dy = ly - p->y0;
        float t = sqrtf(dx * dx + dy * dy) / (p->x1 > 0 ? p->x1 : 1);
        t = t > 1 ? 1 : t;
        return rc_mix(p->c0, p->c1, t);
    }
    case PAINT_FN: return p->fn(lx, ly, d, p->user);
    default: return p->c0;
    }
}

static inline void put(uint8_t *d, RCol c, float cov, int blend)
{
    float a = c.a * cov;
    if (a <= 0.0f && blend != BL_ADD) return;
    if (a > 1) a = 1;
    float sr = c.r * a, sg = c.g * a, sb = c.b * a;
    float dr = d[0] * (1.0f / 255), dg = d[1] * (1.0f / 255), db = d[2] * (1.0f / 255), da = d[3] * (1.0f / 255);
    float r, g, b, aa;
    if (blend == BL_ADD) {
        r = dr + sr; g = dg + sg; b = db + sb; aa = da + a * 0.0f;
        if (aa < fmaxf(r, fmaxf(g, b))) aa = fminf(1.0f, fmaxf(r, fmaxf(g, b)));
    } else if (blend == BL_ERASE) {
        r = dr * (1 - a); g = dg * (1 - a); b = db * (1 - a); aa = da * (1 - a);
    } else {
        r = sr + dr * (1 - a); g = sg + dg * (1 - a); b = sb + db * (1 - a); aa = a + da * (1 - a);
    }
    d[0] = (uint8_t)(fminf(r, 1.0f) * 255.0f + 0.5f);
    d[1] = (uint8_t)(fminf(g, 1.0f) * 255.0f + 0.5f);
    d[2] = (uint8_t)(fminf(b, 1.0f) * 255.0f + 0.5f);
    d[3] = (uint8_t)(fminf(aa, 1.0f) * 255.0f + 0.5f);
}

void cv_fill(Canvas *cv, const Xf *xfp, const Shape *s, int n, const Paint *paint, const FillOpt *opt)
{
    FillOpt o;
    if (opt) o = *opt; else memset(&o, 0, sizeof o);
    if (o.opacity <= 0) o.opacity = 1;
    Xf xf = xfp ? *xfp : xf_identity(), inv;
    float scale;
    if (xf_invert(&xf, &inv, &scale) != 0) return;

    float lx0, ly0, lx1, ly1;
    shape_bounds(s, n, &lx0, &ly0, &lx1, &ly1);
    /* Transform the local box's corners; grow by the edge reach in pixels. */
    float cx[4] = { lx0, lx1, lx1, lx0 }, cy[4] = { ly0, ly0, ly1, ly1 };
    float X0 = 1e9f, Y0 = 1e9f, X1 = -1e9f, Y1 = -1e9f;
    for (int i = 0; i < 4; i++) {
        float X = xf.ax * cx[i] + xf.bx * cy[i] + xf.tx, Y = xf.ay * cx[i] + xf.by * cy[i] + xf.ty;
        X0 = fminf(X0, X); X1 = fmaxf(X1, X); Y0 = fminf(Y0, Y); Y1 = fmaxf(Y1, Y);
    }
    float reach = 2.0f + fmaxf(o.offset, 0) + o.outline + o.feather + (o.glow > 0 ? o.glow * 6.0f : 0);
    int x0 = (int)floorf(X0 - reach), y0 = (int)floorf(Y0 - reach);
    int x1 = (int)ceilf(X1 + reach), y1 = (int)ceilf(Y1 + reach);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > cv->w) x1 = cv->w;
    if (y1 > cv->h) y1 = cv->h;

    float soft = 1.0f + o.feather;
    for (int y = y0; y < y1; y++) {
        uint8_t *row = cv->px + ((size_t)y * (size_t)cv->stride) * 4;
        float py = (float)y + 0.5f;
        for (int x = x0; x < x1; x++) {
            float px = (float)x + 0.5f;
            float lx = inv.ax * px + inv.bx * py + inv.tx, ly = inv.ay * px + inv.by * py + inv.ty;
            float d = shape_sdf(s, n, lx, ly) * scale - o.offset;
            float cov;
            if (o.outline > 0) d = fabsf(d) - o.outline * 0.5f;
            cov = 0.5f - d / soft;
            cov = cov < 0 ? 0 : cov > 1 ? 1 : cov;
            if (o.glow > 0 && d > 0) {
                float g = expf(-d / o.glow);
                if (g > cov) cov = g;
            }
            if (cov <= 0.0f) continue;
            RCol c = paint_at(paint, lx, ly, d);
            put(row + (size_t)x * 4, c, cov * o.opacity, o.blend);
        }
    }
}

void cv_clear(Canvas *cv, RCol c)
{
    uint8_t v[4] = { (uint8_t)(c.r * c.a * 255 + 0.5f), (uint8_t)(c.g * c.a * 255 + 0.5f),
                     (uint8_t)(c.b * c.a * 255 + 0.5f), (uint8_t)(c.a * 255 + 0.5f) };
    for (int y = 0; y < cv->h; y++) {
        uint8_t *row = cv->px + (size_t)y * (size_t)cv->stride * 4;
        for (int x = 0; x < cv->w; x++) memcpy(row + x * 4, v, 4);
    }
}

void cv_fill_rect(Canvas *cv, int x, int y, int w, int h, const Paint *paint)
{
    int x1 = x + w, y1 = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > cv->w) x1 = cv->w;
    if (y1 > cv->h) y1 = cv->h;
    for (int j = y; j < y1; j++) {
        uint8_t *row = cv->px + (size_t)j * (size_t)cv->stride * 4;
        for (int i = x; i < x1; i++) put(row + i * 4, paint_at(paint, i + 0.5f, j + 0.5f, 0), 1.0f, BL_OVER);
    }
}

void cv_blit_alpha(Canvas *cv, const Xf *xfp, const uint8_t *alpha, int aw, int ah, const Paint *paint,
                   float opacity)
{
    Xf inv;
    float scale;
    if (xf_invert(xfp, &inv, &scale) != 0) return;
    float cx[4] = { 0, (float)aw, (float)aw, 0 }, cy[4] = { 0, 0, (float)ah, (float)ah };
    float X0 = 1e9f, Y0 = 1e9f, X1 = -1e9f, Y1 = -1e9f;
    for (int i = 0; i < 4; i++) {
        float X = xfp->ax * cx[i] + xfp->bx * cy[i] + xfp->tx, Y = xfp->ay * cx[i] + xfp->by * cy[i] + xfp->ty;
        X0 = fminf(X0, X); X1 = fmaxf(X1, X); Y0 = fminf(Y0, Y); Y1 = fmaxf(Y1, Y);
    }
    int x0 = (int)floorf(X0) - 1, y0 = (int)floorf(Y0) - 1, x1 = (int)ceilf(X1) + 1, y1 = (int)ceilf(Y1) + 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > cv->w) x1 = cv->w;
    if (y1 > cv->h) y1 = cv->h;
    for (int y = y0; y < y1; y++) {
        uint8_t *row = cv->px + (size_t)y * (size_t)cv->stride * 4;
        for (int x = x0; x < x1; x++) {
            float px = x + 0.5f, py = y + 0.5f;
            float u = inv.ax * px + inv.bx * py + inv.tx - 0.5f, v = inv.ay * px + inv.by * py + inv.ty - 0.5f;
            int iu = (int)floorf(u), iv = (int)floorf(v);
            float fu = u - iu, fv = v - iv;
            float s = 0;
            for (int k = 0; k < 4; k++) {
                int su = iu + (k & 1), sv = iv + (k >> 1);
                if (su < 0 || sv < 0 || su >= aw || sv >= ah) continue;
                float w = ((k & 1) ? fu : 1 - fu) * ((k >> 1) ? fv : 1 - fv);
                s += w * alpha[sv * aw + su];
            }
            if (s <= 0.5f) continue;
            put(row + x * 4, paint_at(paint, u, v, 0), s / 255.0f * opacity, BL_OVER);
        }
    }
}

static void box_pass(float *buf, float *tmp, int w, int h, int r, int horizontal)
{
    int n = horizontal ? w : h, lines = horizontal ? h : w;
    float inv = 1.0f / (float)(2 * r + 1);
    for (int l = 0; l < lines; l++) {
        for (int ch = 0; ch < 4; ch++) {
            float acc = 0;
#define AT(i) buf[((horizontal ? (size_t)l * w + (i) : (size_t)(i) * w + l)) * 4 + ch]
            /* Zero outside the line: glows fade out at the canvas edge. */
            for (int i = 0; i <= r && i < n; i++) acc += AT(i);
            for (int i = 0; i < n; i++) {
                tmp[i] = acc * inv;
                int add = i + r + 1, sub = i - r;
                if (add < n) acc += AT(add);
                if (sub >= 0) acc -= AT(sub);
            }
            for (int i = 0; i < n; i++) AT(i) = tmp[i];
#undef AT
        }
    }
}

void cv_blur(Canvas *cv, int radius)
{
    if (radius < 1) return;
    int w = cv->w, h = cv->h;
    float *buf = malloc((size_t)w * h * 4 * sizeof(float));
    float *tmp = malloc((size_t)(w > h ? w : h) * sizeof(float));
    if (!buf || !tmp) { free(buf); free(tmp); return; }
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            for (int c = 0; c < 4; c++) buf[((size_t)y * w + x) * 4 + c] = cv->px[((size_t)y * cv->stride + x) * 4 + c];
    for (int pass = 0; pass < 3; pass++) {
        box_pass(buf, tmp, w, h, radius, 1);
        box_pass(buf, tmp, w, h, radius, 0);
    }
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            for (int c = 0; c < 4; c++) {
                float v = buf[((size_t)y * w + x) * 4 + c];
                cv->px[((size_t)y * cv->stride + x) * 4 + c] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v + 0.5f);
            }
    free(buf);
    free(tmp);
}

void cv_downsample2(const Canvas *src, Canvas *dst)
{
    for (int y = 0; y < dst->h; y++)
        for (int x = 0; x < dst->w; x++)
            for (int c = 0; c < 4; c++) {
                int sx = x * 2, sy = y * 2;
                int s = 0;
                for (int k = 0; k < 4; k++) {
                    int xx = sx + (k & 1), yy = sy + (k >> 1);
                    if (xx >= src->w) xx = src->w - 1;
                    if (yy >= src->h) yy = src->h - 1;
                    s += src->px[((size_t)yy * src->stride + xx) * 4 + c];
                }
                dst->px[((size_t)y * dst->stride + x) * 4 + c] = (uint8_t)((s + 2) / 4);
            }
}
