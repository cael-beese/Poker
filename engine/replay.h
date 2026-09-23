/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
#ifndef BPL_ENGINE_REPLAY_H
#define BPL_ENGINE_REPLAY_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "input.h"

/* engine/replay.h

   1. The hand log: one text line per hand,
          "<unix-time> <mode> <seed-hex> <result>\n"
      e.g. "1790000000 draw 9f3c0a1b22e4d5f6 JOB bet=5 win=40 FULL HOUSE".
      mode is one word (no spaces), the seed is 16 lower-case hex digits and
      result is free text to the end of the line (newlines are replaced).

   2. The input log: a session's seed plus its InputFrame stream, from which
      the session replays exactly (games are deterministic in seed + frames).
      Binary, little-endian, independent of struct layout:
          "BPLINPUT"  u32 version(1)  u64 seed
          u8 mode_len, mode bytes     u16 meta_len, meta bytes
          records: u32 run (>= 1), then one 17-byte frame repeated run times
                   (u32 down, u32 pressed, i16 slider, i16 touch_x,
                    i16 touch_y, u8 touch)
          trailer: u32 0
      Identical consecutive frames are run-length coded, so an idle hour is a
      few bytes. meta is opaque to the engine: the app stores whatever else
      the session needs to replay (game config, starting credits).
      A log cut short by a power loss has no trailer; the reader returns the
      frames it has and sets `truncated`. */

#define REPLAY_MODE_MAX 15
#define REPLAY_META_MAX 1024

/* Formats one hand line (with the trailing newline) into out. Returns the
   length it needed, like snprintf, or -1 on bad arguments. */
int replay_format_hand(char *out, size_t cap, int64_t unix_time, const char *mode,
                       uint64_t seed, const char *result);

/* Appends one hand line to f and flushes it. 0 ok, -1 on error. */
int replay_write_hand(FILE *f, int64_t unix_time, const char *mode, uint64_t seed,
                      const char *result);

/* Parses a hand line. mode gets at most REPLAY_MODE_MAX chars; result gets
   the rest of the line without the newline (truncated to result_cap - 1).
   0 ok, -1 if the line is not a hand line. */
int replay_parse_hand(const char *line, int64_t *unix_time, char mode[REPLAY_MODE_MAX + 1],
                      uint64_t *seed, char *result, size_t result_cap);

typedef struct {
    FILE    *f;
    int      owns;         /* 1 if replay_writer_open opened f             */
    uint8_t  last[17];    /* the frame being run-length counted           */
    uint32_t run;          /* how many times `last` repeats (0 = none yet) */
    uint64_t frames;       /* frames written so far                        */
    int      err;
} ReplayWriter;

/* Creates path and writes the header. 0 ok, -1 on error. */
int replay_writer_open(ReplayWriter *w, const char *path, uint64_t seed, const char *mode,
                       const void *meta, size_t meta_len);
/* Same, on an already open stream (a file, a memory stream, a pipe). */
int replay_writer_begin(ReplayWriter *w, FILE *f, uint64_t seed, const char *mode,
                        const void *meta, size_t meta_len);
int replay_writer_frame(ReplayWriter *w, const InputFrame *in);
/* Writes out the pending run and flushes the stream, so everything so far
   survives a crash; call it at hand boundaries. */
int replay_writer_flush(ReplayWriter *w);
/* Flushes, writes the trailer and closes the file (fclose only if the writer
   opened it with replay_writer_open). */
int replay_writer_close(ReplayWriter *w);

typedef struct {
    FILE    *f;
    int      owns;
    uint64_t seed;
    char     mode[REPLAY_MODE_MAX + 1];
    uint8_t  meta[REPLAY_META_MAX];
    size_t   meta_len;
    InputFrame cur;
    uint32_t left;         /* repeats of cur still to hand out             */
    uint64_t frames;       /* frames returned so far                       */
    int      done, truncated, err;
} ReplayReader;

int  replay_reader_open(ReplayReader *r, const char *path);
int  replay_reader_begin(ReplayReader *r, FILE *f);
/* 1 = a frame was stored in *out, 0 = end of log, -1 = corrupt log. */
int  replay_reader_frame(ReplayReader *r, InputFrame *out);
void replay_reader_close(ReplayReader *r);

#endif
