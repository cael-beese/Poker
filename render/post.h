/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* post.h - bloom, the one full-screen post effect.
 *
 * With bloom on, the frame is drawn into a 1280x720 scene target instead of
 * the play space, then:
 *   1. threshold + 4x downsample in one pass (4 bilinear taps = 16 texels)
 *      -> 320x180;
 *   2. separable gaussian blur, horizontal then vertical, at 320x180
 *      (9 taps as 5 bilinear fetches per pass);
 *   3. composite: scene + bloom * strength, written to the play space with
 *      blending off (one full-screen pass that replaces the copy the scene
 *      target needs anyway).
 * With bloom off, drawing goes straight to the play space and nothing here
 * costs anything; the neon keeps its baked glow sprites either way.
 *
 * Quality variants exist to measure the alternatives on the Pi (report in
 * docs/RENDER.md): BLOOM_Q8 blurs at 1/8 resolution, BLOOM_Q4_FAST uses a
 * 5-tap blur (3 fetches). */
#ifndef BPL_RENDER_POST_H
#define BPL_RENDER_POST_H

typedef enum { BLOOM_Q4, BLOOM_Q8, BLOOM_Q4_FAST, BLOOM_NQUALITY } BloomQuality;

typedef struct {
    float threshold;     /* brightness (max channel) where bloom starts, default 0.72 */
    float knee;          /* soft knee width, default 0.18                              */
    float strength;      /* composite gain, default 1.0                                */
    BloomQuality quality;
} BloomParams;

int  post_init(void);                    /* shaders and targets; 0 ok */
void post_shutdown(void);
BloomParams *post_params(void);
const char *post_quality_name(BloomQuality q);

/* Wrap the frame's drawing. post_begin decides from g_effects.bloom. */
void post_begin(void);
void post_end(void);
int  post_active(void);                  /* bloom ran this frame */

#endif
