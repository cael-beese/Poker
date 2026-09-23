/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* music.h - the looping music and the ambience bed. */
#ifndef AUDIOGEN_MUSIC_H
#define AUDIOGEN_MUSIC_H

#include "dsp.h"

Buf music_lounge(void);                 /* 48 bars at 98 bpm, seamless loop  */
Buf music_attract(void);                /* 16 bars, the attract-mode variant */
Buf ambience_lounge(double seconds);    /* crowd murmur, seamless loop       */

#endif
