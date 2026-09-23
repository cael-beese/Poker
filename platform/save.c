/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* save.c - see save.h. */
#include "platform/save.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

int    save_fault = SAVE_FAULT_NONE;
size_t save_fault_bytes = 0;

#define SAVE_FILE_MAX (SAVE_PAYLOAD_MAX + 128)
#define SAVE_MAGIC "BPLSAVE 1 "

/* ---- CRC-32 (IEEE, reflected, as zlib) ----------------------------------- */

uint32_t save_crc32(const void *p, size_t n)
{
    static uint32_t table[256];
    static int ready;
    if (!ready) {
        /* Racing initialisations write identical values, so this is safe
           without a lock; in practice the first call is on the main thread. */
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        ready = 1;
    }
    const uint8_t *b = p;
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = table[(c ^ b[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

/* ---- the file format ---------------------------------------------------- */

int save_encode(const SaveData *d, uint64_t seq, char *out, size_t cap)
{
    char payload[SAVE_PAYLOAD_MAX];
    int n = save_data_format(d, payload, sizeof payload);
    if (n < 0 || (size_t)n >= sizeof payload) return -1;
    int h = snprintf(out, cap, SAVE_MAGIC "seq=%" PRIu64 " len=%d crc=%08" PRIx32 "\n", seq, n,
                     save_crc32(payload, (size_t)n));
    if (h < 0 || (size_t)h + (size_t)n > cap) return -1;
    memcpy(out + h, payload, (size_t)n);
    return h + n;
}

int save_decode(const char *buf, size_t n, SaveData *d, uint64_t *seq, int *unknown_lines)
{
    const size_t ml = sizeof SAVE_MAGIC - 1;
    if (n < ml || memcmp(buf, SAVE_MAGIC, ml) != 0) return -1;
    const char *nl = memchr(buf, '\n', n < 128 ? n : 128);
    if (!nl) return -1;
    char head[128];
    size_t hl = (size_t)(nl - buf);
    memcpy(head, buf, hl);
    head[hl] = '\0';
    uint64_t s;
    unsigned long len;
    uint32_t crc;
    if (sscanf(head + ml, "seq=%" SCNu64 " len=%lu crc=%" SCNx32, &s, &len, &crc) != 3) return -1;
    /* The header must be exactly what save_encode writes for these values.
       sscanf alone accepts variants - "crc=0A1B..." for "crc=0a1b...", a
       leading "+", extra spaces - so a damaged header byte could read back
       as the same numbers and go unnoticed. */
    {
        char canon[128];
        int cl = snprintf(canon, sizeof canon, SAVE_MAGIC "seq=%" PRIu64 " len=%lu crc=%08" PRIx32, s, len, crc);
        if (cl < 0 || (size_t)cl != hl || memcmp(canon, head, hl) != 0) return -1;
    }
    const char *payload = nl + 1;
    size_t have = n - (size_t)(payload - buf);
    if (have != len) return -1;                 /* torn (short) or trailing junk */
    if (save_crc32(payload, have) != crc) return -1;
    int bad = save_data_parse(d, payload, have);
    if (seq) *seq = s;
    if (unknown_lines) *unknown_lines = bad;
    return 0;
}

/* ---- files ---------------------------------------------------------------- */

static void set_error(SaveStore *st, const char *what, const char *path)
{
    snprintf(st->error, sizeof st->error, "%s %.96s: %.40s", what, path, strerror(errno));
}

int save_mkdirs(const char *dir)
{
    char p[SAVE_PATH_MAX];
    if (snprintf(p, sizeof p, "%s", dir) >= (int)sizeof p) { errno = ENAMETOOLONG; return -1; }
    for (char *s = p + 1; *s; s++) {
        if (*s != '/') continue;
        *s = '\0';
        if (mkdir(p, 0755) != 0 && errno != EEXIST) return -1;
        *s = '/';
    }
    if (mkdir(p, 0755) != 0 && errno != EEXIST) return -1;
    struct stat sb;
    if (stat(p, &sb) != 0) return -1;
    if (!S_ISDIR(sb.st_mode)) { errno = ENOTDIR; return -1; }
    return 0;
}

static int write_all(int fd, const char *p, size_t n)
{
    while (n > 0) {
        ssize_t w = write(fd, p, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        p += w;
        n -= (size_t)w;
    }
    return 0;
}

static int fsync_dir(const char *dir)
{
    int fd = open(dir, O_RDONLY | O_DIRECTORY);
    if (fd < 0) return -1;
    int rc = fsync(fd);
    /* Some filesystems cannot fsync a directory (EINVAL); their renames are
       as durable as they will ever be, so that is not a failure. */
    if (rc != 0 && (errno == EINVAL || errno == EROFS)) rc = 0;
    close(fd);
    return rc;
}

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

int save_probe_dir(const char *dir, double *ms, char *why, size_t why_cap)
{
    double t0 = now_ms();
    char path[SAVE_PATH_MAX + 32];
    char data[4096], back[4096];
    int rc = -1, fd = -1;
    snprintf(path, sizeof path, "%s/.write-test-%ld", dir, (long)getpid());
    for (size_t i = 0; i < sizeof data; i++) data[i] = (char)('A' + i % 26);

    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { if (why) snprintf(why, why_cap, "create: %s", strerror(errno)); goto out; }
    if (write_all(fd, data, sizeof data) != 0) { if (why) snprintf(why, why_cap, "write: %s", strerror(errno)); goto out; }
    if (fsync(fd) != 0) { if (why) snprintf(why, why_cap, "fsync: %s", strerror(errno)); goto out; }
    close(fd);
    fd = open(path, O_RDONLY);
    if (fd < 0) { if (why) snprintf(why, why_cap, "reopen: %s", strerror(errno)); goto out; }
    if (read(fd, back, sizeof back) != (ssize_t)sizeof back || memcmp(back, data, sizeof data) != 0) {
        if (why) snprintf(why, why_cap, "read-back mismatch");
        goto out;
    }
    rc = 0;
    if (why && why_cap) why[0] = '\0';
out:
    if (fd >= 0) close(fd);
    unlink(path);
    if (rc == 0 && fsync_dir(dir) != 0) {
        if (why) snprintf(why, why_cap, "fsync dir: %s", strerror(errno));
        rc = -1;
    }
    if (ms) *ms = now_ms() - t0;
    return rc;
}

int save_store_open(SaveStore *st, const char *dir)
{
    memset(st, 0, sizeof *st);
    snprintf(st->dir, sizeof st->dir, "%s", dir);
    snprintf(st->path, sizeof st->path, "%s/state.sav", st->dir);
    snprintf(st->bak, sizeof st->bak, "%s/state.sav.bak", st->dir);
    snprintf(st->tmp, sizeof st->tmp, "%s/state.sav.tmp", st->dir);
    if (save_mkdirs(st->dir) != 0) {
        set_error(st, "cannot create", st->dir);
        return -1;
    }
    char why[120];
    if (save_probe_dir(st->dir, NULL, why, sizeof why) != 0) {
        snprintf(st->error, sizeof st->error, "%.80s not writable (%.60s)", st->dir, why);
        return -1;
    }
    st->writable = 1;
    return 0;
}

static int read_file(const char *path, char *buf, size_t cap, size_t *n)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    size_t got = 0;
    for (;;) {
        if (got == cap) { close(fd); return -2; }      /* too big to be ours */
        ssize_t r = read(fd, buf + got, cap - got);
        if (r < 0) {
            if (errno == EINTR) continue;
            close(fd);
            return -2;
        }
        if (r == 0) break;
        got += (size_t)r;
    }
    close(fd);
    *n = got;
    return 0;
}

int save_store_load(SaveStore *st, SaveData *d, SaveLoadInfo *info)
{
    static char buf[SAVE_FILE_MAX];
    const char *names[3] = { st->path, st->bak, st->tmp };
    const int src[3] = { SAVE_SRC_MAIN, SAVE_SRC_BAK, SAVE_SRC_TMP };
    SaveLoadInfo li = { SAVE_SRC_NONE, 0, 0 };
    uint64_t best_seq = 0;
    int found = 0;

    save_data_defaults(d);
    for (int i = 0; i < 3; i++) {
        size_t n = 0;
        int rc = read_file(names[i], buf, sizeof buf, &n);
        if (rc == -1) continue;                  /* absent */
        SaveData cand;
        uint64_t seq = 0;
        int unknown = 0;
        save_data_defaults(&cand);
        if (rc != 0 || save_decode(buf, n, &cand, &seq, &unknown) != 0) {
            li.corrupt++;
            continue;
        }
        if (!found || seq > best_seq) {
            *d = cand;
            best_seq = seq;
            li.source = src[i];
            li.unknown_lines = unknown;
            found = 1;
        }
    }
    if (found && best_seq > st->seq) st->seq = best_seq;
    if (info) *info = li;
    return found ? 0 : -1;
}

int save_store_write(SaveStore *st, const SaveData *d)
{
    static char buf[SAVE_FILE_MAX];
    int n = save_encode(d, st->seq + 1, buf, sizeof buf);
    if (n < 0) {
        snprintf(st->error, sizeof st->error, "state too large to encode");
        return -1;
    }

    int fd = open(st->tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { set_error(st, "create", st->tmp); return -1; }
    size_t len = (size_t)n;
    if (save_fault == SAVE_FAULT_TORN_TMP && save_fault_bytes < len) len = save_fault_bytes;
    if (save_fault == SAVE_FAULT_ENOSPC) {
        close(fd);
        errno = ENOSPC;
        set_error(st, "write", st->tmp);
        return -1;
    }
    if (write_all(fd, buf, len) != 0) { set_error(st, "write", st->tmp); close(fd); return -1; }
    if (save_fault == SAVE_FAULT_TORN_TMP) { close(fd); return -1; }   /* the "power cut" */
    if (fsync(fd) != 0) { set_error(st, "fsync", st->tmp); close(fd); return -1; }
    if (close(fd) != 0) { set_error(st, "close", st->tmp); return -1; }
    if (save_fault == SAVE_FAULT_AFTER_TMP) return -1;

    if (rename(st->path, st->bak) != 0 && errno != ENOENT) { set_error(st, "rename", st->path); return -1; }
    if (save_fault == SAVE_FAULT_AFTER_BAK) return -1;
    if (rename(st->tmp, st->path) != 0) { set_error(st, "rename", st->tmp); return -1; }
    if (fsync_dir(st->dir) != 0) { set_error(st, "fsync", st->dir); return -1; }
    st->seq++;
    st->error[0] = '\0';
    return 0;
}
