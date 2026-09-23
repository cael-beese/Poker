/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* input.h - physical inputs (keyboard, joysticks, touch) -> logical buttons.
 *
 * Bindings come from the [input] section of config.ini (see the documented
 * default platform/config.ini). Each logical button of engine/input.h can have
 * up to INPUT_MAX_BINDINGS physical inputs; one physical input may drive
 * several logical buttons (e.g. a panel button that is both HOLD2 and OK).
 *
 * Timing: input_sample() runs once per rendered frame, right before that
 * frame's ticks. Edges are latched until a tick consumes them, so a press is
 * never lost when a frame runs no tick, and a tap shorter than a frame still
 * reads as "down" for one tick. The first tick of the frame gets the edges;
 * later ticks of the same frame see only "down". */
#ifndef BPL_PLATFORM_INPUT_H
#define BPL_PLATFORM_INPUT_H

#include <stdint.h>
#include "engine/input.h"
#include "raylib.h"

#define INPUT_NBUTTONS      19     /* BTN_HOLD1 .. BTN_DEBUG */
#define INPUT_MAX_BINDINGS  6

enum { BIND_NONE, BIND_KEY, BIND_JOY_BUTTON, BIND_JOY_AXIS, BIND_JOY_HAT };

typedef struct {
    uint8_t kind;      /* BIND_*                                          */
    int8_t  joy;       /* joystick index, -1 = any joystick               */
    int16_t code;      /* raylib key / button / axis / hat index          */
    int8_t  dir;       /* axis: -1 or +1; hat: JOY_HAT_* bit              */
} Binding;

typedef struct {
    Binding  bind[INPUT_NBUTTONS][INPUT_MAX_BINDINGS];
    Binding  exit_bind[INPUT_MAX_BINDINGS];  /* immediate clean exit (ESC)           */
    uint32_t exit_combo;                     /* logical buttons held together = exit */
    Binding  slider;                         /* analogue axis for the Hold'em slider */
    int      touch;                          /* 1 = touch / mouse maps to play space */
} InputConfig;

typedef struct {
    InputConfig cfg;
    uint32_t down;            /* logical buttons down at the last sample        */
    uint32_t frame_pressed;   /* edges seen at the last sample                  */
    uint32_t latch;           /* edges not yet handed to a tick                 */
    int16_t  slider;
    int16_t  touch_x, touch_y;
    uint8_t  touch;
    int      exit_requested;
} Input;

void input_defaults(InputConfig *c);
/* Handles one [input] key of config.ini; 0 ok, -1 bad. Used by the config loader. */
int  input_config_set(InputConfig *c, const char *key, const char *value);

void input_init(Input *in, const InputConfig *cfg);
/* play = where the 1280x720 play space is on screen, for touch mapping. */
void input_sample(Input *in, Rectangle play);
/* Fills one tick's InputFrame and consumes the latched edges. */
void input_next_frame(Input *in, InputFrame *out);

/* "HOLD1" -> BTN_HOLD1; 0 if unknown. Names as in engine/input.h without BTN_. */
uint32_t    input_button_from_name(const char *name);
const char *input_button_name(int bit);      /* bit index 0..18 */
/* Describes a binding as config text, e.g. "KEY_ENTER", "JOY*_B9". */
void        input_binding_str(const Binding *b, char *out, int n);

#endif
