/* A simple scripted player for tests and screenshots (not the original's CPU). */
#pragma once
#include "vs.h"

typedef struct DmBot {
    int skill;        /* 0 holds DOWN (tops out), 1 plays, 2 plays and drops fast */
    int tx, tv, tc;   /* target column, vertical, colour of half 0 */
    int serial;       /* the capsule the target was chosen for */
    unsigned tick;
} DmBot;

void dm_bot_init(DmBot *b, int skill);
uint16_t dm_bot_pad(DmBot *b, const DmVs *g, int player);
