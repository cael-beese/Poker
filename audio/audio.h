/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* audio.h - sound effects, music and ducking on top of raylib's audio.
 *
 * Presentation code calls this in reaction to game events; game logic never
 * does (docs/CONTRACT.md section 4). Everything is loaded in audio_init() and
 * nothing is allocated afterwards: audio_play() picks a free pooled voice,
 * and audio_update() streams the music once per frame.
 *
 * If there is no audio device (a headless test box), audio_init() returns
 * false and every other call is a cheap no-op, so callers never need to check.
 */
#ifndef BPL_AUDIO_H
#define BPL_AUDIO_H

#include <stdbool.h>
#include <stddef.h>

typedef enum SfxId {
    /* cards and table */
    SFX_CARD_DEAL,      /* a card lands: 3 variants, the snap is ~15 ms in   */
    SFX_CARD_FLIP,      /* a card turns face up: 3 variants                  */
    SFX_CARD_SLIDE,     /* a card slid across the felt: 2 variants           */
    SFX_CARD_SHUFFLE,   /* riffle + bridge + squaring the deck, 1.3 s        */
    SFX_FOLD,           /* cards tossed into the muck: 2 variants            */
    SFX_CHECK,          /* two knocks on the table                            */
    /* buttons and menus */
    SFX_BUTTON,         /* arcade button click: 3 variants                    */
    SFX_HOLD_ON,        /* HOLD lit                                            */
    SFX_HOLD_OFF,       /* HOLD cleared                                        */
    SFX_MENU_MOVE,      /* cursor step in a menu                               */
    SFX_MENU_SELECT,    /* menu confirm                                        */
    SFX_SERVICE_BEEP,   /* plain service-menu beep                             */
    SFX_ERROR,          /* deny: not enough credits, illegal action            */
    SFX_NEON_FLICKER,   /* quiet transformer buzz for a marquee flicker        */
    /* credits */
    SFX_BET_ONE,        /* BET ONE; pitch it up per bet level                  */
    SFX_BET_MAX,        /* BET MAX: five rising chimes                         */
    SFX_CREDIT_TICK,    /* count-up tick, a pure C7: meant to be pitched       */
    SFX_CREDIT_END,     /* the count-up has finished                           */
    SFX_COIN_INSERT,    /* coin in, credit registered                          */
    SFX_CASH_OUT,       /* hopper paying out, 4 s                              */
    /* chips */
    SFX_CHIP_SINGLE,    /* one chip placed: 4 variants                         */
    SFX_CHIP_STACK,     /* a small stack set down: 3 variants                  */
    SFX_CHIP_POT,       /* chips pushed into the pot: 3 variants               */
    /* outcomes */
    SFX_WIN_SMALL,      /* pulse + chime                                       */
    SFX_WIN_MEDIUM,     /* coin burst                                          */
    SFX_WIN_BIG,        /* fanfare hit                                         */
    SFX_WIN_JACKPOT,    /* royal flush / big pot: the 7.2 s fanfare            */
    SFX_DOUBLE_WIN,     /* double-up won                                       */
    SFX_DOUBLE_LOSE,    /* double-up lost                                      */
    SFX_ALL_IN,         /* someone moves all in                                */
    SFX_REVEAL,         /* showdown / big reveal hit                           */
    SFX_YOUR_TURN,      /* the player is to act                                */
    SFX_BLINDS_UP,      /* blind level rises                                   */
    SFX_BUST,           /* player eliminated / out of credits                  */
    SFX_WHOOSH,         /* screen or mode transition                           */
    SFX_COUNT
} SfxId;

typedef enum MusicId {
    MUSIC_NONE = -1,
    MUSIC_LOUNGE,       /* the in-game loop, 1:57                              */
    MUSIC_ATTRACT,      /* the attract-mode loop, 0:39                         */
    MUSIC_COUNT
} MusicId;

typedef struct AudioStats {
    size_t sound_bytes;     /* PCM held by loaded sounds, as raylib stores it  */
    int    sounds_loaded;   /* source sounds resident in memory                */
    int    aliases;         /* extra voices sharing that PCM                   */
    int    streams;         /* files streamed from disk (stingers, music)      */
    int    missing;         /* files that failed to load (played as silence)   */
    int    voices_playing;  /* SFX voices sounding right now                   */
    float  duck;            /* current music gain from ducking, 0..1           */
    int    device_rate;     /* output sample rate, Hz                          */
} AudioStats;

/* Open the audio device and load everything under assets_dir (which holds
 * sfx/ and music/). Returns false with no device; the API then does nothing. */
bool audio_init(const char *assets_dir);
void audio_shutdown(void);

/* Once per rendered frame: streams music and stingers, runs fades and ducking. */
void audio_update(float dt);

/* Play a sound effect.
 *   vol    0..1, scaled by the SFX volume
 *   pitch  1 = as recorded; 2 = an octave up. Sounds marked for it in the
 *          table also get a small random detune, so repeats differ.
 *   pan    -1 left .. 0 centre .. +1 right
 * Sounds that duck the music (win stingers, fanfares) do so automatically
 * for their length. */
void audio_play(SfxId id, float vol, float pitch, float pan);
void audio_stop(SfxId id);          /* e.g. the jackpot fanfare is skipped   */
void audio_stop_all(void);          /* every SFX voice                        */
bool audio_is_playing(SfxId id);

/* Switch music with a short crossfade. MUSIC_NONE or play=false fades out.
 * The lounge loop resumes where it left off; the attract loop restarts. */
void audio_music(MusicId id, bool play);
void audio_ambience(bool on);       /* crowd murmur bed, fades in and out     */

/* Extra ducking on demand (e.g. slow-motion showdown): music gain to `gain`
 * for `seconds`, combined with the automatic stinger ducking. */
void audio_duck(float gain, float seconds);

/* Settings, each 0..1. Defaults: master 1, music 0.55, sfx 1, ambience 0.5. */
void audio_set_volumes(float master, float music, float sfx);
void audio_set_ambience_level(float level);

float       audio_sfx_duration(SfxId id);   /* seconds at pitch 1, 0 if missing */
const char *audio_sfx_name(SfxId id);       /* "card_deal", for a sound test    */
void        audio_get_stats(AudioStats *out);

#endif
