/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* options.c - see options.h. */
#include "platform/options.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "platform/app.h"
#include "platform/gputest.h"
#include "platform/ini.h"
#include "platform/fx_settings.h"

static const char *const g_usage =
    "usage: beese-poker [options]\n"
    "  --config FILE        settings file (default: config.ini next to the binary)\n"
    "  --mode NAME          start state: attract menu draw holdem service gputest rendertest\n"
    "  --seed HEX           session seed (default: OS entropy)\n"
    "  --frames N           exit after N rendered frames\n"
    "  --perf-csv FILE      per-frame log: frame,frame_ms,logic_ms,render_ms,draw_calls,...\n"
    "  --shot N:FILE[,...]  save the 1280x720 play space after frame N (PNG);\n"
    "                       N:screen:FILE saves the whole screen instead\n"
    "  --script STEPS       inject logical buttons: \"T:BTN[+BTN];A-B:BTN;...\"\n"
    "                       T/A-B are tick numbers (60 per second, from 0)\n"
    "  --lockstep           exactly one tick per frame (reproducible shots/demos)\n"
    "  --gpu-finish         wait for the GPU after render and compose (true GPU ms)\n"
    "  --overlay            start with the F1 debug overlay on\n"
    "  --fx LIST            effect toggles, e.g. bloom=off,shake=off (none = all off)\n"
    "  --scale MODE         auto | integer | fit | 1x\n"
    "  --filter MODE        auto | point | bilinear\n"
    "  --no-side-art        black margins\n"
    "  --size WxH           window size (desktop) or display mode (DRM; default native)\n"
    "  --no-vsync           do not wait for vblank (desktop only)\n"
    "  --sprites N          gputest: sprite count (default 1000)\n"
    "  --no-shader          gputest: skip the full-screen shader\n"
    "  --verbose            raylib info logging\n"
    "  --help\n";

typedef struct { Options *o; } CfgCtx;

static int cfg_handler(void *user, const char *section, const char *key, const char *value)
{
    Options *o = ((CfgCtx *)user)->o;
    if (strcasecmp(section, "input") == 0) return input_config_set(&o->input, key, value);
    if (strcasecmp(section, "display") == 0) {
        if (strcasecmp(key, "size") == 0) {
            if (strcasecmp(value, "native") == 0) { o->width = o->height = 0; return 0; }
            return sscanf(value, "%dx%d", &o->width, &o->height) == 2 ? 0 : -1;
        }
        if (strcasecmp(key, "vsync") == 0) {
            o->vsync = strcasecmp(value, "off") != 0 && strcmp(value, "0") != 0;
            return 0;
        }
        return screen_config_set(&o->screen, key, value);
    }
    if (strcasecmp(section, "effects") == 0) return fx_settings_set(&g_effects, key, value);
    if (strcasecmp(section, "debug") == 0) {
        if (strcasecmp(key, "overlay") == 0) {
            o->overlay = strcasecmp(value, "on") == 0 || strcmp(value, "1") == 0;
            return 0;
        }
    }
    return -1;
}

static int file_exists(const char *p) { return access(p, R_OK) == 0; }

static void find_config(Options *o, const char *explicit_path)
{
    o->config_path[0] = '\0';
    if (explicit_path) {
        snprintf(o->config_path, sizeof o->config_path, "%s", explicit_path);
        return;
    }
    char exe[400];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n > 0) {
        exe[n] = '\0';
        char *slash = strrchr(exe, '/');
        if (slash) {
            *slash = '\0';
            snprintf(o->config_path, sizeof o->config_path, "%s/config.ini", exe);
            if (file_exists(o->config_path)) return;
        }
    }
    snprintf(o->config_path, sizeof o->config_path, "platform/config.ini");
    if (!file_exists(o->config_path)) o->config_path[0] = '\0';
}

static int parse_shots(Options *o, const char *arg)
{
    char buf[1024];
    snprintf(buf, sizeof buf, "%s", arg);
    for (char *save = NULL, *tok = strtok_r(buf, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
        if (o->nshots >= OPT_MAX_SHOTS) return -1;
        ShotReq *s = &o->shots[o->nshots];
        char *colon = strchr(tok, ':');
        if (!colon) return -1;
        *colon = '\0';
        char *end;
        s->frame = strtoull(tok, &end, 10);
        if (*end) return -1;
        const char *path = colon + 1;
        s->screen = 0;
        if (strncmp(path, "screen:", 7) == 0) { s->screen = 1; path += 7; }
        if (!*path) return -1;
        snprintf(s->path, sizeof s->path, "%s", path);
        o->nshots++;
    }
    return 0;
}

static int parse_script(Options *o, const char *arg)
{
    char buf[4096];
    snprintf(buf, sizeof buf, "%s", arg);
    for (char *save = NULL, *tok = strtok_r(buf, ";", &save); tok; tok = strtok_r(NULL, ";", &save)) {
        while (*tok == ' ') tok++;
        if (!*tok) continue;
        if (o->nscript >= OPT_MAX_SCRIPT) return -1;
        ScriptStep *s = &o->script[o->nscript];
        char *colon = strchr(tok, ':');
        if (!colon) return -1;
        *colon = '\0';
        char *end;
        s->from = strtoull(tok, &end, 10);
        s->to = s->from;
        if (*end == '-') s->to = strtoull(end + 1, &end, 10);
        if (*end || s->to < s->from) return -1;
        s->buttons = 0;
        char *save2 = NULL;
        for (char *b = strtok_r(colon + 1, "+ ", &save2); b; b = strtok_r(NULL, "+ ", &save2)) {
            uint32_t bit = input_button_from_name(b);
            if (!bit) return -1;
            s->buttons |= bit;
        }
        o->nscript++;
    }
    return 0;
}

uint32_t options_script_buttons(const Options *o, uint64_t tick)
{
    uint32_t b = 0;
    for (int i = 0; i < o->nscript; i++)
        if (tick >= o->script[i].from && tick <= o->script[i].to) b |= o->script[i].buttons;
    return b;
}

int options_parse(Options *o, int argc, char **argv)
{
    memset(o, 0, sizeof *o);
    o->start_state = APP_ATTRACT;
    o->sprites = 1000;
    o->shader = 1;
    o->vsync = 1;
    input_defaults(&o->input);
    screen_config_defaults(&o->screen);
    fx_settings_defaults(&g_effects);

    const char *cfg = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) { fputs(g_usage, stdout); return 1; }
        if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) cfg = argv[i + 1];
    }
    find_config(o, cfg);
    if (o->config_path[0]) {
        CfgCtx c = { o };
        int rc = ini_parse(o->config_path, cfg_handler, &c);
        if (rc < 0) {
            fprintf(stderr, "CONFIG: cannot read %s\n", o->config_path);
            if (cfg) return -1;
            o->config_path[0] = '\0';
        }
    } else {
        fprintf(stderr, "CONFIG: no config.ini found, using built-in defaults\n");
    }

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;
#define NEED_VALUE() do { if (!v) { fprintf(stderr, "%s needs a value\n", a); return -1; } i++; } while (0)
        if (strcmp(a, "--config") == 0) { NEED_VALUE(); }
        else if (strcmp(a, "--perf-csv") == 0) { NEED_VALUE(); o->perf_csv = v; }
        else if (strcmp(a, "--frames") == 0) { NEED_VALUE(); o->frames = strtoull(v, NULL, 10); }
        else if (strcmp(a, "--shot") == 0) {
            NEED_VALUE();
            if (parse_shots(o, v) != 0) { fprintf(stderr, "bad --shot '%s'\n", v); return -1; }
        } else if (strcmp(a, "--script") == 0) {
            NEED_VALUE();
            if (parse_script(o, v) != 0) { fprintf(stderr, "bad --script '%s'\n", v); return -1; }
        } else if (strcmp(a, "--seed") == 0) {
            NEED_VALUE();
            char *end;
            o->seed = strtoull(v, &end, 16);
            if (*end || !*v) { fprintf(stderr, "bad --seed '%s' (hex)\n", v); return -1; }
            o->have_seed = 1;
        } else if (strcmp(a, "--mode") == 0) {
            NEED_VALUE();
            int s = app_state_from_name(v);
            if (s < 0) { fprintf(stderr, "unknown --mode '%s'\n", v); return -1; }
            o->start_state = s;
        } else if (strcmp(a, "--lockstep") == 0) o->lockstep = 1;
        else if (strcmp(a, "--gpu-finish") == 0) o->gpu_finish = 1;
        else if (strcmp(a, "--overlay") == 0) o->overlay = 1;
        else if (strcmp(a, "--fx") == 0) {
            NEED_VALUE();
            if (fx_settings_parse(&g_effects, v) != 0) { fprintf(stderr, "bad --fx '%s'\n", v); return -1; }
        }
        else if (strcmp(a, "--verbose") == 0) o->verbose = 1;
        else if (strcmp(a, "--no-vsync") == 0) o->vsync = 0;
        else if (strcmp(a, "--no-side-art") == 0) o->screen.side_art = 0;
        else if (strcmp(a, "--no-shader") == 0) o->shader = 0;
        else if (strcmp(a, "--sprites") == 0) {
            NEED_VALUE();
            o->sprites = atoi(v);
            if (o->sprites < 0 || o->sprites > GPUTEST_MAX_SPRITES) {
                fprintf(stderr, "--sprites must be 0..%d\n", GPUTEST_MAX_SPRITES);
                return -1;
            }
        } else if (strcmp(a, "--scale") == 0) {
            NEED_VALUE();
            if (screen_config_set(&o->screen, "scale", v) != 0) { fprintf(stderr, "bad --scale '%s'\n", v); return -1; }
        } else if (strcmp(a, "--filter") == 0) {
            NEED_VALUE();
            if (screen_config_set(&o->screen, "filter", v) != 0) { fprintf(stderr, "bad --filter '%s'\n", v); return -1; }
        } else if (strcmp(a, "--present") == 0) {
            NEED_VALUE();
            if (screen_config_set(&o->screen, "present", v) != 0) { fprintf(stderr, "bad --present '%s'\n", v); return -1; }
        } else if (strcmp(a, "--size") == 0) {
            NEED_VALUE();
            if (sscanf(v, "%dx%d", &o->width, &o->height) != 2 || o->width <= 0 || o->height <= 0) {
                fprintf(stderr, "bad --size '%s' (WxH)\n", v);
                return -1;
            }
        } else {
            fprintf(stderr, "unknown option '%s'\n%s", a, g_usage);
            return -1;
        }
#undef NEED_VALUE
    }
    return 0;
}
