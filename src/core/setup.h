/* Round setup: capsule magazine, virus layouts and FLASH targets, in the order
 * dm_game_init uses them (SPEC part 01 §1.4, §2, §3, §5). Everything here is a pure
 * function of the seed and the two players' settings. */
#pragma once
#include "rng.h"

#include <stdint.h>

enum { DM_COLS = 8, DM_ROWS = 16, DM_CELLS = DM_COLS * DM_ROWS, DM_ORDER_LEN = 96, DM_FLASH_MAX = 9 };

typedef enum DmMode { DM_MODE_NORMAL = 0, DM_MODE_FLASH = 1, DM_MODE_TIME_ATTACK = 3 } DmMode;

/* Virus colours 0 red, 1 yellow, 2 blue, -1 empty. Cell (x, y) with y = 1..16 from
 * the top is index (y - 1) * 8 + x, the original's virus_map_data layout. */
typedef struct DmVirusMap {
    int8_t cell[DM_CELLS];
    uint8_t order[DM_ORDER_LEN]; /* reveal order: cell indices, 0xFF after the last */
    int count;
} DmVirusMap;

typedef struct DmFlash {
    int count;
    struct { int column, row, color; } pos[DM_FLASH_MAX];
} DmFlash;

typedef struct DmPlayerSetup {
    int virus_level; /* NORMAL: 0..20; FLASH/TIME ATTACK: 6/9/12 from the game level */
    int game_level;  /* FLASH/TIME ATTACK: 0..2 */
} DmPlayerSetup;

typedef struct DmRound {
    uint8_t magazine[256];
    DmVirusMap map[2];
    DmFlash flash[2];
} DmRound;

/* Statistics of how a layout was found, for tests that want the rare paths covered. */
typedef struct DmVirusStats {
    int scans, restarts;
} DmVirusStats;

void dm_make_magazine(DmRng *r, uint8_t mag[256]);                     /* dm_make_magazine */
static inline int dm_mag_left(uint8_t m) { return (m >> 4) % 3; }      /* CAPSMAGAZINE_GET_A */
static inline int dm_mag_right(uint8_t m) { return m % 3; }            /* CAPSMAGAZINE_GET_B */

int dm_virus_count(DmMode mode, const DmPlayerSetup *p);               /* dm_get_first_virus_count */
void dm_virus_init(DmRng *r, DmMode mode, const DmPlayerSetup *p, DmVirusMap *out,
                   DmVirusStats *stats);                               /* _dm_virus_init */
void dm_make_flash(DmRng *r, int game_level, const DmVirusMap *map, DmFlash *out); /* make_flash_virus_pos */

/* dm_game_init's RNG part for a 2P round: seed, magazine, both layouts, the copy for
 * equal levels. The RNG is left where the original leaves it for in-game garbage. */
void dm_round_setup(DmRng *r, int32_t seed, DmMode mode, const DmPlayerSetup p[2], DmRound *out,
                    DmVirusStats *stats);
