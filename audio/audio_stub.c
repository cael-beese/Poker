/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* audio_stub.c - the same API with no sound, for trees configured without
 * raylib (headless CI). CMakeLists.txt builds this instead of audio.c when
 * raylib cannot be found, so everything that calls audio_* still links. */
#include "audio.h"

#include <string.h>

static const char *const NAMES[SFX_COUNT] = {
    "card_deal", "card_flip", "card_slide", "card_shuffle", "fold", "check",
    "button", "hold_on", "hold_off", "menu_move", "menu_select", "service_beep",
    "error", "neon_flicker", "bet_one", "bet_max", "credit_tick", "credit_end",
    "coin_insert", "cash_out", "chip_single", "chip_stack", "chip_pot",
    "win_small", "win_medium", "win_big", "win_jackpot", "double_win",
    "double_lose", "all_in", "reveal", "your_turn", "blinds_up", "bust", "whoosh",
};

bool audio_init(const char *assets_dir) { (void)assets_dir; return false; }
void audio_shutdown(void) {}
void audio_update(float dt) { (void)dt; }
void audio_play(SfxId id, float vol, float pitch, float pan) { (void)id; (void)vol; (void)pitch; (void)pan; }
void audio_stop(SfxId id) { (void)id; }
void audio_stop_all(void) {}
bool audio_is_playing(SfxId id) { (void)id; return false; }
void audio_music(MusicId id, bool play) { (void)id; (void)play; }
void audio_ambience(bool on) { (void)on; }
void audio_duck(float gain, float seconds) { (void)gain; (void)seconds; }
void audio_set_volumes(float master, float music, float sfx) { (void)master; (void)music; (void)sfx; }
void audio_set_ambience_level(float level) { (void)level; }
float audio_sfx_duration(SfxId id) { (void)id; return 0.0f; }
const char *audio_sfx_name(SfxId id) { return (unsigned)id < SFX_COUNT ? NAMES[id] : "?"; }
void audio_get_stats(AudioStats *out) { if (out) { memset(out, 0, sizeof *out); out->duck = 1.0f; } }
