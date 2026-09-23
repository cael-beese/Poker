/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
#include "replay.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

static const char MAGIC[8] = { 'B', 'P', 'L', 'I', 'N', 'P', 'U', 'T' };
#define VERSION 1u
#define FRAME_BYTES 17

/* ---- hand log ---------------------------------------------------------- */

static int mode_ok(const char *mode)
{
    size_t i, n;
    if (!mode) return 0;
    n = strlen(mode);
    if (n == 0 || n > REPLAY_MODE_MAX) return 0;
    for (i = 0; i < n; i++)
        if (!isgraph((unsigned char)mode[i])) return 0;
    return 1;
}

int replay_format_hand(char *out, size_t cap, int64_t unix_time, const char *mode,
                       uint64_t seed, const char *result)
{
    char head[64];
    size_t hn, rn, i, need;
    int k;

    if (!mode_ok(mode)) return -1;
    if (!result) result = "";
    k = snprintf(head, sizeof head, "%" PRId64 " %s %016" PRIx64 " ", unix_time, mode, seed);
    if (k < 0 || (size_t)k >= sizeof head) return -1;
    hn = (size_t)k;
    rn = strlen(result);
    need = hn + rn + 1;
    if (out && cap > 0) {
        /* Copy by hand rather than snprintf so newlines in the result can be
           flattened; one hand must stay one line. */
        size_t o = 0;
        for (i = 0; i < hn && o + 1 < cap; i++) out[o++] = head[i];
        for (i = 0; i < rn && o + 1 < cap; i++)
            out[o++] = (result[i] == '\n' || result[i] == '\r') ? ' ' : result[i];
        if (o + 1 < cap) out[o++] = '\n';
        out[o] = '\0';
    }
    return (int)need;
}

int replay_write_hand(FILE *f, int64_t unix_time, const char *mode, uint64_t seed,
                      const char *result)
{
    char buf[512];
    char *p = buf;
    int n = replay_format_hand(NULL, 0, unix_time, mode, seed, result);
    int rc = 0;
    if (!f || n < 0) return -1;
    if ((size_t)n + 1 > sizeof buf) {
        p = malloc((size_t)n + 1);
        if (!p) return -1;
    }
    replay_format_hand(p, (size_t)n + 1, unix_time, mode, seed, result);
    if (fwrite(p, 1, (size_t)n, f) != (size_t)n || fflush(f) != 0) rc = -1;
    if (p != buf) free(p);
    return rc;
}

int replay_parse_hand(const char *line, int64_t *unix_time, char mode[REPLAY_MODE_MAX + 1],
                      uint64_t *seed, char *result, size_t result_cap)
{
    const char *p = line, *q;
    char *end;
    long long t;
    unsigned long long s;
    size_t n;

    if (!line) return -1;
    errno = 0;
    t = strtoll(p, &end, 10);
    if (end == p || *end != ' ' || errno) return -1;
    p = end + 1;

    q = p;
    while (*q && *q != ' ') q++;
    n = (size_t)(q - p);
    if (n == 0 || n > REPLAY_MODE_MAX || *q != ' ') return -1;
    if (mode) { memcpy(mode, p, n); mode[n] = '\0'; }
    p = q + 1;

    for (q = p; isxdigit((unsigned char)*q); q++) {}
    if (q - p != 16 || (*q != ' ' && *q != '\n' && *q != '\0')) return -1;
    errno = 0;
    s = strtoull(p, &end, 16);
    if (end != q || errno) return -1;
    p = (*q == ' ') ? q + 1 : q;

    if (unix_time) *unix_time = (int64_t)t;
    if (seed) *seed = (uint64_t)s;
    if (result && result_cap > 0) {
        size_t o = 0;
        while (*p && *p != '\n' && *p != '\r' && o + 1 < result_cap) result[o++] = *p++;
        result[o] = '\0';
    }
    return 0;
}

/* ---- input log --------------------------------------------------------- */

static void put_u16(uint8_t *b, uint16_t v) { b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); }
static void put_u32(uint8_t *b, uint32_t v)
{
    b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); b[2] = (uint8_t)(v >> 16); b[3] = (uint8_t)(v >> 24);
}
static void put_u64(uint8_t *b, uint64_t v) { put_u32(b, (uint32_t)v); put_u32(b + 4, (uint32_t)(v >> 32)); }
static uint16_t get_u16(const uint8_t *b) { return (uint16_t)(b[0] | (b[1] << 8)); }
static uint32_t get_u32(const uint8_t *b)
{
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}
static uint64_t get_u64(const uint8_t *b) { return (uint64_t)get_u32(b) | ((uint64_t)get_u32(b + 4) << 32); }

static void frame_pack(uint8_t b[FRAME_BYTES], const InputFrame *in)
{
    put_u32(b, in->down);
    put_u32(b + 4, in->pressed);
    put_u16(b + 8, (uint16_t)in->slider);
    put_u16(b + 10, (uint16_t)in->touch_x);
    put_u16(b + 12, (uint16_t)in->touch_y);
    b[14] = in->touch;
    /* Two spare bytes keep records a fixed size if a field is ever added. */
    b[15] = 0;
    b[16] = 0;
}

static void frame_unpack(InputFrame *out, const uint8_t b[FRAME_BYTES])
{
    memset(out, 0, sizeof *out);
    out->down = get_u32(b);
    out->pressed = get_u32(b + 4);
    out->slider = (int16_t)get_u16(b + 8);
    out->touch_x = (int16_t)get_u16(b + 10);
    out->touch_y = (int16_t)get_u16(b + 12);
    out->touch = b[14];
}

static int wr(ReplayWriter *w, const void *p, size_t n)
{
    if (w->err) return -1;
    if (fwrite(p, 1, n, w->f) != n) { w->err = 1; return -1; }
    return 0;
}

int replay_writer_begin(ReplayWriter *w, FILE *f, uint64_t seed, const char *mode,
                        const void *meta, size_t meta_len)
{
    uint8_t hdr[8 + 4 + 8 + 1];
    uint8_t ml[2];
    size_t mlen;

    memset(w, 0, sizeof *w);
    if (!f || !mode_ok(mode) || meta_len > REPLAY_META_MAX || (meta_len && !meta)) return -1;
    w->f = f;
    mlen = strlen(mode);
    memcpy(hdr, MAGIC, 8);
    put_u32(hdr + 8, VERSION);
    put_u64(hdr + 12, seed);
    hdr[20] = (uint8_t)mlen;
    put_u16(ml, (uint16_t)meta_len);
    if (wr(w, hdr, sizeof hdr) || wr(w, mode, mlen) || wr(w, ml, 2)) return -1;
    if (meta_len && wr(w, meta, meta_len)) return -1;
    return 0;
}

int replay_writer_open(ReplayWriter *w, const char *path, uint64_t seed, const char *mode,
                       const void *meta, size_t meta_len)
{
    FILE *f = fopen(path, "wb");
    if (!f) { memset(w, 0, sizeof *w); w->err = 1; return -1; }
    if (replay_writer_begin(w, f, seed, mode, meta, meta_len) != 0) {
        fclose(f);
        w->f = NULL;
        w->err = 1;
        return -1;
    }
    w->owns = 1;
    return 0;
}

static int flush_run(ReplayWriter *w)
{
    uint8_t rb[4];
    if (w->run == 0) return w->err ? -1 : 0;
    put_u32(rb, w->run);
    w->run = 0;
    if (wr(w, rb, 4) || wr(w, w->last, FRAME_BYTES)) return -1;
    return 0;
}

int replay_writer_frame(ReplayWriter *w, const InputFrame *in)
{
    uint8_t b[FRAME_BYTES];
    if (!w->f || w->err) return -1;
    frame_pack(b, in);
    if (w->run > 0 && w->run < UINT32_MAX && memcmp(b, w->last, FRAME_BYTES) == 0) {
        w->run++;
    } else {
        if (flush_run(w)) return -1;
        memcpy(w->last, b, FRAME_BYTES);
        w->run = 1;
    }
    w->frames++;
    return 0;
}

int replay_writer_flush(ReplayWriter *w)
{
    if (!w->f) return -1;
    if (flush_run(w)) return -1;
    if (fflush(w->f) != 0) { w->err = 1; return -1; }
    return 0;
}

int replay_writer_close(ReplayWriter *w)
{
    uint8_t zero[4] = { 0, 0, 0, 0 };
    int rc = 0;
    if (!w->f) return -1;
    if (flush_run(w) || wr(w, zero, 4)) rc = -1;
    if (fflush(w->f) != 0) rc = -1;
    if (w->owns && fclose(w->f) != 0) rc = -1;
    w->f = NULL;
    return rc;
}

int replay_reader_begin(ReplayReader *r, FILE *f)
{
    uint8_t hdr[8 + 4 + 8 + 1];
    uint8_t ml[2];
    size_t mlen;

    memset(r, 0, sizeof *r);
    if (!f) return -1;
    r->f = f;
    if (fread(hdr, 1, sizeof hdr, f) != sizeof hdr) return -1;
    if (memcmp(hdr, MAGIC, 8) != 0 || get_u32(hdr + 8) != VERSION) return -1;
    r->seed = get_u64(hdr + 12);
    mlen = hdr[20];
    if (mlen == 0 || mlen > REPLAY_MODE_MAX) return -1;
    if (fread(r->mode, 1, mlen, f) != mlen) return -1;
    r->mode[mlen] = '\0';
    if (fread(ml, 1, 2, f) != 2) return -1;
    r->meta_len = get_u16(ml);
    if (r->meta_len > REPLAY_META_MAX) return -1;
    if (r->meta_len && fread(r->meta, 1, r->meta_len, f) != r->meta_len) return -1;
    return 0;
}

int replay_reader_open(ReplayReader *r, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { memset(r, 0, sizeof *r); r->err = 1; return -1; }
    if (replay_reader_begin(r, f) != 0) {
        fclose(f);
        memset(r, 0, sizeof *r);
        r->err = 1;
        return -1;
    }
    r->owns = 1;
    return 0;
}

int replay_reader_frame(ReplayReader *r, InputFrame *out)
{
    if (r->err) return -1;
    if (!r->f || r->done) return 0;
    if (r->left == 0) {
        uint8_t rb[4], fb[FRAME_BYTES];
        size_t k = fread(rb, 1, 4, r->f);
        if (k == 0) { r->done = 1; r->truncated = 1; return 0; }
        if (k != 4) { r->done = 1; r->truncated = 1; return 0; }
        r->left = get_u32(rb);
        if (r->left == 0) { r->done = 1; return 0; }
        if (fread(fb, 1, FRAME_BYTES, r->f) != FRAME_BYTES) {
            /* The record header made it to disk but its frame did not: a cut
               mid-write. Everything before it is still good. */
            r->left = 0;
            r->done = 1;
            r->truncated = 1;
            return 0;
        }
        frame_unpack(&r->cur, fb);
    }
    r->left--;
    r->frames++;
    *out = r->cur;
    return 1;
}

void replay_reader_close(ReplayReader *r)
{
    if (r->f && r->owns) fclose(r->f);
    r->f = NULL;
}
