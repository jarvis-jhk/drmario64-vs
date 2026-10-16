/* Core golden tests. tests/fixtures/rng.txt was produced by compiling the decomp's own
 * randomseed/irandom/random natively (tool: see docs/TRACES.md); each line is
 * "seed S: 12 irandom values | (skip 100) 8 x random(8 or 6)".
 * tests/fixtures/setup.txt comes from tools/oracle (the decomp's dm_make_magazine,
 * _dm_virus_init and make_flash_virus_pos in dm_game_init's order); one round per line. */
#include "rng.h"
#include "setup.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static void check_rng_fixture(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); failures++; return; }
    char line[512];
    int lines = 0;
    while (fgets(line, sizeof line, f)) {
        long seed;
        int used;
        if (sscanf(line, "seed %ld:%n", &seed, &used) != 1) continue;
        DmRng r;
        dm_rng_seed(&r, (int32_t)seed);
        char *p = line + used;
        for (int i = 0; i < 12; i++) {
            long want = strtol(p, &p, 10);
            int32_t got = dm_rng_next(&r);
            if (got != want) { fprintf(stderr, "seed %ld irandom #%d: got %d want %ld\n", seed, i, got, want); failures++; }
        }
        for (int i = 0; i < 100; i++) dm_rng_next(&r);
        p = strchr(p, '|') + 1;
        for (int i = 0; i < 8; i++) {
            long want = strtol(p, &p, 10);
            int32_t got = dm_rng_below(&r, i % 2 ? 6 : 8);
            if (got != want) { fprintf(stderr, "seed %ld random #%d: got %d want %ld\n", seed, i, got, want); failures++; }
        }
        lines++;
    }
    fclose(f);
    if (lines < 7) { fprintf(stderr, "rng fixture: only %d seeds\n", lines); failures++; }
}

static uint64_t fnv(uint64_t h, const uint8_t *p, int n)
{
    for (int i = 0; i < n; i++) { h ^= p[i]; h *= 0x100000001b3ULL; }
    return h;
}
#define FNV0 0xcbf29ce484222325ULL

static void dump_map(const DmVirusMap *m)
{
    for (int y = 1; y <= DM_ROWS; y++) {
        fputs("    ", stderr);
        for (int x = 0; x < DM_COLS; x++) fputc(".RYB"[m->cell[(y - 1) * DM_COLS + x] + 1], stderr);
        fputc('\n', stderr);
    }
}

/* One fixture line: "seed S mode M vl A B gl C D | mag H b1 b2 b3 b4 | p1 r y b H flash
 * col,row,colour... | p2 ... | next N". */
static void check_setup_fixture(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); failures++; return; }
    char line[1024];
    int lines = 0, flashes = 0, same = 0;
    DmVirusStats stats = { 0, 0 };
    while (fgets(line, sizeof line, f)) {
        long seed;
        int mode, used;
        DmPlayerSetup p[2];
        unsigned long long magh;
        if (sscanf(line, "seed %ld mode %d vl %d %d gl %d %d | mag %llx%n", &seed, &mode, &p[0].virus_level,
                   &p[1].virus_level, &p[0].game_level, &p[1].game_level, &magh, &used) != 7) continue;
        lines++;
        DmRng r;
        DmRound rd;
        dm_round_setup(&r, (int32_t)seed, (DmMode)mode, p, &rd, &stats);
        char where[96];
        snprintf(where, sizeof where, "seed %ld mode %d vl %d/%d gl %d/%d", seed, mode, p[0].virus_level,
                 p[1].virus_level, p[0].game_level, p[1].game_level);
        if (fnv(FNV0, rd.magazine, 256) != magh) { fprintf(stderr, "%s: magazine differs\n", where); failures++; }
        if (p[0].virus_level == p[1].virus_level) same++;
        char *s = line + used;
        for (int pl = 0; pl < 2; pl++) {
            s = strstr(s, pl ? "| p2" : "| p1");
            if (!s) { fprintf(stderr, "%s: malformed\n", where); failures++; break; }
            int n[3];
            unsigned long long h;
            if (sscanf(s + 4, "%d %d %d %llx flash%n", &n[0], &n[1], &n[2], &h, &used) != 4) {
                fprintf(stderr, "%s: malformed p%d\n", where, pl + 1); failures++; break;
            }
            const DmVirusMap *m = &rd.map[pl];
            int got[3] = { 0, 0, 0 };
            for (int c = 0; c < DM_CELLS; c++) if (m->cell[c] >= 0) got[m->cell[c]]++;
            uint8_t cells[DM_CELLS];
            memcpy(cells, m->cell, sizeof cells);
            if (fnv(fnv(FNV0, cells, DM_CELLS), m->order, DM_ORDER_LEN) != h || got[0] != n[0] || got[1] != n[1] ||
                got[2] != n[2]) {
                fprintf(stderr, "%s: p%d bottle differs (want %d/%d/%d viruses), ours:\n", where, pl + 1, n[0], n[1], n[2]);
                dump_map(m);
                failures++;
            }
            char *q = s + 4 + used;
            char want[128] = "", ours[128] = "";
            sscanf(q, " %127[^|]", want);
            for (char *e = want + strlen(want); e > want && e[-1] == ' '; ) *--e = 0;
            if (mode == DM_MODE_FLASH) {
                flashes++;
                for (int k = 0; k < rd.flash[pl].count; k++)
                    snprintf(ours + strlen(ours), sizeof ours - strlen(ours), "%s%d,%d,%d", k ? " " : "",
                             rd.flash[pl].pos[k].column, rd.flash[pl].pos[k].row, rd.flash[pl].pos[k].color);
            } else {
                strcpy(ours, "-");
            }
            if (strcmp(want, ours)) { fprintf(stderr, "%s: p%d flash want '%s' got '%s'\n", where, pl + 1, want, ours); failures++; }
            s = q;
        }
        long next;
        char *nx = strstr(line, "| next ");
        if (!nx || sscanf(nx, "| next %ld", &next) != 1 || dm_rng_next(&r) != next) {
            fprintf(stderr, "%s: RNG position after setup differs\n", where);
            failures++;
        }
    }
    fclose(f);
    /* The fixture must keep exercising the rare paths: the deterministic scan, the full
     * restart, FLASH targets and the equal-level copy. */
    if (lines < 200 || flashes < 100 || same < 64 || stats.scans == 0 || stats.restarts == 0) {
        fprintf(stderr, "setup fixture coverage: %d rounds, %d flash bottles, %d copies, %d scans, %d restarts\n",
                lines, flashes, same, stats.scans, stats.restarts);
        failures++;
    }
    printf("setup: %d rounds, %d scans, %d restarts\n", lines, stats.scans, stats.restarts);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "tests/fixtures";
    char path[1024];
    snprintf(path, sizeof path, "%s/rng.txt", dir);
    check_rng_fixture(path);
    snprintf(path, sizeof path, "%s/setup.txt", dir);
    check_setup_fixture(path);
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    puts("core: all checks passed");
    return 0;
}
