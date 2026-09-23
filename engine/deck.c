/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
#include "deck.h"

void deck_init(Deck *d)
{
    int i;
    for (i = 0; i < 52; i++) d->c[i] = (Card)i;
    d->n = 52;
    d->pos = 0;
}

void deck_shuffle(Deck *d, Rng *r)
{
    int i;
    for (i = d->n - 1; i > 0; i--) {
        int j = (int)rng_below(r, (uint32_t)i + 1);
        Card t = d->c[i];
        d->c[i] = d->c[j];
        d->c[j] = t;
    }
    d->pos = 0;
}

Card deck_draw(Deck *d)
{
    if (d->pos >= d->n) return CARD_NONE;
    return d->c[d->pos++];
}

void deck_init_without(Deck *d, const Card *known, int n)
{
    uint8_t used[52] = {0};
    int i;
    for (i = 0; i < n; i++)
        if (card_valid(known[i])) used[known[i]] = 1;
    d->n = 0;
    for (i = 0; i < 52; i++)
        if (!used[i]) d->c[d->n++] = (Card)i;
    d->pos = 0;
}

Card deck_draw_random(Deck *d, Rng *r)
{
    int j;
    Card t;
    if (d->pos >= d->n) return CARD_NONE;
    j = d->pos + (int)rng_below(r, (uint32_t)(d->n - d->pos));
    t = d->c[j];
    d->c[j] = d->c[d->pos];
    d->c[d->pos] = t;
    d->pos++;
    return t;
}
