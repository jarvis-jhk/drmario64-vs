/* Per-tick trace oracle (docs/TRACES.md). The decomp's own dm_game_main.c is compiled
 * into this file (it keeps its watchGame struct private), together with joy.c, main1x.c,
 * calc.c and dm_virus_init.c, and stubs.c for everything outside the game logic.
 *
 * Each tick replays the real per-frame order of dm_game_main's loop for a 2P VS game:
 *   dm_game_main2()               logic, both players (pause, state machine, results)
 *   dm_game_graphic_onDoneSawp()  joyProcCore (pad read + auto-repeat), dm_make_key,
 *                                 key_control_main per player (rotate, move, drop)
 *   countdown                     what dm_game_graphic_effect does with count_down_ctrl
 * The countdown length is LWS animation data, not code: COUNTDOWN_FRAMES stands in for
 * it until it is measured (SPEC 4.8, UNVERIFIED), and the engine uses the same value.
 *
 * Two scripted players ("bots", our code, seeded per movie) produce the pad input from
 * the oracle's own state, so the movies contain real play: aimed placements, DAS holds,
 * soft drops, rotations against walls, chains, garbage, top-outs and pauses.
 *
 * Output (a movie file, text):
 *   cfg ...                 the match settings
 *   i <tick> <p1> <p2>      pad bitmasks (N64 layout) from this tick on
 *   n <tick>                the harness started the next round after this tick
 *   b <tick> <p> x,y,c ...  pills set into bottle p after this tick (glue_junk, glue_chain4;
 *                           c = -1 clears the cell)
 *   h <tick> <hash>         rolling FNV-1a 64 of every tick's state dump so far
 *   end <ticks> <hash> ...  ticks run, final hash and the result
 * With --dump A B the per-tick state dumps of ticks A..B go to stderr. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "glue.h"

static uint64_t fnv(uint64_t h, const char *p)
{
    for (; *p; p++) { h ^= (uint8_t)*p; h *= 0x100000001b3ULL; }
    return h;
}

static void run(const GlueCfg *mv, FILE *f, int dump_from, int dump_to)
{
    glue_start(mv);
    fprintf(f, "cfg mode %d vl %d %d speed %d %d gl %d %d wins %d seed %d salt %u countdown %d\n", mv->mode,
            mv->vl[0], mv->vl[1], mv->speed[0], mv->speed[1], mv->gl[0], mv->gl[1], mv->wins, mv->seed, mv->salt,
            COUNTDOWN_FRAMES);
    uint64_t h = 0xcbf29ce484222325ULL;
    unsigned short pads[2], last[2] = { 0xFFFF, 0xFFFF };
    int junked = 0, prev_mode0 = 0, chain_sets = 0, rounds = 1, garbage = 0, chains = 0, pauses = 0, c3 = 0, c4 = 0, draws = 0, tick;
    for (tick = 0; tick < mv->max_ticks; tick++) {
        glue_pads(pads);
        if (pads[0] != last[0] || pads[1] != last[1]) {
            fprintf(f, "i %d %x %x\n", tick, pads[0], pads[1]);
            last[0] = pads[0];
            last[1] = pads[1];
        }
        int ret = glue_tick();
        glue_stats(&garbage, &chains, &pauses, &c3, &c4, &draws);
        const char *d = glue_dump();
        h = fnv(h, d);
        if (tick >= dump_from && tick <= dump_to) fprintf(stderr, "== tick %d\n%s", tick, d);
        if (tick % 60 == 59) fprintf(f, "h %d %016llx\n", tick, (unsigned long long)h);
        if (ret != 0) { fprintf(stderr, "%s: unexpected main2 result %d at %d\n", mv->name, ret, tick); exit(1); }
        if (mv->junk && mv->junk != 4 && glue_started() && !junked) {
            int cells[128][3];
            for (int p = 0; p < 2; p++) {
                int n = mv->junk == 3 ? glue_thin(p, cells, 3 + p * 2) : mv->junk == 2 ? glue_chain4(p, cells) : glue_junk(p, cells, (unsigned)(mv->seed * 31 + tick * 7 + p));
                fprintf(f, "b %d %d", tick, p);
                for (int k = 0; k < n; k++) fprintf(f, " %d,%d,%d", cells[k][0], cells[k][1], cells[k][2]);
                fprintf(f, "\n");
            }
            junked = 1;
        }
        /* junk mode 4: player 1 gets a four-line attack ready for each of its first
         * capsules, faster than a slow player 2 can drop the garbage it receives, so
         * attacks arrive while garbage is still pending (dm_set_attack_2p's other branch). */
        if (mv->junk == 4 && glue_started()) {
            int m0 = glue_mode(0);
            if (m0 == 4 && prev_mode0 != 4 && chain_sets < 8) {
                int cells[128][3];
                int n = glue_chain4(0, cells);
                fprintf(f, "b %d 0", tick);
                for (int k = 0; k < n; k++) fprintf(f, " %d,%d,%d", cells[k][0], cells[k][1], cells[k][2]);
                fprintf(f, "\n");
                chain_sets++;
            }
            prev_mode0 = m0;
        }
        int rs = glue_round_state();
        if (rs == 2) { tick++; break; }
        if (rs == 1) {
            glue_next_round();
            junked = 0;
            fprintf(f, "n %d\n", tick);
            rounds++;
        }
    }
    fprintf(f, "end %d %016llx wins %d %d\n", tick, (unsigned long long)h, glue_win(0), glue_win(1));
    fprintf(stderr, "%-12s ticks %6d rounds %d wins %d-%d attacks %d (3-line %d, 4-line %d) garbage %d pauses %d draws %d\n",
            mv->name, tick, rounds, glue_win(0), glue_win(1), chains, c3, c4, garbage, pauses, draws);
}

static const GlueCfg movies[] = {
    /* name        mode vl          speed     gl        wins seed        salt       skill     soft          noise       pause mirror junk max */
    { "normal-a",    0, { 5, 5 },   { 1, 1 }, { 0, 0 }, 3, 1234,       4242,      { 3, 3 }, { 60, 60 },   { 2, 2 },   0, 0, 0, 12000 },
    { "normal-b",    0, { 10, 14 }, { 0, 2 }, { 0, 0 }, 2, -77,        9,         { 3, 2 }, { 90, 20 },   { 5, 0 },   0, 0, 0, 12000 },
    { "normal-c",    0, { 0, 20 },  { 2, 2 }, { 0, 0 }, 3, 0x7FFF0001, 123456789, { 3, 3 }, { 100, 100 }, { 0, 0 },   0, 0, 0, 12000 },
    { "normal-d",    0, { 20, 20 }, { 1, 0 }, { 0, 0 }, 1, 555,        1,         { 2, 3 }, { 0, 50 },    { 10, 3 },  0, 0, 0, 12000 },
    { "quick",       0, { 0, 1 },   { 2, 2 }, { 0, 0 }, 3, 42,         4000000000u, { 3, 3 }, { 100, 100 }, { 0, 0 }, 0, 0, 0, 20000 },
    { "quick-b",     0, { 2, 0 },   { 0, 1 }, { 0, 0 }, 3, 1000003,    17,        { 3, 3 }, { 70, 90 },   { 1, 1 },   0, 0, 0, 20000 },
    { "slow-taps",   0, { 3, 8 },   { 0, 0 }, { 0, 0 }, 2, 31337,      77,        { 1, 1 }, { 0, 0 },     { 0, 0 },   0, 0, 0, 12000 },
    { "mash",        0, { 12, 12 }, { 2, 1 }, { 0, 0 }, 2, 8,          5,         { 0, 1 }, { 30, 30 },   { 120, 60 }, 0, 0, 0, 12000 },
    { "pause",       0, { 7, 9 },   { 1, 1 }, { 0, 0 }, 1, 4040,       3,         { 3, 3 }, { 50, 50 },   { 0, 0 },   4, 0, 0, 12000 },
    { "mirror",      0, { 2, 2 },   { 1, 1 }, { 0, 0 }, 3, 777,        8,         { 3, 3 }, { 80, 80 },   { 0, 0 },   0, 1, 0, 20000 },
    { "mirror-top",  0, { 15, 15 }, { 2, 2 }, { 0, 0 }, 2, 12,         8,         { 0, 0 }, { 0, 0 },     { 0, 0 },   0, 1, 0, 12000 },
    { "flash",       1, { 6, 9 },   { 1, 2 }, { 0, 1 }, 3, 99,         11,        { 3, 3 }, { 60, 60 },   { 2, 2 },   0, 0, 0, 12000 },
    { "flash-hi",    1, { 12, 12 }, { 2, 2 }, { 2, 2 }, 2, -5,         12,        { 2, 3 }, { 80, 40 },   { 5, 5 },   0, 0, 0, 12000 },
    { "ta",          3, { 9, 6 },   { 1, 1 }, { 1, 0 }, 2, 2024,       13,        { 3, 3 }, { 70, 70 },   { 1, 1 },   0, 0, 0, 16000 },
    { "ta-top",      3, { 12, 12 }, { 2, 2 }, { 2, 2 }, 1, 6,          14,        { 0, 0 }, { 0, 0 },     { 30, 30 }, 0, 0, 0, 16000 },
    { "ta-mirror",   3, { 6, 6 },   { 0, 0 }, { 0, 0 }, 2, 31,         15,        { 3, 3 }, { 90, 90 },   { 0, 0 },   0, 1, 0, 30000 },
    { "junk",        0, { 4, 6 },   { 1, 1 }, { 0, 0 }, 3, 5150,       21,        { 3, 3 }, { 60, 60 },   { 0, 0 },   0, 0, 1, 20000 },
    { "junk-b",      0, { 10, 10 }, { 2, 0 }, { 0, 0 }, 3, -31,        22,        { 3, 2 }, { 90, 30 },   { 2, 4 },   0, 0, 1, 20000 },
    { "junk-flash",  1, { 6, 6 },   { 1, 1 }, { 0, 0 }, 3, 909,        23,        { 3, 3 }, { 60, 60 },   { 0, 0 },   0, 0, 1, 20000 },
    { "junk-mirror", 0, { 8, 8 },   { 0, 0 }, { 0, 0 }, 3, 4,          24,        { 3, 3 }, { 50, 50 },   { 0, 0 },   0, 1, 1, 20000 },
    { "junk-c",      0, { 3, 9 },   { 1, 2 }, { 0, 0 }, 3, 77,         26,        { 2, 2 }, { 50, 70 },   { 1, 1 },   0, 0, 1, 20000 },
    { "junk-d",      1, { 9, 12 },  { 0, 1 }, { 1, 2 }, 2, -9999,      27,        { 2, 1 }, { 80, 80 },   { 0, 3 },   0, 0, 1, 20000 },
    { "chain4",      0, { 0, 3 },   { 1, 1 }, { 0, 0 }, 3, 2718,       28,        { 3, 3 }, { 100, 100 }, { 0, 0 },   0, 0, 2, 20000 },
    { "chain4-b",    0, { 5, 5 },   { 2, 0 }, { 0, 0 }, 2, 3141,       29,        { 3, 3 }, { 0, 50 },    { 0, 0 },   0, 0, 2, 20000 },
    { "ta-clear",    3, { 6, 9 },   { 1, 2 }, { 0, 1 }, 3, 1618,       30,        { 3, 3 }, { 90, 60 },   { 0, 0 },   0, 0, 3, 30000 },
    { "ta-clear-b",  3, { 12, 12 }, { 0, 0 }, { 2, 2 }, 2, 1414,       31,        { 3, 3 }, { 30, 90 },   { 1, 0 },   0, 1, 3, 30000 },
    { "pending",     0, { 5, 5 },   { 2, 0 }, { 0, 0 }, 3, 60606,      32,        { 3, 1 }, { 100, 0 },   { 0, 0 },   0, 0, 1, 20000 },
    { "pending-b",   0, { 9, 2 },   { 2, 0 }, { 0, 0 }, 3, 70707,      33,        { 3, 1 }, { 100, 0 },   { 0, 0 },   0, 0, 4, 20000 },
    { "junk-ta",     3, { 9, 9 },   { 1, 2 }, { 1, 1 }, 2, 123,        25,        { 3, 3 }, { 80, 80 },   { 0, 0 },   0, 0, 1, 30000 },
};

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s <outdir> [name [dumpfrom dumpto]]\n", argv[0]); return 2; }
    for (unsigned k = 0; k < sizeof movies / sizeof movies[0]; k++) {
        if (argc > 2 && strcmp(argv[2], movies[k].name)) continue;
        char path[512];
        snprintf(path, sizeof path, "%s/%s.dmm", argv[1], movies[k].name);
        /* One process per movie: the original's globals carry over between games (as on the
         * console), so a movie must not depend on which ones ran before it. */
        pid_t pid = fork();
        if (pid == 0) {
            FILE *f = fopen(path, "w");
            if (!f) { perror(path); _exit(1); }
            fprintf(f, "# %s: generated by tools/oracle/trace_oracle.c from the decomp's own game code\n", movies[k].name);
            run(&movies[k], f, argc > 4 ? atoi(argv[3]) : -1, argc > 4 ? atoi(argv[4]) : -2);
            fclose(f);
            _exit(0);
        }
        int status;
        if (pid < 0 || waitpid(pid, &status, 0) != pid || !WIFEXITED(status) || WEXITSTATUS(status)) return 1;
    }
    return 0;
}
