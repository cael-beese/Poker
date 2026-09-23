/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* display_probe.h - native display mode, queried before InitWindow (DRM only). */
#ifndef BPL_PLATFORM_DISPLAY_PROBE_H
#define BPL_PLATFORM_DISPLAY_PROBE_H

typedef struct {
    int  width, height, refresh;   /* preferred mode of the first connected output */
    char connector[32];            /* e.g. "HDMI-A-2"                              */
} DisplayInfo;

/* 0 on success. Always fails on the desktop build. */
int display_probe_native(DisplayInfo *out);

#endif
