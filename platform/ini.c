/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* ini.c - see ini.h. */
#include "platform/ini.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static char *trim(char *s)
{
    while (*s && isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = '\0';
    return s;
}

int ini_parse(const char *path, IniHandler handler, void *user)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[512], section[64] = "";
    int lineno = 0, errors = 0;
    while (fgets(line, sizeof line, f)) {
        lineno++;
        char *s = trim(line);
        if (*s == '\0' || *s == ';' || *s == '#') continue;
        if (*s == '[') {
            char *end = strchr(s, ']');
            if (!end) {
                fprintf(stderr, "CONFIG: %s:%d: missing ']'\n", path, lineno);
                errors++;
                continue;
            }
            *end = '\0';
            snprintf(section, sizeof section, "%s", trim(s + 1));
            continue;
        }
        char *eq = strchr(s, '=');
        if (!eq) {
            fprintf(stderr, "CONFIG: %s:%d: expected key = value\n", path, lineno);
            errors++;
            continue;
        }
        *eq = '\0';
        char *key = trim(s), *val = eq + 1;
        /* A comment may follow the value after whitespace. */
        for (char *c = val; *c; c++) {
            if ((*c == ';' || *c == '#') && (c == val || isspace((unsigned char)c[-1]))) { *c = '\0'; break; }
        }
        val = trim(val);
        if (handler(user, section, key, val) != 0) {
            fprintf(stderr, "CONFIG: %s:%d: bad setting [%s] %s = %s\n", path, lineno, section, key, val);
            errors++;
        }
    }
    fclose(f);
    return errors;
}
