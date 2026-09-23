/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* options.h - command line and config.ini, merged.
 *
 * Precedence: built-in defaults < config.ini < command line. The config file
 * is --config FILE, else config.ini next to the executable, else
 * platform/config.ini under the current directory, else built-in defaults. */
#ifndef BPL_PLATFORM_OPTIONS_H
#define BPL_PLATFORM_OPTIONS_H

#include <stdint.h>

#include "platform/input.h"
#include "platform/screen.h"

#define OPT_MAX_SHOTS  32
#define OPT_MAX_SCRIPT 256

typedef struct {
    uint64_t frame;          /* rendered frame index (0 = first frame)      */
    int      screen;         /* 1 = full screen, 0 = the 1280x720 play space */
    char     path[240];
} ShotReq;

typedef struct {
    uint64_t from, to;       /* tick range, inclusive                         */
    uint32_t buttons;        /* logical buttons held down over the range      */
} ScriptStep;

typedef struct {
    char        config_path[512];   /* the file actually loaded, "" if none */
    const char *perf_csv;
    uint64_t    frames;             /* exit after this many frames; 0 = never  */
    ShotReq     shots[OPT_MAX_SHOTS];
    int         nshots;
    ScriptStep  script[OPT_MAX_SCRIPT];
    int         nscript;
    int         have_seed;
    uint64_t    seed;
    int         start_state;        /* AppState to start in                    */
    int         lockstep;           /* exactly one tick per rendered frame     */
    int         gpu_finish;         /* glFinish after render and compose       */
    int         overlay;            /* F1 overlay visible at start             */
    int         sprites, shader;    /* gputest knobs                           */
    int         verbose;
    int         vsync;
    int         width, height;      /* desktop window / DRM mode; 0 = native   */
    InputConfig  input;
    ScreenConfig screen;
} Options;

/* 0 = run, 1 = --help printed (exit 0), -1 = error printed (exit 2). */
int  options_parse(Options *o, int argc, char **argv);

/* Buttons the script holds at this tick. */
uint32_t options_script_buttons(const Options *o, uint64_t tick);

#endif
