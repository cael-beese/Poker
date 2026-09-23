# Beese's Poker Lounge - architecture contract

Binding for everyone who works on this code (people and agents). `SPEC.md` is
what to build; this is how the pieces fit, so several people can build them at
once without breaking each other. Change this file only through the lead.

## 1. Layout and build targets

```
engine/        cards, RNG, deck, evaluator, events, replay      -> bpl_engine  (pure C11, no raylib)
games/draw/    video poker rules, paytables, EV/strategy         -> bpl_draw    (pure; depends engine)
games/holdem/  No-Limit Hold'em table, betting, pots, showdown   -> bpl_holdem  (pure; depends engine)
ai/            Hold'em opponents                                  -> bpl_ai      (pure + pthreads; depends engine, ai_view.h)
audio/         sound ids, mixer wrapper, music, ducking          -> bpl_audio   (raylib)
render/        atlas, procedural cards, bloom, particles, UI,
               per-mode presentation                             -> bpl_render  (raylib)
platform/      main loop, DRM/desktop, input + config.ini, save,
               service menu, attract driver, debug overlay, perf -> bpl_platform + executable `beese-poker`
tests/         ctest programs                                    -> link ONLY pure libs (engine, draw, holdem, ai)
tools/         offline generators (strategy tables, audio, atlases), scripts
assets/        shipped files (fonts, music, sfx, generated tables)
third_party/   raylib source/build (gitignored; fetched by script)
```

- C11, `-Wall -Wextra -Werror` on our code (not on raylib). No warnings, ever.
- CMake >= 3.18. `-DBPL_PLATFORM=DRM` (the Pi, KMS/DRM, GLES) or `DESKTOP`
  (development: WSL under Xvfb + Mesa llvmpipe, for screenshots and logic;
  its frame times mean nothing). raylib 5.5 is built from source by
  `tools/fetch_raylib.sh` into `third_party/` for the chosen platform.
- `ctest` runs every test without a display, on x86 and on the Pi.
- The Pi build (`cmake --build` from clean, excluding raylib) must stay under
  5 minutes; raylib is built once.

## 2. The engine API (frozen - add, do not change)

```c
/* engine/card.h */
typedef uint8_t Card;                 /* 0..51 = rank*4 + suit              */
#define CARD_NONE 0xFF                /* rank 0 = deuce .. 12 = ace;         */
enum { SUIT_C, SUIT_D, SUIT_H, SUIT_S };/* suit 0 clubs 1 diamonds 2 hearts 3 spades */
static inline int  card_rank(Card c){ return c >> 2; }
static inline int  card_suit(Card c){ return c & 3; }
static inline Card card_make(int rank, int suit){ return (Card)(rank*4 + suit); }
const char *card_str(Card c, char out[3]);          /* "As", "Td", "2c"         */
int         card_parse(const char *s, Card *out);   /* 0 ok, -1 bad             */

/* engine/rng.h - xoshiro256**, seeded through splitmix64 */
typedef struct { uint64_t s[4]; } Rng;
void     rng_seed(Rng *r, uint64_t seed);
uint64_t rng_next(Rng *r);
uint32_t rng_below(Rng *r, uint32_t n);             /* unbiased, [0,n)          */
double   rng_unit(Rng *r);                          /* [0,1), 53 bits           */
uint64_t rng_os_seed(void);                         /* getrandom(); falls back to
                                                       /dev/urandom, then clock^pid */

/* engine/deck.h */
typedef struct { Card c[52]; int n, pos; } Deck;
void deck_init(Deck *d);                            /* 52 cards in order        */
void deck_shuffle(Deck *d, Rng *r);                 /* Fisher-Yates, all n cards */
Card deck_draw(Deck *d);                            /* CARD_NONE when empty     */
void deck_init_without(Deck *d, const Card *known, int n);  /* for simulations  */

/* engine/eval.h - Cactus Kev style, small tables built at start-up */
void eval_init(void);                               /* idempotent, thread-safe after first call */
int  eval5(const Card c[5]);                        /* 1 = royal flush .. 7462 = 7-5-4-3-2 off  */
int  eval7(const Card c[7]);                        /* best five of seven, same scale           */
int  eval_best(const Card *c, int n, Card best[5]); /* n = 5..7; rank + the five used           */
enum HandCat { HC_STRAIGHT_FLUSH = 1, HC_QUADS, HC_FULL_HOUSE, HC_FLUSH, HC_STRAIGHT,
               HC_TRIPS, HC_TWO_PAIR, HC_PAIR, HC_HIGH_CARD };
int  eval_category(int rank);
int  eval_is_royal(int rank);                       /* rank == 1                */
const char *eval_category_name(int cat);            /* "FULL HOUSE" ...         */
/* hot-loop form for Monte Carlo: cards pre-converted once */
uint32_t eval_ck(Card c);                           /* Cactus Kev 32-bit card   */
int  eval5_ck(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e);
int  eval7_ck(const uint32_t ck[7]);

/* engine/input.h - logical inputs, one frame per 60 Hz tick */
enum {
  BTN_HOLD1 = 1u<<0, BTN_HOLD2 = 1u<<1, BTN_HOLD3 = 1u<<2, BTN_HOLD4 = 1u<<3, BTN_HOLD5 = 1u<<4,
  BTN_DEAL  = 1u<<5, BTN_BET_ONE = 1u<<6, BTN_BET_MAX = 1u<<7, BTN_CASH_OUT = 1u<<8,
  BTN_SERVICE = 1u<<9, BTN_UP = 1u<<10, BTN_DOWN = 1u<<11, BTN_LEFT = 1u<<12,
  BTN_RIGHT = 1u<<13, BTN_OK = 1u<<14, BTN_BACK = 1u<<15, BTN_COIN = 1u<<16,
  BTN_START = 1u<<17, BTN_DEBUG = 1u<<18
};
typedef struct {
  uint32_t down;        /* held this tick                                  */
  uint32_t pressed;     /* went down this tick                             */
  int16_t  slider;      /* Hold'em bet slider: -32767..32767 axis, or 0     */
  int16_t  touch_x, touch_y; uint8_t touch;   /* in 1280x720 play space     */
} InputFrame;

/* engine/event.h - what happened, for presentation and audio to react to */
typedef struct { uint16_t type; int16_t a, b; int32_t v; } GameEvent;
typedef struct { GameEvent e[256]; int n; } EventQueue;   /* cleared each tick by the app */
void ev_push(EventQueue *q, uint16_t type, int a, int b, int v);   /* drops if full */
/* event type numbers: engine 0-99, draw 100-199, holdem 200-299, app 300-399 */

/* engine/replay.h */
/* per-hand line to the hand log: "<unix-time> <mode> <seed-hex> <result>" and an
   optional full input log (seed + InputFrame stream) to replay a session */
```

## 3. The game pattern (every mode)

```c
void draw_init(DrawGame *g, const DrawConfig *cfg, uint64_t seed);
void draw_tick(DrawGame *g, const InputFrame *in, Wallet *w, EventQueue *out);
```

- **All game state is one flat struct** per mode (no pointers into itself, no
  heap), so it can be copied, saved and compared.
- `*_tick` runs at exactly 60 Hz from the fixed-step loop, is the ONLY thing
  that changes game state, and is **deterministic**: same seed + same input
  frames = same states and events. No wall clock, no `rand()`, no reading of
  anything outside its arguments. Timers are counted in ticks.
- `Wallet` (`engine/wallet.h`: `int64_t credits; int denom;`) is owned by the
  app and passed in; games change credits only through it.
- Games **push events** (`EventQueue`) for everything presentation or audio
  should react to (a card dealt, a hold toggled, a win and its tier, a chip
  moving, a showdown). Presentation never polls for "did X just happen" by
  diffing state when an event can say it.

## 4. The presentation pattern (render/ and audio/)

```c
void draw_present_update(DrawPresent *p, const DrawGame *g, const GameEvent *ev, int nev, float dt);
void draw_present_draw(const DrawPresent *p, const DrawGame *g);   /* inside the 1280x720 target */
```

- Presentation keeps its own cosmetic state (animations, particles, count-up
  meters) and takes `const` game state. It **never writes game state** and
  never calls the game's RNG. Cosmetic randomness uses its own `Rng` stream.
- Audio is triggered from presentation (reacting to events), never from game
  logic.
- No allocation per frame in render/ or audio/ (pools sized at start-up).
- Every required effect has a settings toggle (`settings.effects.*`), so each
  can be profiled on and off.
- 1280x720 is the play space. It is drawn into a render texture and scaled
  2x to 2560x1440 in the middle of the cabinet's 3440x1440 screen, with side
  art in the 440 px margins (see SPEC adaptations).

## 5. Honesty rules (non-negotiable)

- Cards come only from `deck_shuffle` (Fisher-Yates on the game `Rng`) and
  `deck_draw`, in order. Nothing inspects the undealt deck to decide what to
  show, when to show it, or what an opponent does.
- The game `Rng` is seeded from `rng_os_seed()` per session and re-seeded per
  hand from its own stream; the per-hand seed is logged. Presentation, audio,
  AI and attract mode each have their own `Rng`.
- No near-miss engineering, no adaptive odds, no "due" logic, no difference in
  outcomes between attract/demo and real play.
- **The AI never sees hidden cards or the deck.** Its only input is
  `ai/ai_view.h`:

```c
typedef struct {
  int   seats, me, button, street;            /* street 0 pre-flop .. 3 river */
  Card  hole[2];                              /* MY cards only                */
  Card  board[5]; int nboard;
  int64_t stack[6], bet[6], pot, to_call, min_raise, big_blind;
  uint8_t active[6], allin[6];                /* public seat state            */
  uint8_t history[64]; int nhist;             /* public action history codes  */
  int   personality;                          /* AI_ROCK, AI_MANIAC, AI_SHARK, AI_FISH */
} AiView;
typedef struct { int action; int64_t amount; int think_ticks; float tell; } AiDecision;
void ai_decide(const AiView *v, Rng *ai_rng, AiDecision *out);
```

  The Hold'em module builds an `AiView` for the seat to act; the AI module
  never includes `games/holdem` headers. A test checks both.
- AI decisions must be deterministic too: Monte Carlo runs a **fixed number of
  trials** (sized so it fits in 30 ms on the Pi), on a worker thread, started
  when the seat's turn begins; the decision is applied when the think time
  (>= 24 ticks) expires, and the main thread joins if it is not done yet.

## 6. Performance rules (the Pi 4 is the only judge)

- Measure on the Pi before calling anything done: the F1 overlay (frame ms,
  draw calls, RSS, texture memory) and `--perf-csv` frame logs.
- Budget: 60 fps locked in the heaviest scene, 1% lows >= 55 fps, RSS <= 150
  MB, textures <= 64 MB, cold start to attract <= 4 s, < 100 draw calls.
- Sprites through atlases; post-processing at 1/2 or 1/4 resolution.
- If a technique misses budget, try two alternatives before cutting it.

## 7. Working rules

- Keep to your directory; if you need a change elsewhere, make the minimal one
  and say so in your report.
- Source is ASCII. Comments say why, in full sentences.
- `-Werror` clean, `ctest` green, before every commit.
- Screenshots: `beese-poker --shot N:file.png[,N:file.png...] --script ...`
  (the platform milestone provides it) - look at your work.
- Report measured numbers, not expectations. If something could not be
  measured, say "not measured".
