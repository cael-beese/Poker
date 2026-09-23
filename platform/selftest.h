/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* selftest.h - the service menu's self-test: checks that the game logic on
 * THIS machine computes what it must (evaluator, pay categories, paytable
 * returns, exact EV of the hint against brute force, shuffle and RNG,
 * Hold'em pot splitting, AI equity). Pure C, no raylib: it runs on a worker
 * thread and the tests could call it too. The service menu adds the checks
 * that need the platform (save directory write test, audio device, display). */
#ifndef BPL_PLATFORM_SELFTEST_H
#define BPL_PLATFORM_SELFTEST_H

#define SELFTEST_MAX 24

typedef struct {
    char   name[40];
    int    pass;
    char   detail[100];
    double ms;
} SelfTestItem;

typedef struct {
    SelfTestItem item[SELFTEST_MAX];
    int n, failures;
} SelfTestReport;

/* Appends one item (and counts a failure). */
void selftest_add(SelfTestReport *r, const char *name, int pass, double ms, const char *fmt, ...)
    __attribute__((format(printf, 5, 6)));

/* The pure checks. Takes ~0.5-2 s (the brute-force EV check dominates). */
void selftest_run_logic(SelfTestReport *r);

#endif
