/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* Determinism: the RNG matches the published xoshiro256** / splitmix64
   reference outputs (so a seed means the same thing on x86 and on the Pi),
   the same seed gives the same stream and the same shuffles, and an input
   log reads back exactly as written - including after a simulated power cut. */

#include "engine/deck.h"
#include "engine/replay.h"
#include "engine/rng.h"
#include "test_util.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void rng_reference(void)
{
    /* Reference values computed independently (a direct transcription of
       the published algorithms in Python). */
    static const uint64_t seed0[4] = {
        0x99ec5f36cb75f2b4ull, 0xbf6e1f784956452aull, 0x1a5f849d4933e6e0ull, 0x6aa594f1262d2d2cull };
    static const uint64_t seed42[4] = {
        0x15780b2e0c2ec716ull, 0x6104d9866d113a7eull, 0xae17533239e499a1ull, 0xecb8ad4703b360a1ull };
    static const uint64_t seeddb[4] = {
        0xc5555444a74d7e83ull, 0x65c30d37b4b16e38ull, 0x54f773200a4efa23ull, 0x429aed75fb958af7ull };
    static const uint64_t raw1234[4] = { 11520ull, 0ull, 1509978240ull, 1215971899390074240ull };
    Rng r;
    uint64_t st = 0;
    int i;

    CHECK(rng_splitmix64(&st) == 0xe220a8397b1dcdafull);

    r.s[0] = 1; r.s[1] = 2; r.s[2] = 3; r.s[3] = 4;
    for (i = 0; i < 4; i++) CHECK(rng_next(&r) == raw1234[i]);

    rng_seed(&r, 0);
    for (i = 0; i < 4; i++) CHECK(rng_next(&r) == seed0[i]);
    rng_seed(&r, 42);
    for (i = 0; i < 4; i++) CHECK(rng_next(&r) == seed42[i]);
    rng_seed(&r, 0xDEADBEEF);
    for (i = 0; i < 4; i++) CHECK(rng_next(&r) == seeddb[i]);
}

static void rng_determinism(void)
{
    Rng a, b, c;
    Deck da, db;
    int i, same = 1, differs = 0;

    rng_seed(&a, 0x1234567890ABCDEFull);
    rng_seed(&b, 0x1234567890ABCDEFull);
    rng_seed(&c, 0x1234567890ABCDEEull);
    for (i = 0; i < 100000; i++) {
        uint64_t x = rng_next(&a), y = rng_next(&b), z = rng_next(&c);
        if (x != y) same = 0;
        if (x != z) differs++;
    }
    CHECK(same);
    CHECK(differs > 99990);

    /* Copying the struct forks the stream: both copies continue identically. */
    b = a;
    CHECK(rng_next(&a) == rng_next(&b));
    CHECK(rng_below(&a, 52) == rng_below(&b, 52));
    CHECK(rng_unit(&a) == rng_unit(&b));

    /* Same seed, same shuffle, for many hands in a row. */
    rng_seed(&a, 77);
    rng_seed(&b, 77);
    for (i = 0; i < 1000; i++) {
        deck_init(&da); deck_shuffle(&da, &a);
        deck_init(&db); deck_shuffle(&db, &b);
        if (memcmp(da.c, db.c, sizeof da.c) != 0) same = 0;
    }
    CHECK(same);

    /* A pinned shuffle: seed 1's first deck. If this changes, every logged
       hand seed stops replaying, so it must never change silently. */
    {
        char s[3], buf[160] = "";
        rng_seed(&a, 1);
        deck_init(&da);
        deck_shuffle(&da, &a);
        for (i = 0; i < 10; i++) {
            strcat(buf, card_str(da.c[i], s));
            strcat(buf, i < 9 ? " " : "");
        }
        printf("seed 1 deals: %s\n", buf);
        /* Expected value computed independently in Python (xoshiro256**,
           Lemire rng_below, Fisher-Yates from the top), so this also proves
           the C shuffle is the algorithm it claims to be. */
        CHECK(strcmp(buf, "4d 8d Th Ac 7c 9s 9h As 5d 6c") == 0);
    }

    /* rng_os_seed gives different values call to call. */
    {
        uint64_t s1 = rng_os_seed(), s2 = rng_os_seed();
        CHECK(s1 != s2);
    }
}

static void random_frame(Rng *r, InputFrame *f, uint32_t *prev)
{
    memset(f, 0, sizeof *f);
    /* Mostly idle frames with bursts of activity, like real play. */
    if (rng_below(r, 10) < 7) {
        f->down = *prev;
    } else {
        f->down = (uint32_t)rng_next(r) & BTN_ALL;
        f->slider = (int16_t)(rng_below(r, 65535) - 32767);
        if (rng_below(r, 4) == 0) {
            f->touch = 1;
            f->touch_x = (int16_t)rng_below(r, 1280);
            f->touch_y = (int16_t)rng_below(r, 720);
        }
    }
    input_set_pressed(f, *prev);
    *prev = f->down;
}

static int frames_equal(const InputFrame *a, const InputFrame *b)
{
    return a->down == b->down && a->pressed == b->pressed && a->slider == b->slider
        && a->touch_x == b->touch_x && a->touch_y == b->touch_y && a->touch == b->touch;
}

static void replay_roundtrip(void)
{
    enum { N = 200000 };
    static InputFrame frames[N];
    char path[] = "bpl_replay_test_XXXXXX";
    const char meta[] = "variant=job;credits=1000";
    ReplayWriter w;
    ReplayReader rd;
    InputFrame f;
    Rng r;
    uint32_t prev = 0;
    long size;
    int i, fd, ok = 1, n;
    FILE *fp;

    rng_seed(&r, 99);
    for (i = 0; i < N; i++) random_frame(&r, &frames[i], &prev);
    /* A long idle stretch, as between hands. */
    for (i = N / 2; i < N / 2 + 20000; i++) frames[i] = frames[N / 2 - 1], frames[i].pressed = 0;

    fd = mkstemp(path);
    CHECK(fd >= 0);
    if (fd < 0) return;
    close(fd);

    CHECK(replay_writer_open(&w, path, 0xFEEDFACECAFEBEEFull, "draw", meta, sizeof meta) == 0);
    for (i = 0; i < N; i++) {
        CHECK(replay_writer_frame(&w, &frames[i]) == 0);
        if (i % 5000 == 4999) CHECK(replay_writer_flush(&w) == 0);
    }
    CHECK(replay_writer_close(&w) == 0);

    fp = fopen(path, "rb");
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fclose(fp);
    printf("input log: %d frames -> %ld bytes (%.2f bytes/frame)\n", N, size, (double)size / N);

    CHECK(replay_reader_open(&rd, path) == 0);
    CHECK(rd.seed == 0xFEEDFACECAFEBEEFull);
    CHECK(strcmp(rd.mode, "draw") == 0);
    CHECK(rd.meta_len == sizeof meta && memcmp(rd.meta, meta, sizeof meta) == 0);
    n = 0;
    while (replay_reader_frame(&rd, &f) == 1) {
        if (n >= N || !frames_equal(&f, &frames[n])) ok = 0;
        n++;
    }
    CHECK(ok);
    CHECK_EQ_INT(n, N);
    CHECK(!rd.truncated);
    replay_reader_close(&rd);

    /* Power cut: chop the file mid-record. Everything before the cut must
       read back, flagged as truncated. */
    CHECK(truncate(path, size - 30) == 0);
    CHECK(replay_reader_open(&rd, path) == 0);
    n = 0;
    ok = 1;
    while (replay_reader_frame(&rd, &f) == 1) {
        if (n >= N || !frames_equal(&f, &frames[n])) ok = 0;
        n++;
    }
    CHECK(ok);
    CHECK(rd.truncated);
    CHECK(n > N - 6000 && n < N);
    printf("truncated log: %d of %d frames recovered\n", n, N);
    replay_reader_close(&rd);

    /* Not a log at all. */
    fp = fopen(path, "wb");
    fputs("hello", fp);
    fclose(fp);
    CHECK(replay_reader_open(&rd, path) == -1);
    remove(path);

    /* Bad mode strings are refused. */
    CHECK(replay_writer_open(&w, path, 1, "two words", NULL, 0) == -1);
    CHECK(replay_writer_open(&w, path, 1, "", NULL, 0) == -1);
    remove(path);
}

static void hand_lines(void)
{
    char line[256], mode[REPLAY_MODE_MAX + 1], result[128];
    int64_t t;
    uint64_t seed;
    int n = replay_format_hand(line, sizeof line, 1790000000, "draw", 0x9f3c0a1b22e4d5f6ull,
                               "JOB bet=5 win=40 FULL HOUSE");
    CHECK(n > 0 && (size_t)n == strlen(line));
    CHECK(strcmp(line, "1790000000 draw 9f3c0a1b22e4d5f6 JOB bet=5 win=40 FULL HOUSE\n") == 0);
    CHECK(replay_parse_hand(line, &t, mode, &seed, result, sizeof result) == 0);
    CHECK(t == 1790000000);
    CHECK(strcmp(mode, "draw") == 0);
    CHECK(seed == 0x9f3c0a1b22e4d5f6ull);
    CHECK(strcmp(result, "JOB bet=5 win=40 FULL HOUSE") == 0);

    /* Small seeds keep all 16 digits; newlines in results are flattened. */
    replay_format_hand(line, sizeof line, 5, "holdem", 0x1, "a\nb");
    CHECK(strcmp(line, "5 holdem 0000000000000001 a b\n") == 0);
    CHECK(replay_parse_hand(line, &t, mode, &seed, result, sizeof result) == 0);
    CHECK(seed == 1 && strcmp(result, "a b") == 0);

    /* Truncation behaves like snprintf: the needed length is returned. */
    n = replay_format_hand(line, 10, 1790000000, "draw", 1, "x");
    CHECK(n > 10 && strlen(line) == 9);

    CHECK(replay_parse_hand("garbage", &t, mode, &seed, result, sizeof result) == -1);
    CHECK(replay_parse_hand("1 draw 123 x", &t, mode, &seed, result, sizeof result) == -1);
    CHECK(replay_format_hand(line, sizeof line, 1, "has space", 1, "") == -1);
}

int main(void)
{
    rng_reference();
    rng_determinism();
    replay_roundtrip();
    hand_lines();
    return test_finish("rng_replay_determinism");
}
