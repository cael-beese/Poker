/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* save.h - SaveData on disk, safe against power cuts (SPEC section 6).
 *
 * Files in the save directory:
 *   state.sav      the current state
 *   state.sav.bak  the one before it (the previous good copy)
 *   state.sav.tmp  a write in progress (normally absent)
 *
 * Every file is a one-line header and a text payload (settings.h):
 *   "BPLSAVE 1 seq=<n> len=<payload bytes> crc=<crc32 hex>\n" <payload>
 * seq increases with every write. A file whose header, length or CRC-32 is
 * wrong is torn or corrupt and is ignored.
 *
 * Writing (save_store_write):
 *   1. write state.sav.tmp, fsync it, close it
 *   2. rename state.sav -> state.sav.bak
 *   3. rename state.sav.tmp -> state.sav
 *   4. fsync the directory, so both renames are on disk
 * Reading (save_store_load) looks at all three names and takes the valid one
 * with the highest seq. A cut at any point therefore leaves either the new
 * state (a complete, fsynced .tmp or state.sav) or the previous one (state.sav
 * or .bak): never a torn one, and never nothing once one save has completed.
 *
 * Pure POSIX C, no raylib: tests/platform links it. */
#ifndef BPL_PLATFORM_SAVE_H
#define BPL_PLATFORM_SAVE_H

#include <stddef.h>
#include <stdint.h>

#include "platform/settings.h"

#define SAVE_PATH_MAX 512

typedef struct {
    char     dir[SAVE_PATH_MAX];
    char     path[SAVE_PATH_MAX + 16], bak[SAVE_PATH_MAX + 16], tmp[SAVE_PATH_MAX + 16];
    int      writable;          /* the directory passed the write test          */
    uint64_t seq;               /* of the newest valid file seen or written     */
    char     error[160];        /* the last failure, "" if none                 */
} SaveStore;

enum { SAVE_SRC_NONE, SAVE_SRC_MAIN, SAVE_SRC_BAK, SAVE_SRC_TMP };

typedef struct {
    int source;                 /* SAVE_SRC_*: where the loaded state came from  */
    int corrupt;                /* files present but rejected (torn / bad CRC)   */
    int unknown_lines;          /* payload lines not understood                  */
} SaveLoadInfo;

/* Creates the directory (and its parents) if needed and runs the write test.
 * Always fills the store; returns 0 if the directory is writable, -1 if not
 * (st->error says why, and saves will fail: run from RAM). */
int  save_store_open(SaveStore *st, const char *dir);

/* Loads the newest valid state into d (which is set to defaults first).
 * Returns 0 when one was found, -1 when there was none (d = defaults). */
int  save_store_load(SaveStore *st, SaveData *d, SaveLoadInfo *info);

/* The atomic write described above. 0 ok, -1 failed (st->error). */
int  save_store_write(SaveStore *st, const SaveData *d);

/* The write test: creates, writes, fsyncs, reads back and removes a probe
 * file in dir. 0 ok, -1 failed (why, if not NULL). *ms = time taken. */
int  save_probe_dir(const char *dir, double *ms, char *why, size_t why_cap);

/* mkdir -p. 0 ok. */
int  save_mkdirs(const char *dir);

/* The file format, exposed for the tests. */
uint32_t save_crc32(const void *p, size_t n);
int  save_encode(const SaveData *d, uint64_t seq, char *out, size_t cap);   /* bytes, or -1 */
/* 0 ok (d filled, *seq set), -1 torn or corrupt. d must hold defaults. */
int  save_decode(const char *buf, size_t n, SaveData *d, uint64_t *seq, int *unknown_lines);

/* Fault injection for the tests: make the next save_store_write stop as if
 * the power were cut at a given point. Reset to SAVE_FAULT_NONE after use. */
enum {
    SAVE_FAULT_NONE,
    SAVE_FAULT_TORN_TMP,        /* only save_fault_bytes of the .tmp written      */
    SAVE_FAULT_AFTER_TMP,       /* .tmp complete and synced, no rename yet        */
    SAVE_FAULT_AFTER_BAK,       /* state.sav moved to .bak, .tmp not renamed yet  */
    SAVE_FAULT_ENOSPC           /* the write fails (disk full): an error, no cut  */
};
extern int    save_fault;
extern size_t save_fault_bytes;

#endif
