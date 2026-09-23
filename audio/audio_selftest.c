/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* audio_selftest.c - exercise bpl_audio through raylib itself.
 *
 * usage: audio_selftest [ASSETS_DIR]      (default: assets)
 *
 * Reports load time, the RSS the audio system adds, the PCM held by loaded
 * sounds, the length raylib's own decoder sees for each loop, and then runs
 * the real API in real time: voice pooling, a streamed stinger, ducking under
 * the jackpot fanfare and its release, a skipped fanfare, and whether the
 * heap grows while frames run. Needs an audio device (miniaudio's null
 * backend will do). Exit status 0 = every check passed.
 */
#include "audio.h"

#include "raylib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#if defined(__GLIBC__)
#include <malloc.h>
#endif

static int fails;

static void check(int ok, const char *what)
{
    printf("  [%s] %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) fails++;
}

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + t.tv_nsec * 1e-9;
}

static long rss_kb(void)
{
    long pages = 0, res = 0;
    FILE *f = fopen("/proc/self/statm", "r");
    if (f) {
        if (fscanf(f, "%ld %ld", &pages, &res) != 2) res = 0;
        fclose(f);
    }
    return res * (sysconf(_SC_PAGESIZE) / 1024);
}

static size_t heap_used(void)
{
#if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 33))
    return mallinfo2().uordblks;
#else
    return 0;
#endif
}

/* Run frames at 60 Hz in real time for `s` seconds. */
static void run(double s)
{
    double end = now() + s;
    struct timespec d = { 0, 16666667 };
    while (now() < end) {
        audio_update(1.0f / 60.0f);
        nanosleep(&d, NULL);
    }
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "assets";
    SetTraceLogLevel(LOG_WARNING);

    long rss0 = rss_kb();
    double t0 = now();
    bool ok = audio_init(dir);
    double t1 = now();
    long rss1 = rss_kb();
    if (!ok) {
        printf("no audio device: nothing to test\n");
        return 2;
    }
    AudioStats st;
    audio_get_stats(&st);
    printf("audio_init: %.0f ms, RSS +%ld KB (%ld -> %ld KB)\n", (t1 - t0) * 1000, rss1 - rss0, rss0, rss1);
    printf("loaded: %d sounds + %d aliases, %d streams, %d missing, PCM %.2f MB at %d Hz\n",
           st.sounds_loaded, st.aliases, st.streams, st.missing, (double)st.sound_bytes / 1048576.0, st.device_rate);
    check(st.missing == 0, "every asset loaded");
    for (int id = 0; id < SFX_COUNT; id++)
        if (audio_sfx_duration((SfxId)id) <= 0) printf("    no duration: %s\n", audio_sfx_name((SfxId)id));

    /* What raylib's decoder (stb_vorbis) makes of the loops. */
    static const struct { const char *f; unsigned expect; } LOOPS[] = {
        { "lounge_loop.ogg", 5184000 }, { "attract_loop.ogg", 1728000 }, { "ambience_lounge.ogg", 1764000 },
    };
    for (int k = 0; k < 3; k++) {
        char p[512];
        snprintf(p, sizeof p, "%s/music/%s", dir, LOOPS[k].f);
        Music m = LoadMusicStream(p);
        printf("  %-20s %u frames (%.3f s), %u Hz, %u ch\n", LOOPS[k].f, m.frameCount,
               (double)m.frameCount / m.stream.sampleRate, m.stream.sampleRate, m.stream.channels);
        check(m.frameCount == LOOPS[k].expect, "  loop length is exactly the rendered length");
        UnloadMusicStream(m);
    }

    printf("voice pool:\n");
    for (int k = 0; k < 20; k++) audio_play(SFX_CHIP_SINGLE, 1.0f, 1.0f, 0.0f);
    audio_get_stats(&st);
    printf("  20 x chip_single in one frame -> %d voices\n", st.voices_playing);
    check(st.voices_playing >= 2 && st.voices_playing <= 8, "  chip voices capped by the pool (4 variants x 2)");
    run(0.3);

    printf("every sound once:\n");
    for (int id = 0; id < SFX_COUNT; id++) {
        audio_play((SfxId)id, 0.5f, 1.0f, 0.0f);
        audio_update(1.0f / 60.0f);
        check(audio_is_playing((SfxId)id), audio_sfx_name((SfxId)id));
        audio_stop((SfxId)id);
    }
    run(0.5);

    printf("ducking under the jackpot fanfare (music on):\n");
    audio_music(MUSIC_LOUNGE, true);
    audio_ambience(true);
    run(1.5);
    size_t h0 = heap_used();
    audio_play(SFX_WIN_JACKPOT, 1.0f, 1.0f, 0.0f);
    float dmin = 1.0f, d_at_3 = 0;
    bool playing_at_3 = false, playing_at_85 = true;
    printf("  t(s)  duck\n");
    for (int k = 1; k <= 40; k++) {
        run(0.25);
        audio_get_stats(&st);
        if (st.duck < dmin) dmin = st.duck;
        if (k == 12) { d_at_3 = st.duck; playing_at_3 = audio_is_playing(SFX_WIN_JACKPOT); }
        if (k == 34) playing_at_85 = audio_is_playing(SFX_WIN_JACKPOT);
        if (k % 2 == 0) printf("  %4.1f  %.3f\n", k * 0.25, st.duck);
        /* chips and ticks keep firing during the frames, as in play */
        audio_play(SFX_CREDIT_TICK, 0.6f, 1.0f + 0.02f * (float)(k % 12), 0.0f);
    }
    size_t h1 = heap_used();
    printf("  heap in use across 10 s of frames and 40 plays: %+ld bytes\n", (long)h1 - (long)h0);
    check(dmin < 0.2f, "  music ducks to the jackpot's 0.15");
    check(d_at_3 < 0.2f, "  and stays down while the fanfare plays");
    check(playing_at_3, "  the streamed fanfare is playing at 3 s");
    check(!playing_at_85, "  and has ended by 8.5 s");
    check(st.duck > 0.95f, "  the music is back up by 10 s");
    check(h1 == h0, "  no heap growth while running frames");

    printf("a skipped fanfare releases the duck:\n");
    audio_play(SFX_WIN_JACKPOT, 1.0f, 1.0f, 0.0f);
    run(1.0);
    audio_stop(SFX_WIN_JACKPOT);
    run(2.0);
    audio_get_stats(&st);
    printf("  duck 2 s after audio_stop: %.3f\n", st.duck);
    check(st.duck > 0.9f, "  music recovers after a skip");

    printf("music switch:\n");
    audio_music(MUSIC_ATTRACT, true);
    run(1.5);
    audio_music(MUSIC_NONE, false);
    audio_ambience(false);
    run(1.2);
    check(1, "  crossfade lounge -> attract -> silence ran");

    long rss2 = rss_kb();
    printf("RSS after the run: %ld KB (+%ld KB over start-up)\n", rss2, rss2 - rss0);
    audio_shutdown();
    printf("%s (%d failed)\n", fails ? "FAILED" : "ALL CHECKS PASSED", fails);
    return fails ? 1 : 0;
}
