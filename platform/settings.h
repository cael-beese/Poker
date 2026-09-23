/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* settings.h - the app's runtime settings that are not input or display.
 *
 * For now this is the effect toggles (CONTRACT.md section 4: "every required
 * effect has a settings toggle, settings.effects.*"), so each effect can be
 * switched off to measure what it costs. They come from the [effects] section
 * of config.ini and from --fx on the command line; the F1 overlay lists them.
 * Presentation code reads g_settings.effects every frame, so a toggle takes
 * effect on the next frame. Persistence (platform milestone 7) will save
 * this struct with the rest of the settings. */
#ifndef BPL_PLATFORM_SETTINGS_H
#define BPL_PLATFORM_SETTINGS_H

#include <stdint.h>

typedef struct {
    uint8_t bloom;            /* full-screen bloom (threshold, 1/4 res blur, composite)   */
    uint8_t particles;        /* coins, sparks, droplets, confetti, fireworks              */
    uint8_t shake;            /* screen shake on big wins                                  */
    uint8_t hitpause;         /* presentation freezes 2-3 frames on big reveals            */
    uint8_t marquee_flicker;  /* the neon title sign's flicker                             */
    uint8_t bulb_chase;       /* chasing light bulbs around the play area                  */
    uint8_t shimmer;          /* idle shimmer on held cards                                */
    uint8_t card_specular;    /* specular sweep on card flips                              */
} EffectSettings;

#define EFFECT_COUNT 8

typedef struct {
    EffectSettings effects;
} Settings;

extern Settings g_settings;

void settings_defaults(Settings *s);

/* Effect i (0..EFFECT_COUNT-1) by index, for loops and the overlay. */
const char *settings_effect_name(int i);        /* "bloom", "particles", ... */
uint8_t    *settings_effect_ptr(Settings *s, int i);

/* One "name = on|off" line of [effects]. 0 ok, -1 unknown key or value. */
int settings_effect_set(Settings *s, const char *name, const char *value);

/* --fx "name=on,name=off,...", or "all=off" / "none" to start from all off. */
int settings_effects_parse(Settings *s, const char *list);

#endif
