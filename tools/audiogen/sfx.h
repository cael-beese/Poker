/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* sfx.h - the table of sound-effect recipes. */
#ifndef AUDIOGEN_SFX_H
#define AUDIOGEN_SFX_H

#include "dsp.h"

typedef struct {
    const char *name;               /* file stem; variants get _1.._n       */
    void (*fn)(Buf *b, Rng *r, int variant);
    int variants;
    int channels;
    double seconds;                 /* render length before trimming         */
    double target;                  /* loudness: max 100 ms K-weighted, LUFS */
    double clip_db;                 /* soft-knee allowance above the ceiling */
    int streamed;                   /* audio.c streams it (DEFS voices == 0) */
} SfxRecipe;

extern const SfxRecipe SFX_RECIPES[];
extern const int SFX_RECIPE_COUNT;

#endif
