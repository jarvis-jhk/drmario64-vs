/* The 2P VS game: both bottles, the round state machine, input handling and the
 * round/match result, ported from the decomp's dm_game_main.c (2P paths), joy.c and
 * vr_init.c. Function comments name the original each piece follows; the per-tick order
 * is dm_game_main's loop (see dm_vs_tick). Pure and deterministic: input in, state out,
 * sound effects as a list of ids per tick. docs/SPEC.md has the rules in prose. */
#pragma once
#include "rng.h"
#include "setup.h"

#include <stddef.h>
#include <stdint.h>

/* N64 pad bits (the original's button masks). */
enum {
    DM_BTN_A = 0x8000, DM_BTN_B = 0x4000, DM_BTN_Z = 0x2000, DM_BTN_START = 0x1000,
    DM_BTN_UP = 0x0800, DM_BTN_DOWN = 0x0400, DM_BTN_LEFT = 0x0200, DM_BTN_RIGHT = 0x0100,
    DM_BTN_L = 0x0020, DM_BTN_R = 0x0010,
    DM_BTN_CUP = 0x0008, DM_BTN_CDOWN = 0x0004, DM_BTN_CLEFT = 0x0002, DM_BTN_CRIGHT = 0x0001,
};

/* State-machine values, numbered as in the original (EnumGameStateDataMode/Cnd). */
typedef enum DmPhase {
    DM_M_NULL, DM_M_INIT, DM_M_WAIT, DM_M_THROW, DM_M_DOWN, DM_M_DOWN_WAIT, DM_M_ERASE_CHECK,
    DM_M_ERASE_ANIME, DM_M_BALL_DOWN, DM_M_CAP_SET, DM_M_STAGE_CLEAR, DM_M_GAME_OVER, DM_M_BOTTOM_UP,
    DM_M_WIN, DM_M_WIN_RETRY, DM_M_LOSE, DM_M_LOSE_RETRY, DM_M_DRAW, DM_M_DRAW_RETRY, DM_M_TR_CHECK,
    DM_M_TRAINING, DM_M_TR_ERASE_CHECK, DM_M_TR_CAP_SET, DM_M_PAUSE, DM_M_PAUSE_RETRY, DM_M_NO_ACTION,
    DM_M_CLEAR_WAIT, DM_M_CLEAR_RESULT, DM_M_GOVER_WAIT, DM_M_GOVER_RESULT, DM_M_RETIRE_WAIT,
    DM_M_RETIRE_RESULT,
} DmPhase;

typedef enum DmCond {
    DM_C_NULL, DM_C_WAIT, DM_C_INIT, DM_C_STAGE_CLEAR, DM_C_GAME_OVER, DM_C_WIN, DM_C_WIN_RETRY,
    DM_C_WIN_RETRY_SC, DM_C_LOSE, DM_C_LOSE_RETRY, DM_C_LOSE_RETRY_SC, DM_C_DRAW, DM_C_DRAW_RETRY,
    DM_C_PAUSE, DM_C_PAUSE_RE, DM_C_PAUSE_RE_SC, DM_C_RETIRE, DM_C_TR_CHECK, DM_C_TRAINING,
    DM_C_CLEAR_WAIT, DM_C_CLEAR_RESULT, DM_C_GOVER_WAIT, DM_C_GOVER_RESULT, DM_C_RETIRE_WAIT,
    DM_C_RETIRE_RESULT,
} DmCond;

/* Sound effects (SndIndex). Character voices are reported as DM_SE_VOICE + k (k = 0..4:
 * chain 2/3/4+ lines, garbage received, round won); the voice set is the character's. */
enum {
    DM_SE_LAND_FALL = 55, DM_SE_VIRUS_ERASE = 56, DM_SE_SPEED_UP = 58, DM_SE_ATTACK_P1 = 59,
    DM_SE_PAUSE = 60, DM_SE_ERASE = 61, DM_SE_MENU_OK = 62, DM_SE_TA_TICK = 63, DM_SE_MENU_MOVE = 64,
    DM_SE_MOVE = 65, DM_SE_LAND = 66, DM_SE_ROTATE = 67, DM_SE_COUNT = 72, DM_SE_GO = 73,
    DM_SE_ATTACK_P2 = 75, DM_SE_TA_DONE = 78, DM_SE_WARNING = 79, DM_SE_LAST3 = 80,
    DM_SE_FLASH_ERASE = 102, DM_SE_VOICE = 200,
};

/* Countdown length in display frames (count_down_ctrl / 2), the ETC_LWS_INDEX_9
 * animation's length: asset data, UNVERIFIED placeholder (SPEC 4.8). The trace oracle
 * uses the same value. */
#define DM_COUNTDOWN_FRAMES 164

enum { DM_MAP_ROWS = 17, DM_MAP_CELLS = DM_MAP_ROWS * DM_COLS };

/* GameMapCell. Row r (0-based) holds y = r + 1; row 16 is never filled. */
typedef struct DmCell {
    int8_t x, y;     /* pos_m_x, pos_m_y */
    int8_t g;        /* capsel_m_g: 0 top / 1 bottom / 2 left / 3 right half, 4 single, 5 erasing
                        pill, 13+ erasing virus, else the virus animation frame */
    int8_t p;        /* capsel_m_p: colour, +3 when darkened */
    int8_t used;     /* capsel_m_flg[0] */
    int8_t falls;    /* capsel_m_flg[1] */
    int8_t erasing;  /* capsel_m_flg[2] */
    int8_t color;    /* capsel_m_flg[3] */
    int8_t virus;    /* capsel_m_flg[4]: virus colour, -1 for a pill */
    int8_t flg5;     /* capsel_m_flg[5]: never set */
} DmCell;

/* struct_game_state_data_now_cap: half 0 is the left (horizontal) or bottom (vertical). */
typedef struct DmCap {
    int8_t x[2], y[2], g[2], p[2];
    int8_t live, falling, flg2; /* capsel_flg_0/1/2 */
} DmCap;

typedef struct DmTaResult { /* TimeAttackResult */
    int32_t level, bonus, flow, frame, cnt;
    uint32_t time;
    int32_t combo, virus;
    uint32_t score, delta, result;
} DmTaResult;

typedef struct DmPlayer {
    int mode, cnd, cnd_static, mode_old, cnd_old;
    int virus_order, virus_count;      /* virus_order_number, virus_number */
    int virus_level, speed_setting, game_level; /* virus_level, cap_def_speed, game_level */
    int cap_speed, cap_count, cap_speed_count, cap_speed_vec, cap_speed_max;
    int mag_cnt, mag_save, move_se;    /* cap_magazine_cnt/_save, cap_move_se_flg */
    int erase_anime, erase_anime_count, erase_virus, erase_virus_old;
    int chain_count, chain_line, chain_line_max;
    uint8_t chain_color[4];
    int black_up, warning, retire, game_over; /* black_up_count, warning_se_flag, flg_retire, flg_game_over */
    uint32_t score;
    int total_erase, total_chain;
    uint16_t attack[16][2];            /* cap_attack_work: garbage pattern, sender */
    int flash_count;
    struct { int column, row, color; } flash[DM_FLASH_MAX];
    int flash_bak[DM_FLASH_MAX];
    int virus_anime, virus_anime_vec, virus_anime_count, virus_anime_max, virus_anime_speed;
    DmCap cap, next;
    DmCell map[DM_MAP_CELLS];
    DmVirusMap vmap;                   /* this round's layout; order gets 0x80 marks as it appears */
} DmPlayer;

typedef struct DmVsConfig {
    DmMode mode;
    DmPlayerSetup p[2];
    int speed[2];   /* cap_def_speed 0..2 */
    int wins;       /* evs_vs_count: rounds to win the match, 1..3 */
} DmVsConfig;

typedef struct DmVs {
    DmVsConfig cfg;
    DmRng rng;
    uint8_t magazine[256];
    DmPlayer pl[2];
    /* struct_watchGame, the parts the 2P game logic uses */
    int countdown, started, win_count[2], effect_timer[2], warning_se_count;
    int last3_flag, last3_count, bgm_bpm[2];
    uint32_t game_time;   /* evs_game_time, frames */
    int query_pause, query_pause_se;
    int retry_type[2], retry_select[2], retry_result[2]; /* the result is set by the menu display */
    DmTaResult ta[2];
    uint32_t salt;        /* osGetTime() in dm_game_init's reseed; the frontend sets it per round */
    int ghost[2];         /* visible_fall_point: C buttons toggle the landing preview */
    int saving;           /* eep_rom_flg: the match record is being written */
    int result;           /* this tick's dm_game_main2 result (DmVsResult) */
    /* joy.c */
    uint16_t joycur[4], pressed[4], prev_hold[4], hold[4], joyflg[4], joygam[4];
    uint16_t joycnt[4][16];
    uint8_t joygmf[4];
    uint16_t rep_first, rep_next; /* joycur1, joycur2: 12 and 6 frames */
    /* sound effects of the current tick */
    int se[64], se_n;
} DmVs;

/* What a tick's menus decided (DmMainCnt from dm_game_main_2p). The next round and a
 * restart of the match are started inside the tick, as dm_game_main2 does. */
enum { DM_VS_PLAYING = 0, DM_VS_RESTARTED = 2, DM_VS_REPLAY = 9, DM_VS_MATCH_OVER = -1, DM_VS_QUIT = -2 };

/* Start a match: dm_game_main's setup and dm_game_init for the first round, seeded with
 * seed (randSeed2). */
void dm_vs_start(DmVs *g, const DmVsConfig *cfg, int32_t seed, uint32_t salt);
/* One 60 Hz frame with these pads (index 0 and 1). */
void dm_vs_tick(DmVs *g, uint16_t pad0, uint16_t pad1);
/* 0 playing, 1 round decided and its result shown (the menu may start the next round),
 * 2 match decided and shown. */
int dm_vs_round_state(const DmVs *g);
/* The next round of the match (dm_game_init(true)); seeds from the RNG and g->salt. */
void dm_vs_next_round(DmVs *g);
/* Put a single pill of colour 0..2 at (x, y), y = 1..16, or empty the cell (colour -1):
 * for test and practice setups. */
void dm_vs_set_pill(DmVs *g, int player, int x, int y, int color);
/* The state dump the trace oracle writes for the same tick (tests/movies). */
size_t dm_vs_dump(const DmVs *g, char *buf, size_t size);
