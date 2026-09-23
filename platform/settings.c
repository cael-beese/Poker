/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* settings.c - see settings.h. */
#include "platform/settings.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- effects (effects.h) ------------------------------------------------- */

static const char *const k_fx_name[FX_COUNT] = {
#define BPL_FX_NAME(name, label, def) #name,
    BPL_EFFECTS(BPL_FX_NAME)
#undef BPL_FX_NAME
};
static const char *const k_fx_label[FX_COUNT] = {
#define BPL_FX_LABEL(name, label, def) label,
    BPL_EFFECTS(BPL_FX_LABEL)
#undef BPL_FX_LABEL
};
static const uint8_t k_fx_def[FX_COUNT] = {
#define BPL_FX_DEF(name, label, def) def,
    BPL_EFFECTS(BPL_FX_DEF)
#undef BPL_FX_DEF
};
static const size_t k_fx_off[FX_COUNT] = {
#define BPL_FX_OFF(name, label, def) offsetof(EffectSettings, name),
    BPL_EFFECTS(BPL_FX_OFF)
#undef BPL_FX_OFF
};

const char *effects_name(int fx) { return fx >= 0 && fx < FX_COUNT ? k_fx_name[fx] : "?"; }
const char *effects_label(int fx) { return fx >= 0 && fx < FX_COUNT ? k_fx_label[fx] : "?"; }
int effects_default(int fx) { return fx >= 0 && fx < FX_COUNT ? k_fx_def[fx] : 0; }

uint8_t *effects_field(EffectSettings *e, int fx)
{
    return (fx >= 0 && fx < FX_COUNT) ? (uint8_t *)e + k_fx_off[fx] : NULL;
}

void effects_defaults(EffectSettings *e)
{
    for (int i = 0; i < FX_COUNT; i++) *effects_field(e, i) = k_fx_def[i];
}

/* ---- value tables ---------------------------------------------------------- */

const uint16_t settings_denoms[] = { 1, 5, 10, 25, 50, 100 };
const int settings_ndenoms = (int)(sizeof settings_denoms / sizeof settings_denoms[0]);
const uint16_t settings_coins[] = { 25, 100, 200, 500, 1000, 2000 };
const int settings_ncoins = (int)(sizeof settings_coins / sizeof settings_coins[0]);
const uint32_t settings_buyins[] = { 20, 50, 100, 200, 500, 1000 };
const int settings_nbuyins = (int)(sizeof settings_buyins / sizeof settings_buyins[0]);

void settings_defaults(Settings *s)
{
    memset(s, 0, sizeof *s);
    s->vol_master = 10;
    s->vol_music = 6;
    s->vol_sfx = 10;
    s->music_on = 1;
    s->difficulty = 1;          /* AI_DIFF_NORMAL */
    s->hint_default = 0;
    s->double_up = 1;
    s->denom_cents = 25;
    s->coin_cents = 100;        /* one press = $1 = 4 credits at 25c */
    s->holdem_buyin = 100;
    effects_defaults(&s->effects);
}

void save_data_defaults(SaveData *d)
{
    memset(d, 0, sizeof *d);
    d->credits = 1000;          /* a fresh cabinet starts with a bar float */
    settings_defaults(&d->settings);
}

int64_t settings_coin_credits(const Settings *s)
{
    int64_t n = s->denom_cents ? (int64_t)s->coin_cents / s->denom_cents : 1;
    return n < 1 ? 1 : n;
}

int64_t sng_prize(int64_t buyin, int place)
{
    switch (place) {
    case 1: return buyin * 3;
    case 2: return buyin * 9 / 5;
    case 3: return buyin * 6 / 5;
    default: return 0;
    }
}

/* ---- the key table ------------------------------------------------------- */

enum { T_I64, T_U64, T_U32, T_U16, T_U8 };

typedef struct {
    const char *key;
    uint8_t     type;
    size_t      off;
    int64_t     lo, hi;         /* clamp on read (lo > hi: no clamp) */
} Field;

#define F(key, type, member, lo, hi) { key, type, offsetof(SaveData, member), lo, hi }
#define FN(key, type, member) F(key, type, member, 1, 0)   /* no clamp */

static const Field k_fields[] = {
    F("credits",               T_I64, credits, 0, INT64_C(1000000000000)),
    F("set.vol_master",        T_U8,  settings.vol_master, 0, 10),
    F("set.vol_music",         T_U8,  settings.vol_music, 0, 10),
    F("set.vol_sfx",           T_U8,  settings.vol_sfx, 0, 10),
    F("set.music_on",          T_U8,  settings.music_on, 0, 1),
    F("set.difficulty",        T_U8,  settings.difficulty, 0, 3),
    F("set.hint_default",      T_U8,  settings.hint_default, 0, 1),
    F("set.double_up",         T_U8,  settings.double_up, 0, 1),
    F("set.denom_cents",       T_U16, settings.denom_cents, 1, 10000),
    F("set.coin_cents",        T_U16, settings.coin_cents, 1, 60000),
    F("set.holdem_buyin",      T_U32, settings.holdem_buyin, 5, 1000000),
    FN("stats.draw_hands.job", T_U64, stats.draw_hands[0]),
    FN("stats.draw_hands.bonus", T_U64, stats.draw_hands[1]),
    FN("stats.draw_hands.deuces", T_U64, stats.draw_hands[2]),
    FN("stats.draw_bet.job", T_U64, stats.draw_bet[0]),
    FN("stats.draw_bet.bonus", T_U64, stats.draw_bet[1]),
    FN("stats.draw_bet.deuces", T_U64, stats.draw_bet[2]),
    FN("stats.draw_paid.job", T_U64, stats.draw_paid[0]),
    FN("stats.draw_paid.bonus", T_U64, stats.draw_paid[1]),
    FN("stats.draw_paid.deuces", T_U64, stats.draw_paid[2]),
    FN("stats.royals", T_U64, stats.royals),
    FN("stats.four_deuces", T_U64, stats.four_deuces),
    FN("stats.dbl_played", T_U64, stats.dbl_played),
    FN("stats.dbl_won", T_U64, stats.dbl_won),
    F("stats.biggest_win",       T_I64, stats.biggest_win, 0, INT64_C(1000000000000)),
    FN("stats.holdem_hands", T_U64, stats.holdem_hands),
    FN("stats.holdem_games", T_U64, stats.holdem_games),
    FN("stats.holdem_wins", T_U64, stats.holdem_wins),
    FN("stats.holdem_place1", T_U64, stats.holdem_places[0]),
    FN("stats.holdem_place2", T_U64, stats.holdem_places[1]),
    FN("stats.holdem_place3", T_U64, stats.holdem_places[2]),
    FN("stats.holdem_place4", T_U64, stats.holdem_places[3]),
    FN("stats.holdem_place5", T_U64, stats.holdem_places[4]),
    FN("stats.holdem_place6", T_U64, stats.holdem_places[5]),
    FN("stats.holdem_buyins", T_U64, stats.holdem_buyins),
    FN("stats.holdem_prizes", T_U64, stats.holdem_prizes),
    FN("stats.coins_in", T_U64, stats.coins_in),
    FN("stats.credits_added", T_U64, stats.credits_added),
    FN("stats.credit_resets", T_U64, stats.credit_resets),
};
#define NFIELDS ((int)(sizeof k_fields / sizeof k_fields[0]))

static int64_t get_field(const SaveData *d, const Field *f)
{
    const uint8_t *p = (const uint8_t *)d + f->off;
    int64_t v = 0;
    switch (f->type) {
    case T_I64: { int64_t x; memcpy(&x, p, sizeof x); v = x; break; }
    case T_U64: { uint64_t x; memcpy(&x, p, sizeof x); v = (int64_t)x; break; }
    case T_U32: { uint32_t x; memcpy(&x, p, sizeof x); v = x; break; }
    case T_U16: { uint16_t x; memcpy(&x, p, sizeof x); v = x; break; }
    default:    v = *p; break;
    }
    return v;
}

static void set_field(SaveData *d, const Field *f, int64_t v)
{
    uint8_t *p = (uint8_t *)d + f->off;
    if (f->lo <= f->hi) {
        if (v < f->lo) v = f->lo;
        if (v > f->hi) v = f->hi;
    }
    switch (f->type) {
    case T_I64: { int64_t x = v; memcpy(p, &x, sizeof x); break; }
    case T_U64: { uint64_t x = (uint64_t)v; memcpy(p, &x, sizeof x); break; }
    case T_U32: { uint32_t x = (uint32_t)v; memcpy(p, &x, sizeof x); break; }
    case T_U16: { uint16_t x = (uint16_t)v; memcpy(p, &x, sizeof x); break; }
    default:    *p = (uint8_t)v; break;
    }
}

/* One "key=value" line into out[*n..cap); *n grows by what it needed even when
   it did not fit, like snprintf, so the caller can tell. */
static int put_line(char *out, size_t cap, size_t *n, const char *key, const char *prefix, const char *val)
{
    char line[128];
    int w = snprintf(line, sizeof line, "%s%s=%s\n", prefix, key, val);
    if (w < 0 || (size_t)w >= sizeof line) return -1;
    if (*n + (size_t)w < cap) memcpy(out + *n, line, (size_t)w + 1);
    *n += (size_t)w;
    return 0;
}

int save_data_format(const SaveData *d, char *out, size_t cap)
{
    size_t n = 0;
    char v[32];
    if (!out || !cap) return -1;
    out[0] = '\0';
    for (int i = 0; i < NFIELDS; i++) {
        const Field *f = &k_fields[i];
        if (f->type == T_U64) snprintf(v, sizeof v, "%" PRIu64, (uint64_t)get_field(d, f));
        else snprintf(v, sizeof v, "%" PRId64, get_field(d, f));
        if (put_line(out, cap, &n, f->key, "", v) != 0) return -1;
    }
    for (int i = 0; i < FX_COUNT; i++) {
        snprintf(v, sizeof v, "%d", *effects_field((EffectSettings *)&d->settings.effects, i) ? 1 : 0);
        if (put_line(out, cap, &n, effects_name(i), "fx.", v) != 0) return -1;
    }
    return (int)n;
}

static int parse_line(SaveData *d, const char *key, const char *val)
{
    char *end;
    errno = 0;
    if (strncmp(key, "fx.", 3) == 0) {
        for (int i = 0; i < FX_COUNT; i++) {
            if (strcmp(key + 3, effects_name(i)) == 0) {
                long v = strtol(val, &end, 10);
                if (*end || end == val) return -1;
                *effects_field(&d->settings.effects, i) = v ? 1 : 0;
                return 0;
            }
        }
        return 1;       /* an effect this build does not know: ignore */
    }
    for (int i = 0; i < NFIELDS; i++) {
        const Field *f = &k_fields[i];
        if (strcmp(key, f->key) != 0) continue;
        int64_t v;
        if (f->type == T_U64) v = (int64_t)strtoull(val, &end, 10);
        else v = strtoll(val, &end, 10);
        if (*end || end == val || errno) return -1;
        set_field(d, f, v);
        return 0;
    }
    return 1;           /* unknown key: a newer build wrote it */
}

int save_data_parse(SaveData *d, const char *payload, size_t len)
{
    int bad = 0;
    size_t i = 0;
    while (i < len) {
        size_t j = i;
        while (j < len && payload[j] != '\n') j++;
        char line[256];
        size_t n = j - i;
        if (n > 0 && n < sizeof line) {
            memcpy(line, payload + i, n);
            line[n] = '\0';
            char *eq = strchr(line, '=');
            if (!eq) bad++;
            else {
                *eq = '\0';
                if (parse_line(d, line, eq + 1) < 0) bad++;
            }
        } else if (n >= sizeof line) {
            bad++;
        }
        i = j + 1;
    }
    return bad;
}
