/* Golden-trace test for the VS engine. Each tests/movies/<name>.dmm was written by
 * tools/oracle (the decomp's own game code compiled natively, played by scripted
 * players): the match settings, both pads per tick, the ticks after which the next
 * round was started, and a rolling hash of the full per-tick state dump every 60
 * ticks. The engine replays the pads and must produce the same dumps, tick for tick.
 *
 * On a mismatch it names the 60-tick window. To see it: DM_DUMP=<from>:<to> prints the
 * engine's dumps for those ticks; tools/oracle/build_trace.sh <decomp> <dir> <movie>
 * <from> <to> prints the original's, and diff shows the first wrong field. */
#include "vs.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures, movies;

static uint64_t fnv(uint64_t h, const char *p, size_t n)
{
    for (size_t i = 0; i < n; i++) { h ^= (uint8_t)p[i]; h *= 0x100000001b3ULL; }
    return h;
}

typedef struct Check { int tick; unsigned long long hash; } Check;

static void replay(const char *path, const char *name)
{
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); failures++; return; }
    int mode = 0, vl[2], speed[2], gl[2], wins = 0, seed = 0, countdown = 0, have_cfg = 0;
    unsigned salt = 0;
    int end_ticks = -1, end_w[2] = { 0, 0 };
    unsigned long long end_hash = 0;
    static uint16_t pads[200000][2];
    static unsigned char next[200000];
    static Check checks[4000];
    static struct { int tick, player, n, cell[128][3]; } junk[64];
    int nchecks = 0, last_input = 0, njunk = 0;
    uint16_t cur[2] = { 0, 0 };
    memset(next, 0, sizeof next);
    static char line[4096];
    while (fgets(line, sizeof line, f)) {
        int t, a, b;
        unsigned pa, pb;
        unsigned long long h;
        if (sscanf(line, "cfg mode %d vl %d %d speed %d %d gl %d %d wins %d seed %d salt %u countdown %d", &mode, &vl[0],
                   &vl[1], &speed[0], &speed[1], &gl[0], &gl[1], &wins, &seed, &salt, &countdown) == 11) {
            have_cfg = 1;
        } else if (sscanf(line, "i %d %x %x", &t, &pa, &pb) == 3 && t >= 0 && t < 200000) {
            for (int k = last_input; k < t; k++) { pads[k][0] = cur[0]; pads[k][1] = cur[1]; }
            cur[0] = (uint16_t)pa;
            cur[1] = (uint16_t)pb;
            last_input = t;
        } else if (sscanf(line, "h %d %llx", &t, &h) == 2 && nchecks < 4000) {
            checks[nchecks].tick = t;
            checks[nchecks++].hash = h;
        } else if (sscanf(line, "b %d %d%n", &t, &a, &b) == 2 && njunk < 64) {
            char *q = line + b;
            int x, y, c, used;
            junk[njunk].tick = t;
            junk[njunk].player = a;
            junk[njunk].n = 0;
            while (junk[njunk].n < 128 && sscanf(q, " %d,%d,%d%n", &x, &y, &c, &used) == 3) {
                junk[njunk].cell[junk[njunk].n][0] = x;
                junk[njunk].cell[junk[njunk].n][1] = y;
                junk[njunk].cell[junk[njunk].n++][2] = c;
                q += used;
            }
            njunk++;
        } else if (sscanf(line, "n %d", &t) == 1 && t >= 0 && t < 200000) {
            next[t] = 1;
        } else if (sscanf(line, "end %d %llx wins %d %d", &t, &h, &end_w[0], &end_w[1]) == 4) {
            end_ticks = t;
            end_hash = h;
        }
    }
    fclose(f);
    if (!have_cfg || end_ticks < 0 || end_ticks >= 200000) { fprintf(stderr, "%s: bad movie\n", name); failures++; return; }
    for (int k = last_input; k < end_ticks; k++) { pads[k][0] = cur[0]; pads[k][1] = cur[1]; }
    if (countdown != DM_COUNTDOWN_FRAMES) {
        fprintf(stderr, "%s: movie countdown %d, engine %d\n", name, countdown, DM_COUNTDOWN_FRAMES);
        failures++;
        return;
    }

    int dump_from = -1, dump_to = -2;
    const char *env = getenv("DM_DUMP");
    if (env) sscanf(env, "%d:%d", &dump_from, &dump_to);

    static DmVs g;
    DmVsConfig cfg = { .mode = (DmMode)mode, .wins = wins };
    for (int p = 0; p < 2; p++) {
        cfg.p[p].virus_level = vl[p];
        cfg.p[p].game_level = gl[p];
        cfg.speed[p] = speed[p];
    }
    dm_vs_start(&g, &cfg, seed, salt);
    uint64_t h = 0xcbf29ce484222325ULL;
    static char buf[16384];
    int ci = 0, ok = 1, rounds = 1, prev_ok_tick = -1;
    for (int t = 0; t < end_ticks && ok; t++) {
        dm_vs_tick(&g, pads[t][0], pads[t][1]);
        size_t n = dm_vs_dump(&g, buf, sizeof buf);
        if (n >= sizeof buf) { fprintf(stderr, "%s: dump too long\n", name); failures++; return; }
        h = fnv(h, buf, n);
        if (t >= dump_from && t <= dump_to) fprintf(stderr, "== tick %d\n%s", t, buf);
        if (ci < nchecks && checks[ci].tick == t) {
            if (checks[ci].hash != h) {
                fprintf(stderr, "%s: state differs from the original within ticks %d..%d\n", name, prev_ok_tick + 1, t);
                ok = 0;
                break;
            }
            prev_ok_tick = t;
            ci++;
        }
        for (int k = 0; k < njunk; k++)
            if (junk[k].tick == t)
                for (int c = 0; c < junk[k].n; c++)
                    dm_vs_set_pill(&g, junk[k].player, junk[k].cell[c][0], junk[k].cell[c][1], junk[k].cell[c][2]);
        int rs = dm_vs_round_state(&g);
        if (next[t] != (rs == 1)) {
            fprintf(stderr, "%s: tick %d: round state %d, but the original %s the next round\n", name, t, rs,
                    next[t] ? "started" : "did not start");
            ok = 0;
            break;
        }
        if (rs == 1) {
            dm_vs_next_round(&g);
            rounds++;
        }
    }
    if (ok && (h != end_hash || g.win_count[0] != end_w[0] || g.win_count[1] != end_w[1])) {
        fprintf(stderr, "%s: final state differs (wins %d-%d, want %d-%d)\n", name, g.win_count[0], g.win_count[1],
                end_w[0], end_w[1]);
        ok = 0;
    }
    if (!ok) failures++;
    else printf("%-12s %6d ticks, %d round(s), %d checkpoints: identical\n", name, end_ticks, rounds, nchecks);
    movies++;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "tests/movies";
    DIR *d = opendir(dir);
    if (!d) { fprintf(stderr, "cannot open %s\n", dir); return 1; }
    struct dirent *e;
    char path[1024];
    while ((e = readdir(d))) {
        size_t n = strlen(e->d_name);
        if (n < 5 || strcmp(e->d_name + n - 4, ".dmm")) continue;
        if (argc > 2 && (strlen(argv[2]) != n - 4 || strncmp(e->d_name, argv[2], n - 4))) continue;
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        char name[256];
        snprintf(name, sizeof name, "%.*s", (int)(n - 4), e->d_name);
        replay(path, name);
    }
    closedir(d);
    if (movies < 8 && argc <= 2) { fprintf(stderr, "only %d movies\n", movies); failures++; }
    if (failures) { fprintf(stderr, "%d movie(s) FAILED\n", failures); return 1; }
    printf("all %d movies identical to the original\n", movies);
    return 0;
}
