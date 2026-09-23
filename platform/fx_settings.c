/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* fx_settings.c - see fx_settings.h. */
#include "platform/fx_settings.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

EffectSettings g_effects;           /* set to the defaults by options/session */

/* overrides from config.ini / --fx for this run: which effects, and to what */
static uint8_t g_ovr_set[FX_COUNT], g_ovr_val[FX_COUNT];

void fx_settings_defaults(EffectSettings *s) { effects_defaults(s); }

static int parse_on_off(const char *v, uint8_t *out)
{
    if (strcasecmp(v, "on") == 0 || strcmp(v, "1") == 0 || strcasecmp(v, "true") == 0) { *out = 1; return 0; }
    if (strcasecmp(v, "off") == 0 || strcmp(v, "0") == 0 || strcasecmp(v, "false") == 0) { *out = 0; return 0; }
    return -1;
}

int fx_settings_set(EffectSettings *s, const char *name, const char *value)
{
    uint8_t v;
    if (parse_on_off(value, &v) != 0) return -1;
    int all = strcasecmp(name, "all") == 0, hit = 0;
    for (int i = 0; i < FX_COUNT; i++) {
        if (!all && strcasecmp(name, effects_name(i)) != 0) continue;
        *effects_field(s, i) = v;
        g_ovr_set[i] = 1;
        g_ovr_val[i] = v;
        hit = 1;
    }
    return hit ? 0 : -1;
}

int fx_settings_parse(EffectSettings *s, const char *list)
{
    char buf[512];
    snprintf(buf, sizeof buf, "%s", list);
    for (char *save = NULL, *tok = strtok_r(buf, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
        while (*tok == ' ') tok++;
        if (strcasecmp(tok, "none") == 0) {
            if (fx_settings_set(s, "all", "off") != 0) return -1;
            continue;
        }
        char *eq = strchr(tok, '=');
        if (!eq) return -1;
        *eq = '\0';
        if (fx_settings_set(s, tok, eq + 1) != 0) return -1;
    }
    return 0;
}

void fx_settings_apply_overrides(EffectSettings *live)
{
    for (int i = 0; i < FX_COUNT; i++)
        if (g_ovr_set[i]) *effects_field(live, i) = g_ovr_val[i];
}
