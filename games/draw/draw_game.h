/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
#ifndef BPL_DRAW_GAME_H
#define BPL_DRAW_GAME_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "draw_rules.h"
#include "engine/card.h"
#include "engine/deck.h"
#include "engine/event.h"
#include "engine/input.h"
#include "engine/rng.h"
#include "engine/wallet.h"

/* games/draw/draw_game.h - the Draw Poker state machine (CONTRACT section 3).

   One flat struct, advanced only by draw_tick at 60 Hz, deterministic in
   (seed, config, input frames). Credits move only through the Wallet: the
   bet is debited when the hand is dealt, the win is credited when it is
   collected (after any double-up).

   Controls
     IDLE      BET ONE: bet 1..5, wrapping.  BET MAX: bet 5 and deal.
               DEAL: deal at the current bet.  UP/DOWN: change variant
               (if cfg.allow_variant_select).
     HOLD      HOLD1..5 toggle; DEAL draws.
     OFFER     (after a win, double-up available) HOLD1 or OK: double up.
               HOLD5, CASH OUT, BACK: collect.  DEAL: collect and deal the
               next hand.  BET MAX: collect, bet 5, deal.  BET ONE: collect,
               bet + 1.  No input for cfg.auto_collect_ticks: collect.
     DOUBLE    guess the colour of one card from a fresh 52-card shuffle:
               LEFT, HOLD1, HOLD2 = RED; RIGHT, HOLD4, HOLD5 = BLACK.
               HOLD3, CASH OUT, BACK: collect.  DEAL, BET MAX, BET ONE: as
               in OFFER, and no input for cfg.auto_collect_ticks collects.
               Right: the win doubles; wrong: it is lost. Exactly 26/52.
   To quit mid-hand without losing a pending win, feed one frame with CASH
   OUT pressed (it collects in OFFER and DOUBLE); draw_pending() says what is
   on the meter.
   Double-up limits: at most cfg.double_max_rounds rounds per hand (5), and
   a round is offered only while 2 x stake <= cfg.double_cap_mult x bet
   (2000 x bet). When no further round is allowed the win is collected.

   Timing (ticks, all from DrawConfig; events fire on the tick things
   happen, so presentation animates from the event):
     deal:  card i DEAL_CARD at i*deal_gap, FLIP_CARD at i*deal_gap +
            flip_delay; HOLD_PHASE on the last flip. The DEAL press tick is
            tick 0, so card 0 answers the button on the same frame.
     draw:  DISCARD for every replaced card on the DRAW press tick; the k-th
            replaced card DRAW_CARD at k*deal_gap, FLIP_CARD flip_delay
            later; WIN / NO_WIN result_delay after the last flip (or after
            the press if every card was held).
     double: DOUBLE_CARD double_reveal ticks after the guess.

   Randomness (CONTRACT section 5): the game Rng is seeded by draw_init;
   each hand takes one output of it as the hand seed, seeds the hand Rng
   with it, shuffles a full deck (Fisher-Yates) and deals from the top: five
   cards, then the replacements in position order. Every double-up round
   shuffles a fresh full deck from the hand Rng and turns the top card. The
   hand seed therefore replays the whole hand, and is what the hand log
   records. */

enum DrawState {
    DS_IDLE = 0,       /* between hands (the last hand stays on the table) */
    DS_DEALING,
    DS_HOLD,
    DS_DRAWING,
    DS_OFFER,          /* a win on the meter; double up or collect         */
    DS_DOUBLE,         /* waiting for a red / black guess                  */
    DS_DOUBLE_REVEAL,
    DS_COUNT
};

/* Events (engine/event.h; draw owns 100-199). a, b, v as listed. */
enum DrawEventType {
    EV_DRAW_VARIANT = 100,   /* a = new variant                                          */
    EV_DRAW_BET,             /* a = new bet 1..5, b = 1 if set by BET MAX                */
    EV_DRAW_DENIED,          /* a = DRAW_DENY_*: a button was refused                    */
    EV_DRAW_CREDITS,         /* credit meter changed: v = wallet credits now (clamped to
                                int32), a = DRAW_CREDITS_BET (debit) / _COLLECT (credit),
                                b = 0                                                    */
    EV_DRAW_HAND_START,      /* a = variant, b = bet, v = hand number (from 1)           */
    EV_DRAW_DEAL_CARD,       /* a = position 0..4, b = card: leaves the shoe face down   */
    EV_DRAW_FLIP_CARD,       /* a = position, b = card: turns face up (deal and draw)    */
    EV_DRAW_HOLD_PHASE,      /* holds may be set now: a = pay category of the five dealt
                                cards (light that paytable row), v = what they would pay  */
    EV_DRAW_HOLD,            /* a = position, b = 1 held / 0 released                    */
    EV_DRAW_DISCARD,         /* a = position: a card not held is removed at DRAW          */
    EV_DRAW_DRAW_CARD,       /* a = position, b = card: replacement leaves the shoe      */
    EV_DRAW_WIN,             /* a = category, b = DrawTier, v = credits won               */
    EV_DRAW_NO_WIN,          /* the hand paid nothing                                    */
    EV_DRAW_DOUBLE_OFFER,    /* double up available: a = round it would be (1..), v = stake */
    EV_DRAW_DOUBLE_START,    /* a = round, v = stake; a face-down card awaits the guess  */
    EV_DRAW_DOUBLE_GUESS,    /* a = round, b = DRAW_RED / DRAW_BLACK                     */
    EV_DRAW_DOUBLE_CARD,     /* a = round, b = card: turned face up                      */
    EV_DRAW_DOUBLE_WIN,      /* a = round, b = DrawTier of the new amount, v = new amount */
    EV_DRAW_DOUBLE_LOSE,     /* a = round, v = stake lost                                */
    EV_DRAW_COLLECT,         /* v = credits moved from the win meter to the wallet       */
    EV_DRAW_HAND_END,        /* a = final category, b = double-up rounds won, v = credits
                                paid for the hand; the hand log line is ready            */
    EV_DRAW_LAST_TYPE
};

enum { DRAW_DENY_CREDITS = 1, DRAW_DENY_VARIANT_LOCKED = 2 };
enum { DRAW_CREDITS_BET = 0, DRAW_CREDITS_COLLECT = 1 };
enum { DRAW_RED = 0, DRAW_BLACK = 1 };

typedef struct {
    int32_t variant;              /* DRAW_JOB, DRAW_BONUS, DRAW_DEUCES      */
    int32_t allow_variant_select; /* UP/DOWN changes variant between hands  */
    int32_t bet;                  /* starting bet, 1..5                     */
    int32_t double_up;            /* offer double-up after a win            */
    int32_t double_max_rounds;    /* per hand                               */
    int32_t double_cap_mult;      /* offer only while 2*stake <= this * bet */
    int32_t deal_gap_ticks;       /* between successive cards               */
    int32_t flip_delay_ticks;     /* from a card leaving the shoe to its flip */
    int32_t result_delay_ticks;   /* last flip to WIN / NO_WIN              */
    int32_t double_reveal_ticks;  /* guess to DOUBLE_CARD                   */
    int32_t auto_collect_ticks;   /* idle OFFER / DOUBLE collects; 0 = never */
} DrawConfig;

/* JoB, variant select on, bet 5, double-up on (5 rounds, 2000 x bet),
   deal gap 6, flip delay 12, result delay 8, reveal 30, auto-collect 1800
   (30 s, before the platform's 60 s attract timer). */
void draw_config_default(DrawConfig *c);

/* A finished hand, kept for the hand log and the app's stats. It is filled
   when EV_DRAW_HAND_END fires and survives the next hand starting on the
   same tick (DEAL in the double-up offer collects and deals at once). */
typedef struct {
    uint64_t seed;
    uint32_t hand_no;
    int32_t  variant, bet;
    Card     dealt[5], final[5];
    uint8_t  held;
    int32_t  cat, win;      /* pay category and pay before double-up          */
    int32_t  dbl_won, dbl_round;
    int32_t  paid;          /* credited to the wallet (0 if lost in double-up) */
} DrawHandRecord;

typedef struct {
    DrawConfig cfg;
    Rng      rng;           /* session stream: one output per hand            */
    Rng      hand_rng;      /* this hand: the deal and the double-up shuffles */
    uint64_t hand_seed;     /* logged; replays the hand                       */
    uint32_t hand_no;       /* hands dealt this session                       */
    int32_t  state;         /* DrawState                                      */
    int32_t  t;             /* ticks since the state began                    */
    int32_t  variant, bet;
    Deck     deck;
    Card     cards[5];      /* on the table (CARD_NONE before the first hand) */
    Card     dealt[5];      /* the first five of this hand                    */
    uint8_t  held;          /* hold mask (bit i = card i)                     */
    uint8_t  face_up;       /* cards currently face up                        */
    uint8_t  replaced;      /* positions replaced in the draw                 */
    uint8_t  n_replaced;
    uint8_t  replace_pos[5];/* replaced positions in dealing order            */
    uint8_t  in_hand;       /* 1 from deal to hand end                        */
    int32_t  dealt_cat;     /* pay category of the dealt five                 */
    int32_t  final_cat;     /* after the draw (DC_NONE until then)            */
    int32_t  win;           /* the hand's pay before double-up                */
    int32_t  meter;         /* uncollected win (the WIN meter)                */
    int32_t  dbl_round;     /* double-up rounds played this hand              */
    int32_t  dbl_won;       /* ... and won                                    */
    int32_t  dbl_guess;     /* DRAW_RED / DRAW_BLACK                          */
    Card     dbl_card;      /* last double-up card (CARD_NONE until turned)   */
    Card     dbl_hist[8];   /* double-up cards this hand, in order            */
    int32_t  paid;          /* credited for the last finished hand            */
    DrawHandRecord last;    /* the last finished hand (valid if hand_no > 0)  */
} DrawGame;

void draw_init(DrawGame *g, const DrawConfig *cfg, uint64_t seed);
void draw_tick(DrawGame *g, const InputFrame *in, Wallet *w, EventQueue *out);

/* Credits won but not yet collected (the app may show or save them). */
static inline int32_t draw_pending(const DrawGame *g) { return g->meter; }

const char *draw_state_name(int state);
const char *draw_event_name(int type);

/* The hand log (engine/replay.h), for the app to write on EV_DRAW_HAND_END:
       "<unix-time> draw <hand-seed> JOB bet=5 deal=AsKd7h7c2s hold=00110
        final=9h9d7h7c9s win=45 dbl=2/3 paid=180 FULL HOUSE"
   dbl=<won>/<played>. Both describe g->last, the hand that just ended.
   draw_hand_result formats the result part (returns like snprintf);
   draw_write_hand_log appends the whole line. */
int draw_hand_result(const DrawGame *g, char *out, size_t cap);
int draw_write_hand_log(const DrawGame *g, FILE *f, int64_t unix_time);

#endif
