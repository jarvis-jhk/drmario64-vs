/* The decomp side of the trace oracle: dm_game_main.c compiled into this file (it keeps
 * its watchGame struct private), the stubs that do something, the state dump and the
 * scripted players. Only decomp headers here: its own libc headers clash with the
 * system's, so trace_oracle.c (the driver, file output) is a separate file. */
#include "dm_game_main_oracle.c" /* build_trace.sh: dm_game_main.c minus framebuffer drawing */
#include "dm_virus_init.h"
#include "calc.h"
#include "joy.h"

typedef __builtin_va_list va_list;
#define va_start(ap, x) __builtin_va_start(ap, x)
#define va_end(ap) __builtin_va_end(ap)
int vsnprintf(char *, unsigned long, const char *, va_list);
double fmod(double, double);
typedef unsigned long long uint64_t;
typedef unsigned int uint32_t;
typedef unsigned char uint8_t;

#include "glue.h"

/* ---- the few stubs that do something ---- */
static uint64_t time_salt;
static u16 pad_now[4];
static int se_log[64], se_n;

s32 WrapI(s32 start, s32 end, s32 value)
{
    s32 size = end - start;
    value = (value - start) % size;
    if (value < 0) value += size;
    return value + start;
}
f32 WrapF(f32 start, f32 end, f32 value)
{
    if (start <= value && value < end) return value;
    f32 size = end - start;
    value = fmod((f64)(value - start), (f64)size);
    if (value < 0.0f) value += size;
    return value + start;
}
OSTime osGetTime(void) { return time_salt; }
void osContGetReadData(OSContPad *pad)
{
    for (int i = 0; i < 4; i++) { memset(&pad[i], 0, sizeof pad[i]); pad[i].button = pad_now[i]; }
}
void dm_snd_play_in_game(SndIndex i)
{
    if (se_n < 64) se_log[se_n++] = i;
}

/* ---- state dump: the exact text the engine's dm_vs_dump() must produce ---- */
static char dumpbuf[16384];
static int dlen;
static void out(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void out(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    dlen += vsnprintf(dumpbuf + dlen, sizeof dumpbuf - dlen, fmt, ap);
    va_end(ap);
}

static void dump_cap(const char *tag, const struct_game_state_data_now_cap *c)
{
    out("%s %d,%d %d,%d g%d,%d p%d,%d f%d%d%d\n", tag, c->pos_x[0], c->pos_x[1], c->pos_y[0], c->pos_y[1],
        c->casel_g[0], c->casel_g[1], c->capsel_p[0], c->capsel_p[1], c->capsel_flg_0, c->capsel_flg_1,
        c->capsel_flg_2);
}

static const char *dump_state(void)
{
    struct_watchGame *st = watchGame;
    dlen = 0;
    out("G cd%d sg%d wc%d,%d et%d,%d ws%d l3%d,%d bpm%d gt%d\n", st->count_down_ctrl, st->started_game_flg,
        st->win_count[0], st->win_count[1], st->effect_timer[0], st->effect_timer[1], st->warning_se_count,
        st->last_3_se_flg, st->last_3_se_cnt, st->bgm_bpm_flg[0], evs_game_time);
    out("Q qp%d qs%d rt%d,%d rs%d,%d\n", st->query_pause_player_no, st->query_play_pause_se, st->retry_type[0],
        st->retry_type[1], st->retry_select[0], st->retry_select[1]);
    for (int p = 0; p < 2; p++) {
        struct_game_state_data *s = &game_state_data[p];
        out("P%d m%d c%d s%d mo%d co%d vo%d vn%d sp%d cn%d sc%d sv%d sm%d mg%d,%d mse%d ea%d ec%d ev%d,%d"
            " ch%d cl%d,%d col%d,%d,%d,%d bu%d w%d r%d go%d sco%u te%d tc%d\n",
            p, s->mode_now, s->cnd_now, s->cnd_static, s->mode_old, s->cnd_old, s->virus_order_number,
            s->virus_number, s->cap_speed, s->cap_count, s->cap_speed_count, s->cap_speed_vec, s->cap_speed_max,
            s->cap_magazine_cnt, s->cap_magazine_save, s->cap_move_se_flg, s->erase_anime, s->erase_anime_count,
            s->erase_virus_count, s->erase_virus_count_old, s->chain_count, s->chain_line, s->chain_line_max,
            s->chain_color[0], s->chain_color[1], s->chain_color[2], s->chain_color[3], s->black_up_count,
            s->warning_se_flag, s->flg_retire, s->flg_game_over, s->game_score, s->total_erase_count,
            s->total_chain_count);
        TimeAttackResult *ta = &st->timeAttackResult[p];
        out("TA l%d b%d f%d,%d n%d t%u c%d v%d s%u d%u r%u\n", ta->level, ta->bonus, ta->flow, ta->frame, ta->cnt,
            ta->time, ta->combo, ta->virus, ta->score, ta->scoreDelta, ta->result);
        dump_cap("cap", &s->now_cap);
        dump_cap("nxt", &s->next_cap);
        out("atk");
        for (int i = 0; i < 16; i++) out(" %x:%x", s->cap_attack_work[i].unk_0, s->cap_attack_work[i].unk_2);
        out("\nfl%d", s->flash_virus_count);
        for (int i = 0; i < s->flash_virus_count; i++)
            out(" %d,%d,%d", s->flash_virus_pos[i].column, s->flash_virus_pos[i].row, s->flash_virus_pos[i].color);
        out("\nmap");
        for (int i = 0; i < GAME_MAP_ROWS * GAME_MAP_COLUMNS; i++) {
            GameMapCell *c = &game_map_data[p][i];
            if (i % 8 == 0) out("\n");
            /* A live virus's capsel_m_g is its animation frame, which the graphics thread
             * advances (dm_virus_anime): not logic, so it is masked. */
            int g = (c->capsel_m_flg[0] && c->capsel_m_flg[4] >= 0 && !c->capsel_m_flg[2]) ? -1 : c->capsel_m_g;
            out(" %d,%d,%d,%d,%d,%d,%d,%d,%d,%d", c->pos_m_x, c->pos_m_y, g, c->capsel_m_p, c->capsel_m_flg[0],
                c->capsel_m_flg[1], c->capsel_m_flg[2], c->capsel_m_flg[3], c->capsel_m_flg[4], c->capsel_m_flg[5]);
        }
        out("\nJ gam%x cur%x hold%x cnt", joygam[p], joycur[main_joy[p]], gControllerHoldButtons[main_joy[p]]);
        for (int i = 0; i < 16; i++) out(" %d", joycnt[main_joy[p]][i]);
        out("\n");
    }
    out("SE");
    for (int i = 0; i < se_n; i++) out(" %d", se_log[i]);
    out("\n");
    return dumpbuf;
}

static uint64_t fnv(uint64_t h, const char *p)
{
    for (; *p; p++) { h ^= (uint8_t)*p; h *= 0x100000001b3ULL; }
    return h;
}

/* ---- the countdown, as dm_game_graphic_effect drives it (dgm:6689) ---- */
static void countdown_frame(void)
{
    struct_watchGame *st = watchGame;
    for (int p = 0; p < 2; p++) {
        EnumGameStateDataCnd c = game_state_data[p].cnd_now;
        if ((c != dm_cnd_wait && c != dm_cnd_init) || st->count_down_ctrl < 0) continue;
        s32 lo = st->count_down_ctrl / 2, s = lo / 48;
        if (lo >= COUNTDOWN_FRAMES) { /* disp_count_logo reports the animation's end */
            st->count_down_ctrl = -1;
            if (p == 0) dm_snd_play_in_game(SND_INDEX_73);
        } else {
            if (p == 0 && s * 0x30 == lo - 0x14 && s < 3) dm_snd_play_in_game(SND_INDEX_72);
            st->count_down_ctrl++;
        }
    }
}

/* ---- scripted players ---- */
typedef struct Bot {
    uint32_t rng;
    int skill;       /* 0..3: how carefully it aims */
    int soft;        /* percent of the time it soft-drops once aimed */
    int noise;       /* per-mille chance per frame of random buttons */
    int pause;       /* per-mille chance per frame of START (only bot 0 of pause movies) */
    int plan_cap /* a new capsule needs a plan */, tx, trot, stuck, lastx, hold_noise;
    u16 noise_pad, prev;
} Bot;

static uint32_t brand(Bot *b) { b->rng = b->rng * 1103515245u + 12345u; return b->rng >> 8; }

static int cell(GameMapCell *m, int x, int y) /* y 1..16; -1 empty, else colour */
{
    if (x < 0 || x > 7 || y < 1 || y > 16) return -2;
    GameMapCell *c = &m[(y - 1) * 8 + x];
    return c->capsel_m_flg[0] ? c->capsel_m_flg[3] : -1;
}

/* Score one placement: halves (x0,y0,c0) and (x1,y1,c1). */
static int score_place(GameMapCell *m, int aim, int x0, int y0, int c0, int x1, int y1, int c1)
{
    int sc = 0, xs[2] = { x0, x1 }, ys[2] = { y0, y1 }, cs[2] = { c0, c1 };
    for (int h = 0; h < 2; h++) {
        static const int dx[2] = { 1, 0 }, dy[2] = { 0, 1 };
        for (int d = 0; d < 2; d++) {
            int run = 1, viruses = 0;
            for (int sgn = -1; sgn <= 1; sgn += 2) {
                for (int k = 1; k < 4; k++) {
                    int x = xs[h] + sgn * k * dx[d], y = ys[h] + sgn * k * dy[d];
                    int c = cell(m, x, y);
                    if (x == xs[!h] && y == ys[!h]) c = cs[!h];
                    if (c != cs[h]) break;
                    run++;
                    if (x >= 0 && x < 8 && y >= 1 && y <= 16 && m[(y - 1) * 8 + x].capsel_m_flg[4] >= 0) viruses++;
                }
            }
            sc += run >= 4 ? 400 + viruses * 150 : run * run * 8 + viruses * 10;
        }
        sc += ys[h] * 6;
        /* What it lands on: same colour good, a virus of it better, burying one bad. */
        if (aim && !(xs[!h] == xs[h] && ys[!h] == ys[h] + 1)) {
            int below = cell(m, xs[h], ys[h] + 1);
            int vir = below >= 0 && m[ys[h] * 8 + xs[h]].capsel_m_flg[4] >= 0;
            if (below == cs[h]) sc += vir ? 140 : 60;
            else if (below >= 0) sc -= vir ? 90 : 40;
        }
    }
    return sc;
}

static void plan(Bot *b, struct_game_state_data *s, GameMapCell *m)
{
    int a = s->now_cap.capsel_p[0], bb = s->now_cap.capsel_p[1];
    int best = -1 << 30;
    b->tx = 3; b->trot = 0;
    for (int rot = 0; rot < 4; rot++) {
        for (int x = 0; x < (rot & 1 ? 8 : 7); x++) {
            int y = 1;
            if (rot & 1) {
                if (cell(m, x, 1) != -1) continue;
                while (y < 16 && cell(m, x, y + 1) == -1) y++;
            } else {
                if (cell(m, x, 1) != -1 || cell(m, x + 1, 1) != -1) continue;
                while (y < 16 && cell(m, x, y + 1) == -1 && cell(m, x + 1, y + 1) == -1) y++;
            }
            int sc;
            switch (rot) {
            case 0: sc = score_place(m, b->skill >= 3, x, y, a, x + 1, y, bb); break;
            case 1: sc = score_place(m, b->skill >= 3, x, y, bb, x, y - 1, a); break;
            case 2: sc = score_place(m, b->skill >= 3, x, y, bb, x + 1, y, a); break;
            default: sc = score_place(m, b->skill >= 3, x, y, a, x, y - 1, bb); break;
            }
            sc += (int)(brand(b) % (unsigned)(1 + 400 / (1 + b->skill * 3)));
            if (sc > best) { best = sc; b->tx = x; b->trot = rot; }
        }
    }
    b->stuck = 0;
    b->lastx = -1;
}

static int cur_rot(struct_game_state_data *s, int a, int bb)
{
    const struct_game_state_data_now_cap *c = &s->now_cap;
    if (c->pos_x[0] != c->pos_x[1]) return (c->capsel_p[0] == a && c->capsel_p[1] == bb) ? 0 : 2;
    return (c->capsel_p[0] == bb && c->capsel_p[1] == a) ? 1 : 3;
}

static u16 bot_pad(Bot *b, int p)
{
    struct_game_state_data *s = &game_state_data[p];
    GameMapCell *m = game_map_data[p];
    u16 pad = 0;

    if (b->hold_noise > 0) {
        b->hold_noise--;
        pad = b->noise_pad;
    } else if ((int)(brand(b) % 1000) < b->noise) {
        static const u16 btn[] = { A_BUTTON, B_BUTTON, L_JPAD, R_JPAD, D_JPAD, U_JPAD, Z_TRIG,
                                   L_JPAD | D_JPAD, R_JPAD | A_BUTTON, A_BUTTON | B_BUTTON };
        b->noise_pad = btn[brand(b) % 10];
        b->hold_noise = (int)(brand(b) % 30);
        pad = b->noise_pad;
    } else if (s->mode_now == dm_mode_down && s->cnd_static == dm_cnd_wait && s->now_cap.capsel_flg_0) {
        if (b->plan_cap) {
            b->plan_cap = 0;
            plan(b, s, m);
        }
        int a = CAPSMAGAZINE_GET_A(CapsMagazine[s->cap_magazine_save]);
        int bb = CAPSMAGAZINE_GET_B(CapsMagazine[s->cap_magazine_save]);
        int x = s->now_cap.pos_x[0] < s->now_cap.pos_x[1] ? s->now_cap.pos_x[0] : s->now_cap.pos_x[1];
        int r = cur_rot(s, a, bb);
        if (a == bb) r &= 1;
        int tr = a == bb ? (b->trot & 1) : b->trot;
        if (x == b->lastx) b->stuck++; else { b->stuck = 0; b->lastx = x; }
        if (r != tr && b->stuck < 60 && s->now_cap.pos_y[0] > 0) {
            if (!(b->prev & (A_BUTTON | B_BUTTON))) pad |= (brand(b) % 4) ? A_BUTTON : B_BUTTON;
        }
        if (x < b->tx && b->stuck < 60) pad |= R_JPAD;
        else if (x > b->tx && b->stuck < 60) pad |= L_JPAD;
        else if (r == tr || b->stuck >= 60) {
            if ((int)(brand(b) % 100) < b->soft) pad |= D_JPAD;
        }
        /* Sometimes tap instead of hold, so both DAS paths stay in the movies. */
        if ((pad & (L_JPAD | R_JPAD)) && (b->prev & (L_JPAD | R_JPAD)) && b->skill == 1 && brand(b) % 3 == 0)
            pad &= ~(L_JPAD | R_JPAD);
    } else if ((int)(brand(b) % 100) < 3) {
        pad = (brand(b) & 1) ? L_JPAD : R_JPAD; /* charge DAS while nothing falls */
    }
    if (b->pause && (int)(brand(b) % 1000) < b->pause) pad |= START_BUTTON;
    if (s->mode_now != dm_mode_down) b->plan_cap = 1; /* plan again for the next capsule */
    b->prev = pad;
    return pad;
}


static Bot bot[2];

void glue_start(const GlueCfg *mv)
{
    memset(game_state_data, 0, sizeof game_state_data);
    memset(joycnt, 0, sizeof joycnt);
    memset(gControllerPrevHoldButtons, 0, sizeof gControllerPrevHoldButtons);
    memset(pad_now, 0, sizeof pad_now);
    evs_gamesel = GSL_2PLAY;
    evs_playcnt = 2;
    evs_gamemode = mv->mode;
    evs_vs_count = mv->wins;
    evs_story_flg = 0;
    evs_gamespeed = 1;
    evs_game_time = 0;
    for (int i = 0; i < 4; i++) main_joy[i] = i;
    for (int p = 0; p < 2; p++) {
        game_state_data[p].player_type = PLAYERTYPE_0;
        game_state_data[p].virus_level = mv->vl[p];
        game_state_data[p].cap_def_speed = mv->speed[p];
        game_state_data[p].game_level = mv->gl[p];
        game_state_data[p].charNo = 0;
        game_state_data[p].team_no = p;
        memset(&bot[p], 0, sizeof bot[p]);
        /* mirror: both players get the same script, so equal bottles are played alike */
        bot[p].rng = (uint32_t)mv->seed * 2654435761u + (mv->mirror ? 0 : 977u * p) + 1;
        bot[p].skill = mv->skill[p];
        bot[p].soft = mv->soft[p];
        bot[p].noise = mv->noise[p];
        bot[p].pause = mv->pause;
        bot[p].plan_cap = 1;
    }
    time_salt = mv->salt;

    key_cntrol_init();
    dm_game_init_heap();
    /* First round: dm_game_init with the movie's seed (the replay path takes it as given). */
    watchGame->replayFlag = 1;
    watchGame->randSeed2 = mv->seed;
    dm_game_init(true);
    watchGame->replayFlag = 0;
}

void glue_pads(unsigned short pads[2])
{
    for (int p = 0; p < 2; p++) pads[p] = pad_now[p] = bot_pad(&bot[p], p);
}

int glue_tick(void)
{
    se_n = 0;
    DmMainCnt ret = dm_game_main2();
    dm_game_graphic_onDoneSawp();
    countdown_frame();
    /* dm_save_all queues the EEPROM write when a match is decided, and joyProcCore is
     * skipped (pads read as nothing) until that background task ends. That length is
     * the cartridge's; the remake saves nothing there and takes the write as finished
     * after the frame it was queued in. */
    watchGame->eep_rom_flg = false;
    return ret;
}

const char *glue_dump(void) { return dump_state(); }

void glue_stats(int *garbage, int *chains, int *pauses, int *chains3, int *chains4, int *draws)
{
    for (int i = 0; i < se_n; i++) {
        if (se_log[i] == 201) ++*chains3;
        if (se_log[i] == 202) ++*chains4;
    }
    if (dm_check_one_game_finish() && game_state_data[0].cnd_static == dm_cnd_draw &&
        watchGame->effect_timer[0] == 179) ++*draws;
    for (int p = 0; p < 2; p++) {
        if (game_state_data[p].cap_attack_work[0].unk_0 && game_state_data[p].mode_now == dm_mode_cap_set) ++*garbage;
        if (game_state_data[p].chain_line >= 2 && game_state_data[p].mode_now == dm_mode_cap_set) ++*chains;
    }
    if (game_state_data[0].cnd_static == dm_cnd_pause && (pad_now[0] & START_BUTTON)) ++*pauses;
}

int glue_win(int p) { return watchGame->win_count[p]; }

/* 0 playing, 1 round over (the harness starts the next one), 2 match over. Round over
 * means both result effects ran out: what choosing NEXT in the menu does
 * (dm_game_main2 on dm_ret_game_over). */
int glue_round_state(void)
{
    struct_watchGame *st = watchGame;
    if (!dm_check_one_game_finish() || st->effect_timer[0] || st->effect_timer[1]) return 0;
    return (st->win_count[0] == evs_vs_count || st->win_count[1] == evs_vs_count) ? 2 : 1;
}

void glue_next_round(void)
{
    if (evs_gamemode == GMD_TIME_ATTACK)
        for (int p = 0; p < 2; p++) game_state_data[p].game_score = 0;
    dm_game_init(true);
}

/* Junk: at the start of play, fill each column of a bottle from the bottom with single
 * pills of random colours up to a random height (runs of three, never four). Clearing
 * anything then drops loose pieces into new lines, so the movies get the multi-line
 * chains and big attacks that scripted placement alone rarely builds. The cells go to
 * the movie ("b" lines) and the engine test sets the same ones. Returns the count. */
static uint32_t junk_rng;
static int junk_color(GameMapCell *m, int x, int y)
{
    for (int tries = 0; tries < 6; tries++) {
        int c = (int)((junk_rng = junk_rng * 1103515245u + 12345u) >> 8) % 3, bad = 0;
        static const int dx[4] = { 1, 0, 1, 1 }, dy[4] = { 0, 1, 1, -1 };
        for (int d = 0; d < 2 && !bad; d++) {
            int run = 1;
            for (int sgn = -1; sgn <= 1; sgn += 2)
                for (int k = 1; k < 4; k++) {
                    int xx = x + sgn * k * dx[d], yy = y + sgn * k * dy[d];
                    if (cell(m, xx, yy) != c) break;
                    run++;
                }
            if (run >= 4) bad = 1;
        }
        if (!bad) return c;
    }
    return -1;
}

int glue_junk(int p, int cells[][3], unsigned seed)
{
    GameMapCell *m = game_map_data[p];
    int n = 0;
    junk_rng = seed;
    for (int x = 0; x < 8; x++) {
        int top = 7 + (int)((junk_rng = junk_rng * 1103515245u + 12345u) >> 8) % 7; /* fill up to y = top */
        for (int y = 16; y >= top; y--) {
            if (cell(m, x, y) != -1) continue;
            if (y < 16 && cell(m, x, y + 1) == -1) break;
            int c = junk_color(m, x, y);
            if (c < 0) break;
            set_map(m, x, y, 4, c);
            cells[n][0] = x;
            cells[n][1] = y;
            cells[n++][2] = c;
        }
    }
    return n;
}

int glue_started(void) { return watchGame->started_game_flg; }

/* A ready-made four-line attack for the first capsule (junk mode 2): columns 3 and 4
 * hold three of the capsule's left and right colours, row 13 three of each beside
 * them, so dropping it unrotated where it spawns completes two columns and two rows at
 * once. Colour -1 clears a cell (the capsule's path). */
int glue_chain4(int p, int cells[][3])
{
    GameMapCell *m = game_map_data[p];
    struct_game_state_data_now_cap *c = &game_state_data[p].now_cap;
    int a = c->capsel_p[0], b = c->capsel_p[1], n = 0;
    int o = 0;
    while (o == a || o == b) o++;           /* a third colour (o < 3 when a != b) */
    int o2 = (o + 1) % 3 == a ? (o + 2) % 3 : (o + 1) % 3;
    if (o2 == o) o2 = (o + 1) % 3;
#define PUT(x, y, col) do { if ((col) < 0) clear_map(m, x, y); else set_map(m, x, y, 4, col); \
        cells[n][0] = x; cells[n][1] = y; cells[n++][2] = col; } while (0)
    for (int y = 1; y <= 13; y++) { PUT(3, y, -1); PUT(4, y, -1); }
    for (int y = 14; y <= 16; y++) {
        int alt = y & 1;
        PUT(0, y, alt ? o : o2); PUT(1, y, alt ? o2 : o); PUT(2, y, alt ? o : o2);
        PUT(3, y, a); PUT(4, y, b);
        PUT(5, y, alt ? o2 : o); PUT(6, y, alt ? o : o2); PUT(7, y, alt ? o2 : o);
    }
    for (int x = 0; x < 3; x++) PUT(x, 13, a);
    for (int x = 5; x < 8; x++) PUT(x, 13, b);
#undef PUT
    return n;
}

/* Thin (junk mode 3): empty every virus cell but the lowest few, so a scripted player
 * clears a whole bottle within TIME ATTACK's three minutes. */
int glue_thin(int p, int cells[][3], int keep)
{
    GameMapCell *m = game_map_data[p];
    int n = 0, kept = 0;
    for (int i = 16 * 8 - 1; i >= 0; i--) {
        if (!m[i].capsel_m_flg[0] || m[i].capsel_m_flg[4] < 0) continue;
        if (kept < keep) { kept++; continue; }
        clear_map(m, i % 8, i / 8 + 1);
        cells[n][0] = i % 8;
        cells[n][1] = i / 8 + 1;
        cells[n++][2] = -1;
    }
    return n;
}

int glue_mode(int p) { return game_state_data[p].mode_now; }
