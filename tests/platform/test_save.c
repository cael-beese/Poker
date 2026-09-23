/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* test_save.c - the save file survives power cuts, corruption and read-only
 * storage (SPEC section 6; platform/save.h).
 *
 * A power cut is simulated two ways:
 *   - fault injection: save_store_write stops at each point of its sequence
 *     (part of the .tmp written, .tmp complete, state.sav moved to .bak) and
 *     the next start-up must load either the old or the new state, whole;
 *   - damage: state.sav truncated at every length and every byte flipped;
 *     the loader must reject it and fall back to the previous good copy;
 *   - a writer process SIGKILLed at random moments, many times.
 * Read-only storage: a save directory that cannot be created, and (when not
 * running as root, which ignores permissions) one that has been made
 * read-only; the store must say so, never crash, and still load. */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "platform/save.h"
#include "platform/settings.h"
#include "test_util.h"

static char g_root[256];

static void make_dir(char *out, size_t cap, const char *name)
{
    snprintf(out, cap, "%s/%s", g_root, name);
    CHECK(save_mkdirs(out) == 0);
}

static void rm_rf(const char *dir)
{
    char cmd[600];
    snprintf(cmd, sizeof cmd, "chmod -R u+w '%s' 2>/dev/null; rm -rf '%s'", dir, dir);
    if (system(cmd) != 0) fprintf(stderr, "note: could not remove %s\n", dir);
}

static SaveData state_with(int64_t credits)
{
    SaveData d;
    save_data_defaults(&d);
    d.credits = credits;
    d.stats.draw_hands[0] = (uint64_t)credits * 3;
    d.stats.royals = (uint64_t)credits % 7;
    d.settings.vol_music = (uint8_t)(credits % 11);
    return d;
}

static int64_t load_credits(const char *dir, SaveLoadInfo *info)
{
    SaveStore st;
    save_store_open(&st, dir);
    SaveData d;
    if (save_store_load(&st, &d, info) != 0) return -1;
    /* Whatever loads must be a whole state, never a mix of two. */
    CHECK_EQ_INT(d.stats.draw_hands[0], (uint64_t)d.credits * 3);
    CHECK_EQ_INT(d.settings.vol_music, d.credits % 11);
    return d.credits;
}

static long file_size(const char *path)
{
    struct stat sb;
    return stat(path, &sb) == 0 ? (long)sb.st_size : -1;
}

static void copy_file(const char *from, const char *to)
{
    char buf[16384];
    FILE *a = fopen(from, "rb"), *b = fopen(to, "wb");
    CHECK(a && b);
    if (!a || !b) { if (a) fclose(a); if (b) fclose(b); return; }
    size_t n = fread(buf, 1, sizeof buf, a);
    fwrite(buf, 1, n, b);
    fclose(a);
    fclose(b);
}

static void test_format(void)
{
    SaveData d, e;
    save_data_defaults(&d);
    d.credits = 123456789012LL;
    for (int i = 0; i < SAVE_VARIANTS; i++) {
        d.stats.draw_hands[i] = 1000u + (uint64_t)i;
        d.stats.draw_bet[i] = 5000u + (uint64_t)i;
        d.stats.draw_paid[i] = 4900u + (uint64_t)i;
    }
    d.stats.royals = 3;
    d.stats.four_deuces = 2;
    d.stats.biggest_win = 4000;
    d.stats.holdem_places[5] = 77;
    d.stats.coins_in = UINT64_C(18446744073709551000);
    d.settings.vol_master = 3;
    d.settings.denom_cents = 5;
    d.settings.effects.bloom = 0;
    d.settings.effects.side_art = 0;

    char a[SAVE_PAYLOAD_MAX + 128], b[SAVE_PAYLOAD_MAX + 128];
    int na = save_encode(&d, 42, a, sizeof a);
    CHECK(na > 0);
    uint64_t seq = 0;
    int unknown = -1;
    save_data_defaults(&e);
    CHECK(save_decode(a, (size_t)na, &e, &seq, &unknown) == 0);
    CHECK_EQ_INT(seq, 42);
    CHECK_EQ_INT(unknown, 0);
    int nb = save_encode(&e, 42, b, sizeof b);
    CHECK(na == nb && memcmp(a, b, (size_t)na) == 0);
    CHECK_EQ_INT(e.credits, 123456789012LL);
    CHECK(e.stats.coins_in == UINT64_C(18446744073709551000));
    CHECK_EQ_INT(e.settings.effects.bloom, 0);
    CHECK_EQ_INT(e.settings.effects.particles, 1);

    /* Hand-edited or future files: clamped, unknown keys skipped, junk counted. */
    const char *p = "set.vol_master=99\nset.denom_cents=0\nfuture.key=5\nfx.not_yet=1\n"
                    "credits=-50\ngarbage line\nset.vol_sfx=abc\n";
    save_data_defaults(&e);
    int bad = save_data_parse(&e, p, strlen(p));
    CHECK_EQ_INT(bad, 2);
    CHECK_EQ_INT(e.settings.vol_master, 10);
    CHECK_EQ_INT(e.settings.denom_cents, 1);
    CHECK_EQ_INT(e.credits, 0);
    CHECK_EQ_INT(e.settings.vol_sfx, 10);

    /* The header protects the payload. */
    save_data_defaults(&e);
    CHECK(save_decode(a, (size_t)na - 1, &e, NULL, NULL) != 0);     /* short   */
    a[na - 3] ^= 0x04;
    CHECK(save_decode(a, (size_t)na, &e, NULL, NULL) != 0);         /* flipped */
    CHECK(save_decode("BPLSAVE 1 seq=1 len=0 crc=00000000\n", 35, &e, NULL, NULL) == 0);
    CHECK(save_decode("BPLSAVE 2 seq=1 len=0 crc=00000000\n", 35, &e, NULL, NULL) != 0);
}

static void test_store_basics(void)
{
    char dir[400];
    make_dir(dir, sizeof dir, "basic/nested/deeper");
    SaveStore st;
    CHECK(save_store_open(&st, dir) == 0);
    CHECK(st.writable);
    SaveData d;
    SaveLoadInfo li;
    CHECK(save_store_load(&st, &d, &li) == -1);          /* a fresh cabinet */
    CHECK_EQ_INT(li.source, SAVE_SRC_NONE);
    CHECK_EQ_INT(d.credits, 1000);

    SaveData a = state_with(111), b = state_with(222);
    CHECK(save_store_write(&st, &a) == 0);
    CHECK_EQ_INT(load_credits(dir, &li), 111);
    CHECK_EQ_INT(li.source, SAVE_SRC_MAIN);
    CHECK(save_store_write(&st, &b) == 0);
    CHECK_EQ_INT(load_credits(dir, &li), 222);
    CHECK(access(st.bak, F_OK) == 0);                    /* previous copy kept */
    CHECK(access(st.tmp, F_OK) != 0);                    /* nothing left over  */

    /* ENOSPC: an error, and the state on disk is untouched. */
    SaveData c = state_with(333);
    save_fault = SAVE_FAULT_ENOSPC;
    CHECK(save_store_write(&st, &c) == -1);
    save_fault = SAVE_FAULT_NONE;
    CHECK(st.error[0] != '\0');
    CHECK_EQ_INT(load_credits(dir, NULL), 222);
    CHECK(save_store_write(&st, &c) == 0);
    CHECK_EQ_INT(load_credits(dir, NULL), 333);
}

/* A cut at every point of the write sequence. */
static void test_power_cuts(void)
{
    char dir[400];
    make_dir(dir, sizeof dir, "cuts");
    SaveStore st;
    CHECK(save_store_open(&st, dir) == 0);
    SaveData old = state_with(500), neu = state_with(600);
    CHECK(save_store_write(&st, &old) == 0);
    CHECK(save_store_write(&st, &old) == 0);
    char enc[SAVE_PAYLOAD_MAX + 128];
    /* The size of the file the store will write (its seq is the next one). */
    int len = save_encode(&neu, st.seq + 1, enc, sizeof enc);
    CHECK(len > 0);

    /* Part of the .tmp written: every length, the old state loads. */
    int torn_ok = 0;
    for (int cut = 0; cut < len; cut++) {
        SaveStore s2;
        save_store_open(&s2, dir);
        SaveData tmp;
        save_store_load(&s2, &tmp, NULL);
        save_fault = SAVE_FAULT_TORN_TMP;
        save_fault_bytes = (size_t)cut;
        save_store_write(&s2, &neu);
        save_fault = SAVE_FAULT_NONE;
        SaveLoadInfo li;
        int64_t c = load_credits(dir, &li);
        if (c == 500 && li.source == SAVE_SRC_MAIN) torn_ok++;
        else fprintf(stderr, "torn tmp at %d: loaded %lld from %d\n", cut, (long long)c, li.source);
    }
    CHECK_EQ_INT(torn_ok, len);

    /* .tmp complete, not renamed: the new state is already safe. */
    save_fault = SAVE_FAULT_AFTER_TMP;
    save_store_write(&st, &neu);
    save_fault = SAVE_FAULT_NONE;
    SaveLoadInfo li;
    CHECK_EQ_INT(load_credits(dir, &li), 600);
    CHECK_EQ_INT(li.source, SAVE_SRC_TMP);

    /* state.sav moved to .bak, .tmp not yet renamed: no state.sav at all. */
    {
        SaveStore s2;
        save_store_open(&s2, dir);
        SaveData tmp;
        save_store_load(&s2, &tmp, NULL);
        SaveData n7 = state_with(700);
        save_fault = SAVE_FAULT_AFTER_BAK;
        save_store_write(&s2, &n7);
        save_fault = SAVE_FAULT_NONE;
        CHECK(access(s2.path, F_OK) != 0);
        CHECK_EQ_INT(load_credits(dir, &li), 700);
        CHECK_EQ_INT(li.source, SAVE_SRC_TMP);
        /* ... and the next save puts everything back in order. */
        SaveData n8 = state_with(800);
        CHECK(save_store_write(&s2, &n8) == 0);
        CHECK_EQ_INT(load_credits(dir, &li), 800);
        CHECK_EQ_INT(li.source, SAVE_SRC_MAIN);
        CHECK(access(s2.tmp, F_OK) != 0);
    }
}

/* state.sav damaged after the fact: truncated or with a flipped byte. */
static void test_damage(void)
{
    char dir[400];
    make_dir(dir, sizeof dir, "damage");
    SaveStore st;
    CHECK(save_store_open(&st, dir) == 0);
    SaveData a = state_with(41), b = state_with(42);
    CHECK(save_store_write(&st, &a) == 0);
    CHECK(save_store_write(&st, &b) == 0);
    char good[SAVE_PATH_MAX + 32];
    snprintf(good, sizeof good, "%s/good.copy", g_root);
    copy_file(st.path, good);
    long size = file_size(st.path);
    CHECK(size > 100);

    int trunc_ok = 0;
    for (long cut = 0; cut < size; cut++) {
        copy_file(good, st.path);
        CHECK(truncate(st.path, cut) == 0);
        SaveLoadInfo li;
        int64_t c = load_credits(dir, &li);
        if (c == 41 && li.source == SAVE_SRC_BAK && li.corrupt == 1) trunc_ok++;
    }
    CHECK_EQ_INT(trunc_ok, size);

    int flip_ok = 0;
    for (long pos = 0; pos < size; pos++) {
        copy_file(good, st.path);
        int fd = open(st.path, O_RDWR);
        unsigned char ch;
        CHECK(pread(fd, &ch, 1, pos) == 1);
        ch ^= (unsigned char)(1u << (pos % 8));
        CHECK(pwrite(fd, &ch, 1, pos) == 1);
        close(fd);
        SaveLoadInfo li;
        int64_t c = load_credits(dir, &li);
        if (c == 41 && li.source == SAVE_SRC_BAK) flip_ok++;
        else fprintf(stderr, "flip at %ld: loaded %lld from %d\n", pos, (long long)c, li.source);
    }
    CHECK_EQ_INT(flip_ok, size);

    /* Both copies bad: defaults, and the damage is reported. */
    FILE *f = fopen(st.path, "wb");
    fputs("BPLSAVE 1 seq=9 len=5 crc=00000000\nhello", f);
    fclose(f);
    f = fopen(st.bak, "wb");
    fputs("\x7f" "ELF garbage", f);
    fclose(f);
    SaveData d;
    SaveLoadInfo li;
    CHECK(save_store_load(&st, &d, &li) == -1);
    CHECK_EQ_INT(li.corrupt, 2);
    CHECK_EQ_INT(d.credits, 1000);
    remove(good);
}

/* A writer killed at random moments (the page cache survives a SIGKILL, so
 * this checks the process-level atomicity; the fault injection above covers
 * what the disk may hold after a real power cut). */
static void test_kill(void)
{
    char dir[400];
    make_dir(dir, sizeof dir, "kill");
    SaveStore st;
    CHECK(save_store_open(&st, dir) == 0);
    SaveData first = state_with(1);
    CHECK(save_store_write(&st, &first) == 0);
    int ok = 0, rounds = 40;
    int64_t last = 1;
    for (int r = 0; r < rounds; r++) {
        pid_t pid = fork();
        if (pid == 0) {
            SaveStore s2;
            save_store_open(&s2, dir);
            SaveData d;
            save_store_load(&s2, &d, NULL);
            for (int64_t c = d.credits + 1;; c++) {
                SaveData n = state_with(c);
                save_store_write(&s2, &n);
            }
        }
        usleep((useconds_t)(2000 + (r * 7919) % 20000));
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        int64_t c = load_credits(dir, NULL);
        if (c >= last) ok++;         /* whole, and never older than before */
        last = c;
    }
    CHECK_EQ_INT(ok, rounds);
    fprintf(stderr, "kill test: %d rounds, final credits %lld\n", rounds, (long long)last);
}

static void test_read_only(void)
{
    /* A directory that cannot be made: a regular file is in the way. */
    char file[400], dir[450];
    snprintf(file, sizeof file, "%s/not-a-dir", g_root);
    FILE *f = fopen(file, "w");
    fputs("x", f);
    fclose(f);
    snprintf(dir, sizeof dir, "%s/saves", file);
    SaveStore st;
    CHECK(save_store_open(&st, dir) == -1);
    CHECK(!st.writable);
    CHECK(st.error[0] != '\0');
    SaveData d = state_with(5);
    CHECK(save_store_write(&st, &d) == -1);
    SaveLoadInfo li;
    CHECK(save_store_load(&st, &d, &li) == -1);         /* nothing, but no crash */
    CHECK_EQ_INT(d.credits, 1000);

    /* A read-only kernel filesystem. */
    CHECK(save_store_open(&st, "/proc/bpl-save-test") == -1);

    /* A directory with a save in it that has become read-only (the SD card
     * remounted ro): the state still loads, writes are refused. Root ignores
     * the permission bits, so this part needs a normal user. */
    char ro[400];
    make_dir(ro, sizeof ro, "readonly");
    CHECK(save_store_open(&st, ro) == 0);
    d = state_with(77);
    CHECK(save_store_write(&st, &d) == 0);
    if (geteuid() != 0) {
        CHECK(chmod(ro, 0555) == 0);
        SaveStore s2;
        CHECK(save_store_open(&s2, ro) == -1);
        CHECK(!s2.writable);
        CHECK_EQ_INT(load_credits(ro, NULL), 77);
        SaveData e = state_with(78);
        CHECK(save_store_write(&s2, &e) == -1);
        CHECK_EQ_INT(load_credits(ro, NULL), 77);
        double ms;
        CHECK(save_probe_dir(ro, &ms, NULL, 0) == -1);
        chmod(ro, 0755);
    } else {
        fprintf(stderr, "note: running as root, the chmod read-only case is skipped\n");
    }
    double ms = -1;
    CHECK(save_probe_dir(ro, &ms, NULL, 0) == 0);
    CHECK(ms >= 0);
}

static void test_rules(void)
{
    for (int i = 0; i < settings_nbuyins; i++) {
        int64_t b = settings_buyins[i], sum = 0;
        for (int p = 1; p <= 6; p++) sum += sng_prize(b, p);
        CHECK_EQ_INT(sum, 6 * b);       /* the pool is paid out exactly */
        CHECK(sng_prize(b, 1) > sng_prize(b, 2) && sng_prize(b, 2) > sng_prize(b, 3));
        CHECK_EQ_INT(sng_prize(b, 4), 0);
    }
    Settings s;
    settings_defaults(&s);
    s.denom_cents = 25;
    s.coin_cents = 100;
    CHECK_EQ_INT(settings_coin_credits(&s), 4);
    s.denom_cents = 100;
    s.coin_cents = 25;
    CHECK_EQ_INT(settings_coin_credits(&s), 1);
}

int main(void)
{
    const char *tmp = getenv("TMPDIR");
    snprintf(g_root, sizeof g_root, "%s/bpl-save-XXXXXX", tmp && *tmp ? tmp : "/tmp");
    if (!mkdtemp(g_root)) {
        perror("mkdtemp");
        return 1;
    }
    test_format();
    test_store_basics();
    test_power_cuts();
    test_damage();
    test_kill();
    test_read_only();
    test_rules();
    rm_rf(g_root);
    if (g_test_failures) {
        fprintf(stderr, "save: %d failures\n", g_test_failures);
        return 1;
    }
    printf("save: all passed\n");
    return 0;
}
