/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* ini.h - minimal INI reader for config.ini.
 *
 * Format: "[section]" lines, "key = value" lines, and comments starting with
 * ';' or '#' (whole line, or after whitespace at the end of a value). Keys and
 * section names are case-insensitive for the callers (they use strcasecmp).
 * Leading/trailing whitespace is trimmed. Lines longer than 511 chars are cut. */
#ifndef BPL_PLATFORM_INI_H
#define BPL_PLATFORM_INI_H

/* Return 0 to continue, non-zero to report the line as an error (parsing goes on). */
typedef int (*IniHandler)(void *user, const char *section, const char *key, const char *value);

/* Returns -1 if the file cannot be opened, else the number of lines the
 * handler rejected (0 = all good). Rejected lines are logged to stderr. */
int ini_parse(const char *path, IniHandler handler, void *user);

#endif
