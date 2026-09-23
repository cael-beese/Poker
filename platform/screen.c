/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* screen.c - see screen.h. */
#include "platform/screen.h"

#include <math.h>
#if defined(__GLIBC__) || defined(__linux__)
#include <malloc.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "rlgl.h"
#include "platform/drm_present.h"
#include "platform/texreg.h"

static ScreenConfig g_cfg;
static ScreenLayout g_layout;
static RenderTexture2D g_play;        /* gpu path only */
static SideArtFn g_side_art;
static void *g_side_user;

/* Honeycomb cache: one render target the size of the larger margin. */
static RenderTexture2D g_honey;
static int g_honey_w, g_honey_h, g_honey_vertical;

void screen_config_defaults(ScreenConfig *c)
{
    c->scale = SCALE_AUTO;
    c->filter = FILTER_AUTO;
    c->present = PRESENT_AUTO;
    c->side_art = 1;
}

static int on_off(const char *v, int *out)
{
    if (strcasecmp(v, "on") == 0 || strcmp(v, "1") == 0) { *out = 1; return 0; }
    if (strcasecmp(v, "off") == 0 || strcmp(v, "0") == 0) { *out = 0; return 0; }
    return -1;
}

int screen_config_set(ScreenConfig *c, const char *key, const char *value)
{
    if (strcasecmp(key, "scale") == 0) {
        if (strcasecmp(value, "auto") == 0) c->scale = SCALE_AUTO;
        else if (strcasecmp(value, "integer") == 0) c->scale = SCALE_INTEGER;
        else if (strcasecmp(value, "fit") == 0) c->scale = SCALE_FIT;
        else if (strcasecmp(value, "1x") == 0) c->scale = SCALE_1X;
        else return -1;
        return 0;
    }
    if (strcasecmp(key, "filter") == 0) {
        if (strcasecmp(value, "auto") == 0) c->filter = FILTER_AUTO;
        else if (strcasecmp(value, "point") == 0) c->filter = FILTER_POINT;
        else if (strcasecmp(value, "bilinear") == 0) c->filter = FILTER_BILINEAR;
        else return -1;
        return 0;
    }
    if (strcasecmp(key, "present") == 0) {
        if (strcasecmp(value, "auto") == 0) c->present = PRESENT_AUTO;
        else if (strcasecmp(value, "plane") == 0) c->present = PRESENT_PLANE;
        else if (strcasecmp(value, "gpu") == 0) c->present = PRESENT_GPU;
        else return -1;
        return 0;
    }
    if (strcasecmp(key, "side_art") == 0) return on_off(value, &c->side_art);
    return -1;
}

const char *screen_scale_name(ScaleMode m)
{
    switch (m) {
    case SCALE_INTEGER: return "integer";
    case SCALE_FIT: return "fit";
    case SCALE_1X: return "1x";
    default: return "auto";
    }
}

ScreenLayout screen_plan(const ScreenConfig *c, int sw, int sh)
{
    ScreenLayout L;
    memset(&L, 0, sizeof L);
    L.screen_w = sw;
    L.screen_h = sh;

    float fit = fminf((float)sw / PLAY_W, (float)sh / PLAY_H);
    int whole = (int)floorf(fit + 1e-4f);
    float scale;
    switch (c->scale) {
    case SCALE_1X: scale = 1.0f; break;
    case SCALE_FIT: scale = fit; break;
    case SCALE_INTEGER: scale = whole >= 1 ? (float)whole : fit; break;
    default: scale = (whole >= 1 && (float)whole / fit >= 0.9f) ? (float)whole : fit; break;
    }
    int pw = (int)lroundf(PLAY_W * scale), ph = (int)lroundf(PLAY_H * scale);
    if (pw > sw) pw = sw;
    if (ph > sh) ph = sh;
    int px = (sw - pw) / 2, py = (sh - ph) / 2;
    L.play = (Rectangle){ (float)px, (float)py, (float)pw, (float)ph };
    L.scale = (float)pw / PLAY_W;
    L.integer = fabsf(scale - roundf(scale)) < 1e-4f && pw == PLAY_W * (int)roundf(scale);
    L.point = c->filter == FILTER_POINT || (c->filter == FILTER_AUTO && L.integer);

    if (px > 0) {
        L.margin[L.nmargin++] = (Rectangle){ 0, 0, (float)px, (float)sh };
        L.margin[L.nmargin++] = (Rectangle){ (float)(px + pw), 0, (float)(sw - px - pw), (float)sh };
    }
    if (py > 0) {
        L.margin[L.nmargin++] = (Rectangle){ (float)px, 0, (float)pw, (float)py };
        L.margin[L.nmargin++] = (Rectangle){ (float)px, (float)(py + ph), (float)pw, (float)(sh - py - ph) };
    }
    return L;
}

static void relayout_gpu(void)
{
    g_layout = screen_plan(&g_cfg, GetScreenWidth(), GetScreenHeight());
    SetTextureFilter(g_play.texture, g_layout.point ? TEXTURE_FILTER_POINT : TEXTURE_FILTER_BILINEAR);
}

void screen_init(const ScreenConfig *c, int plane, int display_w, int display_h)
{
    g_cfg = *c;
    g_side_art = g_cfg.side_art ? screen_side_art_honeycomb : NULL;
    g_side_user = NULL;
    if (plane) {
        g_layout = screen_plan(&g_cfg, display_w, display_h);
        g_layout.plane = 1;
        /* What the display controller really does: nearest only if the kernel offers it. */
        g_layout.point = BplDrmPlaneNearest();
        screen_refresh_side_art();
    } else {
        g_play = texreg_load_rt("play space 1280x720", PLAY_W, PLAY_H, 0);
        relayout_gpu();
    }
}

void screen_shutdown(void)
{
    if (g_honey.id) texreg_unload_rt(g_honey);
    memset(&g_honey, 0, sizeof g_honey);
    if (g_play.id) texreg_unload_rt(g_play);
    memset(&g_play, 0, sizeof g_play);
}

void screen_begin_play(Color clear)
{
    if (!g_layout.plane) BeginTextureMode(g_play);
    ClearBackground(clear);
}

void screen_end_play(void)
{
    if (g_layout.plane) rlDrawRenderBatchActive();
    else EndTextureMode();
}

void screen_compose(float time)
{
    if (g_layout.plane) return;    /* the display controller composes */
    if (GetScreenWidth() != g_layout.screen_w || GetScreenHeight() != g_layout.screen_h) relayout_gpu();

    ClearBackground(BLACK);
    if (g_side_art && g_layout.nmargin > 0) g_side_art(&g_layout, time, g_side_user);

    /* Opaque copy: flush what is batched, draw with blending off, restore. */
    rlDrawRenderBatchActive();
    rlDisableColorBlend();
    DrawTexturePro(g_play.texture, (Rectangle){ 0, 0, PLAY_W, -PLAY_H }, g_layout.play,
                   (Vector2){ 0, 0 }, 0.0f, WHITE);
    rlDrawRenderBatchActive();
    rlEnableColorBlend();
}

const ScreenLayout *screen_layout(void) { return &g_layout; }

Rectangle screen_touch_rect(void)
{
    if (!g_layout.plane) return g_layout.play;
    /* raylib spreads an absolute pointer over its screen (the 1280x720
     * surface) although the device covers the whole display. */
    float kx = (float)PLAY_W / (float)g_layout.screen_w, ky = (float)PLAY_H / (float)g_layout.screen_h;
    return (Rectangle){ g_layout.play.x * kx, g_layout.play.y * ky, g_layout.play.width * kx, g_layout.play.height * ky };
}

void screen_set_side_art(SideArtFn fn, void *user)
{
    g_side_art = fn;
    g_side_user = user;
    if (g_layout.plane) screen_refresh_side_art();
}

void screen_refresh_side_art(void)
{
    if (!g_layout.plane) return;
    int w = g_layout.screen_w, h = g_layout.screen_h;
    /* Draw the hook once into a display-sized target, read it back and hand
     * it to the background plane. The target and cache are freed again, so
     * the side art costs no GPU memory while the game runs. */
    RenderTexture2D rt = texreg_load_rt("side art (temporary)", w, h, 0);
    BeginTextureMode(rt);
    ClearBackground(BLACK);
    if (g_side_art && g_layout.nmargin > 0) g_side_art(&g_layout, 0.0f, g_side_user);
    EndTextureMode();
    Image img = LoadImageFromTexture(rt.texture);
    texreg_unload_rt(rt);
    if (g_honey.id) texreg_unload_rt(g_honey);
    memset(&g_honey, 0, sizeof g_honey);
    if (!img.data) return;
    ImageFlipVertical(&img);
    if (img.format != PIXELFORMAT_UNCOMPRESSED_R8G8B8A8) ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    if (BplDrmSetBackground((const unsigned char *)img.data, img.width, img.height) != 0)
        TraceLog(LOG_WARNING, "SCREEN: could not set the background plane");
    UnloadImage(img);
#if defined(__GLIBC__)
    /* The 20 MB read-back buffers were freed, but glibc keeps freed heap
     * memory and counts it as resident; give it back to the system. */
    malloc_trim(0);
#endif
}

/* ---- placeholder side art: neon honeycomb ------------------------------- */

static uint32_t hash32(uint32_t x)
{
    x ^= x >> 16; x *= 0x7feb352dU;
    x ^= x >> 15; x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

/* Renders the pattern once into a cache target of w x h. vertical = 1 means
 * the cache's right edge touches the play space (pillarbox), 0 means its
 * bottom edge does (letterbox), so the glow brightens towards the game and
 * fades towards the bezel. */
static void build_honeycomb(int w, int h, int vertical, int screen_h)
{
    if (g_honey.id) texreg_unload_rt(g_honey);
    g_honey = texreg_load_rt("side art honeycomb", w, h, 0);
    g_honey_w = w;
    g_honey_h = h;
    g_honey_vertical = vertical;

    const Color felt = { 16, 13, 18, 255 };
    const Color honey = { 232, 170, 40, 255 };
    const Color amber = { 255, 128, 16, 255 };
    const Color magenta = { 255, 40, 200, 255 };
    const Color cyan = { 40, 230, 255, 255 };

    float r = (float)screen_h / 36.0f;   /* 40 px at 1440 */
    if (r < 8.0f) r = 8.0f;
    float dx = sqrtf(3.0f) * r, dy = 1.5f * r;

    BeginTextureMode(g_honey);
    ClearBackground(felt);
    int row = 0;
    for (float y = -r; y < (float)h + r; y += dy, row++) {
        int col = 0;
        for (float x = (row & 1) ? dx * 0.5f : 0.0f; x < (float)w + dx; x += dx, col++) {
            /* Distance to the play-space edge, 0 (touching) .. 1 (far). */
            float d = vertical ? 1.0f - x / (float)w : 1.0f - y / (float)h;
            if (d < 0) d = 0;
            if (d > 1) d = 1;
            float glow = 1.0f - d * 0.85f;
            uint32_t hsh = hash32((uint32_t)row * 7919u + (uint32_t)col * 104729u);
            Vector2 c = { x, y };
            if ((hsh & 7u) == 0) DrawPoly(c, 6, r * 0.92f, 30.0f, Fade(amber, 0.10f + 0.18f * glow));
            Color line = honey;
            if ((hsh >> 8) % 23u == 0) line = magenta;
            else if ((hsh >> 8) % 29u == 0) line = cyan;
            DrawPolyLinesEx(c, 6, r * 0.92f, 30.0f, 2.0f, Fade(line, 0.18f + 0.55f * glow));
        }
    }
    /* A thin neon rail along the play-space edge. */
    if (vertical) {
        DrawRectangle(w - 6, 0, 2, h, Fade(cyan, 0.8f));
        DrawRectangle(w - 3, 0, 3, h, Fade(honey, 0.9f));
    } else {
        DrawRectangle(0, h - 6, w, 2, Fade(cyan, 0.8f));
        DrawRectangle(0, h - 3, w, 3, Fade(honey, 0.9f));
    }
    EndTextureMode();
}

void screen_side_art_honeycomb(const ScreenLayout *L, float time, void *user)
{
    (void)user;
    int vertical = L->play.x > 0;              /* pillarbox: margins left and right */
    int w, h;
    if (vertical) {
        w = (int)fmaxf(L->margin[0].width, L->margin[1].width);
        h = L->screen_h;
    } else {
        w = L->screen_w;
        h = (int)fmaxf(L->margin[0].height, L->margin[1].height);
    }
    if (w <= 0 || h <= 0) return;
    if (!g_honey.id || w != g_honey_w || h != g_honey_h || vertical != g_honey_vertical)
        build_honeycomb(w, h, vertical, L->screen_h);

    /* The cache is drawn with its play-side edge against the play space; the
     * second margin is the mirror image. Texture row 0 of a render target is
     * the bottom row that was drawn; a negative source width/height mirrors
     * that axis over the same span. */
    const Rectangle *a = &L->margin[0], *b = &L->margin[1];
    if (vertical) {
        DrawTexturePro(g_honey.texture, (Rectangle){ (float)w - a->width, 0, a->width, -(float)h }, *a,
                       (Vector2){ 0, 0 }, 0, WHITE);
        DrawTexturePro(g_honey.texture, (Rectangle){ (float)w - b->width, 0, -b->width, -(float)h }, *b,
                       (Vector2){ 0, 0 }, 0, WHITE);
    } else {
        DrawTexturePro(g_honey.texture, (Rectangle){ 0, 0, a->width, -a->height }, *a,
                       (Vector2){ 0, 0 }, 0, WHITE);
        DrawTexturePro(g_honey.texture, (Rectangle){ 0, 0, b->width, b->height }, *b,
                       (Vector2){ 0, 0 }, 0, WHITE);
    }

    /* A slow warm light sweep so the cabinet never looks frozen (gpu path). */
    if (time <= 0.0f) return;
    BeginBlendMode(BLEND_ADDITIVE);
    float band = (float)L->screen_h * 0.35f;
    float pos = fmodf(time * 90.0f, (float)L->screen_h + 2.0f * band) - band;
    Color c0 = { 255, 170, 40, 0 }, c1 = { 255, 170, 40, 36 };
    for (int i = 0; i < 2 && vertical; i++) {
        const Rectangle *m = &L->margin[i];
        DrawRectangleGradientV((int)m->x, (int)pos, (int)m->width, (int)(band * 0.5f), c0, c1);
        DrawRectangleGradientV((int)m->x, (int)(pos + band * 0.5f), (int)m->width, (int)(band * 0.5f), c1, c0);
    }
    EndBlendMode();
}

/* ---- captures ----------------------------------------------------------- */

static int save_screen_pixels(const char *path, int w, int h)
{
    rlDrawRenderBatchActive();
    unsigned char *px = rlReadScreenPixels(w, h);
    if (!px) return -1;
    Image img = { px, w, h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    int ok = ExportImage(img, path);
    UnloadImage(img);
    return ok ? 0 : -1;
}

int screen_save_play(const char *path)
{
    if (g_layout.plane) return save_screen_pixels(path, PLAY_W, PLAY_H);
    Image img = LoadImageFromTexture(g_play.texture);
    if (!img.data) return -1;
    ImageFlipVertical(&img);
    int ok = ExportImage(img, path);
    UnloadImage(img);
    return ok ? 0 : -1;
}

int screen_save_screen(const char *path)
{
    return save_screen_pixels(path, GetScreenWidth(), GetScreenHeight());
}
