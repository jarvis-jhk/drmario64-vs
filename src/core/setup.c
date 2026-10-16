#include "setup.h"

#include <string.h>

void dm_make_magazine(DmRng *r, uint8_t mag[256])
{
    /* random(0xFFFF) % 256 stored in a u8; rejected draws are overwritten in place. */
    mag[0] = (uint8_t)dm_rng_below(r, 0xFFFF);
    int pl = dm_mag_left(mag[0]), pr = dm_mag_right(mag[0]);
    for (int i = 1; i < 256;) {
        mag[i] = (uint8_t)dm_rng_below(r, 0xFFFF);
        int l = dm_mag_left(mag[i]), rr = dm_mag_right(mag[i]);
        if (l != pl || rr != pr) {
            pl = l;
            pr = rr;
            i++;
        }
    }
}

static const uint8_t limit_table[25] = {
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 5, 5, 4, 4, 3, 3, 3, 3, 3, 3,
};
static const uint8_t new_mode_count[3] = { 30, 40, 50 }; /* _n_343 */
static const uint8_t new_mode_limit_idx[3] = { 6, 9, 12 }; /* _l_359 */

static int is_new_mode(DmMode m) { return m == DM_MODE_FLASH || m == DM_MODE_TIME_ATTACK; }

int dm_virus_count(DmMode mode, const DmPlayerSetup *p)
{
    if (is_new_mode(mode)) return new_mode_count[p->game_level];
    int lv = p->virus_level < DM_ORDER_LEN / 4 - 1 ? p->virus_level : DM_ORDER_LEN / 4 - 1;
    return (lv + 1) * 4;
}

static int limit_line(DmMode mode, const DmPlayerSetup *p)
{
    int idx = is_new_mode(mode) ? new_mode_limit_idx[p->game_level] : p->virus_level;
    if (idx >= 24) idx = 23; /* the original clamps to ARRAY_COUNT - 2 */
    return limit_table[idx];
}

static int at(const int8_t *cell, int x, int y) { return cell[(y - 1) * DM_COLS + x]; }

/* dm_check_color: no three in a row of colour c through (x, y), horizontally or
 * vertically. The vertical checks mirror the original's guards exactly, including the
 * y > 3 && y < 16 window for the "one above, one below" case. */
static int ok1(const int8_t *cell, int x, int y, int c)
{
    if (x >= 2 && at(cell, x - 1, y) == c && at(cell, x - 2, y) == c) return 0;
    if (x != 0 && x != DM_COLS - 1 && at(cell, x - 1, y) == c && at(cell, x + 1, y) == c) return 0;
    if (x < 6 && at(cell, x + 1, y) == c && at(cell, x + 2, y) == c) return 0;
    if (y < 15 && at(cell, x, y + 1) == c && at(cell, x, y + 2) == c) return 0;
    if (y > 3 && y < 16 && at(cell, x, y + 1) == c && at(cell, x, y - 1) == c) return 0;
    if (y > 5 && at(cell, x, y - 1) == c && at(cell, x, y - 2) == c) return 0;
    return 1;
}

/* dm_check_color_2: colour c not two cells away in the row or column. */
static int ok2(const int8_t *cell, int x, int y, int c)
{
    if (x >= 2 && at(cell, x - 2, y) == c) return 0;
    if (x < 6 && at(cell, x + 2, y) == c) return 0;
    if (y >= 6 && at(cell, x, y - 2) == c) return 0;
    if (y < 15 && at(cell, x, y + 2) == c) return 0;
    return 1;
}

void dm_virus_init(DmRng *r, DmMode mode, const DmPlayerSetup *p, DmVirusMap *out, DmVirusStats *stats)
{
    const int limit = limit_line(mode, p);
    const int count = dm_virus_count(mode, p);
    out->count = count;

restart:
    memset(out->cell, -1, sizeof out->cell);
    memset(out->order, 0xFF, sizeof out->order);

    int left[3] = { count / 3, count / 3, count / 3 };
    for (int k = 0; k < count % 3;) {
        int c = dm_rng_below(r, 3);
        if (left[c] <= left[(c + 1) % 3] && left[c] <= left[(c + 2) % 3]) {
            left[c]++;
            k++;
        }
    }

    for (int n = 0; n < count; n++) {
        int scans = 0, tries = 0, found, c, x = 0, y = 0;
        do {
            do c = dm_rng_below(r, 3); while (left[c] == 0);
            if (tries++ == 2) {
                /* Every third attempt scans the bottle column by column, bottom up. */
                if (++scans > 2) {
                    if (stats) stats->restarts++;
                    goto restart; /* the whole layout again; the RNG carries on */
                }
                if (stats) stats->scans++;
                tries = 0;
                found = 0;
                for (x = 0; x < DM_COLS && !found; x++)
                    for (y = DM_ROWS; y > limit && !found; y--)
                        found = at(out->cell, x, y) < 0 && ok1(out->cell, x, y, c) && ok2(out->cell, x, y, c);
                if (found) { x--; y++; } /* undo the loops' final step */
            } else {
                do {
                    x = dm_rng_below(r, DM_COLS);
                    do y = dm_rng_below(r, DM_ROWS + 1); while (y < limit + 1);
                } while (at(out->cell, x, y) >= 0);
                found = ok1(out->cell, x, y, c) && ok2(out->cell, x, y, c);
            }
        } while (!found);
        left[c]--;
        int idx = (y - 1) * DM_COLS + x;
        out->cell[idx] = (int8_t)c;
        out->order[n] = (uint8_t)idx;
    }
}

/* make_flash_virus_pos with _makeFlash_checkOrdre/_makeFlash_decide inlined. */
void dm_make_flash(DmRng *r, int game_level, const DmVirusMap *map, DmFlash *out)
{
    static const int quota[3][5] = { { 1, 0, 2, 96, 96 }, { 1, 1, 1, 96, 96 }, { 1, 2, 0, 96, 96 } };
    enum { UNDER, UNDER2, UNDER3, CENTER, SIDE, NCAT };
    uint8_t buf[NCAT][3][32];
    int cnt[NCAT][3] = { { 0 } };
    /* The original's selected[0x60] is indexed by cell index (up to 127), so cells in
     * rows 13-16 read and write decideBuf[0], which follows it in the MakeFlash struct.
     * Only cells (0..2, 13) can collide with red's few decided entries; 3M random FLASH
     * rounds never hit it, but one array keeps the overlap exact anyway. */
    uint8_t mem[DM_ORDER_LEN + 3 * 32] = { 0 };
    uint8_t *selected = mem;
    uint8_t (*decided)[32] = (uint8_t (*)[32])(mem + DM_ORDER_LEN);
    int ndecided[3] = { 0, 0, 0 };

    int ymax = -1;
    for (int i = 0; i < map->count; i++) {
        int idx = map->order[i], x = idx % DM_COLS, y = idx / DM_COLS + 1;
        if (x != 0 && x != DM_COLS - 1 && y > ymax) ymax = y;
    }
    if (ymax > 14) ymax = 14;
    for (int i = 0; i < map->count; i++) {
        int idx = map->order[i], x = idx % DM_COLS, y = idx / DM_COLS + 1, c = map->cell[idx];
        if (x == 0 || x == DM_COLS - 1) {
            buf[SIDE][c][cnt[SIDE][c]++] = (uint8_t)idx;
        } else {
            int cat = y >= ymax ? UNDER : y >= ymax - 3 ? UNDER2 : UNDER3;
            buf[cat][c][cnt[cat][c]++] = (uint8_t)idx;
            buf[CENTER][c][cnt[CENTER][c]++] = (uint8_t)idx;
        }
    }

    int remaining = 3;
    int c = dm_rng_below(r, 3);
    for (int cat = 0; cat < NCAT; cat++) {
        int q = remaining < quota[game_level][cat] ? remaining : quota[game_level][cat];
        for (int fails = 0; fails < 3 && q > 0;) {
            c = (c + 1) % 3;
            int pick = -1;
            if (ndecided[c] <= ndecided[(c + 1) % 3] && ndecided[c] <= ndecided[(c + 2) % 3]) {
                for (int i = 0; i < cnt[cat][c]; i++) {
                    int idx = buf[cat][c][i];
                    if (!selected[idx]) {
                        selected[idx] = 1;
                        decided[c][ndecided[c]++] = (uint8_t)idx;
                        pick = idx;
                        break;
                    }
                }
            }
            fails++;
            if (pick >= 0) {
                remaining--;
                q--;
                fails = 0;
            }
        }
    }

    out->count = 0;
    for (int col = 0; col < 3; col++)
        for (int i = 0; i < ndecided[col]; i++) {
            int idx = decided[col][i];
            out->pos[out->count].column = idx & 7;
            out->pos[out->count].row = (idx >> 3) & 0xF;
            out->pos[out->count].color = col;
            out->count++;
        }
}

void dm_round_setup(DmRng *r, int32_t seed, DmMode mode, const DmPlayerSetup p[2], DmRound *out,
                    DmVirusStats *stats)
{
    dm_rng_seed(r, seed);
    dm_make_magazine(r, out->magazine);
    for (int i = 0; i < 2; i++) {
        dm_virus_init(r, mode, &p[i], &out->map[i], stats);
        if (mode == DM_MODE_FLASH) dm_make_flash(r, p[i].game_level, &out->map[i], &out->flash[i]);
        else out->flash[i].count = 0;
    }
    /* Equal virus levels: player 2 gets player 1's bottle, after its own draws. */
    if (p[0].virus_level == p[1].virus_level) {
        out->map[1] = out->map[0];
        out->flash[1] = out->flash[0];
    }
}
