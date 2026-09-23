/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* joy_evdev.h - joysticks and arcade encoders read straight from evdev.
 *
 * Why not raylib's gamepad API: raylib 5.5's DRM backend maps only the
 * standard gamepad key codes (BTN_A, BTN_START, ...). Generic arcade encoders
 * such as the cabinet's two DragonRise "USB gamepad" boards report
 * BTN_TRIGGER..BTN_BASE6 instead, which raylib silently drops. So the platform
 * reads /dev/input/event* itself and numbers buttons, axes and hats the way
 * SDL does, which is the numbering EmulationStation writes into es_input.cfg
 * (button id="9" there is JOY0_B9 here).
 *
 * Joysticks are numbered by event node order. New devices are picked up
 * through inotify on /dev/input (never by polling: opening some nodes takes
 * ~50 ms on the Pi); unplugged ones are closed. Linux only; elsewhere (and when
 * /dev/input is unreadable) there are simply no joysticks. */
#ifndef BPL_PLATFORM_JOY_EVDEV_H
#define BPL_PLATFORM_JOY_EVDEV_H

#define JOY_MAX          8
#define JOY_MAX_BUTTONS  64
#define JOY_MAX_AXES     16
#define JOY_MAX_HATS     4

enum { JOY_HAT_UP = 1, JOY_HAT_DOWN = 2, JOY_HAT_LEFT = 4, JOY_HAT_RIGHT = 8 };

void joy_init(void);
void joy_poll(void);            /* once per rendered frame: read every pending event */
void joy_shutdown(void);

int         joy_count(void);
const char *joy_name(int j);
int         joy_buttons(int j);
int         joy_axes(int j);

int   joy_button_down(int j, int b);
int   joy_button_pressed(int j, int b);   /* went down since the previous joy_poll */
float joy_axis(int j, int a);             /* -1..1 */
int   joy_hat(int j, int h);              /* JOY_HAT_* bits */
int   joy_hat_pressed(int j, int h);      /* bits that went on since the previous poll */

#endif
