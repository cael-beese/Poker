/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* atlas.c - see atlas.h. */
#include "render/atlas.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raylib.h"
#include "platform/texreg.h"

typedef struct { int w, h, x, y, page; } Item;

static Item g_item[ATLAS_MAX_ITEMS];
static int g_n;
static int g_order[ATLAS_MAX_ITEMS];
static struct { int w, h; uint8_t *px; Texture2D tex; } g_page[ATLAS_MAX_PAGES];
static int g_npages;

void atlas_reset(void)
{
    atlas_shutdown();
    g_n = 0;
}

int atlas_declare(int w, int h)
{
    if (g_n >= ATLAS_MAX_ITEMS || w <= 0 || h <= 0) return -1;
    g_item[g_n] = (Item){ w, h, 0, 0, 0 };
    return g_n++;
}

static int cmp_height(const void *a, const void *b)
{
    const Item *x = &g_item[*(const int *)a], *y = &g_item[*(const int *)b];
    if (x->h != y->h) return y->h - x->h;
    if (x->w != y->w) return y->w - x->w;
    return *(const int *)a - *(const int *)b;   /* stable: same input, same atlas */
}

int atlas_pack(int max_tex)
{
    int W = max_tex >= 4096 ? 4096 : max_tex >= 2048 ? 2048 : 1024;
    for (int i = 0; i < g_n; i++) g_order[i] = i;
    qsort(g_order, (size_t)g_n, sizeof g_order[0], cmp_height);

    /* Shelf packing, tallest first: rows of items left to right. */
    int page = 0, x = 0, y = 0, shelf = 0, used_h[ATLAS_MAX_PAGES] = { 0 };
    for (int k = 0; k < g_n; k++) {
        Item *it = &g_item[g_order[k]];
        int w = it->w + 2 * ATLAS_PAD, h = it->h + 2 * ATLAS_PAD;
        if (w > W) { fprintf(stderr, "atlas: item %dx%d wider than a page\n", it->w, it->h); return -1; }
        if (x + w > W) { x = 0; y += shelf; shelf = 0; }
        if (y + h > max_tex) {
            if (++page >= ATLAS_MAX_PAGES) { fprintf(stderr, "atlas: out of pages\n"); return -1; }
            x = y = shelf = 0;
        }
        it->x = x + ATLAS_PAD;
        it->y = y + ATLAS_PAD;
        it->page = page;
        x += w;
        if (h > shelf) shelf = h;
        if (y + shelf > used_h[page]) used_h[page] = y + shelf;
    }
    g_npages = g_n ? page + 1 : 0;
    for (int p = 0; p < g_npages; p++) {
        g_page[p].w = W;
        g_page[p].h = (used_h[p] + 15) & ~15;   /* crop the page to what is used */
        g_page[p].px = calloc((size_t)W * (size_t)g_page[p].h, 4);
        if (!g_page[p].px) return -1;
    }
    return 0;
}

Canvas atlas_canvas(int id)
{
    const Item *it = &g_item[id];
    Canvas c = { g_page[it->page].px + ((size_t)it->y * (size_t)g_page[it->page].w + (size_t)it->x) * 4,
                 it->w, it->h, g_page[it->page].w };
    return c;
}

void atlas_upload(void)
{
    for (int p = 0; p < g_npages; p++) {
        Image img = { g_page[p].px, g_page[p].w, g_page[p].h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
        char tag[32];
        snprintf(tag, sizeof tag, "atlas %d (%dx%d)", p, g_page[p].w, g_page[p].h);
        g_page[p].tex = texreg_load_image(tag, img);
        SetTextureFilter(g_page[p].tex, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(g_page[p].tex, TEXTURE_WRAP_CLAMP);
        free(g_page[p].px);
        g_page[p].px = NULL;
    }
}

void atlas_shutdown(void)
{
    for (int p = 0; p < ATLAS_MAX_PAGES; p++) {
        if (g_page[p].tex.id) texreg_unload(g_page[p].tex);
        free(g_page[p].px);
        memset(&g_page[p], 0, sizeof g_page[p]);
    }
    g_npages = 0;
}

Spr atlas_sub(int id, float x, float y, float w, float h)
{
    Spr s;
    memset(&s, 0, sizeof s);
    if (id < 0 || id >= g_n) return s;
    const Item *it = &g_item[id];
    float iw = 1.0f / (float)g_page[it->page].w, ih = 1.0f / (float)g_page[it->page].h;
    s.u0 = ((float)it->x + x) * iw;
    s.v0 = ((float)it->y + y) * ih;
    s.u1 = ((float)it->x + x + w) * iw;
    s.v1 = ((float)it->y + y + h) * ih;
    s.w = w;
    s.h = h;
    s.tex = g_page[it->page].tex.id;
    return s;
}

Spr atlas_spr(int id)
{
    if (id < 0 || id >= g_n) { Spr s; memset(&s, 0, sizeof s); return s; }
    return atlas_sub(id, 0, 0, (float)g_item[id].w, (float)g_item[id].h);
}

Spr atlas_texel(int id, float x, float y)
{
    Spr s = atlas_sub(id, x + 0.5f, y + 0.5f, 0, 0);
    s.w = s.h = 1;
    return s;
}

int atlas_pages(void) { return g_npages; }

void atlas_page_size(int page, int *w, int *h)
{
    *w = g_page[page].w;
    *h = g_page[page].h;
}

size_t atlas_bytes(void)
{
    size_t b = 0;
    for (int p = 0; p < g_npages; p++) b += (size_t)g_page[p].w * (size_t)g_page[p].h * 4;
    return b;
}

int atlas_items(void) { return g_n; }
