/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* settings.h - what the cabinet remembers: credits, statistics and operator
 * settings (SPEC section 6), and how that is written as text.
 *
 * Pure C (no raylib), so the tests link it. platform/save.h stores it on disk
 * atomically; platform/session.h owns the live copy.
 *
 * The payload is "key=value" lines. Unknown keys are ignored and missing keys
 * keep their defaults, so a newer build reads an older file and the other way
 * round; every numeric setting is clamped to its range when read, so a file
 * edited by hand cannot put the game in a bad state. */
#ifndef BPL_PLATFORM_SETTINGS_H
#define BPL_PLATFORM_SETTINGS_H

#include <stddef.h>
#include <stdint.h>

#include "platform/effects.h"

#define SAVE_VARIANTS 3        /* Draw Poker variants: JoB, Bonus, Deuces     */
#define SAVE_SEATS    6

typedef struct {
    uint8_t  vol_master, vol_music, vol_sfx;   /* 0..10                          */
    uint8_t  music_on;                          /* 0/1                            */
    uint8_t  difficulty;                        /* AI_DIFF_EASY..EXPERT (0..3)    */
    uint8_t  hint_default;                      /* strategy hint on at start      */
    uint8_t  double_up;                         /* offer double-up after a win    */
    uint16_t denom_cents;                       /* what one credit is shown as    */
    uint16_t coin_cents;                        /* value of one COIN press        */
    uint32_t holdem_buyin;                      /* credits per sit-and-go         */
    EffectSettings effects;
} Settings;

typedef struct {
    uint64_t draw_hands[SAVE_VARIANTS];
    uint64_t draw_bet[SAVE_VARIANTS];           /* credits wagered                */
    uint64_t draw_paid[SAVE_VARIANTS];          /* credits paid (after double-up) */
    uint64_t royals, four_deuces;
    uint64_t dbl_played, dbl_won;               /* double-up rounds               */
    int64_t  biggest_win;                       /* one Draw hand, credits paid    */
    uint64_t holdem_hands;                      /* hands the player sat in        */
    uint64_t holdem_games, holdem_wins;
    uint64_t holdem_places[SAVE_SEATS];         /* [0] = 1st place ...            */
    uint64_t holdem_buyins, holdem_prizes;      /* credits                        */
    uint64_t coins_in;                          /* COIN presses                   */
    uint64_t credits_added;                     /* by the service menu            */
    uint64_t credit_resets;
} Stats;

typedef struct {
    int64_t  credits;
    Settings settings;
    Stats    stats;
} SaveData;

void settings_defaults(Settings *s);
void save_data_defaults(SaveData *d);

/* Writes the payload. Returns its length (like snprintf: >= cap means it was
 * cut), or -1. SAVE_PAYLOAD_MAX is always enough. */
#define SAVE_PAYLOAD_MAX 8192
int  save_data_format(const SaveData *d, char *out, size_t cap);

/* Reads a payload into d (which should hold defaults first). Returns the
 * number of lines that were not understood (0 = all good); never fails. */
int  save_data_parse(SaveData *d, const char *payload, size_t len);

/* Credits for one COIN press: coin value / denomination, at least 1. */
int64_t settings_coin_credits(const Settings *s);

/* Allowed values, for the service menu's LEFT / RIGHT. */
extern const uint16_t settings_denoms[];      extern const int settings_ndenoms;
extern const uint16_t settings_coins[];       extern const int settings_ncoins;
extern const uint32_t settings_buyins[];      extern const int settings_nbuyins;

/* The sit-and-go prize for a finishing place (1..6) at a buy-in: six buy-ins
 * make the pool and it pays 50 / 30 / 20 % to 1st / 2nd / 3rd, i.e. 3.0x,
 * 1.8x and 1.2x the buy-in. An average player gets their buy-in back:
 * (3.0 + 1.8 + 1.2) / 6 = 1. Places 4-6 pay nothing. */
int64_t sng_prize(int64_t buyin, int place);

#endif
