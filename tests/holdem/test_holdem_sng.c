/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* Whole sit-and-goes. The blind schedule and eliminations with call bots;
   then thousands of games played to the end by random-legal bots, by
   "chaos" bots that also send illegal decisions, and by a button-mashing
   human, with every tick checked: chip conservation and the rest of
   holdem_check_invariants, every action legal in the state before it,
   every hand's cards reproduced from its logged seed, one winner holding
   every chip, places 1..6 each given once, and a replay of the same seed
   giving the same events and the same final state.

   Usage: test_holdem_sng [games-per-kind]   (default 2000) */

#include "holdem_harness.h"

#include "engine/replay.h"

#include <stdlib.h>

typedef struct {
    long games, hands, ticks, inv_checks, action_checks, replays_checked, hand_seeds_checked;
    long human_actions;
} Totals;

static Totals T;

static uint64_t fnv(uint64_t h, int64_t x)
{
    int i;
    for (i = 0; i < 8; i++) { h ^= (uint8_t)(x >> (8 * i)); h *= 0x100000001b3ull; }
    return h;
}

/* The hand just ended: its cards must be exactly what the logged seed
   deals, and the log line must carry that seed. */
static void check_hand_seed(const HoldemGame *g)
{
    Rng r;
    Deck d;
    int i, s, n = g->ndeal;
    char line[400], mode[REPLAY_MODE_MAX + 1], result[300];
    int64_t tm;
    uint64_t seed;

    rng_seed(&r, g->log_seed);
    deck_init(&d);
    deck_shuffle(&d, &r);
    for (i = 0; i < 2 * n; i++) {
        s = g->deal_order[i % n];
        CHECK_EQ_INT(deck_draw(&d), g->seat[s].hole[i / n]);
    }
    for (i = 0; i < g->nboard; i++) {
        if (i == 0 || i == 3 || i == 4) (void)deck_draw(&d);
        CHECK_EQ_INT(deck_draw(&d), g->board[i]);
    }
    CHECK(holdem_hand_log_line(g, 1790000000, line, sizeof line) > 0);
    CHECK(replay_parse_hand(line, &tm, mode, &seed, result, sizeof result) == 0);
    CHECK(seed == g->log_seed && strcmp(mode, "holdem") == 0 && tm == 1790000000);
    CHECK(strncmp(result, "hand=", 5) == 0);
    T.hand_seeds_checked++;
}

typedef struct {
    uint64_t hash;
    int      hands, winner, gameovers;
    long     ticks;
} GameResult;

static HoldemBots g_bots;
static HoldemGame g_game, g_first;

/* human: 0 = seat 0 is a bot too; 1 = seat 0 is a human mashing buttons
   (and watching on after busting); 2 = the same but leaving after busting. */
static void play(uint64_t seed, int kind, int human, const HoldemConfig *base, GameResult *res)
{
    HoldemConfig c = *base;
    HoldemAiHooks h;
    HoldemGame *g = &g_game;
    Rng mash;
    int16_t slider = 0;
    long k;
    char why[160];

    c.seat0_ai = human == 0;
    holdem_bots_init(&g_bots, kind);
    h = holdem_bots_hooks(&g_bots);
    holdem_init(g, &c, seed, &h);
    rng_seed(&mash, seed ^ 0xabcdefull);
    memset(res, 0, sizeof *res);
    res->hash = 0xcbf29ce484222325ull;

    for (k = 0; k < 20000000 && g->phase != HP_GAME_OVER; k++) {
        EventQueue q;
        InputFrame in;
        TurnRec before;
        int i, had_turn = g->phase == HP_TURN;

        memset(&in, 0, sizeof in);
        if (human) {
            /* A few presses of random buttons now and then, and a slider
               that sometimes moves: everything the cabinet could send. */
            uint32_t roll = rng_below(&mash, 100);
            if (roll < 6) in.pressed = 1u << rng_below(&mash, BTN_COUNT);
            if (roll < 2) in.pressed |= 1u << rng_below(&mash, BTN_COUNT);
            if (g->phase == HP_BUSTED && roll < 20) in.pressed |= human == 1 ? BTN_DEAL : BTN_BACK;
            in.pressed &= ~(uint32_t)(BTN_DEBUG | BTN_SERVICE);
            if (rng_below(&mash, 200) == 0) slider = (int16_t)((int)rng_below(&mash, 65535) - 32767);
            in.slider = slider;
            in.down = in.pressed;
        }
        memset(&before, 0, sizeof before);
        if (had_turn) {
            holdem_legal(g, &before.legal);
            before.seat = g->to_act;
            before.stack = g->seat[before.seat].stack;
            before.bet = g->seat[before.seat].bet;
        }
        q.n = 0;
        holdem_tick(g, &in, NULL, &q);
        T.ticks++;
        T.inv_checks++;
        if (holdem_check_invariants(g, why, sizeof why) != 0) {
            g_test_failures++;
            if (g_test_failures < 10) fprintf(stderr, "seed %llu tick %ld: %s\n", (unsigned long long)seed, k, why);
            break;
        }
        for (i = 0; i < q.n; i++) {
            const GameEvent *e = &q.e[i];
            res->hash = fnv(fnv(fnv(fnv(res->hash, e->type), e->a), e->b), e->v);
            switch (e->type) {
            case EV_HOLDEM_ACTION:
                T.action_checks++;
                if (e->a == 0 && human) T.human_actions++;
                if (!had_turn || e->a != before.seat || !th_action_legal(&before, e->b, e->v)) {
                    g_test_failures++;
                    if (g_test_failures < 10)
                        fprintf(stderr, "seed %llu: illegal action seat %d code %d to %d\n",
                                (unsigned long long)seed, e->a, e->b, e->v);
                }
                break;
            case EV_HOLDEM_HAND_LOG:
                res->hands++;
                check_hand_seed(g);
                break;
            case EV_HOLDEM_GAME_OVER:
                res->gameovers++;
                res->winner = e->a;
                break;
            case EV_HOLDEM_POST_SB:
            case EV_HOLDEM_POST_BB: {
                HoldemLevel lv = holdem_level(g);
                int64_t want = e->type == EV_HOLDEM_POST_SB ? lv.sb : lv.bb;
                /* A blind is the full amount unless it put the seat all-in. */
                CHECK(e->v == want || (e->b == 1 && e->v < want));
                break;
            }
            case EV_HOLDEM_ANTE:
                CHECK(e->v == holdem_level(g).ante || (e->b == 1 && e->v < holdem_level(g).ante));
                break;
            default: break;
            }
        }
    }
    res->ticks = k;
    CHECK(g->phase == HP_GAME_OVER);
    CHECK_EQ_INT(res->gameovers, 1);
    CHECK_EQ_INT(g_bots.protocol_errors, 0);
    CHECK_EQ_INT(g_bots.begins, g_bots.collects);
    if (g->over_reason == HOLDEM_OVER_WINNER) {
        int s, places = 0;
        CHECK(res->winner >= 0 && res->winner < 6);
        CHECK_EQ_INT(g->seat[res->winner].stack, g->chips_total);
        CHECK_EQ_INT(g->seat[res->winner].place, 1);
        CHECK_EQ_INT(g->players_left, 1);
        for (s = 0; s < 6; s++) {
            if (s != res->winner) CHECK_EQ_INT(g->seat[s].stack, 0);
            places |= 1 << g->seat[s].place;
        }
        CHECK_EQ_INT(places, 0x7e);                   /* places 1..6, once each */
    } else {
        CHECK(human == 2);
        CHECK_EQ_INT(res->winner, -1);
        CHECK(g->seat[0].place >= 2);
    }
    T.games++;
    T.hands += res->hands;
}

static void blind_schedule(void)
{
    HoldemConfig c;
    int lvl_seen[HOLDEM_MAX_LEVELS] = { 0 };
    HoldemGame *g = &g_game;
    HoldemAiHooks h;
    long k;

    holdem_config_fast(&c);
    c.hands_per_level = 3;
    c.seat0_ai = 1;
    holdem_bots_init(&g_bots, HOLDEM_BOT_CALL);
    h = holdem_bots_hooks(&g_bots);
    holdem_init(g, &c, 5, &h);
    for (k = 0; k < 5000000 && g->phase != HP_GAME_OVER; k++) {
        EventQueue q;
        int i;
        q.n = 0;
        holdem_tick(g, NULL, NULL, &q);
        CHECK(holdem_check_invariants(g, NULL, 0) == 0);
        for (i = 0; i < q.n; i++) {
            const GameEvent *e = &q.e[i];
            if (e->type == EV_HOLDEM_HAND_START) {
                int want = (e->a - 1) / 3;
                if (want >= c.nlevels) want = c.nlevels - 1;
                CHECK_EQ_INT(e->v, want);
                lvl_seen[e->v]++;
            }
            if (e->type == EV_HOLDEM_LEVEL_UP) {
                CHECK_EQ_INT(g->hand_no % 3, 1);
                CHECK_EQ_INT(e->v, c.levels[e->a].bb);
            }
            if (e->type == EV_HOLDEM_ANTE) CHECK(c.levels[g->level].ante > 0);
            if (e->type == EV_HOLDEM_ELIMINATED) CHECK(e->b >= 2 && e->b <= 6);
        }
    }
    CHECK(g->phase == HP_GAME_OVER);
    CHECK(lvl_seen[0] == 3 && lvl_seen[1] == 3);
    CHECK(lvl_seen[5] > 0);                             /* reached the ante levels */


    /* Antes off: never an ANTE event, same schedule otherwise. */
    c.antes = 0;
    holdem_init(g, &c, 5, &h);
    for (k = 0; k < 5000000 && g->phase != HP_GAME_OVER; k++) {
        EventQueue q;
        int i;
        q.n = 0;
        holdem_tick(g, NULL, NULL, &q);
        for (i = 0; i < q.n; i++) CHECK(q.e[i].type != EV_HOLDEM_ANTE);
    }
    CHECK(g->phase == HP_GAME_OVER);
}

static void many_games(int per_kind)
{
    HoldemConfig c;
    GameResult r, r2;
    int i, mode;
    double t0 = test_now();

    holdem_config_fast(&c);
    for (mode = 0; mode < 4; mode++) {
        int kind = mode == 1 ? HOLDEM_BOT_CHAOS : HOLDEM_BOT_RANDOM;
        int human = mode == 2 ? 1 : mode == 3 ? 2 : 0;
        int n = mode >= 2 ? per_kind / 4 : per_kind;
        if (n < 1) n = 1;
        for (i = 0; i < n; i++) {
            uint64_t seed = 0x5eed0000ull + (uint64_t)mode * 1000003ull + (uint64_t)i;
            /* Vary the table: button, blind speed, starting stacks. */
            c.first_button = i % 7 == 6 ? -1 : i % 6;
            c.hands_per_level = 1 + i % 12;
            c.start_stack = 200 + (i % 9) * 350;
            c.antes = i % 3 != 0;
            play(seed, kind, human, &c, &r);
            if (i % 10 == 0) {
                /* Same seed, same inputs: same events, same final state. */
                g_first = g_game;
                play(seed, kind, human, &c, &r2);
                CHECK(r.hash == r2.hash);
                CHECK_EQ_INT(r.ticks, r2.ticks);
                CHECK(memcmp(&g_first, &g_game, sizeof g_game) == 0);
                T.replays_checked++;
                T.games--;
                T.hands -= r2.hands;
            }
        }
    }
    printf("holdem_sng: %ld games, %ld hands, %ld ticks, %ld invariant checks, %ld actions checked "
           "(%ld by the human), %ld hand seeds replayed, %ld games replayed, %.1f s\n",
           T.games, T.hands, T.ticks, T.inv_checks, T.action_checks, T.human_actions,
           T.hand_seeds_checked, T.replays_checked, test_now() - t0);
}

int main(int argc, char **argv)
{
    int per_kind = argc > 1 ? atoi(argv[1]) : 2000;
    eval_init();
    blind_schedule();
    many_games(per_kind);
    return test_finish("holdem_sng");
}
