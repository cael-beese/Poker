/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* settings.c - see settings.h. */
#include "platform/settings.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

Settings g_settings = { { 1, 1, 1, 1, 1, 1, 1, 1 } };

static const char *const g_names[EFFECT_COUNT] = {
    "bloom", "particles", "shake", "hitpause", "marquee_flicker", "bulb_chase", "shimmer", "card_specular"
};

void settings_defaults(Settings *s)
{
    for (int i = 0; i < EFFECT_COUNT; i++) *settings_effect_ptr(s, i) = 1;
}

const char *settings_effect_name(int i)
{
    return (i >= 0 && i < EFFECT_COUNT) ? g_names[i] : "?";
}

uint8_t *settings_effect_ptr(Settings *s, int i)
{
    EffectSettings *e = &s->effects;
    switch (i) {
    case 0: return &e->bloom;
    case 1: return &e->particles;
    case 2: return &e->shake;
    case 3: return &e->hitpause;
    case 4: return &e->marquee_flicker;
    case 5: return &e->bulb_chase;
    case 6: return &e->shimmer;
    default: return &e->card_specular;
    }
}

static int parse_on_off(const char *v, uint8_t *out)
{
    if (strcasecmp(v, "on") == 0 || strcmp(v, "1") == 0 || strcasecmp(v, "true") == 0) { *out = 1; return 0; }
    if (strcasecmp(v, "off") == 0 || strcmp(v, "0") == 0 || strcasecmp(v, "false") == 0) { *out = 0; return 0; }
    return -1;
}

int settings_effect_set(Settings *s, const char *name, const char *value)
{
    uint8_t v;
    if (parse_on_off(value, &v) != 0) return -1;
    if (strcasecmp(name, "all") == 0) {
        for (int i = 0; i < EFFECT_COUNT; i++) *settings_effect_ptr(s, i) = v;
        return 0;
    }
    for (int i = 0; i < EFFECT_COUNT; i++) {
        if (strcasecmp(name, g_names[i]) == 0) {
            *settings_effect_ptr(s, i) = v;
            return 0;
        }
    }
    return -1;
}

int settings_effects_parse(Settings *s, const char *list)
{
    char buf[512];
    snprintf(buf, sizeof buf, "%s", list);
    for (char *save = NULL, *tok = strtok_r(buf, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
        while (*tok == ' ') tok++;
        if (strcasecmp(tok, "none") == 0) {
            if (settings_effect_set(s, "all", "off") != 0) return -1;
            continue;
        }
        char *eq = strchr(tok, '=');
        if (!eq) return -1;
        *eq = '\0';
        if (settings_effect_set(s, tok, eq + 1) != 0) return -1;
    }
    return 0;
}
