/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* joy_evdev.c - see joy_evdev.h. */
#include "platform/joy_evdev.h"

#include <string.h>

#if defined(__linux__)
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define NBITS(x)   ((((x) - 1) / (8 * sizeof(unsigned long))) + 1)
#define TESTBIT(a, b) (((a)[(b) / (8 * sizeof(unsigned long))] >> ((b) % (8 * sizeof(unsigned long)))) & 1UL)

#define MAX_EVNUM 64

/* Hot-plug: watch /dev/input with inotify instead of rescanning it. Opening
 * some event nodes is slow (the HDMI-CEC ones on the Pi took ~50 ms), so a
 * periodic rescan caused a visible hitch every two seconds on the cabinet. */
static int g_inotify = -1;

typedef struct {
    int      fd, evnum;
    char     name[64];
    int      nbuttons, naxes, nhats;
    int16_t  key_to_btn[KEY_CNT];     /* -1 = not a button of ours */
    int8_t   abs_to_axis[ABS_CNT];    /* -1 = not an axis          */
    int      axis_min[JOY_MAX_AXES], axis_max[JOY_MAX_AXES];
    uint64_t down, pressed;
    float    axis[JOY_MAX_AXES];
    uint8_t  hat[JOY_MAX_HATS], hat_pressed[JOY_MAX_HATS];
    int8_t   hat_of_code[8];          /* ABS_HAT0X..ABS_HAT3Y -> hat index or -1 */
    int8_t   hat_x[JOY_MAX_HATS], hat_y[JOY_MAX_HATS];
} Joy;

static Joy g_joy[JOY_MAX];
static int g_njoy;

static int already_open(int evnum)
{
    for (int i = 0; i < g_njoy; i++)
        if (g_joy[i].evnum == evnum) return 1;
    return 0;
}

/* Opens /dev/input/event<evnum> and keeps it if it looks like a joystick.
 * Buttons, axes and hats are numbered exactly as SDL's Linux backend does. */
static void try_open(int evnum)
{
    if (g_njoy >= JOY_MAX || already_open(evnum)) return;
    char path[32];
    snprintf(path, sizeof path, "/dev/input/event%d", evnum);
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return;

    unsigned long evbit[NBITS(EV_CNT)] = {0}, keybit[NBITS(KEY_CNT)] = {0};
    unsigned long absbit[NBITS(ABS_CNT)] = {0}, relbit[NBITS(REL_CNT)] = {0};
    ioctl(fd, EVIOCGBIT(0, sizeof evbit), evbit);
    ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keybit), keybit);
    ioctl(fd, EVIOCGBIT(EV_ABS, sizeof absbit), absbit);
    ioctl(fd, EVIOCGBIT(EV_REL, sizeof relbit), relbit);

    int joyish = 0;
    for (int b = BTN_JOYSTICK; b < BTN_DIGI; b++) if (TESTBIT(keybit, b)) joyish = 1;
    for (int b = BTN_TRIGGER_HAPPY1; b <= BTN_TRIGGER_HAPPY40; b++) if (TESTBIT(keybit, b)) joyish = 1;
    /* Mice, touch screens and keyboards are raylib's business. */
    if (TESTBIT(relbit, REL_X) || TESTBIT(keybit, BTN_TOUCH) || TESTBIT(absbit, ABS_MT_POSITION_X)) joyish = 0;
    if (!joyish) { close(fd); return; }

    /* Keep the array sorted by event number so indices are stable. */
    int slot = g_njoy;
    while (slot > 0 && g_joy[slot - 1].evnum > evnum) {
        g_joy[slot] = g_joy[slot - 1];
        slot--;
    }
    Joy *j = &g_joy[slot];
    memset(j, 0, sizeof *j);
    j->fd = fd;
    j->evnum = evnum;
    if (ioctl(fd, EVIOCGNAME(sizeof j->name), j->name) < 0) snprintf(j->name, sizeof j->name, "event%d", evnum);
    /* Encoders pad their names with spaces ("USB gamepad           "). */
    for (int k = (int)strlen(j->name) - 1; k >= 0 && j->name[k] == ' '; k--) j->name[k] = '\0';

    memset(j->key_to_btn, 0xff, sizeof j->key_to_btn);
    memset(j->abs_to_axis, 0xff, sizeof j->abs_to_axis);
    memset(j->hat_of_code, 0xff, sizeof j->hat_of_code);
    for (int k = BTN_JOYSTICK; k < KEY_MAX; k++)
        if (TESTBIT(keybit, k) && j->nbuttons < JOY_MAX_BUTTONS) j->key_to_btn[k] = (int16_t)j->nbuttons++;
    for (int k = 0; k < BTN_JOYSTICK; k++)
        if (TESTBIT(keybit, k) && j->nbuttons < JOY_MAX_BUTTONS) j->key_to_btn[k] = (int16_t)j->nbuttons++;
    for (int a = 0; a < ABS_MAX; a++) {
        if (a == ABS_HAT0X) { a = ABS_HAT3Y; continue; }
        if (!TESTBIT(absbit, a) || j->naxes >= JOY_MAX_AXES) continue;
        struct input_absinfo info;
        memset(&info, 0, sizeof info);
        ioctl(fd, EVIOCGABS(a), &info);
        j->axis_min[j->naxes] = info.minimum;
        j->axis_max[j->naxes] = info.maximum;
        j->abs_to_axis[a] = (int8_t)j->naxes++;
    }
    for (int h = ABS_HAT0X; h <= ABS_HAT3Y; h += 2) {
        if ((TESTBIT(absbit, h) || TESTBIT(absbit, h + 1)) && j->nhats < JOY_MAX_HATS) {
            j->hat_of_code[h - ABS_HAT0X] = (int8_t)j->nhats;
            j->hat_of_code[h + 1 - ABS_HAT0X] = (int8_t)j->nhats;
            j->nhats++;
        }
    }
    g_njoy++;
    fprintf(stderr, "INPUT: joystick %d = %s (\"%s\"): %d buttons, %d axes, %d hats\n",
            slot, path, j->name, j->nbuttons, j->naxes, j->nhats);
}

static void rescan(void)
{
    for (int e = 0; e < MAX_EVNUM && g_njoy < JOY_MAX; e++) try_open(e);
}

static void drop(int i)
{
    fprintf(stderr, "INPUT: joystick %d (\"%s\") removed\n", i, g_joy[i].name);
    close(g_joy[i].fd);
    for (int k = i; k < g_njoy - 1; k++) g_joy[k] = g_joy[k + 1];
    g_njoy--;
}

void joy_init(void)
{
    g_njoy = 0;
    g_inotify = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    /* IN_ATTRIB too: udev fixes a new node's permissions just after creating it. */
    if (g_inotify >= 0 && inotify_add_watch(g_inotify, "/dev/input", IN_CREATE | IN_ATTRIB) < 0) {
        close(g_inotify);
        g_inotify = -1;
    }
    rescan();
}

/* Opens event nodes that appeared since the last poll. */
static void hotplug(void)
{
    if (g_inotify < 0) return;
    char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
    for (;;) {
        ssize_t n = read(g_inotify, buf, sizeof buf);
        if (n <= 0) return;
        for (char *p = buf; p < buf + n;) {
            const struct inotify_event *e = (const struct inotify_event *)p;
            if (e->len && strncmp(e->name, "event", 5) == 0) {
                int evnum = atoi(e->name + 5);
                if (evnum >= 0 && evnum < MAX_EVNUM) try_open(evnum);
            }
            p += sizeof *e + e->len;
        }
    }
}

static void handle(Joy *j, const struct input_event *ev)
{
    if (ev->type == EV_KEY && ev->code < KEY_CNT) {
        int b = j->key_to_btn[ev->code];
        if (b < 0) return;
        uint64_t bit = 1ULL << b;
        if (ev->value) {
            if (!(j->down & bit)) j->pressed |= bit;
            j->down |= bit;
        } else {
            j->down &= ~bit;
        }
    } else if (ev->type == EV_ABS && ev->code < ABS_CNT) {
        if (ev->code >= ABS_HAT0X && ev->code <= ABS_HAT3Y) {
            int h = j->hat_of_code[ev->code - ABS_HAT0X];
            if (h < 0) return;
            int v = ev->value < 0 ? -1 : (ev->value > 0 ? 1 : 0);
            if ((ev->code - ABS_HAT0X) % 2 == 0) j->hat_x[h] = (int8_t)v; else j->hat_y[h] = (int8_t)v;
            uint8_t bits = 0;
            if (j->hat_y[h] < 0) bits |= JOY_HAT_UP;
            if (j->hat_y[h] > 0) bits |= JOY_HAT_DOWN;
            if (j->hat_x[h] < 0) bits |= JOY_HAT_LEFT;
            if (j->hat_x[h] > 0) bits |= JOY_HAT_RIGHT;
            j->hat_pressed[h] |= (uint8_t)(bits & ~j->hat[h]);
            j->hat[h] = bits;
            return;
        }
        int a = j->abs_to_axis[ev->code];
        if (a < 0) return;
        int range = j->axis_max[a] - j->axis_min[a];
        float v = range > 0 ? 2.0f * (float)(ev->value - j->axis_min[a]) / (float)range - 1.0f : 0.0f;
        j->axis[a] = v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
    }
}

void joy_poll(void)
{
    hotplug();
    for (int i = 0; i < g_njoy; i++) {
        Joy *j = &g_joy[i];
        j->pressed = 0;
        memset(j->hat_pressed, 0, sizeof j->hat_pressed);
        struct input_event ev;
        for (;;) {
            ssize_t n = read(j->fd, &ev, sizeof ev);
            if (n == (ssize_t)sizeof ev) { handle(j, &ev); continue; }
            if (n < 0 && errno != EAGAIN && errno != EINTR) {
                drop(i);
                i--;
            }
            break;
        }
    }
}

void joy_shutdown(void)
{
    for (int i = 0; i < g_njoy; i++) close(g_joy[i].fd);
    g_njoy = 0;
    if (g_inotify >= 0) close(g_inotify);
    g_inotify = -1;
}

int joy_count(void) { return g_njoy; }
const char *joy_name(int j) { return (j >= 0 && j < g_njoy) ? g_joy[j].name : ""; }
int joy_buttons(int j) { return (j >= 0 && j < g_njoy) ? g_joy[j].nbuttons : 0; }
int joy_axes(int j) { return (j >= 0 && j < g_njoy) ? g_joy[j].naxes : 0; }

int joy_button_down(int j, int b)
{
    if (j < 0 || j >= g_njoy || b < 0 || b >= JOY_MAX_BUTTONS) return 0;
    return (int)((g_joy[j].down >> b) & 1u);
}

int joy_button_pressed(int j, int b)
{
    if (j < 0 || j >= g_njoy || b < 0 || b >= JOY_MAX_BUTTONS) return 0;
    return (int)((g_joy[j].pressed >> b) & 1u);
}

float joy_axis(int j, int a)
{
    if (j < 0 || j >= g_njoy || a < 0 || a >= JOY_MAX_AXES) return 0.0f;
    return g_joy[j].axis[a];
}

int joy_hat(int j, int h)
{
    if (j < 0 || j >= g_njoy || h < 0 || h >= JOY_MAX_HATS) return 0;
    return g_joy[j].hat[h];
}

int joy_hat_pressed(int j, int h)
{
    if (j < 0 || j >= g_njoy || h < 0 || h >= JOY_MAX_HATS) return 0;
    return g_joy[j].hat_pressed[h];
}

#else  /* not Linux: no joysticks */

void joy_init(void) {}
void joy_poll(void) {}
void joy_shutdown(void) {}
int joy_count(void) { return 0; }
const char *joy_name(int j) { (void)j; return ""; }
int joy_buttons(int j) { (void)j; return 0; }
int joy_axes(int j) { (void)j; return 0; }
int joy_button_down(int j, int b) { (void)j; (void)b; return 0; }
int joy_button_pressed(int j, int b) { (void)j; (void)b; return 0; }
float joy_axis(int j, int a) { (void)j; (void)a; return 0.0f; }
int joy_hat(int j, int h) { (void)j; (void)h; return 0; }
int joy_hat_pressed(int j, int h) { (void)j; (void)h; return 0; }

#endif
