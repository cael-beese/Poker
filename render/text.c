/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* text.c - see text.h. */
#include "render/text.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "render/render.h"

#define NCHARS 95            /* ' ' .. '~' */
#define CARD_SIZES 2

static const struct {
    const char *file;
    int size, caps, glow;
} k_fonts[FONT_COUNT] = {
    { "BarlowCondensed-SemiBold.ttf", 20, 0, 0 },
    { "BarlowCondensed-SemiBold.ttf", 28, 0, 0 },
    { "BarlowCondensed-SemiBold.ttf", 44, 0, 0 },
    { "Bungee-Regular.ttf", 26, 1, 0 },
    { "Bungee-Regular.ttf", 52, 1, 1 },
    { "Bungee-Regular.ttf", 104, 1, 1 },
    { "TiltNeon.ttf", 36, 0, 1 },
    { "TiltNeon.ttf", 64, 0, 1 },
    { "Neonderthaw-Regular.ttf", 84, 0, 1 },
};
static const int k_card_size[CARD_SIZES] = { 42, 26 };
static const char k_card_chars[] = "A0123456789JQK";

#define NFILES 4
static const char *const k_files[NFILES] = {
    "BarlowCondensed-SemiBold.ttf", "Bungee-Regular.ttf", "TiltNeon.ttf", "Neonderthaw-Regular.ttf"
};
static unsigned char *g_file[NFILES];
static int g_file_len[NFILES];

typedef struct {
    Spr   s, gs;
    float ox, oy, w, h, adv;          /* face, in base-size pixels  */
    float gox, goy, gw, gh;           /* glow quad, base-size pixels */
    int   item, gitem;                /* atlas ids, -1 = none        */
} Glyph;

typedef struct {
    GlyphInfo *raw;                   /* from LoadFontData, freed after painting */
    int        nraw;
    Glyph      g[NCHARS];
    float      base;
    int        pad;                   /* glow padding, half-res px   */
    int        ok;
} BFont;

static BFont g_font[FONT_COUNT];
static GlyphInfo *g_card[CARD_SIZES];
static int g_cardn[CARD_SIZES];

static int file_index(const char *name)
{
    for (int i = 0; i < NFILES; i++)
        if (strcmp(k_files[i], name) == 0) return i;
    return -1;
}

int text_load(void)
{
    int missing = 0;
    for (int i = 0; i < NFILES; i++) {
        char path[512];
        render_asset_path(path, sizeof path, "fonts", k_files[i]);
        g_file[i] = LoadFileData(path, &g_file_len[i]);
        if (!g_file[i]) {
            fprintf(stderr, "render: font %s missing, using raylib's default font\n", path);
            missing++;
        }
    }
    return missing ? -1 : 0;
}

int text_have_font(FontId f) { return g_font[f].ok; }

void text_raster_job(int i)
{
    if (i < FONT_COUNT) {
        BFont *f = &g_font[i];
        int fi = file_index(k_fonts[i].file);
        f->base = (float)k_fonts[i].size;
        if (fi < 0 || !g_file[fi]) return;
        int cps[NCHARS];
        for (int c = 0; c < NCHARS; c++) cps[c] = 32 + c;
        f->raw = LoadFontData(g_file[fi], g_file_len[fi], k_fonts[i].size, cps, NCHARS, FONT_DEFAULT);
        f->nraw = f->raw ? NCHARS : 0;
        f->ok = f->raw != NULL;
        f->pad = k_fonts[i].glow ? (int)ceilf(k_fonts[i].size * 0.09f) : 0;
        return;
    }
    int k = i - FONT_COUNT;
    if (k < 0 || k >= CARD_SIZES) return;
    int fi = file_index("Bungee-Regular.ttf");
    if (fi < 0 || !g_file[fi]) return;
    int cps[sizeof k_card_chars];
    int n = (int)strlen(k_card_chars);
    for (int c = 0; c < n; c++) cps[c] = k_card_chars[c];
    g_card[k] = LoadFontData(g_file[fi], g_file_len[fi], k_card_size[k], cps, n, FONT_DEFAULT);
    g_cardn[k] = g_card[k] ? n : 0;
}

int text_card_glyph(int size_class, char ch, GlyphBitmap *out)
{
    if (size_class < 0 || size_class >= CARD_SIZES || !g_card[size_class]) return -1;
    const char *p = strchr(k_card_chars, ch);
    if (!p || !ch) return -1;
    const GlyphInfo *g = &g_card[size_class][p - k_card_chars];
    out->px = g->image.data;
    out->w = g->image.width;
    out->h = g->image.height;
    out->ox = (float)g->offsetX;
    out->oy = (float)g->offsetY;
    out->adv = (float)g->advanceX;
    return 0;
}

static int glyph_empty(const GlyphInfo *g)
{
    if (!g->image.data || g->image.width <= 0 || g->image.height <= 0) return 1;
    const unsigned char *p = g->image.data;
    for (int i = 0; i < g->image.width * g->image.height; i++)
        if (p[i]) return 0;
    return 1;
}

void text_declare(void)
{
    for (int i = 0; i < FONT_COUNT; i++) {
        BFont *f = &g_font[i];
        for (int c = 0; c < NCHARS; c++) {
            Glyph *g = &f->g[c];
            g->item = g->gitem = -1;
            if (!f->ok) continue;
            GlyphInfo *r = &f->raw[c];
            g->adv = (float)r->advanceX;
            g->ox = (float)r->offsetX;
            g->oy = (float)r->offsetY;
            g->w = (float)r->image.width;
            g->h = (float)r->image.height;
            if (glyph_empty(r)) continue;
            /* Only the glyphs a font style actually shows: capitals-only
             * faces skip a-z, which are drawn as their capitals. */
            if (k_fonts[i].caps && c + 32 >= 'a' && c + 32 <= 'z') continue;
            g->item = atlas_declare(r->image.width, r->image.height);
            if (f->pad) {
                int hw = (r->image.width + 1) / 2, hh = (r->image.height + 1) / 2;
                g->gitem = atlas_declare(hw + 2 * f->pad, hh + 2 * f->pad);
                g->gw = (float)(hw + 2 * f->pad) * 2;
                g->gh = (float)(hh + 2 * f->pad) * 2;
                g->gox = g->ox - (float)f->pad * 2;
                g->goy = g->oy - (float)f->pad * 2;
            }
        }
    }
}

void text_paint_job(int i)
{
    BFont *f = &g_font[i];
    if (!f->ok) return;
    for (int c = 0; c < NCHARS; c++) {
        Glyph *g = &f->g[c];
        if (g->item < 0) continue;
        const GlyphInfo *r = &f->raw[c];
        const unsigned char *a = r->image.data;
        int w = r->image.width, h = r->image.height;
        Canvas cv = atlas_canvas(g->item);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                unsigned char v = a[y * w + x];
                uint8_t *d = cv.px + ((size_t)y * cv.stride + x) * 4;
                d[0] = d[1] = d[2] = d[3] = v;
            }
        if (g->gitem < 0) continue;
        Canvas gc = atlas_canvas(g->gitem);
        int hw = (w + 1) / 2, hh = (h + 1) / 2;
        for (int y = 0; y < hh; y++)
            for (int x = 0; x < hw; x++) {
                int s = 0;
                for (int k = 0; k < 4; k++) {
                    int xx = 2 * x + (k & 1), yy = 2 * y + (k >> 1);
                    if (xx < w && yy < h) s += a[yy * w + xx];
                }
                uint8_t v = (uint8_t)(s / 4);
                uint8_t *d = gc.px + ((size_t)(y + f->pad) * gc.stride + x + f->pad) * 4;
                d[0] = d[1] = d[2] = d[3] = v;
            }
        int r3 = f->pad / 3 > 1 ? f->pad / 3 : 1;
        cv_blur(&gc, r3);
        /* Thin strokes lose most of their peak in the blur; lift it back. */
        for (int y = 0; y < gc.h; y++)
            for (int x = 0; x < gc.w; x++) {
                uint8_t *d = gc.px + ((size_t)y * gc.stride + x) * 4;
                int v = d[3] * 5 / 2;
                d[0] = d[1] = d[2] = d[3] = (uint8_t)(v > 255 ? 255 : v);
            }
    }
}

void text_finish(void)
{
    for (int i = 0; i < FONT_COUNT; i++) {
        BFont *f = &g_font[i];
        for (int c = 0; c < NCHARS; c++) {
            Glyph *g = &f->g[c];
            if (g->item >= 0) g->s = atlas_spr(g->item);
            if (g->gitem >= 0) g->gs = atlas_spr(g->gitem);
        }
        if (f->raw) UnloadFontData(f->raw, f->nraw);
        f->raw = NULL;
    }
    for (int k = 0; k < CARD_SIZES; k++) {
        if (g_card[k]) UnloadFontData(g_card[k], g_cardn[k]);
        g_card[k] = NULL;
    }
    for (int i = 0; i < NFILES; i++) {
        if (g_file[i]) UnloadFileData(g_file[i]);
        g_file[i] = NULL;
    }
}

void text_shutdown(void) { memset(g_font, 0, sizeof g_font); }

float text_base_size(FontId f) { return g_font[f].base > 0 ? g_font[f].base : 20.0f; }
float text_line_height(FontId f, float size) { (void)f; return size; }

static const Glyph *glyph_of(const BFont *f, int caps, unsigned char ch)
{
    if (caps && ch >= 'a' && ch <= 'z') ch = (unsigned char)(ch - 32);
    if (ch < 32 || ch > 126) ch = '?';
    return &f->g[ch - 32];
}

float text_width(FontId fid, const char *s, float size, float spacing)
{
    const BFont *f = &g_font[fid];
    if (!f->ok) return (float)MeasureText(s, (int)size);
    float k = size / f->base, w = 0;
    int n = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++, n++) w += glyph_of(f, k_fonts[fid].caps, *p)->adv * k;
    if (n > 1) w += spacing * (float)(n - 1);
    return w;
}

void text_draw(FontId f, const char *s, float x, float y, float size, Color c)
{
    TextStyle st;
    memset(&st, 0, sizeof st);
    st.color = c;
    text_draw_ex(f, s, x, y, size, &st);
}

static PCol pc_scale(PCol c, float k)
{
    if (k >= 1) return c;
    if (k <= 0) return (PCol){ 0, 0, 0, 0 };
    return (PCol){ (unsigned char)(c.r * k), (unsigned char)(c.g * k), (unsigned char)(c.b * k), (unsigned char)(c.a * k) };
}

static Color lerp_color(Color a, Color b, float t)
{
    return (Color){ (unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t),
                    (unsigned char)(a.b + (b.b - a.b) * t), (unsigned char)(a.a + (b.a - a.a) * t) };
}

void text_draw_ex(FontId fid, const char *s, float x, float y, float size, const TextStyle *st)
{
    const BFont *f = &g_font[fid];
    float op = st->opacity > 0 ? st->opacity : 1.0f;
    float w = text_width(fid, s, size, st->spacing);
    if (st->align == ALIGN_CENTER) x -= w * 0.5f;
    else if (st->align == ALIGN_RIGHT) x -= w;
    if (!f->ok) {
        /* The font file was missing: raylib's own font, so the game still shows text. */
        gfx_flush();
        DrawText(s, (int)x, (int)y, (int)size, Fade(st->color, op));
        return;
    }
    float k = size / f->base;
    int caps = k_fonts[fid].caps;
    int has_glow = f->pad > 0;

    /* Passes back to front so no glyph's halo covers a neighbour's face:
     * 0 shadow, 1 glow, 2 outline, 3 face. */
    for (int pass = 0; pass < 4; pass++) {
        if (pass == 0 && st->shadow_k <= 0) continue;
        if (pass == 1 && (st->glow_k <= 0 || !has_glow)) continue;
        if (pass == 2 && st->outline_px <= 0) continue;
        float pen = x;
        int i = 0;
        for (const unsigned char *p = (const unsigned char *)s; *p; p++, i++) {
            const Glyph *g = glyph_of(f, caps, *p);
            float dx = 0, dy = 0, sc = 1, al = 1;
            Color tint = st->color;
            if (st->fn) st->fn(i, &dx, &dy, &sc, &al, &tint, st->user);
            al *= op;
            float cxg = pen + (g->ox + g->w * 0.5f) * k, cyg = y + (g->oy + g->h * 0.5f) * k;
            if (g->item >= 0 && al > 0.003f) {
                float gw = g->w * k * sc, gh = g->h * k * sc;
                float gx = cxg - gw * 0.5f + dx, gy = cyg - gh * 0.5f + dy;
                switch (pass) {
                case 0:
                    if (has_glow && g->gitem >= 0) {
                        float hw = g->gw * k * sc, hh = g->gh * k * sc;
                        gfx_spr(&g->gs, cxg - hw * 0.5f + dx, cyg - hh * 0.5f + dy + size * 0.04f, hw, hh,
                                gfx_cola(st->shadow, st->shadow_k * al));
                    } else {
                        gfx_spr(&g->s, gx + size * 0.04f, gy + size * 0.05f, gw, gh, gfx_cola(st->shadow, st->shadow_k * al));
                    }
                    break;
                case 1:
                    if (g->gitem >= 0) {
                        float hw = g->gw * k * sc, hh = g->gh * k * sc;
                        gfx_spr(&g->gs, cxg - hw * 0.5f + dx, cyg - hh * 0.5f + dy, hw, hh,
                                gfx_add(st->glow, st->glow_k * al));
                    }
                    break;
                case 2: {
                    /* The outline is the glyph itself, a little larger, in the
                     * outline colour behind the face: one quad per glyph
                     * instead of eight offset copies (on the fill-bound Pi a
                     * 120 px banner's eight copies cost ~2 ms). Close to a
                     * true outline for the bold display faces it is used on. */
                    PCol oc = gfx_cola(st->outline, al);
                    float o = st->outline_px;
                    gfx_spr(&g->s, gx - o, gy - o * 0.8f, gw + 2 * o, gh + 2 * o * 0.8f, oc);
                    break;
                }
                default:
                    if (st->color2.a) {
                        float lh = f->base;
                        float t0 = g->oy / lh, t1 = (g->oy + g->h) / lh;
                        Color top = lerp_color(tint, st->color2, t0 < 0 ? 0 : t0 > 1 ? 1 : t0);
                        Color bot = lerp_color(tint, st->color2, t1 < 0 ? 0 : t1 > 1 ? 1 : t1);
                        gfx_spr_vgrad(&g->s, gx, gy, gw, gh, gfx_cola(top, al), gfx_cola(bot, al));
                    } else {
                        gfx_spr(&g->s, gx, gy, gw, gh, pc_scale(gfx_col(tint), al));
                    }
                    break;
                }
            }
            pen += g->adv * k + st->spacing;
        }
    }
}

void text_neon(FontId f, const char *s, float cx, float cy, float size, Color tube, float power, float glow_k)
{
    if (power < 0) power = 0;
    TextStyle st;
    memset(&st, 0, sizeof st);
    st.align = ALIGN_CENTER;
    /* The core runs pastel-hot: the tube colour pushed towards white, and
     * dimmed to the unlit glass colour as power drops. */
    Color hot = { (unsigned char)(tube.r + (255 - tube.r) * 0.62f), (unsigned char)(tube.g + (255 - tube.g) * 0.62f),
                  (unsigned char)(tube.b + (255 - tube.b) * 0.62f), 255 };
    Color off = { (unsigned char)(tube.r * 0.28f + 20), (unsigned char)(tube.g * 0.28f + 16), (unsigned char)(tube.b * 0.28f + 22), 255 };
    st.color = lerp_color(off, hot, power);
    st.glow = tube;
    st.glow_k = glow_k * power;
    text_draw_ex(f, s, cx, cy - size * 0.5f, size, &st);
}

void text_gold(FontId f, const char *s, float cx, float cy, float size, float opacity, float glow_k)
{
    TextStyle st;
    memset(&st, 0, sizeof st);
    st.align = ALIGN_CENTER;
    st.opacity = opacity;
    /* No per-glyph shadow: the outline gives the edge, and callers put a
     * dark halo behind banners - a shadow quad per glyph is 0.3 Mpx at 120 px. */
    st.glow = (Color){ 255, 170, 40, 255 };
    st.glow_k = glow_k;
    st.outline = (Color){ 90, 40, 8, 255 };
    st.outline_px = size * 0.045f;
    st.color = (Color){ 255, 246, 196, 255 };
    st.color2 = (Color){ 214, 128, 22, 255 };
    text_draw_ex(f, s, cx, cy - size * 0.5f, size, &st);
}
