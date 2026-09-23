/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* fx_settings.h - the LIVE effect toggles the renderer reads.
 *
 * The list of effects lives in platform/effects.h (one X-macro drives the
 * struct, the save-file keys and the service menu). Two copies exist:
 *
 *   - the SAVED settings (session.c, effects_get()): what the service menu
 *     edits and the save file keeps;
 *   - the LIVE copy, g_effects: what render/ reads every frame. It is the
 *     saved settings, with any config.ini [effects] / --fx overrides laid on
 *     top for this run only (those are for performance testing and are never
 *     saved). A change made in the service menu replaces the live copy.
 *
 * RENDERTEST may flip g_effects directly; that too is never saved. */
#ifndef BPL_PLATFORM_FX_SETTINGS_H
#define BPL_PLATFORM_FX_SETTINGS_H

#include <stdint.h>
#include "platform/effects.h"

#define EFFECT_COUNT FX_COUNT

extern EffectSettings g_effects;

static inline const char *fx_settings_name(int i) { return effects_name(i); }
static inline uint8_t *fx_settings_ptr(EffectSettings *s, int i) { return effects_field(s, i); }
void fx_settings_defaults(EffectSettings *s);

/* One "name = on|off" line (config.ini [effects]) - also remembered as an
   override for this run. 0 ok, -1 unknown key or value. "all" sets every one. */
int fx_settings_set(EffectSettings *s, const char *name, const char *value);

/* --fx "name=on,name=off,...", or "all=off" / "none" to start from all off. */
int fx_settings_parse(EffectSettings *s, const char *list);

/* Lay the remembered overrides over `live` (session.c calls this after it
   loads the saved settings into g_effects). */
void fx_settings_apply_overrides(EffectSettings *live);

#endif
