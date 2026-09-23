/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* gputest.h - knobs of the GPU smoke-test mode (APP_GPUTEST). */
#ifndef BPL_PLATFORM_GPUTEST_H
#define BPL_PLATFORM_GPUTEST_H

#define GPUTEST_MAX_SPRITES 8000

/* Call before app_init(). sprites: 0..GPUTEST_MAX_SPRITES; shader: 0/1. */
void gputest_configure(int sprites, int shader);

#endif
