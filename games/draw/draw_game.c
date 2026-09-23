/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* The Draw Poker state machine. See draw_game.h for the rules, controls,
   timing and events. Each tick handles the input for the current state,
   then advances the state's timer and fires whatever is due at that tick.
   Entering a timed state runs its tick-0 step at once, so a button press
   and the first thing it causes land on the same frame. */

#include "draw_game.h"

#include "engine/replay.h"

#include <inttypes.h>
#include <string.h>

#define MAX_ROUNDS 8        /* size of dbl_hist */

void draw_config_default(DrawConfig *c)
{
    memset(c, 0, sizeof *c);
    c->variant = DRAW_JOB;
    c->allow_variant_select = 1;
    c->bet = DRAW_MAX_BET;
    c->double_up = 1;
    c->double_max_rounds = 5;
    c->double_cap_mult = 2000;
    c->deal_gap_ticks = 6;
    c->flip_delay_ticks = 12;
    c->result_delay_ticks = 8;
    c->double_reveal_ticks = 30;
    c->auto_collect_ticks = 1800;
}

static int32_t clampi(int32_t v, int32_t lo, int32_t hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

void draw_init(DrawGame *g, const DrawConfig *cfg, uint64_t seed)
{
    DrawConfig c;
    int i;
    if (cfg) c = *cfg; else draw_config_default(&c);
    /* Out-of-range settings are clamped rather than trusted: the config
       comes from a file a person edits. */
    c.variant = clampi(c.variant, 0, DRAW_VARIANTS - 1);
    c.bet = clampi(c.bet, 1, DRAW_MAX_BET);
    c.double_max_rounds = clampi(c.double_max_rounds, 0, MAX_ROUNDS);
    c.double_cap_mult = clampi(c.double_cap_mult, 1, 1000000);
    c.deal_gap_ticks = clampi(c.deal_gap_ticks, 0, 600);
    c.flip_delay_ticks = clampi(c.flip_delay_ticks, 0, 600);
    c.result_delay_ticks = clampi(c.result_delay_ticks, 0, 600);
    c.double_reveal_ticks = clampi(c.double_reveal_ticks, 0, 600);
    c.auto_collect_ticks = clampi(c.auto_collect_ticks, 0, 1000000);

    /* Zero everything, padding included, so two games can be memcmp'd. */
    memset(g, 0, sizeof *g);
    draw_rules_init();
    g->cfg = c;
    rng_seed(&g->rng, seed);
    g->state = DS_IDLE;
    g->variant = c.variant;
    g->bet = c.bet;
    deck_init(&g->deck);
    for (i = 0; i < 5; i++) g->cards[i] = g->dealt[i] = CARD_NONE;
    g->dbl_card = CARD_NONE;
    for (i = 0; i < MAX_ROUNDS; i++) g->dbl_hist[i] = CARD_NONE;
}

/* ---- helpers ----------------------------------------------------------- */

static int32_t clamp32(int64_t v)
{
    return v > INT32_MAX ? INT32_MAX : v < INT32_MIN ? INT32_MIN : (int32_t)v;
}

static void enter(DrawGame *g, int state)
{
    g->state = state;
    g->t = 0;
}

static int double_allowed(const DrawGame *g)
{
    return g->cfg.double_up && g->dbl_round < g->cfg.double_max_rounds
        && 2LL * g->meter <= (int64_t)g->cfg.double_cap_mult * g->bet;
}

static void end_hand(DrawGame *g, EventQueue *out)
{
    DrawHandRecord *r = &g->last;
    memset(r, 0, sizeof *r);
    r->seed = g->hand_seed;
    r->hand_no = g->hand_no;
    r->variant = g->variant;
    r->bet = g->bet;
    memcpy(r->dealt, g->dealt, sizeof r->dealt);
    memcpy(r->final, g->cards, sizeof r->final);
    r->held = g->held;
    r->cat = g->final_cat;
    r->win = g->win;
    r->dbl_won = g->dbl_won;
    r->dbl_round = g->dbl_round;
    r->paid = g->paid;
    ev_push(out, EV_DRAW_HAND_END, g->final_cat, g->dbl_won, g->paid);
    g->in_hand = 0;
    enter(g, DS_IDLE);
}

static void collect(DrawGame *g, Wallet *w, EventQueue *out)
{
    g->paid = g->meter;
    if (g->meter > 0) {
        wallet_credit(w, g->meter);
        ev_push(out, EV_DRAW_COLLECT, 0, 0, g->meter);
        ev_push(out, EV_DRAW_CREDITS, DRAW_CREDITS_COLLECT, 0, clamp32(w->credits));
    }
    g->meter = 0;
    end_hand(g, out);
}

static void offer_or_collect(DrawGame *g, Wallet *w, EventQueue *out)
{
    if (double_allowed(g)) {
        enter(g, DS_OFFER);
        ev_push(out, EV_DRAW_DOUBLE_OFFER, g->dbl_round + 1, 0, g->meter);
    } else {
        collect(g, w, out);
    }
}

static void step_dealing(DrawGame *g, EventQueue *out);

static void start_hand(DrawGame *g, Wallet *w, EventQueue *out)
{
    int i;
    if (wallet_debit(w, g->bet) != 0) {
        ev_push(out, EV_DRAW_DENIED, DRAW_DENY_CREDITS, 0, 0);
        return;
    }
    ev_push(out, EV_DRAW_CREDITS, DRAW_CREDITS_BET, 0, clamp32(w->credits));

    g->hand_seed = rng_next(&g->rng);
    rng_seed(&g->hand_rng, g->hand_seed);
    g->hand_no++;
    deck_init(&g->deck);
    deck_shuffle(&g->deck, &g->hand_rng);
    for (i = 0; i < 5; i++) g->dealt[i] = g->cards[i] = deck_draw(&g->deck);
    g->held = g->face_up = g->replaced = 0;
    g->n_replaced = 0;
    memset(g->replace_pos, 0, sizeof g->replace_pos);
    g->in_hand = 1;
    g->dealt_cat = g->final_cat = DC_NONE;
    g->win = g->meter = g->paid = 0;
    g->dbl_round = g->dbl_won = 0;
    g->dbl_guess = DRAW_RED;
    g->dbl_card = CARD_NONE;
    for (i = 0; i < MAX_ROUNDS; i++) g->dbl_hist[i] = CARD_NONE;

    ev_push(out, EV_DRAW_HAND_START, g->variant, g->bet, clamp32(g->hand_no));
    enter(g, DS_DEALING);
    step_dealing(g, out);
}

static void set_bet(DrawGame *g, int bet, int by_max, EventQueue *out)
{
    if (bet != g->bet) {
        g->bet = bet;
        ev_push(out, EV_DRAW_BET, bet, by_max, 0);
    }
}

/* ---- timed steps ------------------------------------------------------- */

static void step_dealing(DrawGame *g, EventQueue *out)
{
    int i, gap = g->cfg.deal_gap_ticks, flip = g->cfg.flip_delay_ticks;
    for (i = 0; i < 5; i++) {
        if (g->t == i * gap) ev_push(out, EV_DRAW_DEAL_CARD, i, g->cards[i], 0);
        if (g->t == i * gap + flip) {
            g->face_up |= (uint8_t)(1u << i);
            ev_push(out, EV_DRAW_FLIP_CARD, i, g->cards[i], 0);
        }
    }
    if (g->t >= 4 * gap + flip) {
        g->dealt_cat = draw_classify(g->variant, g->cards);
        enter(g, DS_HOLD);
        ev_push(out, EV_DRAW_HOLD_PHASE, g->dealt_cat, 0, draw_pay(g->variant, g->dealt_cat, g->bet));
    }
}

static void resolve(DrawGame *g, Wallet *w, EventQueue *out)
{
    g->final_cat = draw_classify(g->variant, g->cards);
    g->win = draw_pay(g->variant, g->final_cat, g->bet);
    if (g->win <= 0) {
        ev_push(out, EV_DRAW_NO_WIN, 0, 0, 0);
        g->meter = 0;
        g->paid = 0;
        end_hand(g, out);
        return;
    }
    g->meter = g->win;
    ev_push(out, EV_DRAW_WIN, g->final_cat, draw_tier(g->final_cat, g->win, g->bet), g->win);
    offer_or_collect(g, w, out);
}

static void step_drawing(DrawGame *g, Wallet *w, EventQueue *out)
{
    int k, gap = g->cfg.deal_gap_ticks, flip = g->cfg.flip_delay_ticks, n = g->n_replaced;
    int done_at = n ? (n - 1) * gap + flip + g->cfg.result_delay_ticks : g->cfg.result_delay_ticks;
    for (k = 0; k < n; k++) {
        int i = g->replace_pos[k];
        if (g->t == k * gap) ev_push(out, EV_DRAW_DRAW_CARD, i, g->cards[i], 0);
        if (g->t == k * gap + flip) {
            g->face_up |= (uint8_t)(1u << i);
            ev_push(out, EV_DRAW_FLIP_CARD, i, g->cards[i], 0);
        }
    }
    if (g->t >= done_at) resolve(g, w, out);
}

static void step_reveal(DrawGame *g, Wallet *w, EventQueue *out)
{
    int red, right;
    if (g->t < g->cfg.double_reveal_ticks) return;
    ev_push(out, EV_DRAW_DOUBLE_CARD, g->dbl_round, g->dbl_card, 0);
    red = card_suit(g->dbl_card) == SUIT_D || card_suit(g->dbl_card) == SUIT_H;
    right = red == (g->dbl_guess == DRAW_RED);
    if (right) {
        g->meter *= 2;
        g->dbl_won++;
        ev_push(out, EV_DRAW_DOUBLE_WIN, g->dbl_round, draw_tier(DC_NONE, g->meter, g->bet), g->meter);
        offer_or_collect(g, w, out);
    } else {
        ev_push(out, EV_DRAW_DOUBLE_LOSE, g->dbl_round, 0, g->meter);
        g->meter = 0;
        g->paid = 0;
        end_hand(g, out);
    }
}

/* ---- input per state --------------------------------------------------- */

static void do_draw(DrawGame *g, Wallet *w, EventQueue *out)
{
    int i;
    g->n_replaced = 0;
    g->replaced = 0;
    for (i = 0; i < 5; i++) {
        if (g->held >> i & 1) continue;
        ev_push(out, EV_DRAW_DISCARD, i, g->cards[i], 0);
        g->face_up &= (uint8_t)~(1u << i);
        g->cards[i] = deck_draw(&g->deck);
        g->replaced |= (uint8_t)(1u << i);
        g->replace_pos[g->n_replaced++] = (uint8_t)i;
    }
    enter(g, DS_DRAWING);
    step_drawing(g, w, out);
}

static void start_double(DrawGame *g, EventQueue *out)
{
    g->dbl_round++;
    g->dbl_card = CARD_NONE;
    enter(g, DS_DOUBLE);
    ev_push(out, EV_DRAW_DOUBLE_START, g->dbl_round, 0, g->meter);
}

static void guess(DrawGame *g, int colour, Wallet *w, EventQueue *out)
{
    /* The card does not exist until the guess is in: a fresh full deck,
       shuffled by the hand's Rng, top card. 26 of its 52 cards are red. */
    g->dbl_guess = colour;
    ev_push(out, EV_DRAW_DOUBLE_GUESS, g->dbl_round, colour, g->meter);
    deck_init(&g->deck);
    deck_shuffle(&g->deck, &g->hand_rng);
    g->dbl_card = deck_draw(&g->deck);
    if (g->dbl_round >= 1 && g->dbl_round <= MAX_ROUNDS) g->dbl_hist[g->dbl_round - 1] = g->dbl_card;
    enter(g, DS_DOUBLE_REVEAL);
    step_reveal(g, w, out);
}

/* The "take the win" family, shared by OFFER and DOUBLE. Returns 1 if the
   input was consumed. */
static int collect_buttons(DrawGame *g, uint32_t p, uint32_t collect_mask, Wallet *w, EventQueue *out)
{
    if (p & BTN_DEAL) {
        collect(g, w, out);
        start_hand(g, w, out);
        return 1;
    }
    if (p & BTN_BET_MAX) {
        collect(g, w, out);
        set_bet(g, DRAW_MAX_BET, 1, out);
        start_hand(g, w, out);
        return 1;
    }
    if (p & BTN_BET_ONE) {
        collect(g, w, out);
        set_bet(g, g->bet % DRAW_MAX_BET + 1, 0, out);
        return 1;
    }
    if (p & collect_mask) {
        collect(g, w, out);
        return 1;
    }
    return 0;
}

void draw_tick(DrawGame *g, const InputFrame *in, Wallet *w, EventQueue *out)
{
    static const InputFrame none;
    EventQueue scratch;
    uint32_t p;
    int i;

    if (!in) in = &none;
    if (!out) { scratch.n = 0; out = &scratch; }
    p = in->pressed;

    switch (g->state) {
    case DS_IDLE:
        if (p & (BTN_UP | BTN_DOWN)) {
            if (!g->cfg.allow_variant_select) {
                ev_push(out, EV_DRAW_DENIED, DRAW_DENY_VARIANT_LOCKED, 0, 0);
            } else {
                int d = (p & BTN_UP) ? 1 : DRAW_VARIANTS - 1;
                g->variant = (g->variant + d) % DRAW_VARIANTS;
                ev_push(out, EV_DRAW_VARIANT, g->variant, 0, 0);
            }
        }
        if (p & BTN_BET_MAX) {
            set_bet(g, DRAW_MAX_BET, 1, out);
            start_hand(g, w, out);
        } else if (p & BTN_DEAL) {
            start_hand(g, w, out);
        } else if (p & BTN_BET_ONE) {
            set_bet(g, g->bet % DRAW_MAX_BET + 1, 0, out);
        }
        break;

    case DS_DEALING:
        g->t++;
        step_dealing(g, out);
        break;

    case DS_HOLD:
        for (i = 0; i < 5; i++) {
            if (p & BTN_HOLD_N(i)) {
                g->held ^= (uint8_t)(1u << i);
                ev_push(out, EV_DRAW_HOLD, i, g->held >> i & 1, 0);
            }
        }
        if (p & BTN_DEAL) do_draw(g, w, out);
        break;

    case DS_DRAWING:
        g->t++;
        step_drawing(g, w, out);
        break;

    case DS_OFFER:
        g->t++;
        if (p & (BTN_HOLD1 | BTN_OK)) { start_double(g, out); break; }
        if (collect_buttons(g, p, BTN_HOLD5 | BTN_CASH_OUT | BTN_BACK, w, out)) break;
        if (g->cfg.auto_collect_ticks > 0 && g->t >= g->cfg.auto_collect_ticks) collect(g, w, out);
        break;

    case DS_DOUBLE:
        g->t++;
        if (p & (BTN_LEFT | BTN_HOLD1 | BTN_HOLD2)) { guess(g, DRAW_RED, w, out); break; }
        if (p & (BTN_RIGHT | BTN_HOLD4 | BTN_HOLD5)) { guess(g, DRAW_BLACK, w, out); break; }
        if (collect_buttons(g, p, BTN_HOLD3 | BTN_CASH_OUT | BTN_BACK, w, out)) break;
        if (g->cfg.auto_collect_ticks > 0 && g->t >= g->cfg.auto_collect_ticks) collect(g, w, out);
        break;

    case DS_DOUBLE_REVEAL:
        g->t++;
        step_reveal(g, w, out);
        break;

    default:
        /* A corrupted state (e.g. a bad save) goes back to idle rather than
           sticking; any bet in play was already debited, and that is logged. */
        enter(g, DS_IDLE);
        break;
    }
}

/* ---- names and the hand log -------------------------------------------- */

const char *draw_state_name(int state)
{
    static const char *const N[DS_COUNT] = {
        "IDLE", "DEALING", "HOLD", "DRAWING", "OFFER", "DOUBLE", "DOUBLE_REVEAL"
    };
    return (state >= 0 && state < DS_COUNT) ? N[state] : "?";
}

const char *draw_event_name(int type)
{
    static const char *const N[EV_DRAW_LAST_TYPE - EV_DRAW_VARIANT] = {
        "VARIANT", "BET", "DENIED", "CREDITS", "HAND_START", "DEAL_CARD", "FLIP_CARD",
        "HOLD_PHASE", "HOLD", "DISCARD", "DRAW_CARD", "WIN", "NO_WIN", "DOUBLE_OFFER",
        "DOUBLE_START", "DOUBLE_GUESS", "DOUBLE_CARD", "DOUBLE_WIN", "DOUBLE_LOSE",
        "COLLECT", "HAND_END"
    };
    if (type < EV_DRAW_VARIANT || type >= EV_DRAW_LAST_TYPE) return "?";
    return N[type - EV_DRAW_VARIANT];
}

int draw_hand_result(const DrawGame *g, char *out, size_t cap)
{
    const DrawHandRecord *r = &g->last;
    const DrawPaytable *pt = draw_paytable(r->variant);
    char deal[11], fin[11], hold[6], tmp[3];
    int i;
    /* Two characters per card, no separators, so the line stays short. */
    for (i = 0; i < 5; i++) {
        card_str(r->dealt[i], tmp);
        deal[2 * i] = tmp[0]; deal[2 * i + 1] = tmp[1];
        card_str(r->final[i], tmp);
        fin[2 * i] = tmp[0]; fin[2 * i + 1] = tmp[1];
        hold[i] = (r->held >> i & 1) ? '1' : '0';
    }
    deal[10] = fin[10] = hold[5] = '\0';
    return snprintf(out, cap, "%s bet=%d deal=%s hold=%s final=%s win=%d dbl=%d/%d paid=%d%s%s",
                    pt ? pt->short_name : "?", r->bet, deal, hold, fin, r->win,
                    r->dbl_won, r->dbl_round, r->paid,
                    r->cat ? " " : "", draw_cat_name(r->cat));
}

int draw_write_hand_log(const DrawGame *g, FILE *f, int64_t unix_time)
{
    char res[160];
    if (draw_hand_result(g, res, sizeof res) < 0) return -1;
    return replay_write_hand(f, unix_time, "draw", g->last.seed, res);
}
