/* Core golden tests. tests/fixtures/rng.txt was produced by compiling the decomp's own
 * randomseed/irandom/random natively (tool: see docs/TRACES.md); each line is
 * "seed S: 12 irandom values | (skip 100) 8 x random(8 or 6)". */
#include "rng.h"

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

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "tests/fixtures";
    char path[1024];
    snprintf(path, sizeof path, "%s/rng.txt", dir);
    check_rng_fixture(path);
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    puts("core: all checks passed");
    return 0;
}
