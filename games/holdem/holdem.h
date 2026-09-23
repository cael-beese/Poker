/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
#ifndef BPL_GAMES_HOLDEM_H
#define BPL_GAMES_HOLDEM_H

#include <stddef.h>
#include <stdint.h>

#include "ai/ai_view.h"
#include "engine/card.h"
#include "engine/deck.h"
#include "engine/event.h"
#include "engine/input.h"
#include "engine/rng.h"
#include "engine/wallet.h"

/* games/holdem - No-Limit Texas Hold'em, 6-max sit-and-go.

   Seats are 0..5 and clockwise is increasing seat number (seat s + 1 is on
   the left of seat s). Seat 0 is the human unless cfg.seat0_ai is set
   (attract mode, simulations), in which case the hooks play it as well.

   Rules decisions (the rest is standard No-Limit):
   - Moving button: the button advances to the next seat that still has
     chips, the small blind is the next such seat after it and the big blind
     the next after that. When a player busts someone may pay a blind twice
     or skip one; the dead-button rule is not used.
   - Heads-up: the button posts the small blind, acts first pre-flop and
     last on every later street. Dealing always starts left of the button.
   - Antes (per level, optional) are posted by every seat before the blinds
     and go straight into the pot as dead money.
   - A blind or ante larger than the stack puts that player all-in for what
     they have. The others must still call the FULL big blind; the short
     player can only win what they matched (main pot), and any excess over
     what anyone else put in is returned as an uncalled bet.
   - Min-raise: a raise must increase the bet by at least the largest full
     bet or raise on this street (the big blind to start each street). An
     all-in for less is allowed; it does not reopen the betting: a player
     who already acted since the last full raise may only call or fold,
     unless the total now facing them is itself at least a full raise
     (several short all-ins adding up), which reopens it (TDA rule).
     An all-in bet below the big blind is incomplete in the same way.
   - Nobody may raise when no other player in the hand could respond.
   - Uncalled bets are returned when each betting round ends.
   - Side pots: one pot per distinct all-in level; odd chips of a split go
     one at a time to the winners in clockwise order from the button.
   - All-in run-out: when at most one player can still act and the board is
     not complete, every live hand is turned face up and the remaining board
     is dealt with no betting.
   - Showdown order: the last player to bet or raise on the river shows
     first; with no river bet, the first live seat left of the button. A
     player who can win or tie some pot against the hands shown so far must
     show; one who cannot may muck (AI seats always muck then; the human is
     asked). An uncontested winner never has to show (AI mucks, human asked).
   - Players busting in the same hand: the one who started it with more
     chips finishes higher; equal stacks by seat order from the button.

   Human input (InputFrame, edges from `pressed`; the platform maps physical
   buttons and touch areas onto these logical ones):
     on the human's turn
       HOLD1           FOLD (ignored when checking is free)
       HOLD2           CHECK / CALL (calls all-in when short)
       HOLD3/4/5       set the amount to 1/2, 3/4, 1x pot
       BET MAX         set the amount to all-in
       BET ONE, UP, RIGHT   amount + one big blind;  DOWN, LEFT  - one
       slider          when it moves: min raise (-32767) .. all-in (+32767)
       DEAL or OK      BET / RAISE to the selected amount
     The amount is a "raise to" total, snapped to a multiple of the small
     blind, clamped to [min raise, all-in], and within one small blind of
     all-in becomes all-in. It is announced by EV_HOLDEM_AMOUNT. Two action
     buttons (HOLD1, HOLD2, DEAL/OK) on the same tick do nothing.
     show/muck prompt (EV_HOLDEM_SHOW_PROMPT): DEAL, OK or HOLD2 show;
       HOLD1 or BACK muck; muck when human_show_ticks run out.
     after the human busts (EV_HOLDEM_HUMAN_OUT): DEAL or OK watch the AI
       play on to a winner; BACK or CASH OUT end the game.

   Determinism: holdem_tick is the only thing that changes the game, and
   depends only on the game struct, the input frame and the AI hooks'
   decisions (which are themselves deterministic in their seed). */

#define HOLDEM_SEATS 6
#define HOLDEM_MAX_LEVELS 32
#define HOLDEM_MAX_POTS HOLDEM_SEATS
#define HOLDEM_AI_MIN_THINK 24        /* contract: collect no earlier       */
#define HOLDEM_AI_MAX_THINK 150       /* a wild think_ticks is clamped (2.5 s) */
#define HOLDEM_HIST_MAX 64

typedef struct { int32_t sb, bb, ante; } HoldemLevel;

typedef struct {
    int64_t start_stack;            /* every seat starts with this (1500)          */
    int64_t seat_stack[HOLDEM_SEATS];/* override per seat: 0 = start_stack, < 0 = empty seat */
    int     hands_per_level;        /* blinds go up every N hands (>= 1)           */
    int     nlevels;                /* the last level repeats forever              */
    HoldemLevel levels[HOLDEM_MAX_LEVELS];
    int     antes;                  /* 0 = ignore the schedule's antes             */
    int     first_button;           /* -1 = drawn from the session seed            */
    int     personality[HOLDEM_SEATS];/* copied into each seat's AiView            */
    int     seat0_ai;               /* 1 = seat 0 is played by the hooks too       */
    /* Pacing, in 60 Hz ticks. Every step waits at least 1 tick. */
    int t_hand_start;   /* after a hand ends, before the next is shuffled */
    int t_post;         /* between antes and each blind post              */
    int t_deal;         /* between hole cards                             */
    int t_action;       /* after an action, before the next turn          */
    int t_collect;      /* after a betting round, before bets move in     */
    int t_board;        /* between board cards                            */
    int t_street;       /* between streets of an all-in run-out           */
    int t_show;         /* between showdown reveals                       */
    int t_award;        /* between pot awards                             */
    int human_show_ticks;   /* show/muck prompt; muck when it runs out       */
    int human_turn_ticks;   /* 0 = wait forever; else check/fold on expiry   */
} HoldemConfig;

/* Default 6-max sit-and-go: 1500 chips, blinds up every 10 hands from 10/20,
   antes from 100/200, arcade pacing. */
void holdem_config_default(HoldemConfig *c);
/* Same rules with every delay at 1 tick (simulations, tests). */
void holdem_config_fast(HoldemConfig *c);

enum HoldemPhase {
    HP_HAND_START, HP_ANTES, HP_POST_SB, HP_POST_BB, HP_DEAL, HP_TURN, HP_AFTER_ACTION,
    HP_COLLECT, HP_BOARD, HP_TABLE, HP_SHOWDOWN, HP_UNCONTESTED, HP_AWARD, HP_HAND_END,
    HP_BUSTED,      /* the human is out: DEAL/OK watches on, BACK/CASH_OUT leaves */
    HP_GAME_OVER
};

enum { HOLDEM_SHOW_SHOWDOWN, HOLDEM_SHOW_ALLIN, HOLDEM_SHOW_VOLUNTARY };
enum { HOLDEM_OVER_WINNER, HOLDEM_OVER_HUMAN_LEFT };

/* Events (engine/event.h; a, b, v as listed). */
enum {
    EV_HOLDEM_HAND_START = 200, /* a hand number, b button seat, v level index        */
    EV_HOLDEM_LEVEL_UP,         /* a new level index, v big blind (holdem_level())    */
    EV_HOLDEM_ANTE,             /* a seat, b 1 if it left the seat all-in, v amount    */
    EV_HOLDEM_POST_SB,          /* a seat, b all-in flag, v amount                     */
    EV_HOLDEM_POST_BB,          /* a seat, b all-in flag, v amount                     */
    EV_HOLDEM_DEAL_HOLE,        /* a seat, b slot 0/1, v the card for seat 0, else -1  */
    EV_HOLDEM_TURN,             /* a seat, b 1 = the human must act now, v to call     */
    EV_HOLDEM_TELL,             /* a seat, v tell * 1000 (AI decision collected)       */
    EV_HOLDEM_AMOUNT,           /* the human's selected bet/raise-to changed, v amount */
    EV_HOLDEM_ACTION,           /* a seat, b ACT_*, v seat's total bet this street     */
    EV_HOLDEM_UNCALLED,         /* a seat, v uncalled chips returned to the stack      */
    EV_HOLDEM_BETS_TO_POT,      /* a street, v chips in the middle after collecting    */
    EV_HOLDEM_POT,              /* a pot index (0 = main), b eligible seat mask, v amount */
    EV_HOLDEM_BOARD,            /* a board index 0..4, b street it belongs to, v card  */
    EV_HOLDEM_STREET,           /* a street whose betting starts                       */
    EV_HOLDEM_RUNOUT,           /* hands are tabled, board runs out, no more betting   */
    EV_HOLDEM_SHOW,             /* a seat, b HOLDEM_SHOW_*, v c0 | c1<<8 | rank<<16    */
    EV_HOLDEM_HAND_RANK,        /* a seat, v rank: a tabled hand's final value         */
    EV_HOLDEM_MUCK,             /* a seat, b 1 = uncontested winner                    */
    EV_HOLDEM_SHOW_PROMPT,      /* a seat 0, b 0 losing at showdown / 1 uncontested    */
    EV_HOLDEM_AWARD,            /* a seat, b pot index, v chips won from that pot      */
    EV_HOLDEM_HAND_END,         /* a hand number                                       */
    EV_HOLDEM_HAND_LOG,         /* the hand's log line is ready (holdem_hand_log_line) */
    EV_HOLDEM_ELIMINATED,       /* a seat, b finishing place                           */
    EV_HOLDEM_HUMAN_OUT,        /* a finishing place: waiting for watch / leave        */
    EV_HOLDEM_GAME_OVER,        /* a winner seat (-1 if the human left), b HOLDEM_OVER_* */
    EV_HOLDEM_LAST_
};

typedef struct {
    int64_t stack;          /* chips behind                                   */
    int64_t bet;            /* chips in front, this street                    */
    int64_t committed;      /* everything put in this hand (antes, all bets)  */
    int64_t hand_start;     /* stack when the hand started                    */
    Card    hole[2];
    int     rank;           /* eval7 rank once known at showdown, else 0      */
    int     place;          /* finishing place, 0 while still playing         */
    uint8_t in_game;        /* still has chips (or never sat: see empty)      */
    uint8_t empty;          /* no player in this seat                         */
    uint8_t in_hand;        /* dealt into this hand                           */
    uint8_t folded, allin;
    uint8_t acted;          /* acted since the last full raise this street    */
    uint8_t shown, mucked;
} HoldemSeat;

typedef struct { int64_t amount; uint8_t eligible; } HoldemPot;

/* What the seat to act may do. min_to/max_to are "raise to" totals. */
typedef struct {
    int     seat;           /* -1 when nobody is to act                       */
    int     can_fold, can_check, can_call, can_bet, can_raise;
    int64_t to_call;        /* chips a call costs (all-in if short)          */
    int64_t call_to;        /* the seat's bet after calling                   */
    int64_t min_to, max_to; /* valid when can_bet or can_raise                */
} HoldemLegal;

typedef struct {
    HoldemConfig  cfg;
    HoldemAiHooks hooks;
    Rng      rng;           /* session stream: one seed per hand             */
    uint64_t hand_seed;
    Deck     deck;
    HoldemSeat seat[HOLDEM_SEATS];
    Card     board[5];
    int      nboard;
    uint32_t tick;
    int      phase, timer;
    int      hand_no, level, players_left;
    int      button, sb_seat, bb_seat;
    int      street;
    int      to_act;
    int64_t  cur_bet;       /* the bet to match this street (full BB pre-flop) */
    int64_t  last_raise;    /* size of the last full bet or raise this street  */
    int64_t  pot;           /* chips in the middle (earlier streets, antes)    */
    int64_t  chips_total;   /* conserved: stacks + bets + pot                  */
    int      last_aggr, river_aggr;
    int      nactions;      /* voluntary actions this hand (AI seed)           */
    uint8_t  history[HOLDEM_HIST_MAX];
    int      nhist;
    int      deal_idx, deal_order[HOLDEM_SEATS], ndeal;
    int      board_target;  /* board size the current dealing step aims for    */
    int      runout;        /* 1 once hands are tabled for a run-out           */
    /* the current turn */
    int      turn_ticks, ai_begun, ai_collected, ai_think;
    AiDecision ai_dec;
    int64_t  sel_amount;    /* the human's selected bet/raise-to               */
    int16_t  last_slider;
    int      prompt;        /* 1 while the human's show/muck prompt is open    */
    int      prompt_ticks;
    /* showdown and awards */
    int      show_order[HOLDEM_SEATS], nshow, show_idx;
    HoldemPot pots[HOLDEM_MAX_POTS];
    int      npots, award_idx;
    int64_t  won[HOLDEM_SEATS];   /* chips won this hand, for the log          */
    /* result */
    int      winner, over_reason, human_watching;
    uint64_t log_seed;
    char     log_result[240];
} HoldemGame;

/* ---- the game ------------------------------------------------------------ */

/* hooks may be NULL (or have NULL functions): AI seats then check or call.
   The hooks are stored in the game; after restoring a saved game call
   holdem_set_hooks again. */
void holdem_init(HoldemGame *g, const HoldemConfig *cfg, uint64_t seed, const HoldemAiHooks *hooks);
void holdem_set_hooks(HoldemGame *g, const HoldemAiHooks *hooks);
/* w may be NULL: sit-and-go chips are tournament chips, so the table never
   touches credits (the app charges a buy-in and pays by finishing place). */
void holdem_tick(HoldemGame *g, const InputFrame *in, Wallet *w, EventQueue *out);
/* If an AI seat is thinking (a worker may be running), collects and drops
   its decision. Call before discarding a game mid-hand. */
void holdem_release(HoldemGame *g);

/* ---- reading the state (presentation, tests) ----------------------------- */

void    holdem_legal(const HoldemGame *g, HoldemLegal *out);
int     holdem_is_human_turn(const HoldemGame *g);
int     holdem_seat_live(const HoldemGame *g, int seat);   /* in the hand, not folded */
int64_t holdem_pot_total(const HoldemGame *g);             /* middle + all bets */
HoldemLevel holdem_level(const HoldemGame *g);             /* antes zeroed if off */
/* Quick-bet sizes as legal raise-to totals: frac_pct 50, 75, 100 of the pot
   (pot after calling), snapped. 0 if the seat to act may not bet/raise. */
int64_t holdem_quick_bet(const HoldemGame *g, int frac_pct);
/* The AiView the table would build for seat now (tests, debug overlay). */
void    holdem_build_view(const HoldemGame *g, int seat, AiView *v);
/* 0 if the state is consistent; else -1 and a reason in why. */
int     holdem_check_invariants(const HoldemGame *g, char *why, size_t cap);
/* The finished hand's log line ("<time> holdem <seed> <result>\n"), valid
   after EV_HOLDEM_HAND_LOG. Returns the length as replay_format_hand does. */
int     holdem_hand_log_line(const HoldemGame *g, int64_t unix_time, char *out, size_t cap);

/* ---- pure pot arithmetic (holdem_pots.c) --------------------------------- */

/* Builds main and side pots from what each seat put in. live marks seats
   that can still win (in the hand, not folded). Returns the pot count; pot
   0 is the main pot. Chips put in above every live seat's total (only dead
   money from folded seats can be there) join the last pot. */
int  holdem_build_pots(const int64_t committed[HOLDEM_SEATS], const uint8_t live[HOLDEM_SEATS],
                       HoldemPot out[HOLDEM_MAX_POTS]);
/* Splits amount among the seats in winners (a mask): equal shares, and the
   odd chips one each to the winners in clockwise order starting left of the
   button. Adds to share[]. */
void holdem_split_pot(int64_t amount, uint8_t winners, int button, int64_t share[HOLDEM_SEATS]);

#endif
