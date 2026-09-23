/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* main.c - render every sound in the game.
 *
 * usage: audiogen SFX_DIR MUSIC_WAV_DIR [sfx|music]
 *   SFX_DIR        receives the finished sound effects (16-bit WAV)
 *   MUSIC_WAV_DIR  receives lounge_loop.wav, attract_loop.wav and
 *                  ambience_lounge.wav, which build.sh encodes to OGG
 *   sfx|music      render only that half (for quick iteration)
 */
#include "dsp.h"
#include "music.h"
#include "sfx.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int render_sfx(const SfxRecipe *rc, int v, const char *dir)
{
    char path[1024];
    if (rc->variants > 1) snprintf(path, sizeof path, "%s/%s_%d.wav", dir, rc->name, v + 1);
    else snprintf(path, sizeof path, "%s/%s.wav", dir, rc->name);

    Buf b = buf_new(secs(rc->seconds), rc->channels);
    Rng r;
    rng_seed(&r, hash_str(rc->name) + (uint64_t)v * 7919u);
    rc->fn(&b, &r, v);

    /* Finishing, in an order that keeps the edges clean: remove DC first
       (a filter may move the ends), trim, then fade the ends to exactly
       zero, and only then set the level (a gain cannot undo a zero). */
    fx_dcblock(&b);
    long full = b.n;
    fx_trim(&b, -50.0, -42.0, 1.0);
    if (b.n >= full - secs(0.002))
        printf("  WARNING: %s tail still above -42 dB at the end of its %.2f s buffer\n", rc->name, rc->seconds);
    /* The tail is cut where it falls 42 dB below the peak, under a fade of
       up to 250 ms, so it has faded out rather than stopped by the time it
       is cut, and no sound carries seconds of inaudible reverb in memory
       (raylib holds every loaded sound as 32-bit float stereo). */
    double dur_ms = 1000.0 * b.n / SR;
    fx_fade(&b, 0.5, fmin(250.0, dur_ms * 0.25));
    fx_normalize(&b, rc->target, -1.0, rc->clip_db);
    if (rc->streamed) {
        /* raylib 5.5 stops a non-looping stream as soon as its last chunk is
           queued, which can drop up to ~70 ms of the end. Streamed stingers
           therefore end in 100 ms of silence, so only silence is lost. */
        long pad = secs(0.1);
        Buf p = buf_new(b.n + pad, b.ch);
        for (long i = 0; i < b.n; i++) { p.l[i] = b.l[i]; if (b.r) p.r[i] = b.r[i]; }
        buf_free(&b);
        b = p;
    }

    int rv = wav_write(path, &b);
    printf("  %-26s %s %6.3f s\n", path + strlen(dir) + 1, b.ch == 2 ? "st" : "mo", (double)b.n / SR);
    buf_free(&b);
    return rv;
}

static int write_loop(const char *dir, const char *name, Buf b)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s.wav", dir, name);
    int rv = wav_write(path, &b);
    printf("  %-26s st %6.3f s  rms %.1f dBFS  peak %.1f dBFS\n", name, (double)b.n / SR, rms_db(&b), lin2db(buf_peak(&b)));
    buf_free(&b);
    return rv;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s SFX_DIR MUSIC_WAV_DIR\n", argv[0]);
        return 2;
    }
    int bad = 0;
    int do_sfx = argc < 4 || !strcmp(argv[3], "sfx"), do_music = argc < 4 || !strcmp(argv[3], "music");
    if (do_sfx) printf("sound effects -> %s\n", argv[1]);
    for (int k = 0; do_sfx && k < SFX_RECIPE_COUNT; k++)
        for (int v = 0; v < SFX_RECIPES[k].variants; v++)
            bad |= render_sfx(&SFX_RECIPES[k], v, argv[1]) != 0;

    if (!do_music) return bad ? 1 : 0;
    printf("music -> %s\n", argv[2]);
    printf("  lounge_loop\n");
    bad |= write_loop(argv[2], "lounge_loop", music_lounge()) != 0;
    printf("  attract_loop\n");
    bad |= write_loop(argv[2], "attract_loop", music_attract()) != 0;
    bad |= write_loop(argv[2], "ambience_lounge", ambience_lounge(40.0)) != 0;
    return bad ? 1 : 0;
}
