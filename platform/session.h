/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* session.h - what lives for the whole run, across the app states: the
 * credits on disk, stats, operator settings, the coin mechanism, the idle
 * timer (for attract), the hand log, the AI worker pool, the audio device and
 * the menu's choices.
 *
 * Persistence rule. The file always holds the credits the player would be
 * owed if the power went now:
 *     saved credits = wallet + pending
 * where `pending` is what the current game holds on the player's behalf
 * (session_set_pending): the bet of a Draw hand in progress before any win
 * (an interrupted hand is void and the bet goes back), a win on the meter not
 * yet collected, the Hold'em buy-in while the player is still in the
 * sit-and-go. The file is rewritten whenever that figure changes and after
 * every hand (stats), coin, service-menu change and on exit - always at the
 * end of a tick, i.e. at a consistent point, never halfway through a hand's
 * bookkeeping. Writing happens on a background thread (save.h does the
 * temp + fsync + rename + fsync-dir sequence), so an SD card that takes
 * 20 ms to fsync never costs a frame.
 *
 * If the save directory cannot be written the game runs from RAM and the
 * service menu says so. */
#ifndef BPL_PLATFORM_SESSION_H
#define BPL_PLATFORM_SESSION_H

#include <stdint.h>
#include <stdio.h>

#include "platform/app.h"
#include "platform/settings.h"

/* App events owned by the session (the 300-399 app range; 320+ is ours). */
enum {
    APP_EV_COIN = 320,          /* a coin was credited: v = credits added        */
    APP_EV_CREDITS = 321,       /* the service menu changed credits: v = now      */
};

enum { MENU_GAME_JOB, MENU_GAME_BONUS, MENU_GAME_DEUCES, MENU_GAME_HOLDEM, MENU_NGAMES };
enum { PENDING_DRAW, PENDING_HOLDEM, PENDING_SLOTS };

#define SESSION_IDLE_ATTRACT_TICKS (60u * 60u)   /* SPEC: attract after 60 s idle */

typedef struct {
    char     save_dir[512];
    char     assets_dir[512];
    int      ram_only;              /* saves are not reaching the disk           */
    char     save_error[160];
    int      loaded_from;           /* SAVE_SRC_* at start-up                    */
    int      corrupt_files;         /* rejected at start-up (torn / bad CRC)     */
    unsigned saves, save_failures;
    double   save_last_ms, save_max_ms, save_total_ms;
    int      audio_ok;
    int      hand_log_ok;
    double   init_ms, audio_ms;     /* session start-up cost, of which audio     */
} SessionInfo;

/* [game] keys of config.ini: save_dir, assets_dir, hand_log (on/off).
 * Called by the config loader before app_init. 0 ok, -1 unknown / bad. */
int  session_config_set(const char *key, const char *value);

/* Called by app.c. */
void session_init(AppCtx *ctx);                           /* in app_init, before the modes */
void session_before_tick(AppCtx *ctx, const InputFrame *in);
void session_after_tick(AppCtx *ctx);
void session_shutdown(AppCtx *ctx);                        /* after the modes' shutdown */

/* Live settings and stats. After changing them call the _changed function:
 * it applies them (volumes) and schedules a save at the end of the tick. */
Settings *session_settings(void);
void      session_settings_changed(void);
Stats    *session_stats(void);
void      session_stats_changed(void);

/* Credits the current game holds for the player (see the rule above). */
void session_set_pending(int slot, int64_t credits);

/* Service menu: add / reset credits (counted in the stats, saved). */
void session_add_credits(int64_t n);
void session_reset_credits(void);
void session_reset_stats(void);

/* Blocks until every scheduled save has been written (or failed). */
void session_flush(void);

/* The hand log (<save_dir>/hands.log), NULL when running from RAM. */
FILE *session_hand_log(void);

uint32_t session_idle_ticks(void);

int  session_menu_game(void);
void session_set_menu_game(int g);
int  session_hint_on(void);
void session_set_hint_on(int on);

struct AiPool *session_ai_pool(void);      /* created on first use */
uint64_t session_next_seed(void);          /* the session's seed stream */

const SessionInfo *session_info(void);

#endif
