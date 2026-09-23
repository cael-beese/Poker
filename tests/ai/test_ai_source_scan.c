/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* AI integrity, part 1: what the AI's source is allowed to touch.

   Scans every .c and .h file in ai/ and fails if any of them
     - includes anything outside an allow-list (ai/ headers, the card, RNG
       and evaluator headers of the engine, and the C / POSIX library), or
     - mentions the Hold'em module's headers or the engine's deck at all
       (the Deck type, deck_ functions, engine/deck.h) - the AI has no
       business with the undealt cards, not even to "simulate" with them.
   Also checks the other direction: if games/holdem exists, it may include
   nothing from ai/ except ai/ai_view.h (docs/CONTRACT.md section 5). */

#include "test_util.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef BPL_SOURCE_DIR
#error "BPL_SOURCE_DIR must be defined by the build"
#endif

static const char *const FORBIDDEN[] = {
  "games/holdem", "holdem.h", "engine/deck.h", "Deck", "deck_", NULL
};

static const char *const ALLOWED_INCLUDES[] = {
  "\"ai/", "\"engine/card.h\"", "\"engine/rng.h\"", "\"engine/eval.h\"",
  "<stdint.h>", "<stddef.h>", "<string.h>", "<stdlib.h>", "<math.h>",
  "<pthread.h>", "<stdio.h>", NULL
};

static int is_source(const char *name)
{
  size_t n = strlen(name);
  return n > 2 && name[n - 2] == '.' && (name[n - 1] == 'c' || name[n - 1] == 'h');
}

static char *slurp(const char *path, long *len)
{
  FILE *f = fopen(path, "rb");
  char *buf;
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  *len = ftell(f);
  fseek(f, 0, SEEK_SET);
  buf = (char *)malloc((size_t)*len + 1);
  if (buf && fread(buf, 1, (size_t)*len, f) != (size_t)*len) { free(buf); buf = NULL; }
  if (buf) buf[*len] = 0;
  fclose(f);
  return buf;
}

/* Returns the number of problems found in one line of an ai/ file. */
static int check_ai_line(const char *file, int lineno, const char *line)
{
  int bad = 0, k;
  const char *p = line;
  for (k = 0; FORBIDDEN[k]; k++)
    if (strstr(line, FORBIDDEN[k])) {
      fprintf(stderr, "FAIL %s:%d mentions \"%s\"\n", file, lineno, FORBIDDEN[k]);
      bad++;
    }
  while (*p == ' ' || *p == '\t') p++;
  if (strncmp(p, "#include", 8) == 0) {
    int ok = 0;
    p += 8;
    while (*p == ' ' || *p == '\t') p++;
    for (k = 0; ALLOWED_INCLUDES[k]; k++)
      if (strncmp(p, ALLOWED_INCLUDES[k], strlen(ALLOWED_INCLUDES[k])) == 0) ok = 1;
    if (!ok) {
      fprintf(stderr, "FAIL %s:%d includes something outside the allow-list: %s\n", file, lineno, p);
      bad++;
    }
  }
  return bad;
}

static int scan_dir(const char *dir, int ai_side, int *nfiles)
{
  DIR *d = opendir(dir);
  struct dirent *e;
  int bad = 0;
  if (!d) return -1;
  while ((e = readdir(d)) != NULL) {
    char path[1024];
    long len = 0;
    char *buf, *line;
    int lineno = 0;
    if (!is_source(e->d_name)) continue;
    snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
    buf = slurp(path, &len);
    if (!buf) { fprintf(stderr, "FAIL cannot read %s\n", path); bad++; continue; }
    (*nfiles)++;
    line = buf;
    while (line && *line) {
      char *nl = strchr(line, '\n');
      if (nl) *nl = 0;
      lineno++;
      if (ai_side) {
        bad += check_ai_line(e->d_name, lineno, line);
      } else {
        const char *inc = strstr(line, "#include \"ai/");
        if (inc && strncmp(inc, "#include \"ai/ai_view.h\"", 23) != 0) {
          fprintf(stderr, "FAIL games/holdem/%s:%d includes an AI header other than ai_view.h\n",
                  e->d_name, lineno);
          bad++;
        }
      }
      line = nl ? nl + 1 : NULL;
    }
    free(buf);
  }
  closedir(d);
  return bad;
}

int main(void)
{
  int nfiles = 0, nholdem = 0, bad;

  /* The checker must be able to fail: a line it has to reject. */
  CHECK(check_ai_line("self-test", 1, "#include \"games/holdem/holdem.h\"") > 0);
  CHECK(check_ai_line("self-test", 2, "  Deck d; deck_draw(&d);") > 0);
  CHECK(check_ai_line("self-test", 3, "#include <unistd.h>") > 0);
  CHECK(check_ai_line("self-test", 4, "#include \"engine/eval.h\"") == 0);
  fprintf(stderr, "(the FAIL lines above are the checker's self-test: it must reject those)\n");

  bad = scan_dir(BPL_SOURCE_DIR "/ai", 1, &nfiles);
  CHECK(bad == 0);
  CHECK(nfiles >= 8);                   /* it really looked at the AI */
  printf("scanned %d ai/ source files: %d problems\n", nfiles, bad);

  bad = scan_dir(BPL_SOURCE_DIR "/games/holdem", 0, &nholdem);
  if (bad < 0) printf("games/holdem not present in this tree (checked when it is)\n");
  else {
    CHECK(bad == 0);
    printf("scanned %d games/holdem source files: %d problems\n", nholdem, bad);
  }
  return test_finish("ai_source_scan");
}
