/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* drm_present.h - the scaled-plane presentation added to raylib by
 * tools/raylib-5.5-drm-plane.patch (DRM builds only; the desktop gets stubs).
 *
 * With a plane, raylib's screen IS the 1280x720 play space: the Pi's display
 * controller scales it onto the display at scan-out, over a background
 * framebuffer that holds the side art. See that patch for the measurements
 * that motivated it. */
#ifndef BPL_PLATFORM_DRM_PRESENT_H
#define BPL_PLATFORM_DRM_PRESENT_H

#if defined(BPL_PLATFORM_DRM)
void BplDrmConfigurePlane(int surfaceWidth, int surfaceHeight, int dstX, int dstY, int dstWidth, int dstHeight,
                          int nearest);
int  BplDrmPlaneActive(void);
int  BplDrmPlaneNearest(void);
int  BplDrmSetBackground(const unsigned char *rgba, int width, int height);
#else
static inline void BplDrmConfigurePlane(int sw, int sh, int dx, int dy, int dw, int dh, int nearest)
{
    (void)sw; (void)sh; (void)dx; (void)dy; (void)dw; (void)dh; (void)nearest;
}
static inline int BplDrmPlaneActive(void) { return 0; }
static inline int BplDrmPlaneNearest(void) { return 0; }
static inline int BplDrmSetBackground(const unsigned char *rgba, int w, int h) { (void)rgba; (void)w; (void)h; return -1; }
#endif

#endif
