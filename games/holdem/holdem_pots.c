/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
/* Main and side pots, and splitting a pot among tied winners. Kept apart and
   pure so the arithmetic can be tested exhaustively on its own. */

#include "games/holdem/holdem.h"

int holdem_build_pots(const int64_t committed[HOLDEM_SEATS], const uint8_t live[HOLDEM_SEATS],
                      HoldemPot out[HOLDEM_MAX_POTS])
{
    int64_t prev = 0;
    int n = 0, s;

    /* Each pass takes the smallest total a live seat put in above the last
       level: every seat's chips between the two levels form one pot, and the
       live seats that reached the new level are the ones who can win it. */
    for (;;) {
        int64_t lvl = -1, amount = 0;
        uint8_t elig = 0;
        for (s = 0; s < HOLDEM_SEATS; s++)
            if (live[s] && committed[s] > prev && (lvl < 0 || committed[s] < lvl)) lvl = committed[s];
        if (lvl < 0) break;
        for (s = 0; s < HOLDEM_SEATS; s++) {
            int64_t c = committed[s];
            if (c > prev) amount += (c < lvl ? c : lvl) - prev;
            if (live[s] && c >= lvl) elig |= (uint8_t)(1u << s);
        }
        if (n < HOLDEM_MAX_POTS) {
            out[n].amount = amount;
            out[n].eligible = elig;
            n++;
        } else {
            /* Six seats give at most six distinct live levels, so this is
               unreachable; merging keeps the chips if it ever were not. */
            out[n - 1].amount += amount;
        }
        prev = lvl;
    }

    /* Dead money above the highest live level. The table returns uncalled
       chips before pots are built, so this is normally nothing. */
    {
        int64_t extra = 0;
        for (s = 0; s < HOLDEM_SEATS; s++)
            if (committed[s] > prev) extra += committed[s] - prev;
        if (extra > 0 && n > 0) out[n - 1].amount += extra;
    }
    return n;
}

void holdem_split_pot(int64_t amount, uint8_t winners, int button, int64_t share[HOLDEM_SEATS])
{
    int k = 0, i, s;
    int64_t each, odd;

    for (s = 0; s < HOLDEM_SEATS; s++)
        if (winners & (1u << s)) k++;
    if (k == 0 || amount <= 0) return;
    each = amount / k;
    odd = amount % k;
    for (i = 1; i <= HOLDEM_SEATS; i++) {
        s = (button + i) % HOLDEM_SEATS;
        if (!(winners & (1u << s))) continue;
        share[s] += each;
        if (odd > 0) { share[s]++; odd--; }
    }
}
