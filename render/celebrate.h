/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* celebrate.h - win-tier celebrations, built from the kit's pieces.
 *
 *   WIN_SMALL    pulse ring + twinkles at the winning spot, chime        ~1.2 s
 *   WIN_MEDIUM   coin burst + "WINNER" banner, sparks                    ~2.4 s
 *   WIN_BIG      shake, bulb chase, count-up meter, coin fountains,
 *                "BIG WIN" banner with sunburst                          ~4.5 s
 *   WIN_JACKPOT  full takeover: hit-pause, white flash, slow motion, the
 *                room dims, two spotlights sweep, fireworks, coin shower,
 *                honey fountains, confetti, count-up, fanfare            ~7.0 s
 * Every tier can be skipped (celeb_skip, "any button"): the meter lands on
 * its final value and everything fades out in 0.35 s.
 *
 * The celebration never touches game state. Sound goes out through a cue
 * callback, so the presentation maps cues to audio_play() (render/ does not
 * link audio). It drives the shared Shake, FxClock and BulbRing it is given. */
#ifndef BPL_RENDER_CELEBRATE_H
#define BPL_RENDER_CELEBRATE_H

#include "raylib.h"
#include "render/fx.h"
#include "render/ui.h"

typedef enum { WIN_NONE, WIN_SMALL, WIN_MEDIUM, WIN_BIG, WIN_JACKPOT } WinTier;

typedef enum {
    CUE_CHIME,          /* small win: SFX_WIN_SMALL           */
    CUE_COINS,          /* medium: SFX_WIN_MEDIUM             */
    CUE_BIG,            /* big: SFX_WIN_BIG                    */
    CUE_JACKPOT,        /* jackpot fanfare: SFX_WIN_JACKPOT    */
    CUE_REVEAL,         /* the hit at the start: SFX_REVEAL    */
    CUE_TICK,           /* count-up tick, a = pitch: SFX_CREDIT_TICK */
    CUE_COUNT_END,      /* SFX_CREDIT_END                      */
    CUE_FIREWORK,       /* a shell bursts: (optional)          */
    CUE_SKIP,           /* skipped: stop the fanfare           */
    CUE_DONE
} CelebCue;

typedef void (*CelebCueFn)(void *user, CelebCue cue, float a);

typedef struct {
    WinTier tier;
    int     active;
    float   t, dur, out;      /* out: 0..1 fade-out progress once ending */
    int     skipping;
    long long amount;
    char    title[32];
    Vector2 focus;            /* the winning cards' centre              */
    Meter   meter;
    Flash   flash;
    float   dim;              /* current room dim, 1 = none             */
    float   spot;             /* spotlight intensity                    */
    int     step;             /* timeline position                      */
    float   next_fx;
    CelebCueFn cue;
    void   *user;
    Shake    *shake;
    FxClock  *clock;
    BulbRing *bulbs;
} Celebration;

void celeb_init(Celebration *c, Shake *shake, FxClock *clock, BulbRing *bulbs, CelebCueFn cue, void *user);
/* title NULL = the tier's default ("WINNER", "BIG WIN", "JACKPOT"). */
void celeb_start(Celebration *c, WinTier tier, long long amount, const char *title, Vector2 focus);
void celeb_skip(Celebration *c);
/* dt = the presentation's (clock-scaled) dt; real_dt drives timers that
 * must not slow down (the takeover's length). */
void celeb_update(Celebration *c, float dt, float real_dt);
/* The multiplier for the game layer under the celebration (tint it with
 * gfx_set_tint(dim, dim, dim)). */
float celeb_dim(const Celebration *c);
/* Layers: back = spotlights (after the game layer, before particles);
 * front = banner, meter and flash (after particles). */
void celeb_draw_back(const Celebration *c, double time);
void celeb_draw_front(const Celebration *c, double time);

#endif
