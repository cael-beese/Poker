/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md */
#include "card.h"

#include <ctype.h>
#include <string.h>

const char CARD_RANK_CHARS[14] = "23456789TJQKA";
const char CARD_SUIT_CHARS[5]  = "cdhs";

const char *card_str(Card c, char out[3])
{
    if (!card_valid(c)) {
        out[0] = '?'; out[1] = '?';
    } else {
        out[0] = CARD_RANK_CHARS[card_rank(c)];
        out[1] = CARD_SUIT_CHARS[card_suit(c)];
    }
    out[2] = '\0';
    return out;
}

static int rank_of_char(int ch)
{
    const char *p;
    ch = toupper((unsigned char)ch);
    if (ch == '\0') return -1;
    p = strchr(CARD_RANK_CHARS, ch);
    return p ? (int)(p - CARD_RANK_CHARS) : -1;
}

static int suit_of_char(int ch)
{
    const char *p;
    ch = tolower((unsigned char)ch);
    if (ch == '\0') return -1;
    p = strchr(CARD_SUIT_CHARS, ch);
    return p ? (int)(p - CARD_SUIT_CHARS) : -1;
}

/* Parses one card starting at s; returns the number of characters used, or
   0 if s does not start with a card. */
static int parse_one(const char *s, Card *out)
{
    int rank, suit, used;
    if (s[0] == '1' && s[1] == '0') {
        rank = RANK_T;
        used = 2;
    } else {
        rank = rank_of_char(s[0]);
        used = 1;
    }
    if (rank < 0) return 0;
    suit = suit_of_char(s[used]);
    if (suit < 0) return 0;
    *out = card_make(rank, suit);
    return used + 1;
}

int card_parse(const char *s, Card *out)
{
    Card c;
    int used;
    if (!s) return -1;
    used = parse_one(s, &c);
    if (used == 0 || s[used] != '\0') return -1;
    *out = c;
    return 0;
}

int cards_parse(const char *s, Card *out, int max)
{
    int n = 0;
    if (!s) return -1;
    for (;;) {
        Card c;
        int used;
        while (isspace((unsigned char)*s)) s++;
        if (*s == '\0') return n;
        used = parse_one(s, &c);
        if (used == 0) return -1;
        s += used;
        if (*s != '\0' && !isspace((unsigned char)*s)) return -1;
        if (n >= max) return -1;
        out[n++] = c;
    }
}
