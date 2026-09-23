/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* overlay.h - the F1 debug overlay (frame time, FPS, draw calls, RSS, textures).
 * Drawn on the real screen, over the composed image, scaled with the display
 * height so it is readable on the ultrawide. The draw-call figure is the
 * previous frame's full count (the overlay's own calls included). */
#ifndef BPL_PLATFORM_OVERLAY_H
#define BPL_PLATFORM_OVERLAY_H

#include "platform/app.h"
#include "platform/perf.h"

void overlay_draw(const PerfRing *ring, const PerfFrame *last, const AppCtx *ctx, long rss_kb,
                  double startup_ms);

#endif
