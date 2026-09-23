/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* input.c - see input.h. */
#include "platform/input.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "platform/joy_evdev.h"

/* Axis deflection that counts as a digital press. */
#define AXIS_ON 0.5f

static const char *const g_btn_names[INPUT_NBUTTONS] = {
    "HOLD1", "HOLD2", "HOLD3", "HOLD4", "HOLD5", "DEAL", "BET_ONE", "BET_MAX", "CASH_OUT",
    "SERVICE", "UP", "DOWN", "LEFT", "RIGHT", "OK", "BACK", "COIN", "START", "DEBUG"
};

typedef struct { const char *name; int key; } KeyName;
#define K(x) { #x, x }
static const KeyName g_keys[] = {
    K(KEY_A), K(KEY_B), K(KEY_C), K(KEY_D), K(KEY_E), K(KEY_F), K(KEY_G), K(KEY_H), K(KEY_I),
    K(KEY_J), K(KEY_K), K(KEY_L), K(KEY_M), K(KEY_N), K(KEY_O), K(KEY_P), K(KEY_Q), K(KEY_R),
    K(KEY_S), K(KEY_T), K(KEY_U), K(KEY_V), K(KEY_W), K(KEY_X), K(KEY_Y), K(KEY_Z),
    K(KEY_ZERO), K(KEY_ONE), K(KEY_TWO), K(KEY_THREE), K(KEY_FOUR), K(KEY_FIVE), K(KEY_SIX),
    K(KEY_SEVEN), K(KEY_EIGHT), K(KEY_NINE),
    { "KEY_0", KEY_ZERO }, { "KEY_1", KEY_ONE }, { "KEY_2", KEY_TWO }, { "KEY_3", KEY_THREE },
    { "KEY_4", KEY_FOUR }, { "KEY_5", KEY_FIVE }, { "KEY_6", KEY_SIX }, { "KEY_7", KEY_SEVEN },
    { "KEY_8", KEY_EIGHT }, { "KEY_9", KEY_NINE },
    K(KEY_SPACE), K(KEY_ESCAPE), K(KEY_ENTER), K(KEY_TAB), K(KEY_BACKSPACE), K(KEY_INSERT),
    K(KEY_DELETE), K(KEY_RIGHT), K(KEY_LEFT), K(KEY_DOWN), K(KEY_UP), K(KEY_PAGE_UP),
    K(KEY_PAGE_DOWN), K(KEY_HOME), K(KEY_END), K(KEY_PAUSE),
    K(KEY_F1), K(KEY_F2), K(KEY_F3), K(KEY_F4), K(KEY_F5), K(KEY_F6), K(KEY_F7), K(KEY_F8),
    K(KEY_F9), K(KEY_F10), K(KEY_F11), K(KEY_F12),
    K(KEY_LEFT_SHIFT), K(KEY_LEFT_CONTROL), K(KEY_LEFT_ALT), K(KEY_RIGHT_SHIFT),
    K(KEY_RIGHT_CONTROL), K(KEY_RIGHT_ALT),
    K(KEY_APOSTROPHE), K(KEY_COMMA), K(KEY_MINUS), K(KEY_PERIOD), K(KEY_SLASH), K(KEY_SEMICOLON),
    K(KEY_EQUAL), K(KEY_LEFT_BRACKET), K(KEY_BACKSLASH), K(KEY_RIGHT_BRACKET), K(KEY_GRAVE),
    K(KEY_KP_0), K(KEY_KP_1), K(KEY_KP_2), K(KEY_KP_3), K(KEY_KP_4), K(KEY_KP_5), K(KEY_KP_6),
    K(KEY_KP_7), K(KEY_KP_8), K(KEY_KP_9), K(KEY_KP_DECIMAL), K(KEY_KP_DIVIDE),
    K(KEY_KP_MULTIPLY), K(KEY_KP_SUBTRACT), K(KEY_KP_ADD), K(KEY_KP_ENTER), K(KEY_KP_EQUAL),
};
#undef K

uint32_t input_button_from_name(const char *name)
{
    for (int i = 0; i < INPUT_NBUTTONS; i++)
        if (strcasecmp(name, g_btn_names[i]) == 0) return 1u << i;
    return 0;
}

const char *input_button_name(int bit)
{
    return (bit >= 0 && bit < INPUT_NBUTTONS) ? g_btn_names[bit] : "?";
}

static const char *key_name(int key)
{
    for (size_t i = 0; i < sizeof g_keys / sizeof g_keys[0]; i++)
        if (g_keys[i].key == key) return g_keys[i].name;
    return "KEY_?";
}

void input_binding_str(const Binding *b, char *out, int n)
{
    char joy[8];
    if (b->joy < 0) snprintf(joy, sizeof joy, "*"); else snprintf(joy, sizeof joy, "%d", b->joy);
    switch (b->kind) {
    case BIND_KEY: snprintf(out, (size_t)n, "%s", key_name(b->code)); break;
    case BIND_JOY_BUTTON: snprintf(out, (size_t)n, "JOY%s_B%d", joy, b->code); break;
    case BIND_JOY_AXIS: snprintf(out, (size_t)n, "JOY%s_AXIS%d%c", joy, b->code, b->dir < 0 ? '-' : '+'); break;
    case BIND_JOY_HAT:
        snprintf(out, (size_t)n, "JOY%s_HAT%d_%s", joy, b->code,
                 b->dir == JOY_HAT_UP ? "UP" : b->dir == JOY_HAT_DOWN ? "DOWN" : b->dir == JOY_HAT_LEFT ? "LEFT" : "RIGHT");
        break;
    default: snprintf(out, (size_t)n, "NONE"); break;
    }
}

/* Parses one binding token. Returns 0 on success. */
static int parse_binding(const char *tok, Binding *b)
{
    memset(b, 0, sizeof *b);
    if (strncasecmp(tok, "KEY_", 4) == 0) {
        for (size_t i = 0; i < sizeof g_keys / sizeof g_keys[0]; i++) {
            if (strcasecmp(tok, g_keys[i].name) == 0) {
                b->kind = BIND_KEY;
                b->code = (int16_t)g_keys[i].key;
                return 0;
            }
        }
        return -1;
    }
    if (strncasecmp(tok, "JOY", 3) != 0) return -1;
    const char *p = tok + 3;
    if (*p == '*') { b->joy = -1; p++; }
    else if (isdigit((unsigned char)*p)) { b->joy = (int8_t)strtol(p, (char **)&p, 10); }
    else return -1;
    if (*p++ != '_') return -1;
    if (toupper((unsigned char)p[0]) == 'B' && isdigit((unsigned char)p[1])) {
        char *end;
        long k = strtol(p + 1, &end, 10);
        if (*end || k < 0 || k >= JOY_MAX_BUTTONS) return -1;
        b->kind = BIND_JOY_BUTTON;
        b->code = (int16_t)k;
        return 0;
    }
    if (strncasecmp(p, "AXIS", 4) == 0) {
        char *end;
        long k = strtol(p + 4, &end, 10);
        if (end == p + 4 || k < 0 || k >= JOY_MAX_AXES) return -1;
        b->kind = BIND_JOY_AXIS;
        b->code = (int16_t)k;
        if (*end == '+') b->dir = 1;
        else if (*end == '-') b->dir = -1;
        else if (*end == '\0') b->dir = 0;       /* slider: whole axis */
        else return -1;
        return (*end && end[1]) ? -1 : 0;
    }
    if (strncasecmp(p, "HAT", 3) == 0) {
        char *end;
        long k = strtol(p + 3, &end, 10);
        if (end == p + 3 || k < 0 || k >= JOY_MAX_HATS || *end != '_') return -1;
        b->kind = BIND_JOY_HAT;
        b->code = (int16_t)k;
        end++;
        if (strcasecmp(end, "UP") == 0) b->dir = JOY_HAT_UP;
        else if (strcasecmp(end, "DOWN") == 0) b->dir = JOY_HAT_DOWN;
        else if (strcasecmp(end, "LEFT") == 0) b->dir = JOY_HAT_LEFT;
        else if (strcasecmp(end, "RIGHT") == 0) b->dir = JOY_HAT_RIGHT;
        else return -1;
        return 0;
    }
    return -1;
}

/* "KEY_A, JOY0_B1" -> list. Returns 0 on success; "NONE" or "" clears. */
static int parse_list(const char *value, Binding *list, int max)
{
    Binding tmp[INPUT_MAX_BINDINGS];
    int n = 0;
    char buf[256];
    snprintf(buf, sizeof buf, "%s", value);
    for (char *save = NULL, *tok = strtok_r(buf, ", \t", &save); tok; tok = strtok_r(NULL, ", \t", &save)) {
        if (strcasecmp(tok, "NONE") == 0) continue;
        if (n >= max) return -1;
        if (parse_binding(tok, &tmp[n]) != 0) return -1;
        n++;
    }
    memset(list, 0, sizeof(Binding) * (size_t)max);
    memcpy(list, tmp, sizeof(Binding) * (size_t)n);
    return 0;
}

static void set_defaults_for(InputConfig *c, uint32_t btn, const char *spec)
{
    for (int i = 0; i < INPUT_NBUTTONS; i++)
        if (btn == (1u << i)) parse_list(spec, c->bind[i], INPUT_MAX_BINDINGS);
}

/* The same defaults as the shipped config.ini, so the game is usable even
 * when the file is missing. JOY bindings follow the cabinet's DragonRise
 * encoders as EmulationStation numbers them (select = B8, start = B9). */
void input_defaults(InputConfig *c)
{
    memset(c, 0, sizeof *c);
    set_defaults_for(c, BTN_HOLD1, "KEY_ONE, KEY_Z, JOY0_B0");
    set_defaults_for(c, BTN_HOLD2, "KEY_TWO, KEY_X, JOY0_B1");
    set_defaults_for(c, BTN_HOLD3, "KEY_THREE, KEY_C, JOY0_B2");
    set_defaults_for(c, BTN_HOLD4, "KEY_FOUR, KEY_V, JOY0_B3");
    set_defaults_for(c, BTN_HOLD5, "KEY_FIVE, KEY_B, JOY0_B4");
    set_defaults_for(c, BTN_DEAL, "KEY_ENTER, KEY_SPACE, KEY_KP_ENTER, JOY0_B5");
    set_defaults_for(c, BTN_BET_ONE, "KEY_A, JOY1_B0");
    set_defaults_for(c, BTN_BET_MAX, "KEY_S, JOY1_B1");
    set_defaults_for(c, BTN_CASH_OUT, "KEY_Q, JOY1_B2");
    set_defaults_for(c, BTN_SERVICE, "KEY_F2, KEY_NINE, JOY1_B5");
    set_defaults_for(c, BTN_UP, "KEY_UP, JOY*_AXIS1-, JOY*_HAT0_UP");
    set_defaults_for(c, BTN_DOWN, "KEY_DOWN, JOY*_AXIS1+, JOY*_HAT0_DOWN");
    set_defaults_for(c, BTN_LEFT, "KEY_LEFT, JOY*_AXIS0-, JOY*_HAT0_LEFT");
    set_defaults_for(c, BTN_RIGHT, "KEY_RIGHT, JOY*_AXIS0+, JOY*_HAT0_RIGHT");
    set_defaults_for(c, BTN_OK, "KEY_ENTER, KEY_SPACE, JOY0_B1, JOY0_B5");
    set_defaults_for(c, BTN_BACK, "KEY_BACKSPACE, JOY0_B2");
    set_defaults_for(c, BTN_COIN, "KEY_INSERT, JOY*_B8");
    set_defaults_for(c, BTN_START, "KEY_HOME, JOY*_B9");
    set_defaults_for(c, BTN_DEBUG, "KEY_F1");
    parse_list("KEY_ESCAPE", c->exit_bind, INPUT_MAX_BINDINGS);
    c->exit_combo = BTN_COIN | BTN_START;
    c->touch = 1;
}

int input_config_set(InputConfig *c, const char *key, const char *value)
{
    uint32_t btn = input_button_from_name(key);
    if (btn) {
        for (int i = 0; i < INPUT_NBUTTONS; i++)
            if (btn == (1u << i)) return parse_list(value, c->bind[i], INPUT_MAX_BINDINGS);
    }
    if (strcasecmp(key, "EXIT") == 0) return parse_list(value, c->exit_bind, INPUT_MAX_BINDINGS);
    if (strcasecmp(key, "EXIT_COMBO") == 0) {
        uint32_t combo = 0;
        char buf[128];
        snprintf(buf, sizeof buf, "%s", value);
        for (char *save = NULL, *tok = strtok_r(buf, "+ \t", &save); tok; tok = strtok_r(NULL, "+ \t", &save)) {
            if (strcasecmp(tok, "NONE") == 0) continue;
            uint32_t b = input_button_from_name(tok);
            if (!b) return -1;
            combo |= b;
        }
        c->exit_combo = combo;
        return 0;
    }
    if (strcasecmp(key, "SLIDER") == 0) {
        if (strcasecmp(value, "NONE") == 0 || *value == '\0') { memset(&c->slider, 0, sizeof c->slider); return 0; }
        Binding b;
        if (parse_binding(value, &b) != 0 || b.kind != BIND_JOY_AXIS) return -1;
        c->slider = b;
        return 0;
    }
    if (strcasecmp(key, "TOUCH") == 0) {
        if (strcasecmp(value, "on") == 0 || strcmp(value, "1") == 0) c->touch = 1;
        else if (strcasecmp(value, "off") == 0 || strcmp(value, "0") == 0) c->touch = 0;
        else return -1;
        return 0;
    }
    return -1;
}

void input_init(Input *in, const InputConfig *cfg)
{
    memset(in, 0, sizeof *in);
    in->cfg = *cfg;
}

/* Evaluates one binding: *down = held now, returns 1 if it went down since the
 * previous sample (keys and joystick buttons report this themselves, so even a
 * tap between two samples is seen). */
static int binding_state(const Binding *b, int *down)
{
    *down = 0;
    int pressed = 0;
    int j0 = b->joy < 0 ? 0 : b->joy, j1 = b->joy < 0 ? joy_count() - 1 : b->joy;
    switch (b->kind) {
    case BIND_KEY:
        *down = IsKeyDown(b->code);
        pressed = IsKeyPressed(b->code);
        break;
    case BIND_JOY_BUTTON:
        for (int j = j0; j <= j1; j++) {
            if (joy_button_down(j, b->code)) *down = 1;
            if (joy_button_pressed(j, b->code)) pressed = 1;
        }
        break;
    case BIND_JOY_AXIS:
        for (int j = j0; j <= j1; j++) {
            float v = joy_axis(j, b->code);
            if ((b->dir < 0 && v <= -AXIS_ON) || (b->dir > 0 && v >= AXIS_ON)) *down = 1;
        }
        break;
    case BIND_JOY_HAT:
        for (int j = j0; j <= j1; j++) {
            if (joy_hat(j, b->code) & b->dir) *down = 1;
            if (joy_hat_pressed(j, b->code) & b->dir) pressed = 1;
        }
        break;
    default:
        break;
    }
    return pressed;
}

void input_sample(Input *in, Rectangle play)
{
    joy_poll();
    uint32_t down = 0, pressed = 0;
    for (int i = 0; i < INPUT_NBUTTONS; i++) {
        for (int k = 0; k < INPUT_MAX_BINDINGS; k++) {
            const Binding *b = &in->cfg.bind[i][k];
            if (b->kind == BIND_NONE) break;
            int d;
            if (binding_state(b, &d)) pressed |= 1u << i;
            if (d) down |= 1u << i;
        }
    }
    /* Axis directions have no event of their own: derive their edges. */
    pressed |= down & ~in->down;
    in->down = down;
    in->frame_pressed = pressed;
    in->latch |= pressed;

    for (int k = 0; k < INPUT_MAX_BINDINGS; k++) {
        const Binding *b = &in->cfg.exit_bind[k];
        if (b->kind == BIND_NONE) break;
        int d;
        if (binding_state(b, &d) || d) in->exit_requested = 1;
    }
    if (in->cfg.exit_combo && (down & in->cfg.exit_combo) == in->cfg.exit_combo) in->exit_requested = 1;

    in->slider = 0;
    if (in->cfg.slider.kind == BIND_JOY_AXIS) {
        int j = in->cfg.slider.joy < 0 ? 0 : in->cfg.slider.joy;
        in->slider = (int16_t)(joy_axis(j, in->cfg.slider.code) * 32767.0f);
    }

    in->touch = 0;
    if (in->cfg.touch && play.width > 0 && play.height > 0) {
        Vector2 p;
        int active = 0;
        if (GetTouchPointCount() > 0) { p = GetTouchPosition(0); active = 1; }
        else if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) { p = GetMousePosition(); active = 1; }
        if (active) {
            float x = (p.x - play.x) * 1280.0f / play.width;
            float y = (p.y - play.y) * 720.0f / play.height;
            if (x >= 0 && x < 1280 && y >= 0 && y < 720) {
                in->touch = 1;
                in->touch_x = (int16_t)x;
                in->touch_y = (int16_t)y;
            }
        }
    }
}

void input_next_frame(Input *in, InputFrame *out)
{
    memset(out, 0, sizeof *out);
    out->pressed = in->latch;
    out->down = in->down | in->latch;
    in->latch = 0;
    out->slider = in->slider;
    out->touch = in->touch;
    out->touch_x = in->touch_x;
    out->touch_y = in->touch_y;
}
