/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* effects.h - the per-effect on/off switches of SPEC section 2 ("each
 * individually toggleable in settings for perf testing").
 *
 * One X-macro list drives everything: the struct below, the save file keys
 * ("fx.<name>"), and the rows of the service menu's EFFECTS page. To add an
 * effect, add one X(...) line here - nothing else has to change, and old save
 * files simply load the new switch at its default.
 *
 *   X(field, "SERVICE MENU LABEL", default_on)
 *
 * The render module reads the live values with effects_get() (implemented by
 * platform/session.c; the defaults until the save file is loaded). */
#ifndef BPL_PLATFORM_EFFECTS_H
#define BPL_PLATFORM_EFFECTS_H

#include <stdint.h>

#define BPL_EFFECTS(X)                                  \
    X(bloom,           "BLOOM",               1)        \
    X(particles,       "PARTICLES",           1)        \
    X(shake,           "SCREEN SHAKE",        1)        \
    X(hitpause,        "HIT PAUSE",           1)        \
    X(marquee_flicker, "MARQUEE FLICKER",     1)        \
    X(bulb_chase,      "LIGHT CHASE",         1)        \
    X(shimmer,         "HELD CARD SHIMMER",   1)        \
    X(card_specular,   "CARD SPECULAR",       1)        \
    X(card_anim,       "CARD DEAL / FLIP",    1)        \
    X(chip_anim,       "CHIP ANIMATION",      1)        \
    X(takeover,        "JACKPOT TAKEOVER",    1)        \
    X(side_art,        "SIDE ART ANIMATION",  1)

typedef struct {
#define BPL_FX_FIELD(name, label, def) uint8_t name;
    BPL_EFFECTS(BPL_FX_FIELD)
#undef BPL_FX_FIELD
} EffectSettings;

enum {
#define BPL_FX_ENUM(name, label, def) FX_##name,
    BPL_EFFECTS(BPL_FX_ENUM)
#undef BPL_FX_ENUM
    FX_COUNT
};

/* Name / label / default / pointer by index, for generic code (save file,
 * service menu). */
const char *effects_name(int fx);          /* "bloom"   */
const char *effects_label(int fx);         /* "BLOOM"   */
int         effects_default(int fx);
uint8_t    *effects_field(EffectSettings *e, int fx);
void        effects_defaults(EffectSettings *e);

/* The live settings (session.c). Never NULL. */
const EffectSettings *effects_get(void);

#endif
