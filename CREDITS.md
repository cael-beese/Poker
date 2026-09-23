# Credits

## Audio

Every sound and every piece of music in `assets/sfx/` and `assets/music/` is
**original**, made for Beese's Poker Lounge, and is under **the same license as
the game** (PolyForm Noncommercial 1.0.0, see `LICENSE.md`). No samples,
recordings, sound libraries or third-party audio were used.

All of it is synthesised from scratch by the offline generator in
`tools/audiogen/` (C11, part of this repository): band-limited (PolyBLEP)
oscillators, FM, filtered white and pink noise, modal (damped-sinusoid)
resonators for chips, coins, bells and knocks, envelopes, biquad and
state-variable filters, a Freeverb-style reverb, a ping-pong delay, and a
small step sequencer for the music. The music and the melodies were composed
for the game and are written as data in `tools/audiogen/music.c`.

The generator is deterministic. `tools/audiogen/build.sh` renders everything,
encodes the loops with `oggenc` (vorbis-tools, used only as an encoder, with
fixed stream serials) and prints a level report; `build.sh --check` renders
again into `out/` and confirms the result is bit-identical to the committed
files.

| file | what it is | how it is made |
|---|---|---|
| `sfx/card_deal_1..3.wav` | a dealt card landing | rising noise swish into a high-passed paper snap, a band-passed card-flex body and a felt thud |
| `sfx/card_flip_1..3.wav` | a card turned face up | lift click, air sweep, then the slap |
| `sfx/card_slide_1..2.wav` | a card slid across felt | grainy band-passed friction noise ending in a tap |
| `sfx/card_shuffle.wav` | riffle shuffle | 52 accelerating band-passed flicks, a cascading bridge, two taps squaring the deck |
| `sfx/fold_1..2.wav` | cards tossed into the muck | falling noise sweep and a soft slap |
| `sfx/check.wav` | two knuckle knocks on the table | modal wood/felt resonances plus noise |
| `sfx/button_1..3.wav` | arcade button click | noise transient and modal plastic body, three tunings |
| `sfx/hold_on.wav`, `hold_off.wav` | HOLD toggled | FM chirp gliding up (on) or down (off) with a click |
| `sfx/menu_move.wav`, `menu_select.wav` | menu cursor and confirm | short FM blips |
| `sfx/service_beep.wav` | service-menu beep | 1 kHz sine with a little second harmonic |
| `sfx/error.wav` | deny | two beating square-wave buzzes, low-passed |
| `sfx/neon_flicker.wav` | neon sign flicker | gated 120 Hz transformer buzz with crackle |
| `sfx/bet_one.wav`, `bet_max.wav` | betting | chip click with bell chimes; bet max is a rising five-chime run with coins |
| `sfx/credit_tick.wav` | count-up tick | a pure C7 tick, made to be pitched up by the game |
| `sfx/credit_end.wav` | count-up finished | bell chord |
| `sfx/coin_insert.wav` | coin in | coin strike, roll, clunk into the box, two-note credit chime |
| `sfx/cash_out.wav` | pay-out | hopper motor, coins at eleven a second, closing chime |
| `sfx/chip_single_1..4.wav`, `chip_stack_1..3.wav`, `chip_pot_1..3.wav` | poker chips | modal clay-chip impacts (randomised per hit), bounces, felt slide noise |
| `sfx/win_small.wav` | small win | bell arpeggio over a soft sine pulse, reverb |
| `sfx/win_medium.wav` | medium win | riser, six-note bell run, a burst of 26 coins |
| `sfx/win_big.wav` | big win | synth-brass pickup and C major hit, sub boom, timpani, cymbal, bell run, coin shower |
| `sfx/win_jackpot.wav` | royal flush / big pot | a 7.2 s composed brass fanfare with harmony, chords, timpani, snare roll, cymbals, bell glissando and coins |
| `sfx/double_win.wav`, `double_lose.wav` | double-up outcome | brass "ta-da" with bells; falling vibraphone line with a closing filter sweep |
| `sfx/all_in.wav` | all in | chip shove, sub boom, timpani, low C minor brass stab, airy sweep |
| `sfx/reveal.wav` | showdown hit | boom, timpani, bells, cymbal |
| `sfx/your_turn.wav` | your turn | two-note vibraphone cue |
| `sfx/blinds_up.wav` | blinds rise | two bell strikes |
| `sfx/bust.wav` | eliminated | falling vibraphone line over a closing pad |
| `sfx/whoosh.wav` | transition | panned band-pass noise sweep |
| `music/lounge_loop.ogg` | in-game music, 1:57, seamless loop | 48 bars at 98 bpm in C minor: FM electric piano, synth bass, detuned-saw pad, plucked arpeggio, glide lead, synthesised drums; stems balanced to fixed levels, reverb and delay tails folded onto the loop start |
| `music/attract_loop.ogg` | attract-mode music, 0:39, seamless loop | 16 bars of the same material at full energy |
| `music/ambience_lounge.ogg` | bar crowd murmur, 0:40, seamless loop | sixteen synthetic "talkers" (glottal buzz through moving vowel formants, in syllables and phrases), glass clinks, room tone, a large reverb; crossfaded loop point |

## Fonts

Four typefaces, all under the **SIL Open Font License 1.1**, taken unmodified
from the Google Fonts repository (`github.com/google/fonts`, branch `main`,
commit `b5efa9c3`, fetched 2026-09-23). Each license travels with its font as
`assets/fonts/<Family>-OFL.txt`. The game rasterises them into its texture
atlas at start-up; it does not modify or redistribute them in any other form.

| file | family, designer | used for | source |
|---|---|---|---|
| `fonts/BarlowCondensed-SemiBold.ttf` | Barlow Condensed SemiBold, Jeremy Tribby (The Barlow Project Authors) | paytables, labels, UI text | `ofl/barlowcondensed/` |
| `fonts/Bungee-Regular.ttf` | Bungee, David Jonathan Ross (The Bungee Project Authors) | card indices, meters, buttons, win banners | `ofl/bungee/` |
| `fonts/TiltNeon.ttf` | Tilt Neon (variable, default instance), Andy Clymer (The Tilt Project Authors) | neon signs and tags ("POKER LOUNGE", "HELD") | `ofl/tiltneon/TiltNeon[XROT,YROT].ttf` |
| `fonts/Neonderthaw-Regular.ttf` | Neonderthaw, Robert Leuschke (The Neonderthaw Project Authors) | the script "Beese's" on the marquee | `ofl/neonderthaw/` |

## Graphics

All other graphics - the 52 card faces and the backs (including the court
figures), the bee mascot, the honeycomb backgrounds, the side art, coins,
sparks, bulbs, glows and every UI shape - are **original**, drawn by code at
start-up (`render/`) and under the same license as the game. No images,
clip art or third-party artwork are used.
