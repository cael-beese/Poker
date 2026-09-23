/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* engine/input.h - logical inputs, one frame per 60 Hz tick.
 *
 * PLACEHOLDER copied verbatim from docs/CONTRACT.md section 2 by the platform
 * branch so it can build before the engine branch merges. The engine agent
 * owns this file; at merge the engine's version wins. */
#ifndef BPL_ENGINE_INPUT_H
#define BPL_ENGINE_INPUT_H

#include <stdint.h>

enum {
  BTN_HOLD1 = 1u<<0, BTN_HOLD2 = 1u<<1, BTN_HOLD3 = 1u<<2, BTN_HOLD4 = 1u<<3, BTN_HOLD5 = 1u<<4,
  BTN_DEAL  = 1u<<5, BTN_BET_ONE = 1u<<6, BTN_BET_MAX = 1u<<7, BTN_CASH_OUT = 1u<<8,
  BTN_SERVICE = 1u<<9, BTN_UP = 1u<<10, BTN_DOWN = 1u<<11, BTN_LEFT = 1u<<12,
  BTN_RIGHT = 1u<<13, BTN_OK = 1u<<14, BTN_BACK = 1u<<15, BTN_COIN = 1u<<16,
  BTN_START = 1u<<17, BTN_DEBUG = 1u<<18
};

typedef struct {
  uint32_t down;        /* held this tick                                  */
  uint32_t pressed;     /* went down this tick                             */
  int16_t  slider;      /* Hold'em bet slider: -32767..32767 axis, or 0     */
  int16_t  touch_x, touch_y; uint8_t touch;   /* in 1280x720 play space     */
} InputFrame;

#endif
