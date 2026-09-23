/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* texreg.h - registry of every GPU texture the game creates.
 *
 * The budget is <= 64 MB of GPU textures, and the F1 overlay shows the total,
 * so every texture and render target must be created and destroyed through
 * these calls (or registered by hand with texreg_add/texreg_remove when raylib
 * creates it for you, e.g. a font atlas from LoadFontEx):
 *
 *     Texture2D t = texreg_load_image("cards atlas", img);    // instead of LoadTextureFromImage
 *     RenderTexture2D rt = texreg_load_rt("bloom 1/4", 320, 180, 0);  // instead of LoadRenderTexture
 *     texreg_unload(t);  texreg_unload_rt(rt);                // instead of Unload*
 *
 *     Font f = LoadFontEx(...);                               // raylib made the texture
 *     texreg_add_texture("ui font", f.texture);
 *     ...  texreg_remove(f.texture.id); UnloadFont(f);
 *
 * Sizes are estimated from width, height, pixel format and mip levels
 * (GetPixelDataSize); a render target also counts its depth buffer at 4 bytes
 * per pixel when it has one. The tag is copied (up to 31 chars), so it can be
 * a temporary string. The registry is a fixed array: no allocation, not
 * thread-safe (GL calls are main-thread only anyway). */
#ifndef BPL_PLATFORM_TEXREG_H
#define BPL_PLATFORM_TEXREG_H

#include <stddef.h>
#include <stdio.h>
#include "raylib.h"

#define TEXREG_MAX 256

typedef struct {
    unsigned int id;        /* GL texture name; 0 = free slot               */
    char   tag[32];
    int    width, height, format, mipmaps;
    int    render_target;   /* 1 when it is a render texture's color buffer */
    size_t bytes;           /* estimated GPU bytes, including depth buffer   */
} TexRegEntry;

/* Registers a texture made elsewhere. extra_bytes adds e.g. a depth buffer. */
void texreg_add(const char *tag, unsigned int id, int w, int h, int format, int mipmaps,
                int render_target, size_t extra_bytes);
void texreg_add_texture(const char *tag, Texture2D t);
void texreg_remove(unsigned int id);

/* Create + register / unregister + destroy. */
Texture2D       texreg_load_image(const char *tag, Image img);
Texture2D       texreg_load_file(const char *tag, const char *path);
/* with_depth = 0 skips the depth buffer, which 2D passes never need. */
RenderTexture2D texreg_load_rt(const char *tag, int w, int h, int with_depth);
void            texreg_unload(Texture2D t);
void            texreg_unload_rt(RenderTexture2D rt);

size_t texreg_bytes(void);
int    texreg_count(void);
const TexRegEntry *texreg_entry(int i);   /* i in 0..TEXREG_MAX-1; NULL if free */
void   texreg_dump(FILE *f);

#endif
