/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* gpustats.h - draw-call counting and GPU synchronisation.
 *
 * Draw calls are counted where they actually happen, at the GL entry points
 * (glDrawElements / glDrawArrays), so the number includes every rlgl batch
 * flush, shader switch, render-target change and the final upscale:
 *   - DRM (GLES): raylib calls the GLES functions directly, so the link wraps
 *     them (-Wl,--wrap=glDrawElements,--wrap=glDrawArrays).
 *   - Desktop (GL 3.3): raylib calls through glad function pointers, so
 *     gpustats_init() swaps those pointers for counting shims after InitWindow.
 * The count is per frame: read it with gpustats_take() after EndDrawing(). */
#ifndef BPL_PLATFORM_GPUSTATS_H
#define BPL_PLATFORM_GPUSTATS_H

void gpustats_init(void);            /* after InitWindow() */
unsigned gpustats_take(void);        /* draw calls since the last take; resets */
unsigned gpustats_peek(void);        /* draw calls so far this frame */
void gpustats_finish(void);          /* flush rlgl's batch and glFinish(): GPU idle on return */

#endif
