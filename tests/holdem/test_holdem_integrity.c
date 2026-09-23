/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* The AI can never see hidden cards.
   1. Include direction: nothing under ai/ includes a games/holdem header,
      and games/holdem includes nothing from ai/ except ai/ai_view.h.
   2. AiView has room for one pair of hole cards and nothing else hidden.
   3. At every AI turn of many random games, the view the AI received is
      byte-identical to a view built from a copy of the game in which every
      other seat's hole cards and the whole undealt deck are replaced by
      sentinels - so nothing hidden can have leaked into it - and it holds
      exactly the seat's own two cards and the dealt board. */

#include "holdem_harness.h"

#include <dirent.h>
#include <stddef.h>
#include <sys/stat.h>

#ifndef BPL_SOURCE_DIR
#error "BPL_SOURCE_DIR must point at the source tree"
#endif

_Static_assert(sizeof(((AiView *)0)->hole) == 2 * sizeof(Card), "AiView holds exactly two hole cards");
_Static_assert(sizeof(((AiView *)0)->board) == 5 * sizeof(Card), "AiView holds the five board cards");

static int files_scanned;

/* Calls fn on every line starting with #include in C sources under dir. */
static void scan_dir(const char *dir, void (*fn)(const char *path, const char *line))
{
    DIR *d = opendir(dir);
    struct dirent *de;
    if (!d) return;                              /* a module not in this tree yet */
    while ((de = readdir(d)) != NULL) {
        char path[1024];
        struct stat st;
        size_t n;
        if (de->d_name[0] == '.') continue;
        snprintf(path, sizeof path, "%s/%s", dir, de->d_name);
        if (stat(path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) { scan_dir(path, fn); continue; }
        n = strlen(de->d_name);
        if (n < 3 || (strcmp(de->d_name + n - 2, ".c") != 0 && strcmp(de->d_name + n - 2, ".h") != 0)) continue;
        {
            FILE *f = fopen(path, "r");
            char line[1024];
            if (!f) { CHECK(!"cannot read a source file"); continue; }
            files_scanned++;
            while (fgets(line, sizeof line, f)) {
                const char *p = line;
                while (*p == ' ' || *p == '\t') p++;
                if (*p != '#') continue;
                p++;
                while (*p == ' ' || *p == '\t') p++;
                if (strncmp(p, "include", 7) == 0) fn(path, p + 7);
            }
            fclose(f);
        }
    }
    closedir(d);
}

static void ai_side(const char *path, const char *inc)
{
    if (strstr(inc, "games/") || strstr(inc, "holdem")) {
        fprintf(stderr, "FAIL %s includes %s", path, inc);
        g_test_failures++;
    }
}

static void holdem_side(const char *path, const char *inc)
{
    const char *p = strstr(inc, "ai/");
    if (p && strncmp(p, "ai/ai_view.h", 12) != 0) {
        fprintf(stderr, "FAIL %s includes %s", path, inc);
        g_test_failures++;
    }
}

static void include_direction(void)
{
    scan_dir(BPL_SOURCE_DIR "/ai", ai_side);
    scan_dir(BPL_SOURCE_DIR "/games/holdem", holdem_side);
    CHECK(files_scanned >= 5);                   /* ai_view.h and the holdem sources */
}

/* ---- the view test ---- */

typedef struct {
    HoldemBots  bots;
    HoldemGame *game;
    long        views, board_cards_seen;
    HoldemGame  copy;
} Spy;

static Spy spy;
static long total_views, total_board;

static void spy_begin(void *ctx, int seat, const AiView *v, uint64_t seed)
{
    Spy *s = ctx;
    const HoldemGame *g = s->game;
    AiView blind;
    int t, i, k;
    static const Card sentinels[] = { 0, 51, 13, 26 };

    s->views++;
    CHECK_EQ_INT(v->me, seat);
    CHECK_EQ_INT(v->hole[0], g->seat[seat].hole[0]);
    CHECK_EQ_INT(v->hole[1], g->seat[seat].hole[1]);
    CHECK_EQ_INT(v->nboard, g->nboard);
    for (i = 0; i < 5; i++) CHECK_EQ_INT(v->board[i], i < g->nboard ? g->board[i] : CARD_NONE);
    s->board_cards_seen += v->nboard;
    for (t = 0; t < HOLDEM_SEATS; t++) {
        if (t == seat || !g->seat[t].in_hand) continue;
        for (k = 0; k < 2; k++) {
            Card c = g->seat[t].hole[k];
            CHECK(v->hole[0] != c && v->hole[1] != c);
            for (i = 0; i < 5; i++) CHECK(v->board[i] != c);
        }
    }
    /* Rebuild the view with everything hidden replaced by sentinels. */
    for (k = 0; k < 4; k++) {
        s->copy = *g;
        for (t = 0; t < HOLDEM_SEATS; t++)
            if (t != seat) s->copy.seat[t].hole[0] = s->copy.seat[t].hole[1] = sentinels[k];
        for (i = s->copy.deck.pos; i < 52; i++) s->copy.deck.c[i] = sentinels[(k + i) & 3];
        s->copy.hand_seed = ~s->copy.hand_seed;
        holdem_build_view(&s->copy, seat, &blind);
        CHECK(memcmp(&blind, v, sizeof blind) == 0);
    }
    /* The rest of the view is what the table publicly shows. */
    {
        HoldemLegal L;
        holdem_legal(g, &L);
        CHECK_EQ_INT(L.seat, seat);
        CHECK_EQ_INT(v->to_call, L.to_call);
        CHECK_EQ_INT(v->min_raise, (L.can_bet || L.can_raise) ? L.min_to : 0);
        CHECK_EQ_INT(v->pot, holdem_pot_total(g));
        CHECK_EQ_INT(v->nhist, g->nhist);
    }
    s->bots.kind[seat] = HOLDEM_BOT_RANDOM;
    holdem_bots_hooks(&s->bots).begin(&s->bots, seat, v, seed);
}

static void spy_collect(void *ctx, int seat, AiDecision *out)
{
    Spy *s = ctx;
    holdem_bots_hooks(&s->bots).collect(&s->bots, seat, out);
}

static void views_hide_everything(void)
{
    static HoldemGame g;
    HoldemConfig c;
    HoldemAiHooks h;
    int game;

    holdem_config_fast(&c);
    c.seat0_ai = 1;
    h.ctx = &spy;
    h.begin = spy_begin;
    h.collect = spy_collect;
    for (game = 0; game < 40; game++) {
        long k;
        memset(&spy, 0, sizeof spy);
        holdem_bots_init(&spy.bots, HOLDEM_BOT_RANDOM);
        spy.game = &g;
        c.start_stack = 300 + game * 50;
        holdem_init(&g, &c, 1000 + (uint64_t)game, &h);
        for (k = 0; k < 5000000 && g.phase != HP_GAME_OVER; k++) holdem_tick(&g, NULL, NULL, NULL);
        CHECK(g.phase == HP_GAME_OVER);
        CHECK(spy.views > 0);
        total_board += spy.board_cards_seen;
        CHECK_EQ_INT(spy.bots.protocol_errors, 0);
        total_views += spy.views;
    }
    CHECK(total_board > 1000);                   /* plenty of post-flop decisions */
}

static void history_codes(void)
{
    /* The history is the public action sequence in AI_HIST form, blinds
       included, with the street each happened on. */
    static TH t;
    static const Step sc[] = {
        { 3, ACT_RAISE, 60, 0 }, { 4, ACT_FOLD, 0, 0 }, { 5, ACT_FOLD, 0, 0 },
        { 0, ACT_FOLD, 0, 0 },   { 1, ACT_FOLD, 0, 0 }, { 2, ACT_CALL, 0, 0 },
        { 2, ACT_BET, 20, 0 },
    };
    const AiView *v;
    HoldemConfig c;
    th_config(&c, 0, 10, 20);
    th_init(&t, &c, 3);
    th_script(&t, sc, 7);
    th_play_hand(&t, NULL, NULL);
    /* views[7] is seat 3 facing the flop bet. */
    CHECK(t.sc.nviews >= 8);
    v = &t.sc.views[7];
    CHECK_EQ_INT(v->me, 3);
    CHECK_EQ_INT(v->nhist, 9);
    CHECK_EQ_INT(v->history[0], AI_HIST(0, 1, ACT_POST_SB));
    CHECK_EQ_INT(v->history[1], AI_HIST(0, 2, ACT_POST_BB));
    CHECK_EQ_INT(v->history[2], AI_HIST(0, 3, ACT_RAISE));
    CHECK_EQ_INT(v->history[7], AI_HIST(0, 2, ACT_CALL));
    CHECK_EQ_INT(v->history[8], AI_HIST(1, 2, ACT_BET));
    CHECK_EQ_INT(v->street, 1);
    CHECK_EQ_INT(v->to_call, 20);
    CHECK_EQ_INT(v->min_raise, 40);
    CHECK_EQ_INT(v->pot, 10 + 60 + 60 + 20);
    CHECK_EQ_INT(v->big_blind, 20);
    CHECK(v->active[2] && v->active[3] && !v->active[1] && !v->active[4]);
    CHECK_EQ_INT(v->nboard, 3);
}

int main(void)
{
    eval_init();
    include_direction();
    views_hide_everything();
    history_codes();
    printf("holdem_integrity: %d source files scanned, %ld AI views checked against sentinel copies\n",
           files_scanned, total_views);
    return test_finish("holdem_integrity");
}
