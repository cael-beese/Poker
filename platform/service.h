/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* service.h - the operator's service menu (service.c). */
#ifndef BPL_PLATFORM_SERVICE_H
#define BPL_PLATFORM_SERVICE_H

#include "platform/app.h"

/* Opens the service menu on its LEARN PANEL wizard. autorun = offered by the
 * game on its first start (panel.h): nobody pressing anything cancels it
 * quietly, and it returns to attract when done. */
void service_request_learn(AppCtx *ctx, int autorun);

#endif
