<!-- SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md -->
# Systems: game flow, credits, persistence, service, attract, install

The cabinet-side systems around the two games: the app states and their
controls, free-play credits, power-cut-safe saving, the operator's service
menu, attract mode, and installing as a RetroPie port. Code: `platform/`
(`mode_*.c`, `session.*`, `save.*`, `settings.*`, `service.c`,
`selftest.*`, `placeholder_*`), `install.sh`, tests in `tests/platform/`.

## 1. Flow and controls

`ATTRACT -> MENU -> DRAW | HOLDEM`, `SERVICE` from anywhere (the hidden
SERVICE button; leaving it returns to where you were). Everything works with
the arcade panel alone: 5 HOLD, DEAL/DRAW, BET ONE, BET MAX, CASH OUT,
SERVICE (plus COIN and START). UP/DOWN/LEFT/RIGHT/OK/BACK are aliases only.

| screen | buttons |
|---|---|
| attract | any button: menu (a COIN is credited too) |
| menu | HOLD 1-4 pick Jacks or Better / Bonus Poker / Deuces Wild / Hold'em; BET ONE next; HOLD 5 strategy hint on/off; DEAL, BET MAX or START play |
| Draw, between hands | BET ONE bet 1-5; BET MAX bet 5 and deal; DEAL deal; CASH OUT menu |
| Draw, holding | HOLD 1-5 hold; DEAL draw; BET ONE hint on/off |
| Draw, win offered | HOLD 1 double up; HOLD 5 or CASH OUT take the win; DEAL / BET MAX take it and deal |
| Draw, double up | HOLD 1-2 red; HOLD 4-5 black; HOLD 3 or CASH OUT take the win |
| Hold'em lobby / result | DEAL buy in (again); CASH OUT menu |
| Hold'em, your turn | HOLD 1 fold; HOLD 2 check/call; HOLD 3/4/5 raise to 1/2, 3/4, 1x pot; BET MAX all-in; BET ONE +1 big blind; DEAL bet/raise to the amount shown |
| Hold'em, any time seated | CASH OUT: "leave the table?" - CASH OUT again leaves, any other button stays |
| Hold'em, busted | DEAL watch the rest; CASH OUT leave |
| show / muck prompt | DEAL or HOLD 2 show; HOLD 1 muck |

CASH OUT in Draw collects a pending win first, then returns to the menu; in
the middle of a deal or draw it is refused ("finish the hand"). Exit to
EmulationStation: ESC or COIN+START (both save first; see 3).

The strategy hint is `draw_hint_compute`: the exact expected return of all 32
holds of the player's own five cards (never the deck), best hold outlined,
its EV shown. It is computed synchronously when the hold phase starts.

## 2. Credits

Free-play / bar credits only; nothing is ever paid out. A COIN press adds
`coin value / denomination` credits (defaults $1 and 25c: 4 credits), counted
when COIN is released, so COIN+START (exit) never credits. Credits, the
denomination and the coin value are set in the service menu. A fresh cabinet
starts with 1000 credits.

**Hold'em sit-and-go money.** The buy-in (service menu; 20-1000, default 100
credits) is debited when you sit down. Six buy-ins make the pool, paid 50 /
30 / 20 %: 1st 3.0x, 2nd 1.8x, 3rd 1.2x the buy-in (`sng_prize`; an average
player gets exactly their buy-in back). The prize is credited the moment your
place is known: when you bust, or when you win. Table chips (1500 each,
blinds up every 10 hands) are tournament chips, never credits. Leaving while
still in finishes you in the place of the players left (three left: 3rd, paid
as 3rd) - the same as being blinded out first. Exiting the program mid-game
applies the same rule.

## 3. Persistence (SPEC section 6)

**What.** One file holds credits, statistics (hands, wagered and paid per
Draw variant, royals, four deuces, biggest win, double-up rounds, Hold'em
games / wins / places / hands / buy-ins / prizes, coins, service credits)
and settings (volumes, music, difficulty, hint default, double-up,
denomination, coin value, buy-in, the effect toggles of `platform/effects.h`).

**Where.** `[game] save_dir` in `config.ini`; default
`$XDG_DATA_HOME/beese-poker`, else `~/.local/share/beese-poker`: on the
cabinet that is the SD card's ext4 root filesystem. Deliberately not the
RetroPie ROM folder, which on this cabinet is a vfat USB stick: vfat has no
journal, `rename` over an existing file is not atomic there and `fsync` of a
directory means little. If the directory cannot be created or fails the
write test (read-only card, permissions), the game runs from RAM: everything
works, nothing is kept, and the service menu shows a red RUNNING FROM RAM
warning with the reason.

**Format.** `state.sav` = one header line
`BPLSAVE 1 seq=<n> len=<bytes> crc=<crc32>` + `key=value` lines. A wrong
length or CRC means torn or corrupt; unknown keys are ignored and missing
keys default, so builds can add settings (e.g. new effects) freely; values
are clamped on load.

**Writing** (`save_store_write`): write `state.sav.tmp`, `fsync` it; rename
`state.sav` to `state.sav.bak`; rename the `.tmp` to `state.sav`; `fsync` the
directory. **Loading** reads all three names and takes the valid file with the
highest `seq`. A cut at any point leaves the new state (a complete, synced
`.tmp` or `state.sav`) or the previous one (`state.sav` or `.bak`) - never a
torn one. The service menu reports a recovery from `.bak`/`.tmp`.

**When** (safe points only). The file always holds what the player is owed if
the power went now: `wallet + pending`, where pending is the bet of a Draw
hand still being dealt or drawn (an interrupted hand is void and the bet goes
back), a win on the meter not yet collected (it is the player's), or the
Hold'em buy-in while seated (a power cut voids the game). It is rewritten at
the end of the tick in which that figure changes, after every hand (stats),
coin, service-menu change, and on exit (after the modes settle up). Writes run
on a background thread with the latest snapshot winning, because the SD card
is slow and uneven: measured on the cabinet, 100 writes took 9.3 ms mean /
42 ms max on one run and 109 ms mean / 906 ms max on another (other I/O on
the card). On the main thread that would drop up to 54 frames.

**Hand log.** `save_dir/hands.log`, one line per hand (`draw_write_hand_log`,
`holdem_hand_log_line`: time, mode, hand seed, result), flushed per line, not
fsynced (it is a log, not state); rotated to `hands.log.1` at 8 MB. Attract
demos are not logged.

**Tests** (`tests/platform/test_save.c`, ctest `save_persistence`): format
round trip, clamping, unknown keys; a power cut injected at every byte of the
`.tmp` write, after the `.tmp` is complete, and between the two renames;
`state.sav` truncated at every length and every byte bit-flipped (must fall
back to `.bak`); a writer process SIGKILLed 40 times at random moments (loads
must be whole and never go backwards); ENOSPC; a save directory under a
regular file, under `/proc`, and (as a normal user) chmod read-only with a
save already in it (loads, refuses writes); the prize pool sums exactly.

## 4. Service menu

Hidden SERVICE button. HOLD 1/2 up/down, HOLD 4/5 value, DEAL choose,
CASH OUT back. Lines: credits; add credits (10/100/1000/10000); reset credits
(DEAL twice); denomination; coin value; master / music / SFX volume; music
on/off; strategy hint default; double-up on/off; Hold'em opponents
(easy/normal/hard/expert -> `ai_assign_personalities`); Hold'em buy-in;
EFFECTS (every toggle); STATISTICS (with measured return per variant); INPUT
TEST (logical buttons live, raw joystick buttons/axes/hats in ES numbering,
touch; leave with SERVICE or CASH OUT held 2 s); SOUND TEST (every SfxId by
name and length, one or all in turn); SELF-TEST; reset statistics; exit. The
save status (directory, write count and times, RAM-only warning) is always on
screen.

Self-test (PASS/FAIL each, summary): evaluator categories and extremes;
pay categories of all three variants and the 9/6 table; the paytable returns
(99.5439 / 99.1660 / 100.7620 %); a dealt royal is held for exactly 4000; the
fast exact-EV hint equals brute force on two hands x 32 holds; RNG replay,
shuffle permutation and a 20,000-shuffle chi-square, OS entropy; Hold'em side
pots and odd-chip split; AI equity AA vs one hand; save directory write +
fsync + read-back; audio device; display mode. It runs on a worker thread.

## 5. Attract

After 60 s without input in the menu, between Draw hands with nothing on the
meter, or in the Hold'em lobby/result screen. Loop: title and paytable (6 s),
five Draw hands played by the exact-EV hint (the variant changes each loop),
45 s of Hold'em with all six seats AI (`seat0_ai`), "PRESS DEAL" pulsing
throughout. The demos are the real games (`draw_tick`, `holdem_tick`) with
real shuffles on attract's own Rng stream and a private wallet; nothing is
staged and the player's credits are never touched.

## 6. install.sh and RetroPie

See the header of `install.sh`. Program in `/opt/beese-poker` (binary,
`assets/`, `config.ini`; an edited `config.ini` is kept). RetroPie: the
launcher `~/RetroPie/roms/ports/Beese's Poker Lounge.sh` runs
`runcommand.sh 0 _PORT_ "beese-poker" ""` like the other ports, and
`/opt/retropie/configs/ports/beese-poker/emulators.cfg` names the binary. It
is a separate entry beside any other port; nothing else is touched.
`--dry-run` prints every action and changes nothing; `--uninstall` removes
only its own files (`--purge` also the saves). `config.txt`: only missing
lines are added (`dtoverlay=vc4-kms-v3d`; a CMA line only if none is set and
the running CMA is under 128 MB), after a timestamped backup.

## 7. The presentation seam

`platform/placeholder_view.h` is where render/ takes over. Each mode passes a
read-only `DrawViewInfo` / `HoldemViewInfo` / `MenuViewInfo` /
`AttractViewInfo` (const game state + credits, hint, demo flag, message) to
`*_placeholder_update(info, events, n, dt)` (cosmetic state, sounds) and
`*_placeholder_view(info, time)` (draw into 1280x720). Replace those two per
screen; no game logic lives there. Effects read `effects_get()`.

The Draw, menu and attract screens are called through a table,
`app_draw_views` (`DrawScreenViews` in `placeholder_view.h`), which the
executable picks in `platform/app_modes.c` like the mode table: the renderer's
`draw_views_render` (`render/draw_view.c`, `render/lounge_view.c`) when
render/ is built, else `draw_views_placeholder`. That keeps `bpl_platform`
free of any link dependency on `bpl_render`.
