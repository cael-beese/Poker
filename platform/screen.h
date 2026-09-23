/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* screen.h - the 1280x720 play space and how it reaches the display.
 *
 * Everything the game draws goes into the 1280x720 play space. Where it lands
 * on the display (the layout):
 *   - scale "auto": a whole-number scale when that fills at least 90% of what
 *     a fractional fit would (3440x1440 -> 2x = 2560x1440 at x = 440), else a
 *     fractional fit that keeps 16:9 (1920x1080 -> 1.5x);
 *     "integer", "fit" and "1x" force one behaviour;
 *   - filter "auto": point sampling at whole-number scales (exact pixels),
 *     bilinear otherwise; "point" / "bilinear" force one;
 *   - the margins left over (pillarbox on the ultrawide, letterbox on taller
 *     screens) are drawn by the side-art hook; with no hook they stay black.
 *
 * Two ways to get it there (present = auto | plane | gpu):
 *   plane (the Pi, default): raylib's screen IS the 1280x720 play space. The
 *     display controller scales it at scan-out onto the layout rectangle, over
 *     a background framebuffer that holds the side art (drawn once by the
 *     hook, re-drawn by screen_refresh_side_art()). Costs the GPU nothing.
 *     See tools/raylib-5.5-drm-plane.patch.
 *   gpu (desktop, or fallback): the play space is a render target; every frame
 *     screen_compose() clears the screen, calls the side-art hook and draws the
 *     play space scaled, with blending off (it is opaque by definition). On
 *     the cabinet this costs ~15.5 ms of GPU time per frame at 3440x1440.
 * Callers do not need to care which is in use: draw the play space between
 * screen_begin_play() and screen_end_play(), then call screen_compose(). */
#ifndef BPL_PLATFORM_SCREEN_H
#define BPL_PLATFORM_SCREEN_H

#include "raylib.h"

#define PLAY_W 1280
#define PLAY_H 720

typedef enum { SCALE_AUTO, SCALE_INTEGER, SCALE_FIT, SCALE_1X } ScaleMode;
typedef enum { FILTER_AUTO, FILTER_POINT, FILTER_BILINEAR } FilterMode;
typedef enum { PRESENT_AUTO, PRESENT_PLANE, PRESENT_GPU } PresentMode;

typedef struct {
    ScaleMode   scale;
    FilterMode  filter;
    PresentMode present;
    int         side_art;      /* 1 = draw the side-art hook in the margins */
} ScreenConfig;

typedef struct {
    int       screen_w, screen_h;  /* the display (plane: the mode, not raylib's screen) */
    Rectangle play;            /* the play space on the display, whole pixels     */
    float     scale;           /* play.width / 1280                               */
    int       integer;         /* scale is a whole number                         */
    int       point;           /* point / nearest filtering in use                */
    int       plane;           /* 1 = presented through a scaled DRM plane        */
    Rectangle margin[4];       /* uncovered display areas: left, right, top, bottom */
    int       nmargin;
} ScreenLayout;

/* Side art: draws into the margins of L (display coordinates) inside a
 * drawing pass. time = seconds, for animation (gpu path: every frame; plane
 * path: once per refresh, time 0). Must not touch the play rectangle.
 * Render targets it caches must be registered with texreg. */
typedef void (*SideArtFn)(const ScreenLayout *L, float time, void *user);

void screen_config_defaults(ScreenConfig *c);
int  screen_config_set(ScreenConfig *c, const char *key, const char *value);   /* [display] keys */

/* The layout the config gives on a display of w x h, before any window exists
 * (main uses it to set up the DRM plane). */
ScreenLayout screen_plan(const ScreenConfig *c, int w, int h);

/* After InitWindow. plane = BplDrmPlaneActive(); display_w/h = the display
 * mode (only used with a plane; otherwise the window size is used). */
void screen_init(const ScreenConfig *c, int plane, int display_w, int display_h);
void screen_shutdown(void);

void screen_begin_play(Color clear);   /* inside BeginDrawing: start drawing the play space */
void screen_end_play(void);
void screen_compose(float time);       /* inside BeginDrawing, after the play space */

const ScreenLayout *screen_layout(void);
/* The play rectangle in raylib's touch/mouse coordinates (for input mapping). */
Rectangle screen_touch_rect(void);

void screen_set_side_art(SideArtFn fn, void *user);   /* NULL = black margins */
void screen_refresh_side_art(void);                   /* plane path: redraw the background */

/* The placeholder side art: a cached neon honeycomb with a slow light sweep. */
void screen_side_art_honeycomb(const ScreenLayout *L, float time, void *user);

/* Captures for --shot. Call screen_save_play right after screen_end_play and
 * screen_save_screen before EndDrawing. The plane path has no composed
 * screen in GPU memory, so there screen_save_screen saves the play space.
 * Both return 0 on success. */
int screen_save_play(const char *path);
int screen_save_screen(const char *path);

const char *screen_scale_name(ScaleMode m);

#endif
