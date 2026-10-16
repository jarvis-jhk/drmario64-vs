/* Round-setup oracle: the decomp's own dm_make_magazine, _dm_virus_init and
 * make_flash_virus_pos, compiled natively, replaying dm_game_init's order for a 2P VS
 * round (SPEC part 01 §1.4, §5). Prints tests/fixtures/setup.txt. Output is hashes and
 * colour counts only — no ROM data. Built by build.sh against a decomp checkout. */
#include "dm_game_main.h"
#include "dm_virus_init.h"
#include "calc.h"

#include <stdio.h>
#include <string.h>

u8 CapsMagazine[0x100];
#include "magazine.inc" /* dm_make_magazine, cut verbatim from dm_game_main.c */

/* calc.c's geometry helpers are never called here. */
f32 get_angle(f32 a, f32 b) { (void)a; (void)b; return 0; }
f32 sqrt_a2b2(f32 a, f32 b) { (void)a; (void)b; return 0; }

static struct_game_state_data st[2];
static struct_virus_map_data vmap[2][(GAME_MAP_ROWS - 1) * GAME_MAP_COLUMNS];
static u8 order[2][VIRUS_MAP_DISP_ORDER_LEN];
static int decided[2]; /* make_flash_virus_pos's return: flash_virus_count says 3 regardless */

static unsigned long long fnv(unsigned long long h, const u8 *p, int n)
{
    for (int i = 0; i < n; i++) { h ^= p[i]; h *= 0x100000001b3ULL; }
    return h;
}
#define FNV0 0xcbf29ce484222325ULL

static void round_setup(s32 seed, int mode, int vl0, int vl1, int gl0, int gl1)
{
    memset(st, 0, sizeof st);
    st[0].virus_level = vl0; st[1].virus_level = vl1;
    st[0].game_level = gl0;  st[1].game_level = gl1;
    randomseed(seed);
    dm_make_magazine();
    for (int i = 0; i < 2; i++) {
        dm_virus_init(mode, &st[i], vmap[i], order[i]);
        if (mode == GMD_FLASH) decided[i] = make_flash_virus_pos(&st[i], vmap[i], order[i]);
        else st[i].flash_virus_count = 0;
    }
    if (st[0].virus_level == st[1].virus_level) {
        dm_virus_map_copy(vmap[0], vmap[1], order[0], order[1]);
        if (mode == GMD_FLASH) {
            st[1].flash_virus_count = st[0].flash_virus_count;
            decided[1] = decided[0];
            memcpy(st[1].flash_virus_pos, st[0].flash_virus_pos, sizeof st[1].flash_virus_pos);
            memcpy(st[1].flash_virus_bak, st[0].flash_virus_bak, sizeof st[1].flash_virus_bak);
        }
    }

    printf("seed %d mode %d vl %d %d gl %d %d | mag %016llx", seed, mode, vl0, vl1, gl0, gl1,
           fnv(FNV0, CapsMagazine, 256));
    for (int i = 1; i <= 4; i++) printf(" %02x", CapsMagazine[i]);
    for (int p = 0; p < 2; p++) {
        u8 cells[128];
        int n[3] = { 0, 0, 0 };
        for (int c = 0; c < 128; c++) {
            cells[c] = (u8)vmap[p][c].virus_type;
            if (vmap[p][c].virus_type >= 0) n[vmap[p][c].virus_type]++;
        }
        unsigned long long h = fnv(fnv(FNV0, cells, 128), order[p], VIRUS_MAP_DISP_ORDER_LEN);
        printf(" | p%d %d %d %d %016llx flash", p + 1, n[0], n[1], n[2], h);
        if (mode == GMD_FLASH) {
            for (int k = 0; k < decided[p]; k++)
                printf(" %d,%d,%d", st[p].flash_virus_pos[k].column, st[p].flash_virus_pos[k].row,
                       st[p].flash_virus_pos[k].color);
        } else {
            printf(" -");
        }
    }
    printf(" | next %d\n", irandom());
}

int main(void)
{
    static const s32 fixed[] = { 0, 1, 2, 0x1234, 0xFFFF, 0x10000, -1, 0x7FFFFFFF, (s32)0x80000000, 12345678 };
    unsigned x = 0x2545F491u;
    for (int k = 0; k < 64; k++) {
        s32 seed;
        if (k < (int)(sizeof fixed / sizeof fixed[0])) seed = fixed[k];
        else { x = x * 1664525u + 1013904223u; seed = (s32)x; }
        round_setup(seed, GMD_NORMAL, k % 21, (k * 7 + 3) % 21, 0, 0);
        round_setup(seed, GMD_NORMAL, (k * 5 + 11) % 21, (k * 5 + 11) % 21, 0, 0);
        static const u8 tal[3] = { 6, 9, 12 };
        int a = k % 3, b = (k / 3) % 3;
        round_setup(seed, GMD_FLASH, tal[a], tal[b], a, b);
        round_setup(seed, GMD_TIME_ATTACK, tal[b], tal[a], b, a);
    }
    /* Level 20 (84 viruses) is where layouts restart; these seeds do, some twice. */
    static const s32 crowded[] = { 17, 5019, 10025, 15028, 20040, 25056, 30076, 35088, 40138, 45169, 50209, 55226 };
    for (int k = 0; k < (int)(sizeof crowded / sizeof crowded[0]); k++) {
        round_setup(crowded[k], GMD_NORMAL, 20, 20, 0, 0);
        round_setup(crowded[k], GMD_NORMAL, 20 - k % 3, 20, 0, 0);
    }
    return 0;
}
_Static_assert(sizeof(s32) == 4 && sizeof(u32) == 4, "build.sh's ultratypes shim must pin s32/u32 to 32 bits");
