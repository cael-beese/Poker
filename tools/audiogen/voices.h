/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* voices.h - one-shot instruments shared by the SFX and the music.
 *
 * Every voice ADDS into a Buf at sample `at` with gain `amp` and pan
 * (-1 left .. +1 right). Voices that need variation take an Rng, so the
 * caller decides the seed and the result stays deterministic.
 */
#ifndef AUDIOGEN_VOICES_H
#define AUDIOGEN_VOICES_H

#include "dsp.h"

/* Damped sinusoid bank: n modes of frequency f, amplitude a, decay tau (s). */
void v_modal(Buf *b, long at, double amp, double pan,
             const double *f, const double *a, const double *tau, int n);

enum { NF_LP, NF_HP, NF_BP };
/* Filtered noise burst: linear attack `att`, exponential decay `tau`. */
void v_noise(Buf *b, long at, double amp, double pan, double att, double tau,
             int ftype, double f, double Q, Rng *r);

/* Filtered noise swept from f0 to f1 over dur, envelope shape:
   0 = swell and fade (sine), 1 = rise to the end (riser), 2 = fall. */
void v_sweep(Buf *b, long at, double dur, double f0, double f1, double Q,
             double amp, double pan0, double pan1, int shape, Rng *r);

void v_chip(Buf *b, long at, double amp, double pan, Rng *r);    /* clay chip click   */
void v_coin(Buf *b, long at, double amp, double pan, Rng *r);    /* metal coin ring   */
void v_bell(Buf *b, long at, double f, double amp, double pan, double tau);
void v_vibe(Buf *b, long at, double f, double amp, double pan, double tau);
void v_glass(Buf *b, long at, double amp, double pan, Rng *r);   /* glass clink       */

/* Synth brass section note, len samples long (plus release). */
void v_brass(Buf *b, long at, long len, double f, double amp, double pan, Rng *r);

/* Drums. */
void v_kick(Buf *b, long at, double amp, double pan);
void v_snare(Buf *b, long at, double amp, double pan, Rng *r);
void v_clap(Buf *b, long at, double amp, double pan, Rng *r);
void v_hat(Buf *b, long at, double amp, double pan, double tau, Rng *r);
void v_rim(Buf *b, long at, double amp, double pan, Rng *r);
void v_shaker(Buf *b, long at, double amp, double pan, Rng *r);
void v_crash(Buf *b, long at, double amp, double tau, Rng *r);    /* stereo spread    */
void v_boom(Buf *b, long at, double amp, double pan);             /* cinematic sub hit */
void v_timpani(Buf *b, long at, double f, double amp, double pan, Rng *r);
void v_tom(Buf *b, long at, double f, double amp, double pan, Rng *r);

#endif
