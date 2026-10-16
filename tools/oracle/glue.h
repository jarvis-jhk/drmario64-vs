/* Interface between the trace oracle's driver and its decomp side (glue.c). */
#pragma once
#define COUNTDOWN_FRAMES 164 /* placeholder for the ETC_LWS_INDEX_9 length, SPEC 4.8 */

typedef struct GlueCfg {
    const char *name;
    int mode, vl[2], speed[2], gl[2], wins;
    int seed;
    unsigned salt;
    int skill[2], soft[2], noise[2], pause, mirror, junk;
    int max_ticks;
} GlueCfg;

void glue_start(const GlueCfg *cfg);
void glue_pads(unsigned short pads[2]); /* the bots' input for the coming tick */
int glue_tick(void);                    /* dm_game_main2's result: always dm_ret_null (0) here */
const char *glue_dump(void);
void glue_stats(int *garbage, int *chains, int *pauses, int *chains3, int *chains4, int *draws);
int glue_round_state(void);
void glue_next_round(void);
int glue_win(int p);
int glue_started(void);
int glue_junk(int p, int cells[][3], unsigned seed);
int glue_chain4(int p, int cells[][3]);
int glue_thin(int p, int cells[][3], int keep);
int glue_mode(int p);
