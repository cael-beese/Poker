/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* panel.h - the cabinet's control panel: where each button is, which encoder
 * input it is wired to, and what it does in the game.
 *
 * The panel has two sides (player 1 left, player 2 right), each a stick, two
 * rows of three buttons, and SELECT and START. The game lays its video-poker
 * deck out over it by POSITION, so the on-screen labels, the CONTROLS screen
 * and the README can all say "top row, far left" and be right on any wiring:
 *
 *        P1 side                      P2 side
 *   top:  HOLD 1  HOLD 2  HOLD 3  |  HOLD 4  HOLD 5  DEAL
 *   bot:  CASH OUT BET ONE BET MAX |  -       -       DEAL
 *         SELECT = COIN, START     |  SELECT = COIN, START held 3 s = SERVICE
 *
 * The top rows of both sides read as one line of six, like a video-poker
 * machine's deck: each HOLD sits under its card and DEAL/DRAW is last.
 *
 * Which encoder input is in which position (the "wiring") comes from, last
 * wins: the built-in guess (RetroPie's usual 6-button layout, Y X L over
 * B A R, player 1 = the first joystick), [panel] in config.ini, and
 * <save dir>/panel.ini, written by the LEARN PANEL wizard in the service menu
 * (which asks for each position in turn). */
#ifndef BPL_PLATFORM_PANEL_H
#define BPL_PLATFORM_PANEL_H

#include <stdint.h>
#include "platform/input.h"

typedef enum {
    PS_P1_T1, PS_P1_T2, PS_P1_T3, PS_P1_B1, PS_P1_B2, PS_P1_B3, PS_P1_SELECT, PS_P1_START,
    PS_P2_T1, PS_P2_T2, PS_P2_T3, PS_P2_B1, PS_P2_B2, PS_P2_B3, PS_P2_SELECT, PS_P2_START,
    PS_COUNT
} PanelSlot;
#define PS_PER_SIDE 8

typedef struct {
    int8_t joy;       /* joystick index (JOY<n>)            */
    int8_t button;    /* button number (_B<k>), -1 = none    */
} PanelWire;

/* ---- set-up (platform/options.c, main.c) --------------------------------- */
void panel_defaults(void);                                  /* the built-in wiring  */
int  panel_config_set(const char *key, const char *value);  /* [panel] P1_T1 = JOY0_B3; 0 ok */
/* After session_init (the save directory is known): reads panel.ini, then
 * rebuilds *live as base plus the panel buttons. live stays the config the
 * input layer samples, so a learned wiring takes effect at once. */
void panel_init(const char *save_dir, const InputConfig *base, InputConfig *live);
/* Interactive run (no --script / --frames) on a machine with joysticks: only
 * then does the game offer the wizard and the CONTROLS screen by itself. */
void panel_set_interactive(int on);

/* ---- the layout ------------------------------------------------------------ */
uint32_t    panel_slot_buttons(PanelSlot s);   /* logical buttons it drives, 0 = none */
int         panel_slot_hold(PanelSlot s);      /* seconds to hold (SERVICE), else 0   */
int         panel_slot_of(uint32_t btn);       /* first slot driving btn, -1 = none   */
const char *panel_slot_name(PanelSlot s);      /* "P1 TOP LEFT", "P2 START", ...       */
const char *panel_slot_label(PanelSlot s);     /* what it does: "HOLD 1", "COIN", ...  */
PanelWire   panel_wire(PanelSlot s);
int         panel_slot_down(PanelSlot s);      /* the wired input is held now          */

/* ---- learning ---------------------------------------------------------------- */
int  panel_learned(void);          /* the wiring came from the wizard              */
int  panel_wizard_due(void);       /* interactive, and the wizard was never offered */
void panel_wizard_offered(void);   /* remember it was (saved)                      */
int  panel_controls_due(void);     /* interactive, CONTROLS screen never shown yet  */
void panel_controls_shown(void);   /* remember it was (saved)                      */
/* Suspends the panel buttons (keyboard only) while the wizard listens to the
 * raw encoders, so learning presses do nothing else. */
void panel_suspend(int on);
/* The wizard's result: applied at once and saved to panel.ini. 0 saved. */
int  panel_set_wiring(const PanelWire w[PS_COUNT]);

/* The panel.ini text for a wiring (tests; also what panel_set_wiring saves). */
int  panel_format(char *out, int n, const PanelWire w[PS_COUNT], int learned, int wizard_offered, int controls_shown);

#endif
