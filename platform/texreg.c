/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* texreg.c - see texreg.h. */
#include "platform/texreg.h"

#include <string.h>
#include "rlgl.h"

static TexRegEntry g_reg[TEXREG_MAX];
static size_t g_bytes;
static int g_count;

static size_t texture_bytes(int w, int h, int format, int mipmaps)
{
    size_t total = 0;
    if (mipmaps < 1) mipmaps = 1;
    for (int i = 0; i < mipmaps; i++) {
        total += (size_t)GetPixelDataSize(w, h, format);
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }
    return total;
}

void texreg_add(const char *tag, unsigned int id, int w, int h, int format, int mipmaps,
                int render_target, size_t extra_bytes)
{
    if (id == 0) return;
    for (int i = 0; i < TEXREG_MAX; i++) {
        if (g_reg[i].id != 0) continue;
        TexRegEntry *e = &g_reg[i];
        e->id = id;
        strncpy(e->tag, tag ? tag : "?", sizeof e->tag - 1);
        e->tag[sizeof e->tag - 1] = '\0';
        e->width = w;
        e->height = h;
        e->format = format;
        e->mipmaps = mipmaps;
        e->render_target = render_target;
        e->bytes = texture_bytes(w, h, format, mipmaps) + extra_bytes;
        g_bytes += e->bytes;
        g_count++;
        return;
    }
    TraceLog(LOG_WARNING, "TEXREG: registry full (%d), '%s' not counted", TEXREG_MAX, tag);
}

void texreg_add_texture(const char *tag, Texture2D t)
{
    texreg_add(tag, t.id, t.width, t.height, t.format, t.mipmaps, 0, 0);
}

void texreg_remove(unsigned int id)
{
    if (id == 0) return;
    for (int i = 0; i < TEXREG_MAX; i++) {
        if (g_reg[i].id != id) continue;
        g_bytes -= g_reg[i].bytes;
        g_count--;
        memset(&g_reg[i], 0, sizeof g_reg[i]);
        return;
    }
}

Texture2D texreg_load_image(const char *tag, Image img)
{
    Texture2D t = LoadTextureFromImage(img);
    texreg_add_texture(tag, t);
    return t;
}

Texture2D texreg_load_file(const char *tag, const char *path)
{
    Texture2D t = LoadTexture(path);
    texreg_add_texture(tag, t);
    return t;
}

RenderTexture2D texreg_load_rt(const char *tag, int w, int h, int with_depth)
{
    RenderTexture2D rt;
    if (with_depth) {
        rt = LoadRenderTexture(w, h);
    } else {
        /* raylib's LoadRenderTexture always attaches a depth renderbuffer.
         * 2D passes never depth-test, so build the framebuffer by hand and
         * save w*h*4 bytes per target. */
        memset(&rt, 0, sizeof rt);
        rt.id = rlLoadFramebuffer();
        if (rt.id > 0) {
            rlEnableFramebuffer(rt.id);
            rt.texture.id = rlLoadTexture(NULL, w, h, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1);
            rt.texture.width = w;
            rt.texture.height = h;
            rt.texture.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
            rt.texture.mipmaps = 1;
            rlFramebufferAttach(rt.id, rt.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
            if (!rlFramebufferComplete(rt.id)) TraceLog(LOG_WARNING, "TEXREG: framebuffer '%s' incomplete", tag);
            rlDisableFramebuffer();
        }
    }
    texreg_add(tag, rt.texture.id, rt.texture.width, rt.texture.height, rt.texture.format, 1, 1,
               with_depth ? (size_t)w * (size_t)h * 4u : 0u);
    return rt;
}

void texreg_unload(Texture2D t)
{
    texreg_remove(t.id);
    UnloadTexture(t);
}

void texreg_unload_rt(RenderTexture2D rt)
{
    texreg_remove(rt.texture.id);
    UnloadRenderTexture(rt);
}

size_t texreg_bytes(void) { return g_bytes; }
int texreg_count(void) { return g_count; }

const TexRegEntry *texreg_entry(int i)
{
    if (i < 0 || i >= TEXREG_MAX || g_reg[i].id == 0) return NULL;
    return &g_reg[i];
}

void texreg_dump(FILE *f)
{
    fprintf(f, "textures: %d, %.2f MB\n", g_count, (double)g_bytes / (1024.0 * 1024.0));
    for (int i = 0; i < TEXREG_MAX; i++) {
        const TexRegEntry *e = &g_reg[i];
        if (!e->id) continue;
        fprintf(f, "  %-31s %5dx%-5d fmt %2d mips %d %s %8.2f KB\n", e->tag, e->width, e->height,
                e->format, e->mipmaps, e->render_target ? "RT " : "tex", (double)e->bytes / 1024.0);
    }
}
