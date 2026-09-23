/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* audio.c - see audio.h.
 *
 * Memory: raylib converts every loaded Sound to 32-bit float stereo at the
 * device rate, so one second of any WAV costs ~384 KB once loaded. Short,
 * overlapping sounds are loaded (a Sound per variant plus LoadSoundAlias
 * voices that share its PCM); the long stingers and fanfares, which never
 * overlap themselves, are streamed from their WAV files as Music instead and
 * cost only a small stream buffer each.
 */
#include "audio.h"

#include "raylib.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAX_VARIANTS 4
#define MAX_VOICES 4        /* per variant: the Sound plus up to 3 aliases */

typedef struct {
    const char *file;       /* stem under sfx/; variants get _1.._n       */
    uint8_t variants;
    uint8_t voices;         /* 0 = streamed from disk, else pooled voices  */
    float jitter;           /* random detune, +- semitones                 */
    float duck;             /* music gain while it plays; 1 = no ducking   */
} SfxDef;

static const SfxDef DEFS[SFX_COUNT] = {
    [SFX_CARD_DEAL]    = { "card_deal",    3, 2, 0.6f, 1.0f },
    [SFX_CARD_FLIP]    = { "card_flip",    3, 2, 0.5f, 1.0f },
    [SFX_CARD_SLIDE]   = { "card_slide",   2, 2, 0.5f, 1.0f },
    [SFX_CARD_SHUFFLE] = { "card_shuffle", 1, 1, 0.3f, 1.0f },
    [SFX_FOLD]         = { "fold",         2, 1, 0.5f, 1.0f },
    [SFX_CHECK]        = { "check",        1, 2, 0.4f, 1.0f },
    [SFX_BUTTON]       = { "button",       3, 2, 0.3f, 1.0f },
    [SFX_HOLD_ON]      = { "hold_on",      1, 3, 0.0f, 1.0f },
    [SFX_HOLD_OFF]     = { "hold_off",     1, 3, 0.0f, 1.0f },
    [SFX_MENU_MOVE]    = { "menu_move",    1, 3, 0.0f, 1.0f },
    [SFX_MENU_SELECT]  = { "menu_select",  1, 2, 0.0f, 1.0f },
    [SFX_SERVICE_BEEP] = { "service_beep", 1, 2, 0.0f, 1.0f },
    [SFX_ERROR]        = { "error",        1, 2, 0.0f, 1.0f },
    [SFX_NEON_FLICKER] = { "neon_flicker", 1, 2, 1.0f, 1.0f },
    [SFX_BET_ONE]      = { "bet_one",      1, 3, 0.0f, 1.0f },
    [SFX_BET_MAX]      = { "bet_max",      1, 1, 0.0f, 0.85f },
    [SFX_CREDIT_TICK]  = { "credit_tick",  1, 4, 0.0f, 1.0f },
    [SFX_CREDIT_END]   = { "credit_end",   1, 1, 0.0f, 1.0f },
    [SFX_COIN_INSERT]  = { "coin_insert",  1, 3, 0.2f, 1.0f },
    [SFX_CASH_OUT]     = { "cash_out",     1, 0, 0.0f, 0.6f },
    [SFX_CHIP_SINGLE]  = { "chip_single",  4, 2, 0.8f, 1.0f },
    [SFX_CHIP_STACK]   = { "chip_stack",   3, 2, 0.7f, 1.0f },
    [SFX_CHIP_POT]     = { "chip_pot",     3, 2, 0.6f, 1.0f },
    [SFX_WIN_SMALL]    = { "win_small",    1, 0, 0.0f, 0.7f },
    [SFX_WIN_MEDIUM]   = { "win_medium",   1, 0, 0.0f, 0.5f },
    [SFX_WIN_BIG]      = { "win_big",      1, 0, 0.0f, 0.3f },
    [SFX_WIN_JACKPOT]  = { "win_jackpot",  1, 0, 0.0f, 0.15f },
    [SFX_DOUBLE_WIN]   = { "double_win",   1, 0, 0.0f, 0.6f },
    [SFX_DOUBLE_LOSE]  = { "double_lose",  1, 0, 0.0f, 0.7f },
    [SFX_ALL_IN]       = { "all_in",       1, 0, 0.0f, 0.45f },
    [SFX_REVEAL]       = { "reveal",       1, 0, 0.0f, 0.55f },
    [SFX_YOUR_TURN]    = { "your_turn",    1, 0, 0.0f, 0.85f },
    [SFX_BLINDS_UP]    = { "blinds_up",    1, 0, 0.0f, 0.8f },
    [SFX_BUST]         = { "bust",         1, 0, 0.0f, 0.5f },
    [SFX_WHOOSH]       = { "whoosh",       1, 2, 0.4f, 1.0f },
};

static const char *const MUSIC_FILES[MUSIC_COUNT] = { "lounge_loop.ogg", "attract_loop.ogg" };
static const bool MUSIC_RESTART[MUSIC_COUNT] = { false, true };

#define FADE_S 0.8f         /* music crossfade and ambience fade time      */
#define DUCK_ATTACK_S 0.08f /* music drops this fast under a stinger...    */
#define DUCK_RELEASE_S 0.6f /* ...and comes back this slowly               */

typedef struct {
    Sound snd[MAX_VARIANTS][MAX_VOICES];
    Music stream;               /* when DEFS[].voices == 0                  */
    uint8_t loaded[MAX_VARIANTS];
    uint8_t next[MAX_VARIANTS]; /* round-robin voice to steal               */
    int8_t last;                /* last variant played, to avoid repeats    */
    bool streamed, stream_ok;
    float seconds;              /* length at pitch 1                         */
    float duck_left;            /* seconds of ducking still owed             */
} SfxSlot;

typedef struct {
    Music m;
    bool ok, active, paused;
    float fade, target;
} Track;

static struct {
    bool ok, own_device;
    SfxSlot sfx[SFX_COUNT];
    Track music[MUSIC_COUNT];
    Track amb;
    float master, vol_music, vol_sfx, amb_level;
    float duck, manual_gain, manual_left;
    uint32_t rng;
    AudioStats st;
} A;

/* Cosmetic randomness only (variant choice, detune): audio's own stream,
   never the game's Rng. */
static uint32_t rnd(void)
{
    uint32_t x = A.rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return A.rng = x;
}
static float rnd_bi(void) { return (float)(rnd() >> 8) * (2.0f / 16777216.0f) - 1.0f; }

static float clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }

/* raylib 5.5's pan is 0..1 with 1.0 = full LEFT (MixAudioFrames weights the
   left channel by `pan`); our API is -1 left .. +1 right. */
static float rl_pan(float pan) { return 0.5f - 0.5f * (pan < -1 ? -1 : (pan > 1 ? 1 : pan)); }

static void load_sfx(const char *dir, SfxId id)
{
    const SfxDef *d = &DEFS[id];
    SfxSlot *s = &A.sfx[id];
    char path[512];
    s->last = -1;
    if (d->voices == 0) {
        snprintf(path, sizeof path, "%s/sfx/%s.wav", dir, d->file);
        s->stream = LoadMusicStream(path);
        s->streamed = true;
        s->stream_ok = s->stream.frameCount > 0;
        if (!s->stream_ok) { A.st.missing++; return; }
        s->stream.looping = false;
        s->seconds = (float)s->stream.frameCount / (float)s->stream.stream.sampleRate;
        A.st.streams++;
        return;
    }
    for (int v = 0; v < d->variants; v++) {
        if (d->variants > 1) snprintf(path, sizeof path, "%s/sfx/%s_%d.wav", dir, d->file, v + 1);
        else snprintf(path, sizeof path, "%s/sfx/%s.wav", dir, d->file);
        Sound src = LoadSound(path);
        if (src.frameCount == 0) { A.st.missing++; continue; }
        s->snd[v][0] = src;
        s->loaded[v] = 1;
        for (int k = 1; k < d->voices && k < MAX_VOICES; k++) {
            s->snd[v][k] = LoadSoundAlias(src);
            A.st.aliases++;
        }
        A.st.sounds_loaded++;
        A.st.sound_bytes += (size_t)src.frameCount * src.stream.channels * (src.stream.sampleSize / 8);
        float sec = (float)src.frameCount / (float)src.stream.sampleRate;
        if (sec > s->seconds) s->seconds = sec;
    }
}

static void load_track(Track *t, const char *dir, const char *file)
{
    char path[512];
    snprintf(path, sizeof path, "%s/music/%s", dir, file);
    t->m = LoadMusicStream(path);
    t->ok = t->m.frameCount > 0;
    if (!t->ok) { A.st.missing++; return; }
    t->m.looping = true;
    A.st.streams++;
}

bool audio_init(const char *assets_dir)
{
    memset(&A, 0, sizeof A);
    A.master = 1.0f;
    A.vol_music = 0.55f;
    A.vol_sfx = 1.0f;
    A.amb_level = 0.5f;
    A.duck = 1.0f;
    A.manual_gain = 1.0f;
    A.rng = 0x9E3779B9u;
    if (!IsAudioDeviceReady()) {
        InitAudioDevice();
        A.own_device = true;
    }
    if (!IsAudioDeviceReady()) {
        TraceLog(LOG_WARNING, "AUDIO: no audio device, running silent");
        A.own_device = false;
        return false;
    }
    for (int id = 0; id < SFX_COUNT; id++) load_sfx(assets_dir, (SfxId)id);
    for (int k = 0; k < MUSIC_COUNT; k++) load_track(&A.music[k], assets_dir, MUSIC_FILES[k]);
    load_track(&A.amb, assets_dir, "ambience_lounge.ogg");
    /* raylib allocates its shared decode buffer on the first UpdateMusicStream
       and grows it for wider frames. Feeding one stereo 16-bit stream now
       (the widest we have) means that happens here, not during a frame. */
    if (A.music[MUSIC_LOUNGE].ok) {
        UpdateMusicStream(A.music[MUSIC_LOUNGE].m);
        StopMusicStream(A.music[MUSIC_LOUNGE].m);
    }
    SetMasterVolume(A.master);
    A.ok = true;
    TraceLog(LOG_INFO, "AUDIO: %d sounds (%d voices shared), %d streams, %.2f MB PCM, %d missing",
             A.st.sounds_loaded, A.st.aliases, A.st.streams, (double)A.st.sound_bytes / 1048576.0, A.st.missing);
    return true;
}

void audio_shutdown(void)
{
    if (!A.ok) return;
    for (int id = 0; id < SFX_COUNT; id++) {
        SfxSlot *s = &A.sfx[id];
        if (s->streamed) {
            if (s->stream_ok) UnloadMusicStream(s->stream);
            continue;
        }
        for (int v = 0; v < MAX_VARIANTS; v++) {
            if (!s->loaded[v]) continue;
            for (int k = 1; k < DEFS[id].voices && k < MAX_VOICES; k++) UnloadSoundAlias(s->snd[v][k]);
            UnloadSound(s->snd[v][0]);
        }
    }
    for (int k = 0; k < MUSIC_COUNT; k++)
        if (A.music[k].ok) UnloadMusicStream(A.music[k].m);
    if (A.amb.ok) UnloadMusicStream(A.amb.m);
    if (A.own_device) CloseAudioDevice();
    memset(&A, 0, sizeof A);
}

void audio_play(SfxId id, float vol, float pitch, float pan)
{
    if (!A.ok || (unsigned)id >= SFX_COUNT) return;
    const SfxDef *d = &DEFS[id];
    SfxSlot *s = &A.sfx[id];
    if (d->jitter > 0) pitch *= exp2f(d->jitter * rnd_bi() / 12.0f);
    if (pitch < 0.05f) pitch = 0.05f;
    float v = clamp01(vol) * A.vol_sfx;

    if (s->streamed) {
        if (!s->stream_ok) return;
        /* Rewind, pre-fill the stream buffer, then start it: the first
           samples are ready before the mixer asks, so a stinger starts as
           promptly as a loaded Sound does. */
        StopMusicStream(s->stream);
        UpdateMusicStream(s->stream);
        SetMusicVolume(s->stream, v);
        SetMusicPitch(s->stream, pitch);
        SetMusicPan(s->stream, rl_pan(pan));
        PlayMusicStream(s->stream);
    } else {
        int var = 0;
        if (d->variants > 1) {
            /* a different variant from last time, so repeats never match */
            var = (int)(rnd() % (uint32_t)(d->variants - 1));
            if (var >= s->last) var++;
        }
        if (!s->loaded[var]) return;
        s->last = (int8_t)var;
        int n = d->voices < MAX_VOICES ? d->voices : MAX_VOICES, pick = -1;
        for (int k = 0; k < n; k++) {
            int c = (s->next[var] + k) % n;
            if (!IsSoundPlaying(s->snd[var][c])) { pick = c; break; }
        }
        if (pick < 0) pick = s->next[var];   /* all busy: steal the oldest */
        s->next[var] = (uint8_t)((pick + 1) % n);
        Sound snd = s->snd[var][pick];
        SetSoundVolume(snd, v);
        SetSoundPitch(snd, pitch);
        SetSoundPan(snd, rl_pan(pan));
        PlaySound(snd);
    }
    if (d->duck < 1.0f && s->seconds > 0) {
        /* Hold the duck until the sound's tail begins, then let the music
           swell back under the tail rather than after it. */
        float len = s->seconds / pitch;
        float tail = len * 0.3f < 1.2f ? len * 0.3f : 1.2f;
        if (len - tail > s->duck_left) s->duck_left = len - tail;
    }
}

void audio_stop(SfxId id)
{
    if (!A.ok || (unsigned)id >= SFX_COUNT) return;
    SfxSlot *s = &A.sfx[id];
    if (s->streamed) {
        if (s->stream_ok) StopMusicStream(s->stream);
    } else {
        for (int v = 0; v < DEFS[id].variants; v++)
            for (int k = 0; s->loaded[v] && k < DEFS[id].voices && k < MAX_VOICES; k++)
                StopSound(s->snd[v][k]);
    }
    s->duck_left = 0;
}

void audio_stop_all(void)
{
    for (int id = 0; id < SFX_COUNT; id++) audio_stop((SfxId)id);
}

bool audio_is_playing(SfxId id)
{
    if (!A.ok || (unsigned)id >= SFX_COUNT) return false;
    SfxSlot *s = &A.sfx[id];
    if (s->streamed) return s->stream_ok && IsMusicStreamPlaying(s->stream);
    for (int v = 0; v < DEFS[id].variants; v++)
        for (int k = 0; s->loaded[v] && k < DEFS[id].voices && k < MAX_VOICES; k++)
            if (IsSoundPlaying(s->snd[v][k])) return true;
    return false;
}

static void track_start(Track *t, bool restart)
{
    if (!t->ok) return;
    t->target = 1.0f;
    if (t->active) return;
    if (t->paused && !restart) {
        ResumeMusicStream(t->m);          /* the lounge loop picks up where it was */
    } else {
        StopMusicStream(t->m);            /* rewind */
        UpdateMusicStream(t->m);          /* pre-fill before the mixer asks */
        SetMusicVolume(t->m, 0.0f);
        PlayMusicStream(t->m);
    }
    t->paused = false;
    t->active = true;
}

void audio_music(MusicId id, bool play)
{
    if (!A.ok) return;
    for (int k = 0; k < MUSIC_COUNT; k++) {
        if (play && k == (int)id) track_start(&A.music[k], MUSIC_RESTART[k] && A.music[k].fade <= 0.0f);
        else A.music[k].target = 0.0f;
    }
}

void audio_ambience(bool on)
{
    if (!A.ok) return;
    if (on) track_start(&A.amb, false);
    else A.amb.target = 0.0f;
}

void audio_duck(float gain, float seconds)
{
    A.manual_gain = clamp01(gain);
    A.manual_left = seconds;
}

void audio_set_volumes(float master, float music, float sfx)
{
    A.master = clamp01(master);
    A.vol_music = clamp01(music);
    A.vol_sfx = clamp01(sfx);
    if (A.ok) SetMasterVolume(A.master);
}

void audio_set_ambience_level(float level) { A.amb_level = clamp01(level); }

static void track_update(Track *t, float dt, float gain)
{
    if (!t->ok || !t->active) return;
    float step = dt / FADE_S;
    if (t->fade < t->target) t->fade = t->fade + step > t->target ? t->target : t->fade + step;
    else if (t->fade > t->target) t->fade = t->fade - step < t->target ? t->target : t->fade - step;
    if (t->fade <= 0.0f && t->target <= 0.0f) {
        PauseMusicStream(t->m);
        t->paused = true;
        t->active = false;
        return;
    }
    /* an equal-power curve, so a crossfade does not dip in the middle */
    SetMusicVolume(t->m, sinf(t->fade * 1.5707963f) * gain);
    UpdateMusicStream(t->m);
}

void audio_update(float dt)
{
    if (!A.ok) return;
    if (dt < 0) dt = 0;
    if (dt > 0.25f) dt = 0.25f;

    float target = 1.0f;
    if (A.manual_left > 0) {
        A.manual_left -= dt;
        target = A.manual_gain;
    }
    int voices = 0;
    for (int id = 0; id < SFX_COUNT; id++) {
        SfxSlot *s = &A.sfx[id];
        if (s->duck_left > 0) {
            s->duck_left -= dt;
            if (DEFS[id].duck < target) target = DEFS[id].duck;
        }
        if (s->streamed && s->stream_ok && IsMusicStreamPlaying(s->stream)) {
            UpdateMusicStream(s->stream);
            voices++;
        }
    }
    float tc = target < A.duck ? DUCK_ATTACK_S : DUCK_RELEASE_S;
    A.duck += (target - A.duck) * (1.0f - expf(-dt / tc));
    A.st.duck = A.duck;
    A.st.voices_playing = voices;

    for (int k = 0; k < MUSIC_COUNT; k++) track_update(&A.music[k], dt, A.vol_music * A.duck);
    /* the room noise dips a little under a fanfare too, but only half as much */
    track_update(&A.amb, dt, A.vol_sfx * A.amb_level * (0.5f + 0.5f * A.duck));
}

float audio_sfx_duration(SfxId id)
{
    return (unsigned)id < SFX_COUNT ? A.sfx[id].seconds : 0.0f;
}

const char *audio_sfx_name(SfxId id)
{
    return (unsigned)id < SFX_COUNT ? DEFS[id].file : "?";
}

void audio_get_stats(AudioStats *out)
{
    if (!out) return;
    *out = A.st;
    out->duck = A.ok ? A.duck : 1.0f;
    if (!A.ok) return;
    int n = out->voices_playing;
    for (int id = 0; id < SFX_COUNT; id++) {
        const SfxSlot *s = &A.sfx[id];
        if (s->streamed) continue;
        for (int v = 0; v < DEFS[id].variants; v++)
            for (int k = 0; s->loaded[v] && k < DEFS[id].voices && k < MAX_VOICES; k++)
                n += IsSoundPlaying(s->snd[v][k]);
    }
    out->voices_playing = n;
    for (int id = 0; id < SFX_COUNT; id++)
        if (A.sfx[id].loaded[0]) { out->device_rate = (int)A.sfx[id].snd[0][0].stream.sampleRate; break; }
}
