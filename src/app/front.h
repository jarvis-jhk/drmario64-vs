/* The playable game around the VS engine: the setup screen (our own; the original's
 * VS path runs through character select and a per-player level panel, SPEC 01 §4), the
 * match itself, the retry and pause menus the engine asks for, and REPLAY. No SDL in
 * here: it takes two N64-style pads per 60 Hz tick, so a whole session can be driven
 * and checked headless (tests/test_front.c). */
#pragma once
#include "vs.h"

enum { DM_FRONT_SETUP, DM_FRONT_GAME };
enum { DM_ROW_MODE, DM_ROW_WINS, DM_ROW_LEVEL, DM_ROW_SPEED, DM_ROWS_SETUP };

typedef struct DmPanel {
    int row;        /* cursor row, DM_ROW_* */
    int level;      /* NORMAL virus level 0..20 */
    int glevel;     /* FLASH / TIME ATTACK level 0..2 (LOW/MED/HI) */
    int speed;      /* 0..2 */
    int ready;
    uint16_t prev;  /* the pad last tick, for new presses */
    int rep;        /* left/right auto-repeat counter, joy.c timings */
} DmPanel;

typedef struct DmFront {
    int screen;
    DmMode mode;
    int wins;              /* 1..3 */
    DmPanel panel[2];
    DmVs vs;
    uint32_t seed_base;    /* the platform's clock (osGetTime); the test fixes it */
    uint32_t frame;        /* ticks since the program started */
    int go_timer;          /* frames the "GO" logo stays up after the countdown */
    int match_wins[2];     /* matches won this session: the board's record counter */
    int counted;           /* 1 + the winner once this match is in match_wins, else 0 */
    /* REPLAY: the state at the start of the round and the pads since then */
    DmVs *round_start;
    uint16_t (*log)[2];
    int log_n, log_cap, replay_pos, replaying;
    int se[64], se_n;      /* this tick's sound effects, menus included */
    int8_t se_player[64];  /* the player of a character voice, else -1 */
    int locked;            /* ranked online: the settings are fixed, the setup only readies */
} DmFront;

void dm_front_init(DmFront *f, uint32_t seed_base);
void dm_front_free(DmFront *f);
void dm_front_tick(DmFront *f, uint16_t pad0, uint16_t pad1);
/* The item labels of a retry/pause menu type, three each (NULL for type 0: logo only). */
const char *const *dm_front_menu_items(int retry_type);
/* A hash of everything the next ticks depend on (online play compares it). */
uint32_t dm_front_hash(const DmFront *f);
