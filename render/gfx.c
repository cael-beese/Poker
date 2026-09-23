/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* gfx.c - see gfx.h. */
#include "render/gfx.h"

#include <math.h>

#include "rlgl.h"

static float g_tr = 1, g_tg = 1, g_tb = 1;
static Spr g_white;

static inline unsigned char u8(float v) { return (unsigned char)(v <= 0 ? 0 : v >= 255 ? 255 : v + 0.5f); }

PCol gfx_col(Color c)
{
    float a = c.a / 255.0f;
    return (PCol){ u8(c.r * a), u8(c.g * a), u8(c.b * a), c.a };
}

PCol gfx_cola(Color c, float alpha)
{
    if (alpha < 0) alpha = 0;
    if (alpha > 1) alpha = 1;
    float a = c.a / 255.0f * alpha;
    return (PCol){ u8(c.r * a), u8(c.g * a), u8(c.b * a), u8(a * 255.0f) };
}

PCol gfx_add(Color c, float k)
{
    if (k < 0) k = 0;
    return (PCol){ u8(c.r * k), u8(c.g * k), u8(c.b * k), 0 };
}

PCol gfx_mixp(PCol a, PCol b, float t)
{
    return (PCol){ u8(a.r + (b.r - a.r) * t), u8(a.g + (b.g - a.g) * t), u8(a.b + (b.b - a.b) * t),
                   u8(a.a + (b.a - a.a) * t) };
}

void gfx_begin(void)
{
    g_tr = g_tg = g_tb = 1;
    rlSetBlendMode(RL_BLEND_ALPHA_PREMULTIPLY);
}

void gfx_end(void)
{
    rlDrawRenderBatchActive();
    rlSetBlendMode(RL_BLEND_ALPHA);
}

void gfx_flush(void) { rlDrawRenderBatchActive(); }

void gfx_set_tint(float r, float g, float b)
{
    g_tr = r;
    g_tg = g;
    g_tb = b;
}

void gfx_set_white(Spr white) { g_white = white; }
const Spr *gfx_white(void) { return &g_white; }

static inline void vcol(PCol c)
{
    rlColor4ub(u8(c.r * g_tr), u8(c.g * g_tg), u8(c.b * g_tb), c.a);
}

void gfx_quad4(const Spr *s, const Vector2 p[4], const PCol c[4])
{
    rlCheckRenderBatchLimit(4);
    rlSetTexture(s->tex);
    rlBegin(RL_QUADS);
    vcol(c[0]); rlTexCoord2f(s->u0, s->v0); rlVertex2f(p[0].x, p[0].y);
    vcol(c[1]); rlTexCoord2f(s->u0, s->v1); rlVertex2f(p[1].x, p[1].y);
    vcol(c[2]); rlTexCoord2f(s->u1, s->v1); rlVertex2f(p[2].x, p[2].y);
    vcol(c[3]); rlTexCoord2f(s->u1, s->v0); rlVertex2f(p[3].x, p[3].y);
    rlEnd();
}

void gfx_quad(const Spr *s, const Vector2 p[4], PCol c)
{
    const PCol cc[4] = { c, c, c, c };
    gfx_quad4(s, p, cc);
}

void gfx_spr(const Spr *s, float x, float y, float w, float h, PCol c)
{
    const Vector2 p[4] = { { x, y }, { x, y + h }, { x + w, y + h }, { x + w, y } };
    gfx_quad(s, p, c);
}

void gfx_spr_vgrad(const Spr *s, float x, float y, float w, float h, PCol top, PCol bottom)
{
    const Vector2 p[4] = { { x, y }, { x, y + h }, { x + w, y + h }, { x + w, y } };
    const PCol c[4] = { top, bottom, bottom, top };
    gfx_quad4(s, p, c);
}

void gfx_spr_rot(const Spr *s, float cx, float cy, float w, float h, float rot, PCol c)
{
    float cs = cosf(rot), sn = sinf(rot), hx = w * 0.5f, hy = h * 0.5f;
    const Vector2 p[4] = {
        { cx + (-hx * cs - -hy * sn), cy + (-hx * sn + -hy * cs) },
        { cx + (-hx * cs - hy * sn), cy + (-hx * sn + hy * cs) },
        { cx + (hx * cs - hy * sn), cy + (hx * sn + hy * cs) },
        { cx + (hx * cs - -hy * sn), cy + (hx * sn + -hy * cs) },
    };
    gfx_quad(s, p, c);
}

void gfx_streak(const Spr *s, Vector2 a, Vector2 b, float w, PCol c)
{
    float dx = b.x - a.x, dy = b.y - a.y, l = sqrtf(dx * dx + dy * dy);
    if (l < 1e-3f) { dx = 1; dy = 0; l = 1; }
    float nx = -dy / l * w * 0.5f, ny = dx / l * w * 0.5f;
    const Vector2 p[4] = { { a.x + nx, a.y + ny }, { a.x - nx, a.y - ny }, { b.x - nx, b.y - ny }, { b.x + nx, b.y + ny } };
    gfx_quad(s, p, c);
}

void gfx_rect(float x, float y, float w, float h, PCol c) { gfx_spr(&g_white, x, y, w, h, c); }

void gfx_rect_vgrad(float x, float y, float w, float h, PCol top, PCol bottom)
{
    gfx_spr_vgrad(&g_white, x, y, w, h, top, bottom);
}

void gfx_rect_hgrad(float x, float y, float w, float h, PCol l, PCol r)
{
    const Vector2 p[4] = { { x, y }, { x, y + h }, { x + w, y + h }, { x + w, y } };
    const PCol c[4] = { l, l, r, r };
    gfx_quad4(&g_white, p, c);
}

void gfx_nine(const Nine *n, float x, float y, float w, float h, float cp, PCol c)
{
    gfx_nine_vgrad(n, x, y, w, h, cp, c, c);
}

void gfx_nine_vgrad(const Nine *n, float x, float y, float w, float h, float cp, PCol top, PCol bottom)
{
    const Spr *s = &n->s;
    float cu = n->corner / s->w * (s->u1 - s->u0), cv = n->corner / s->h * (s->v1 - s->v0);
    if (cp * 2 > w) cp = w * 0.5f;
    if (cp * 2 > h) cp = h * 0.5f;
    float xs[4] = { x, x + cp, x + w - cp, x + w }, ys[4] = { y, y + cp, y + h - cp, y + h };
    float us[4] = { s->u0, s->u0 + cu, s->u1 - cu, s->u1 }, vs[4] = { s->v0, s->v0 + cv, s->v1 - cv, s->v1 };
    for (int j = 0; j < 3; j++)
        for (int i = 0; i < 3; i++) {
            if (xs[i + 1] <= xs[i] || ys[j + 1] <= ys[j]) continue;
            if (n->hollow && i == 1 && j == 1) continue;
            Spr q = *s;
            q.u0 = us[i]; q.u1 = us[i + 1]; q.v0 = vs[j]; q.v1 = vs[j + 1];
            PCol ct = gfx_mixp(top, bottom, (ys[j] - y) / h), cb = gfx_mixp(top, bottom, (ys[j + 1] - y) / h);
            gfx_spr_vgrad(&q, xs[i], ys[j], xs[i + 1] - xs[i], ys[j + 1] - ys[j], ct, cb);
        }
}

void gfx_push_offset(float dx, float dy, float rot_deg, float cx, float cy)
{
    rlPushMatrix();
    rlTranslatef(cx + dx, cy + dy, 0);
    if (rot_deg != 0) rlRotatef(rot_deg, 0, 0, 1);
    rlTranslatef(-cx, -cy, 0);
}

void gfx_pop_offset(void) { rlPopMatrix(); }
