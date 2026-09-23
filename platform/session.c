/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* session.c - see session.h. */
#include "platform/session.h"

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "ai/ai.h"
#include "audio.h"
#include "engine/rng.h"
#include "platform/save.h"

#define HAND_LOG_ROTATE_BYTES (8L * 1024 * 1024)

static struct {
    /* config.ini [game] */
    char cfg_save_dir[512], cfg_assets_dir[512];
    int  cfg_hand_log;
    int  cfg_set;

    AppCtx     *ctx;
    SaveData    data;              /* live settings + stats; credits at the last commit */
    SessionInfo info;
    SaveStore   store;
    int64_t     pending[PENDING_SLOTS];
    int64_t     committed_credits;
    int         dirty;
    FILE       *hand_log;
    uint32_t    idle;
    int         coin_armed;
    int         menu_game, hint_on;
    int         audio_started;
    AiPool     *pool;
    Rng         seeds;

    /* the writer thread */
    pthread_t       thr;
    int             thr_ok;
    pthread_mutex_t mu;
    pthread_cond_t  cv, done_cv;
    SaveData        queued;
    int             has_queued, busy, stop;
} S = { .cfg_hand_log = 1 };

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

/* ---- config --------------------------------------------------------------- */

static void expand_home(char *out, size_t cap, const char *v)
{
    const char *home = getenv("HOME");
    if (v[0] == '~' && (v[1] == '/' || v[1] == '\0') && home) snprintf(out, cap, "%s%s", home, v + 1);
    else snprintf(out, cap, "%s", v);
}

int session_config_set(const char *key, const char *value)
{
    if (strcasecmp(key, "save_dir") == 0) {
        expand_home(S.cfg_save_dir, sizeof S.cfg_save_dir, value);
        return 0;
    }
    if (strcasecmp(key, "assets_dir") == 0) {
        expand_home(S.cfg_assets_dir, sizeof S.cfg_assets_dir, value);
        return 0;
    }
    if (strcasecmp(key, "hand_log") == 0) {
        S.cfg_hand_log = strcasecmp(value, "off") != 0 && strcmp(value, "0") != 0;
        return 0;
    }
    return -1;
}

/* The default save directory: $XDG_DATA_HOME/beese-poker, else
 * ~/.local/share/beese-poker. On the cabinet that is the SD card's ext4 root
 * filesystem, where fsync and rename mean what they say - deliberately not
 * the ROM stick, which is vfat (no atomic rename-over, weak fsync). */
static void default_save_dir(char *out, size_t cap)
{
    const char *xdg = getenv("XDG_DATA_HOME");
    const char *home = getenv("HOME");
    if (xdg && xdg[0] == '/') snprintf(out, cap, "%s/beese-poker", xdg);
    else if (home && home[0]) snprintf(out, cap, "%s/.local/share/beese-poker", home);
    else snprintf(out, cap, "./beese-poker-save");
}

static int is_dir(const char *p)
{
    struct stat sb;
    return stat(p, &sb) == 0 && S_ISDIR(sb.st_mode);
}

/* assets/ next to the binary (the install), or up from the build tree. */
static void find_assets(char *out, size_t cap)
{
    if (S.cfg_assets_dir[0]) {
        snprintf(out, cap, "%s", S.cfg_assets_dir);
        return;
    }
    char exe[400];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n > 0) {
        exe[n] = '\0';
        char *slash = strrchr(exe, '/');
        if (slash) *slash = '\0';
        static const char *const rel[] = { "assets", "../assets", "../../assets", "../../../assets" };
        for (size_t i = 0; i < sizeof rel / sizeof rel[0]; i++) {
            char p[512];
            snprintf(p, sizeof p, "%s/%s", exe, rel[i]);
            char sfx[540];
            snprintf(sfx, sizeof sfx, "%s/sfx", p);
            if (is_dir(sfx)) {
                char *r = realpath(p, NULL);
                snprintf(out, cap, "%s", r ? r : p);
                free(r);
                return;
            }
        }
    }
    snprintf(out, cap, "assets");
}

/* ---- the writer thread ---------------------------------------------------- */

static void *writer_main(void *arg)
{
    (void)arg;
    pthread_mutex_lock(&S.mu);
    for (;;) {
        while (!S.has_queued && !S.stop) pthread_cond_wait(&S.cv, &S.mu);
        if (!S.has_queued && S.stop) break;
        SaveData d = S.queued;
        S.has_queued = 0;
        S.busy = 1;
        pthread_mutex_unlock(&S.mu);

        double t0 = now_ms();
        int rc = save_store_write(&S.store, &d);
        double ms = now_ms() - t0;

        pthread_mutex_lock(&S.mu);
        S.busy = 0;
        if (rc == 0) {
            S.info.saves++;
            S.info.save_last_ms = ms;
            S.info.save_total_ms += ms;
            if (ms > S.info.save_max_ms) S.info.save_max_ms = ms;
            S.info.ram_only = 0;
            S.info.save_error[0] = '\0';
        } else {
            S.info.save_failures++;
            S.info.ram_only = 1;
            snprintf(S.info.save_error, sizeof S.info.save_error, "%s", S.store.error);
            fprintf(stderr, "SAVE: failed: %s\n", S.store.error);
        }
        pthread_cond_broadcast(&S.done_cv);
    }
    pthread_mutex_unlock(&S.mu);
    return NULL;
}

static int64_t safe_credits(void)
{
    int64_t c = S.ctx ? S.ctx->wallet.credits : S.data.credits;
    for (int i = 0; i < PENDING_SLOTS; i++) c += S.pending[i];
    return c;
}

static void commit(void)
{
    S.data.credits = safe_credits();
    S.committed_credits = S.data.credits;
    S.dirty = 0;
    if (!S.thr_ok) return;           /* RAM only: the live copy is all there is */
    pthread_mutex_lock(&S.mu);
    S.queued = S.data;
    S.has_queued = 1;
    pthread_cond_signal(&S.cv);
    pthread_mutex_unlock(&S.mu);
}

void session_flush(void)
{
    if (!S.thr_ok) return;
    pthread_mutex_lock(&S.mu);
    while (S.has_queued || S.busy) pthread_cond_wait(&S.done_cv, &S.mu);
    pthread_mutex_unlock(&S.mu);
}

/* ---- audio ------------------------------------------------------------------ */

static void apply_volumes(void)
{
    const Settings *s = &S.data.settings;
    audio_set_volumes(s->vol_master / 10.0f, s->music_on ? s->vol_music / 10.0f : 0.0f, s->vol_sfx / 10.0f);
}

/* ---- life cycle ------------------------------------------------------------ */

static void open_hand_log(void)
{
    if (!S.cfg_hand_log || !S.store.writable) return;
    char path[600], old[610];
    snprintf(path, sizeof path, "%s/hands.log", S.info.save_dir);
    struct stat sb;
    if (stat(path, &sb) == 0 && sb.st_size > HAND_LOG_ROTATE_BYTES) {
        snprintf(old, sizeof old, "%s.1", path);
        rename(path, old);
    }
    S.hand_log = fopen(path, "a");
    S.info.hand_log_ok = S.hand_log != NULL;
}

void session_init(AppCtx *ctx)
{
    double t0 = now_ms();
    S.ctx = ctx;
    rng_seed(&S.seeds, ctx->session_seed ^ UINT64_C(0x5E551000B0B0CAFE));
    pthread_mutex_init(&S.mu, NULL);
    pthread_cond_init(&S.cv, NULL);
    pthread_cond_init(&S.done_cv, NULL);

    if (S.cfg_save_dir[0]) snprintf(S.info.save_dir, sizeof S.info.save_dir, "%s", S.cfg_save_dir);
    else default_save_dir(S.info.save_dir, sizeof S.info.save_dir);

    int ok = save_store_open(&S.store, S.info.save_dir) == 0;
    SaveLoadInfo li;
    save_store_load(&S.store, &S.data, &li);        /* defaults if there is none */
    S.info.loaded_from = li.source;
    S.info.corrupt_files = li.corrupt;
    if (!ok) {
        S.info.ram_only = 1;
        snprintf(S.info.save_error, sizeof S.info.save_error, "%s", S.store.error);
        fprintf(stderr, "SAVE: %s - running from RAM, nothing will be kept\n", S.store.error);
    } else {
        S.thr_ok = pthread_create(&S.thr, NULL, writer_main, NULL) == 0;
        if (!S.thr_ok) {
            S.info.ram_only = 1;
            snprintf(S.info.save_error, sizeof S.info.save_error, "cannot start the save thread");
        }
    }
    static const char *const src_name[] = { "none (fresh start)", "state.sav", "state.sav.bak", "state.sav.tmp" };
    fprintf(stderr, "SAVE: dir %s, loaded %s%s, %lld credits\n", S.info.save_dir, src_name[li.source],
            li.corrupt ? " (damaged copies rejected)" : "", (long long)S.data.credits);

    ctx->wallet.credits = S.data.credits;
    ctx->wallet.denom = S.data.settings.denom_cents;
    S.committed_credits = S.data.credits;
    S.hint_on = S.data.settings.hint_default;
    open_hand_log();

    find_assets(S.info.assets_dir, sizeof S.info.assets_dir);
    S.info.init_ms = now_ms() - t0;
    fprintf(stderr, "SESSION: init %.0f ms (assets %s; audio loads after the first frame)\n", S.info.init_ms,
            S.info.assets_dir);
}

/* Audio is loaded after the first frame is on screen, not before: reading
   ~7 MB of WAV from a cold SD card would otherwise come out of the 4 s
   cold-start budget, and nothing needs sound on the very first frame. */
static void start_audio(void)
{
    double ta = now_ms();
    S.audio_started = 1;
    S.info.audio_ok = audio_init(S.info.assets_dir);
    apply_volumes();
    S.info.audio_ms = now_ms() - ta;
    fprintf(stderr, "SESSION: audio %s in %.0f ms\n", S.info.audio_ok ? "loaded" : "unavailable (no device)",
            S.info.audio_ms);
}

void session_before_tick(AppCtx *ctx, const InputFrame *in)
{
    if (in->down || in->pressed || in->touch) S.idle = 0;
    else if (S.idle < UINT32_MAX) S.idle++;

    /* A coin is credited when COIN is released, unless START was pressed
       while it was held: COIN+START is the exit combo, not a coin. */
    if (ctx->state == APP_GPUTEST) return;
    if (in->pressed & BTN_COIN) S.coin_armed = 1;
    if ((in->down & BTN_COIN) && (in->down & BTN_START)) S.coin_armed = 0;
    if (S.coin_armed && !(in->down & BTN_COIN)) {
        S.coin_armed = 0;
        int64_t n = settings_coin_credits(&S.data.settings);
        wallet_credit(&ctx->wallet, n);
        S.data.stats.coins_in++;
        S.dirty = 1;
        ev_push(&ctx->events, APP_EV_COIN, 0, 0, (int)n);
    }
}

void session_after_tick(AppCtx *ctx)
{
    if (!S.audio_started && ctx->tick >= 1) start_audio();
    ctx->wallet.denom = S.data.settings.denom_cents;
    if (S.dirty || safe_credits() != S.committed_credits) commit();
}

void session_shutdown(AppCtx *ctx)
{
    (void)ctx;
    commit();
    if (S.thr_ok) {
        session_flush();
        pthread_mutex_lock(&S.mu);
        S.stop = 1;
        pthread_cond_signal(&S.cv);
        pthread_mutex_unlock(&S.mu);
        pthread_join(S.thr, NULL);
        S.thr_ok = 0;
    }
    fprintf(stderr, "SAVE: %u writes (%u failed), last %.1f ms, mean %.1f ms, max %.1f ms; %lld credits saved%s\n",
            S.info.saves, S.info.save_failures, S.info.save_last_ms,
            S.info.saves ? S.info.save_total_ms / S.info.saves : 0.0, S.info.save_max_ms,
            (long long)S.data.credits, S.info.ram_only ? " (RAM ONLY - not on disk)" : "");
    if (S.hand_log) fclose(S.hand_log);
    S.hand_log = NULL;
    if (S.pool) ai_pool_destroy(S.pool);
    S.pool = NULL;
    audio_shutdown();
    S.ctx = NULL;
}

/* ---- accessors ------------------------------------------------------------ */

Settings *session_settings(void) { return &S.data.settings; }
Stats *session_stats(void) { return &S.data.stats; }
const SessionInfo *session_info(void) { return &S.info; }
const EffectSettings *effects_get(void) { return &S.data.settings.effects; }
FILE *session_hand_log(void) { return S.hand_log; }
uint32_t session_idle_ticks(void) { return S.idle; }
int session_menu_game(void) { return S.menu_game; }
void session_set_menu_game(int g) { S.menu_game = (g >= 0 && g < MENU_NGAMES) ? g : 0; }
int session_hint_on(void) { return S.hint_on; }
void session_set_hint_on(int on) { S.hint_on = on ? 1 : 0; }
uint64_t session_next_seed(void) { return rng_next(&S.seeds); }

void session_settings_changed(void)
{
    apply_volumes();
    if (S.ctx) S.ctx->wallet.denom = S.data.settings.denom_cents;
    S.dirty = 1;
}

void session_stats_changed(void) { S.dirty = 1; }

void session_set_pending(int slot, int64_t credits)
{
    if (slot >= 0 && slot < PENDING_SLOTS) S.pending[slot] = credits;
}

void session_add_credits(int64_t n)
{
    if (!S.ctx || n <= 0) return;
    wallet_credit(&S.ctx->wallet, n);
    S.data.stats.credits_added += (uint64_t)n;
    S.dirty = 1;
}

void session_reset_credits(void)
{
    if (!S.ctx) return;
    S.ctx->wallet.credits = 0;
    S.data.stats.credit_resets++;
    S.dirty = 1;
}

void session_reset_stats(void)
{
    memset(&S.data.stats, 0, sizeof S.data.stats);
    S.dirty = 1;
}

AiPool *session_ai_pool(void)
{
    if (!S.pool) S.pool = ai_pool_create(2);
    return S.pool;
}
