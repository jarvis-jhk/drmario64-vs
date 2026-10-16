#include "vs.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* main1x.c tables */
static const uint8_t bonus_wait[3][3] = { { 2, 2, 3 }, { 1, 1, 1 }, { 1, 0, 1 } }; /* BonusWait[y-1][speed] */
static const int8_t game_speed[4] = { 0, 10, 15, 55 };                            /* GameSpeed */
static const uint8_t fall_speed[56] = {                                           /* FallSpeed */
    39, 37, 35, 33, 31, 29, 27, 25, 23, 21, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 9, 8, 8, 7, 7, 6, 6,
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 4, 4, 4, 4, 4, 3, 3, 3, 3, 3, 2, 2, 2, 2, 2, 1,
};
static const uint8_t score_table[3][6] = { /* Score1p, ×100 */
    { 1, 2, 4, 8, 16, 32 }, { 2, 4, 8, 16, 32, 64 }, { 3, 6, 12, 24, 48, 96 },
};
static const uint8_t virus_anime_table[3][4] = { { 7, 8, 16, 17 }, { 9, 10, 18, 19 }, { 11, 12, 20, 21 } };
static const int rotate_table[4] = { 1, 3, 4, 2 };   /* rotate_table_474 */
static const int rotate_mtx[6] = { 2, 0, 3, 1, 2, 0 }; /* rotate_mtx_475 */
static const int retry_items[6] = { 2, 3, 2, 3, 2, 3 }; /* _retryMenu_itemCount */
static const int ta_points[3] = { 10, 20, 30 };        /* _n_1199 */

enum { /* DmMainCnt */
    RET_GAME_END = -2, RET_GAME_OVER = -1, RET_NULL = 0, RET_RETRY = 2, RET_VIRUS_WAIT = 3,
    RET_CLEAR = 6, RET_REPLAY = 9, RET_END = 100,
};
enum { TA_TIME = 10800 }; /* three minutes */
enum { JOY_REPEAT_MASK = 0xCF0F }; /* ANY_BUTTON without Z, START, L, R */

static void se(DmVs *g, int id)
{
    if (id >= 0 && g->se_n < (int)(sizeof g->se / sizeof g->se[0])) g->se[g->se_n++] = id;
}

static int wrap(int start, int end, int v) /* WrapI */
{
    int size = end - start;
    v = (v - start) % size;
    if (v < 0) v += size;
    return v + start;
}

/* ---- the bottle (vr_init.c / dm_virus_init.c map helpers) ---- */

static DmCell *at(DmPlayer *p, int x, int y) { return &p->map[(y - 1) * DM_COLS + x]; }

static int occupied(DmPlayer *p, int x, int y) { return at(p, x, y)->used != 0; } /* get_map_info */

static void clear_cell(DmPlayer *p, int x, int y) /* clear_map */
{
    DmCell *c = at(p, x, y);
    c->p = c->g = 0;
    c->used = c->falls = c->erasing = c->color = c->virus = c->flg5 = 0;
}

static void init_map(DmPlayer *p) /* init_map_all */
{
    memset(p->map, 0, sizeof p->map);
    for (int r = 0; r < DM_MAP_ROWS; r++)
        for (int x = 0; x < DM_COLS; x++) {
            p->map[r * DM_COLS + x].x = (int8_t)x;
            p->map[r * DM_COLS + x].y = (int8_t)(r + 1);
        }
}

static void set_map(DmPlayer *p, int x, int y, int g, int pal) /* set_map */
{
    DmCell *c = at(p, x, y);
    c->g = (int8_t)g;
    c->p = (int8_t)pal;
    c->used = 1;
    c->falls = 0;
    c->erasing = 0;
    c->virus = -1;
    c->color = (int8_t)(pal >= 3 ? pal - 3 : pal);
}

static void set_virus(DmPlayer *p, int x, int y, int virus, int frame) /* set_virus */
{
    DmCell *c = at(p, x, y);
    c->g = (int8_t)frame;
    c->p = (int8_t)virus;
    c->used = 1;
    c->falls = 0;
    c->erasing = 0;
    c->virus = (int8_t)virus;
    c->color = (int8_t)(virus >= 3 ? virus - 3 : virus);
}

static int count_viruses(DmPlayer *p) /* get_virus_count */
{
    int n = 0;
    for (int i = 0; i < (DM_MAP_ROWS - 1) * DM_COLS; i++) {
        DmCell *c = &p->map[i];
        if (c->used && !c->erasing && c->virus >= 0) n++;
    }
    return n;
}

/* ---- joy.c ---- */

static void joy_init(DmVs *g) /* key_cntrol_init */
{
    for (int i = 0; i < 4; i++) {
        g->joyflg[i] = JOY_REPEAT_MASK;
        g->joygmf[i] = 1;
        g->joygam[i] = 0;
    }
    g->rep_first = 12; /* evs_keyrept */
    g->rep_next = 6;
}

static void joy_read(DmVs *g, const uint16_t pads[4]) /* joyProcCore */
{
    for (int i = 0; i < 4; i++) {
        g->hold[i] = pads[i];
        g->pressed[i] = (uint16_t)(~g->prev_hold[i] & pads[i]);
        g->joycur[i] = 0;
        uint16_t mask = 0x8000;
        for (int j = 0; j < 16; j++, mask >>= 1) {
            if (!(g->joyflg[i] & mask)) continue;
            if (mask & g->hold[i]) {
                g->joycnt[i][j]++;
                if (g->joycnt[i][j] == 1 ||
                    (g->joycnt[i][j] >= g->rep_first && (g->joycnt[i][j] - g->rep_first) % g->rep_next == 0))
                    g->joycur[i] |= mask;
            } else {
                g->joycnt[i][j] = 0;
            }
        }
        g->prev_hold[i] = g->hold[i];
    }
}

/* joyCursorFastSet: the next frame with the direction held repeats at once. */
static void joy_fast(DmVs *g, uint16_t mask, int pad)
{
    int j = 15;
    for (; j >= 0; j--) {
        if (mask & 1) break;
        mask >>= 1;
    }
    g->joycnt[pad][j] = (uint16_t)(g->rep_first + g->rep_next - 1);
}

static void make_key(DmVs *g) /* dm_make_key */
{
    for (int i = 0; i < 2; i++) {
        if (!g->joygmf[i]) continue;
        uint16_t k = (uint16_t)((g->pressed[i] & (DM_BTN_A | DM_BTN_B)) | (g->joycur[i] & (DM_BTN_LEFT | DM_BTN_RIGHT)) |
                                (g->pressed[i] & (DM_BTN_L | DM_BTN_R)));
        if (!(g->hold[i] & (DM_BTN_LEFT | DM_BTN_RIGHT))) k |= g->hold[i] & DM_BTN_DOWN;
        g->joygam[i] = k;
    }
}

/* ---- the falling capsule ---- */

static void init_cap(DmCap *c, int left, int right) /* dm_init_capsel */
{
    c->x[0] = 3;
    c->x[1] = 4;
    c->y[0] = c->y[1] = 0;
    c->g[0] = 2;
    c->g[1] = 3;
    c->p[0] = (int8_t)left;
    c->p[1] = (int8_t)right;
    c->live = 1;
    c->flg2 = 0;
    c->falling = 0;
}

static void set_capsule(DmVs *g, DmPlayer *p) /* dm_set_capsel */
{
    p->move_se = 0;
    init_cap(&p->cap, dm_mag_left(g->magazine[p->mag_cnt]), dm_mag_right(g->magazine[p->mag_cnt]));
    p->cap.falling = 1; /* dm_init_capsel_go */
    p->mag_save = p->mag_cnt;
    if (++p->mag_cnt >= 254) p->mag_cnt = 1;
    init_cap(&p->next, dm_mag_left(g->magazine[p->mag_cnt]), dm_mag_right(g->magazine[p->mag_cnt]));
}

static void speed_up(DmVs *g, DmPlayer *p) /* dm_capsel_speed_up */
{
    if (++p->cap_count >= 10) {
        se(g, DM_SE_SPEED_UP);
        p->cap_count = 0;
        p->cap_speed++;
        if (p->cap_speed > game_speed[3]) p->cap_speed = game_speed[3];
    }
}

static void translate(DmVs *g, DmPlayer *p, int dir, int pad) /* translate_capsel */
{
    DmCap *c = &p->cap;
    int vec = 0;
    if (c->y[0] <= 0 || !c->live) return;
    if (dir == 1) {
        if (c->x[0] == c->x[1]) {
            if (c->x[1] < 7 && occupied(p, c->x[1] + 1, c->y[0]) != 1) {
                if (c->y[1] == 0 || occupied(p, c->x[0] + 1, c->y[1]) != 1) vec = 1;
            }
        } else if (c->x[1] < 7 && occupied(p, c->x[1] + 1, c->y[0]) != 1) {
            vec = 1;
        }
    } else {
        if (c->x[0] == c->x[1]) {
            if (c->x[0] > 0 && occupied(p, c->x[0] - 1, c->y[0]) != 1) {
                if (c->y[1] == 0 || occupied(p, c->x[0] - 1, c->y[1]) != 1) vec = -1;
            }
        } else if (c->x[0] > 0 && occupied(p, c->x[0] - 1, c->y[0]) != 1) {
            vec = -1;
        }
    }
    if (vec) {
        se(g, DM_SE_MOVE);
        p->move_se = 0;
        c->x[0] += (int8_t)vec;
        c->x[1] += (int8_t)vec;
    } else {
        /* Blocked: one bump sound per capsule, and the held direction stays charged. */
        if (!p->move_se) {
            p->move_se = 1;
            se(g, DM_SE_MOVE);
        }
        joy_fast(g, dir == 1 ? DM_BTN_RIGHT : DM_BTN_LEFT, pad);
    }
}

static void rotate(DmVs *g, DmPlayer *p, int dir) /* rotate_capsel: A = +1, B = -1 */
{
    DmCap *c = &p->cap;
    int vec = 0;
    if (c->y[0] <= 0 || !c->live) return;
    if (c->x[0] == c->x[1]) { /* vertical → horizontal, kicking left off the wall or a block */
        if (c->x[0] == 7 || occupied(p, c->x[0] + 1, c->y[0]) == 1) {
            if (c->x[0] != 0 && occupied(p, c->x[0] - 1, c->y[0]) != 1) {
                c->x[0]--;
                vec = 1;
            }
        } else {
            c->x[1]++;
            vec = 1;
        }
        if (vec) {
            c->y[1]++;
            if (dir == -1) {
                int8_t t = c->p[0];
                c->p[0] = c->p[1];
                c->p[1] = t;
            }
        }
    } else { /* horizontal → vertical; under a ceiling it shifts right instead */
        if (c->y[0] == 1) {
            c->x[1]--;
            vec = -1;
        } else if (occupied(p, c->x[0], c->y[0] - 1) == 1) {
            if (occupied(p, c->x[0] + 1, c->y[0] - 1) != 1) {
                c->x[0]++;
                vec = -1;
            }
        } else {
            c->x[1]--;
            vec = -1;
        }
        if (vec) {
            c->y[1]--;
            if (dir == 1) {
                int8_t t = c->p[0];
                c->p[0] = c->p[1];
                c->p[1] = t;
            }
        }
    }
    if (vec) {
        se(g, DM_SE_ROTATE);
        for (int i = 0; i < 2; i++) c->g[i] = (int8_t)rotate_mtx[rotate_table[c->g[i]] + vec];
    }
}

static void capsule_down(DmVs *g, DmPlayer *p) /* dm_capsel_down */
{
    DmCap *c = &p->cap;
    if (c->y[0] > 0) {
        int t = fall_speed[p->cap_speed];
        if (c->y[0] < 4) t += bonus_wait[c->y[0] - 1][p->speed_setting];
        /* + touch_down_wait when resting on something: never set, always 0 */
        p->cap_speed_max = t;
    } else {
        p->cap_speed_max = 30;
    }
    p->cap_speed_count += p->cap_speed_vec;
    if (p->cap_speed_count < p->cap_speed_max) return;
    p->cap_speed_count = 0;
    if (!c->live) return;

    if (c->y[0] > 0) {
        if (c->x[0] == c->x[1]) {
            if (occupied(p, c->x[0], c->y[0] + 1)) c->falling = 0;
        } else {
            for (int j = 0; j < 2; j++)
                if (occupied(p, c->x[j], c->y[j] + 1)) {
                    c->falling = 0;
                    break;
                }
        }
    }
    for (int i = 0; i < 2; i++)
        if (c->y[i] == 16) {
            c->falling = 0;
            break;
        }
    if (c->falling) {
        for (int i = 0; i < 2; i++)
            if (c->y[i] < 16) c->y[i]++;
        for (int i = 0; i < 2; i++)
            if (occupied(p, c->x[i], c->y[i])) { /* entered onto something: topped out */
                p->cnd_static = DM_C_GAME_OVER;
                p->next.live = 0;
                c->falling = 0;
                break;
            }
        if (c->falling) return;
    }
    se(g, DM_SE_LAND);
    p->mode = DM_M_DOWN_WAIT;
    c->live = 0;
    for (int i = 0; i < 2; i++)
        if (c->y[i] != 0) set_map(p, c->x[i], c->y[i], c->g[i], c->p[i] + (p->game_over ? 3 : 0));
}

/* ---- clears, falling pieces (dm_game_main.c) ---- */

static int set_down_flags(DmPlayer *p) /* set_down_flg */
{
    DmCell *m = p->map;
    for (int i = 0; i < (DM_MAP_ROWS - 2) * DM_COLS; i++) m[i].falls = 1;
    for (int r = DM_MAP_ROWS - 3; r >= 0; r--) {
        for (int x = 0; x < DM_COLS; x++) {
            int i = r * DM_COLS + x;
            DmCell *c = &m[i];
            if (!c->used || c->virus >= 0) {
                c->falls = 0;
                continue;
            }
            if (c->g == 2 || c->g == 3) {
                int step = c->g == 2 ? 1 : -1, ok[2];
                for (int l = 0; l < 2; l++) {
                    DmCell *h = &m[i + l * step];
                    ok[l] = 1;
                    if (occupied(p, h->x, h->y + 1) == 1 && m[i + l * step + DM_COLS].falls != 1) ok[l] = 0;
                }
                if (!ok[0] || !ok[1])
                    for (int l = 0; l < 2; l++) m[i + l * step].falls = 0;
            } else if (occupied(p, c->x, c->y + 1) == 1) {
                if (m[i + DM_COLS].falls != 1) c->falls = 0;
            }
        }
    }
    int n = 0;
    for (int i = 0; i < (DM_MAP_ROWS - 1) * DM_COLS; i++)
        if (m[i].used && m[i].falls) n++;
    return n;
}

/* dm_h_erase_chack: a vertical run of 4+ (the decomp's "h" is a column). The early
 * exits at row 13 mirror the original: a run cannot start that low. */
static int vertical_match(DmPlayer *p)
{
    for (int x = 0; x < DM_COLS; x++) {
        int run = 0, col = -1;
        for (int r = 0; r < DM_MAP_ROWS - 1; r++) {
            DmCell *c = &p->map[r * DM_COLS + x];
            if (c->used) {
                if (c->color != col) {
                    if (run >= 3) return 1;
                    if (r >= 13) break;
                    col = c->color;
                    run = 0;
                } else {
                    run++;
                    if (r == 15 && run >= 3) return 1;
                }
            } else if (run >= 3) {
                return 1;
            } else if (r < 13) {
                run = 0;
                col = -1;
            } else {
                break;
            }
        }
    }
    return 0;
}

static int horizontal_match(DmPlayer *p) /* dm_w_erase_chack */
{
    for (int r = 0; r < DM_MAP_ROWS - 1; r++) {
        int run = 0, col = -1;
        for (int x = 0; x < DM_COLS; x++) {
            DmCell *c = &p->map[r * DM_COLS + x];
            if (c->used) {
                if (c->color != col) {
                    if (run >= 3) return 1;
                    if (x >= 5) break;
                    col = c->color;
                    run = 0;
                } else {
                    run++;
                    if (x == 7 && run >= 3) return 1;
                }
            } else if (run >= 3) {
                return 1;
            } else if (x < 5) {
                run = 0;
                col = -1;
            } else {
                break;
            }
        }
    }
    return 0;
}

static void mark_erase(DmPlayer *p, DmCell *c) /* the body of dm_make_erase_h_line / _w_line */
{
    if (c->erasing == 1) return;
    c->erasing = 1;
    if (c->virus < 0) {
        c->g = 5;
    } else {
        c->g = 13;
        p->erase_virus++;
        p->chain_color[3] |= 8;
        p->chain_color[3] |= (uint8_t)(0x10 << c->color);
    }
}

static void line_found(DmPlayer *p, int col)
{
    if (p->chain_count == 0) p->chain_color[3] |= (uint8_t)(1 << col);
    p->chain_color[col]++;
    p->chain_line++;
}

static void vertical_erase(DmPlayer *p) /* dm_h_erase_chack_set */
{
    for (int x = 0; x < DM_COLS; x++) {
        int run = 0, start = -1, col = -1, stop = 0;
        for (int r = 0; r < DM_MAP_ROWS - 1; r++) {
            DmCell *c = &p->map[r * DM_COLS + x];
            if (c->used) {
                if (c->color != col) {
                    if (run >= 3) {
                        for (int k = start; k < start + run + 1; k++) mark_erase(p, &p->map[k * DM_COLS + x]);
                        line_found(p, col);
                    }
                    if (r < 13) {
                        start = r;
                        col = c->color;
                        run = 0;
                    } else {
                        stop = 1;
                    }
                } else {
                    run++;
                    if (r == 15 && run >= 3) {
                        for (int k = start; k < start + run + 1; k++) mark_erase(p, &p->map[k * DM_COLS + x]);
                        line_found(p, col);
                    }
                }
            } else {
                if (run >= 3) {
                    for (int k = start; k < start + run + 1; k++) mark_erase(p, &p->map[k * DM_COLS + x]);
                    line_found(p, col);
                }
                if (r >= 13) {
                    stop = 1;
                } else {
                    run = 0;
                    start = -1;
                    col = -1;
                }
            }
            if (stop) break;
        }
    }
}

static void horizontal_erase(DmPlayer *p) /* dm_w_erase_chack_set */
{
    for (int r = 0; r < DM_MAP_ROWS - 1; r++) {
        int run = 0, start = -1, col = -1, stop = 0;
        for (int x = 0; x < DM_COLS; x++) {
            DmCell *c = &p->map[r * DM_COLS + x];
            if (c->used) {
                if (c->color != col) {
                    if (run >= 3) {
                        for (int k = start; k < start + run + 1; k++) mark_erase(p, &p->map[r * DM_COLS + k]);
                        line_found(p, col);
                    }
                    if (x >= 5) {
                        stop = 1;
                    } else {
                        start = x;
                        col = c->color;
                        run = 0;
                    }
                } else {
                    run++;
                    if (x == 7 && run >= 3) {
                        for (int k = start; k < start + run + 1; k++) mark_erase(p, &p->map[r * DM_COLS + k]);
                        line_found(p, col);
                    }
                }
            } else {
                if (run >= 3) {
                    for (int k = start; k < start + run + 1; k++) mark_erase(p, &p->map[r * DM_COLS + k]);
                    line_found(p, col);
                }
                if (x >= 5) {
                    stop = 1;
                } else {
                    run = 0;
                    start = -1;
                    col = -1;
                }
            }
            if (stop) break;
        }
    }
}

/* dm_h_ball_chack / dm_w_ball_chack: a half whose partner is gone becomes a single.
 * The row/column neighbour reads at the edges are the original's (they may read the
 * neighbouring row's cell, which never holds the matching half). */
static void orphan_halves(DmPlayer *p)
{
    DmCell *m = p->map;
    for (int x = 0; x < DM_COLS; x++)
        for (int r = 0; r < DM_MAP_ROWS - 1; r++) {
            DmCell *c = &m[r * DM_COLS + x];
            if (!c->used) continue;
            if (c->g == 0) {
                if (m[(r + 1) * DM_COLS + x].g != 1) c->g = 4;
            } else if (c->g == 1) {
                if (c->y == 1) c->g = 4;
                else if (m[(r - 1) * DM_COLS + x].g != 0) c->g = 4;
            }
        }
    for (int r = 0; r < DM_MAP_ROWS - 1; r++)
        for (int x = 0; x < DM_COLS; x++) {
            int i = r * DM_COLS + x;
            if (!m[i].used) continue;
            if (m[i].g == 2) {
                if (m[i + 1].g != 3) m[i].g = 4;
            } else if (m[i].g == 3 && (i == 0 || m[i - 1].g != 2)) {
                m[i].g = 4;
            }
        }
}

static void erase_step(DmPlayer *p) /* erase_anime */
{
    for (int i = 0; i < (DM_MAP_ROWS - 1) * DM_COLS; i++) {
        DmCell *c = &p->map[i];
        if (!c->used || !c->erasing) continue;
        c->g++;
        if (c->virus >= 0 ? c->g >= 15 : c->g >= 7) clear_cell(p, c->x, c->y);
    }
}

static void after_settle(DmPlayer *p) /* the tail of go_down and the dm_mode_down_wait case */
{
    if (vertical_match(p) || horizontal_match(p)) {
        p->mode = p->game_over ? DM_M_TR_ERASE_CHECK : DM_M_ERASE_CHECK;
        p->cap_speed_count = 0;
    } else {
        p->mode = p->game_over ? DM_M_TR_CAP_SET : DM_M_CAP_SET;
    }
}

static void go_down(DmVs *g, DmPlayer *p, int wait) /* go_down */
{
    if (++p->erase_anime_count < wait) return;
    p->erase_anime_count = 0;
    int moved = 0;
    for (int r = DM_MAP_ROWS - 1; r >= 0; r--)
        for (int x = 0; x < DM_COLS; x++) {
            DmCell *c = &p->map[r * DM_COLS + x];
            if (c->falls) {
                set_map(p, c->x, c->y + 1, c->g, c->p);
                clear_cell(p, c->x, c->y);
                moved = 1;
            }
        }
    if (moved) se(g, DM_SE_LAND_FALL);
    /* (the training darkening, cnd_training == dm_cnd_training, is 1P only) */
    if (set_down_flags(p) == 0) after_settle(p);
}

static void erase_anime(DmPlayer *p) /* dm_capsel_erase_anime */
{
    p->erase_anime_count++;
    if (p->erase_anime_count == 10) {
        erase_step(p);
        p->erase_anime++;
    } else if (p->erase_anime_count > 26) {
        erase_step(p);
        set_down_flags(p);
        p->erase_anime = 0;
        p->mode = DM_M_BALL_DOWN;
        p->erase_anime_count = 14;
    }
}

static int flash_left(DmVs *g, DmPlayer *p, int mark) /* update_flash_virus_count */
{
    (void)g;
    int n = 0;
    for (int i = 0; i < p->flash_count; i++) {
        DmCell *c = &p->map[p->flash[i].row * DM_COLS + p->flash[i].column];
        if (!c->used || c->erasing || c->virus < 0) {
            if (mark) p->flash[i].color = -1;
        } else {
            n++;
        }
    }
    return n;
}

static int update_virus_count(DmVs *g, DmPlayer *p, int mark) /* dm_update_virus_count */
{
    p->virus_count = g->cfg.mode == DM_MODE_FLASH ? flash_left(g, p, mark) : count_viruses(p);
    return p->virus_count;
}

static void reveal_virus(DmVs *g, DmPlayer *p) /* dm_set_virus */
{
    if (p->cnd != DM_C_INIT) return;
    int max = dm_virus_count(g->cfg.mode, &g->cfg.p[p - g->pl]);
    if (p->virus_order < max) {
        while (p->virus_order < max && (p->vmap.order[p->virus_order] & 0x80)) p->virus_order++;
        if (p->virus_order < max) {
            int idx = p->vmap.order[p->virus_order];
            p->vmap.order[p->virus_order] |= 0x80;
            int type = p->vmap.cell[idx];
            set_virus(p, idx % DM_COLS, idx / DM_COLS + 1, type, virus_anime_table[type][p->virus_anime]);
        }
    }
    if (p->virus_order >= max) {
        p->cnd = DM_C_WAIT;
        p->mode = DM_M_WAIT;
    }
    update_virus_count(g, p, 0);
}

/* dm_virus_anime: the graphics thread's virus wobble. Not part of the logic (the trace
 * masks a live virus's frame); kept so a renderer has it. */
static void virus_anime(DmPlayer *p)
{
    if (p->virus_anime_speed >= ++p->virus_anime_count) return;
    p->virus_anime_count = 0;
    p->virus_anime += p->virus_anime_vec;
    if (p->virus_anime_max < p->virus_anime) p->virus_anime_vec = -1;
    else if (p->virus_anime == 0) p->virus_anime_vec = 1;
    for (int i = 0; i < (DM_MAP_ROWS - 1) * DM_COLS; i++) {
        DmCell *c = &p->map[i];
        if (c->used && !c->erasing && c->virus >= 0) c->g = (int8_t)virus_anime_table[c->virus][p->virus_anime & 3];
    }
}

/* ---- scoring, garbage ---- */

static void add_score(DmPlayer *p, uint32_t s) /* dm_add_score */
{
    p->score += s;
    if (p->score > 9999900) p->score = 9999900;
}

static void make_score(DmPlayer *p) /* dm_make_score / dm_calc_score at the speed setting */
{
    uint32_t s = 0;
    for (int i = p->erase_virus_old; i < p->erase_virus; i++) s += score_table[p->speed_setting][i < 5 ? i : 5] * 100u;
    p->erase_virus_old = p->erase_virus;
    add_score(p, s);
}

static void attack_se(DmVs *g, DmPlayer *p) /* dm_attack_se */
{
    if (p->chain_line < 2) return;
    se(g, p == &g->pl[0] ? DM_SE_ATTACK_P1 : DM_SE_ATTACK_P2);
}

static void warning_line(DmPlayer *p) /* dm_warning_h_line: anything in the top three rows */
{
    int f = 0;
    for (int r = 1; r < 4 && !f; r++)
        for (int x = 0; x < DM_COLS; x++)
            if (occupied(p, x, r) == 1) { /* get_map_info(map, column, row): y = row */
                f = 1;
                break;
            }
    p->warning = f;
}

static int attack_pattern(DmVs *g, int lines) /* dm_make_attack_pattern */
{
    if (lines >= 4) return 0x55 << dm_rng_below(&g->rng, 2);
    if (lines >= 3) return 0x15 << dm_rng_below(&g->rng, 4);
    if (lines >= 2) return 0x11 << dm_rng_below(&g->rng, 4);
    return 0;
}

static int send_attack(DmVs *g, DmPlayer *p) /* dm_set_attack_2p */
{
    if (p->chain_line < 2) return 0;
    DmPlayer *e = &g->pl[(p - g->pl) ^ 1];
    int lines = p->chain_line < 4 ? p->chain_line : 4, pattern, parity = 0;
    if (e->attack[0][0]) {
        /* Garbage already waiting: fill the columns of the other parity. */
        pattern = 0;
        for (int c = 0; c < 8; c++)
            if (e->attack[0][0] & (3 << (c << 1))) {
                pattern |= 1 << c;
                parity = c & 1;
            }
        for (int c = 0; c < 8; c++)
            if ((c & 1) == parity) pattern ^= 1 << c;
        if (pattern == 0) return 1;
    } else {
        pattern = attack_pattern(g, lines);
    }
    e->attack[0][1] = (uint16_t)(p - g->pl);
    for (int c = 0; c < 8; c++) {
        if (!((pattern >> c) & 1)) continue;
        while (p->chain_color[0] || p->chain_color[1] || p->chain_color[2]) {
            int col = dm_rng_below(&g->rng, 3);
            if (p->chain_color[col]) {
                p->chain_color[col]--;
                e->attack[0][0] |= (uint16_t)((col + 1) << (c << 1));
                break;
            }
        }
    }
    return 1;
}

static void reset_chain(DmPlayer *p)
{
    if (p->chain_line_max < p->chain_line) p->chain_line_max = p->chain_line;
    p->chain_line = 0;
    p->chain_count = 0;
    p->erase_virus = 0;
    p->erase_virus_old = 0;
    memset(p->chain_color, 0, sizeof p->chain_color);
}

static int drop_garbage(DmPlayer *p) /* dm_broken_set */
{
    if (!p->attack[0][0]) return 0;
    reset_chain(p);
    for (int i = 0, x = 7; i < 16; i += 2, x--) {
        int c = p->attack[0][0] & (3 << i);
        if (c) set_map(p, x, 1, 4, (c >> i) - 1);
    }
    orphan_halves(p);
    set_down_flags(p);
    memmove(p->attack[0], p->attack[1], sizeof p->attack - sizeof p->attack[0]);
    p->attack[15][0] = p->attack[15][1] = 0;
    return 1;
}

static int black_up(DmPlayer *p) /* dm_black_up: the loser's bottle darkens row by row */
{
    if (!p->retire || p->game_over) return 0;
    if (++p->erase_anime_count < 6) return 0;
    p->erase_anime_count = 0;
    uint8_t base = (uint8_t)((p->black_up - 1) * DM_COLS);
    for (int x = 0; x < DM_COLS; x++)
        if (p->map[(uint8_t)(base + x)].used) p->map[(uint8_t)(base + x)].p += 3;
    if (--p->black_up == 0) {
        p->game_over = 1;
        return 1;
    }
    return 0;
}

/* ---- menus and results ---- */

static void retry_init(DmVs *g, int p, int type) /* retryMenu_init */
{
    g->retry_type[p] = type;
    g->retry_select[p] = type >= 2 ? 1 : 0;
    g->retry_result[p] = -1;
}

static int retry_input(DmVs *g, int p) /* retryMenu_input */
{
    int n = retry_items[g->retry_type[p]], dir = 0, result = -1;
    g->retry_select[p] = wrap(0, n, g->retry_select[p]);
    if (g->joycur[p] & DM_BTN_UP) dir--;
    if (g->joycur[p] & DM_BTN_DOWN) dir++;
    int sel = wrap(0, n, g->retry_select[p] + dir);
    if (sel != g->retry_select[p]) {
        g->retry_select[p] = sel;
        se(g, DM_SE_MENU_MOVE);
    } else if (g->pressed[p] & (DM_BTN_A | DM_BTN_START)) {
        result = g->retry_result[p];
    }
    return result;
}

static void ta_set(DmTaResult *t, int level, int bonus, uint32_t time, int combo, int virus, uint32_t score)
{
    t->level = level;
    t->bonus = bonus;
    t->flow = 0;
    t->frame = 0;
    t->time = time;
    t->delta = 0;
    t->combo = combo;
    t->virus = virus;
    t->score = score;
}

static int ta_update(DmVs *g, DmTaResult *t, int sound) /* timeAttackResult_update */
{
    int more = 1;
    switch (t->flow) {
    case 0:
        if (t->frame >= 120) {
            t->flow = 10;
            t->frame = 0;
            t->cnt = 0;
        } else {
            t->frame++;
        }
        break;
    case 10:
        if (t->time == 0) {
            t->flow = 20;
            t->frame = 0;
        } else {
            t->time -= t->time < 6 ? t->time : 6;
            if (sound) {
                if (t->time == 0) se(g, DM_SE_TA_DONE);
                else if (t->cnt % 10 == 0) se(g, DM_SE_TA_TICK);
            }
            int pts = ta_points[t->level];
            if (t->cnt >= 200) pts *= 3;
            else if (t->cnt >= 100) pts *= 2;
            if (t->bonus) {
                uint32_t s = t->score + (uint32_t)pts;
                t->score = s < 9999900 ? s : 9999900;
                t->delta += (uint32_t)pts;
            }
            t->cnt++;
        }
        t->frame++;
        break;
    case 20:
        if (t->frame < 60) {
            t->frame++;
        } else {
            t->flow = 99;
            t->frame = 0;
        }
        break;
    case 99:
        more = 0;
        break;
    }
    return more;
}

static void ta_result(DmVs *g, DmTaResult *t, int level, int bonus, uint32_t time, int combo, int virus,
                      uint32_t score) /* timeAttackResult_result: the final score, then shown again from the start */
{
    ta_set(t, level, bonus, time, combo, virus, score);
    while (ta_update(g, t, 0)) {}
    t->result = t->score;
    ta_set(t, level, bonus, time, combo, virus, score);
}

static int add_win(DmVs *g, int p) /* dm_add_win_2p (stars and lifetime totals are display) */
{
    g->win_count[p]++;
    return g->win_count[p] == g->cfg.wins;
}

static int set_win(DmVs *g, int p, int finish, int menu) /* dm_set_win_2p */
{
    DmPlayer *s = &g->pl[p];
    s->cnd_static = DM_C_WIN;
    g->effect_timer[p] = 180;
    se(g, DM_SE_VOICE + 4);
    if (!menu) {
        menu = 1;
        s->cnd = DM_C_WIN_RETRY;
        s->mode = DM_M_WIN_RETRY;
        retry_init(g, p, finish ? 3 : 5);
    } else {
        s->cnd = DM_C_WIN;
        s->mode = DM_M_WIN;
    }
    return menu;
}

static int set_lose(DmVs *g, int p, int finish, int menu) /* dm_set_lose_2p */
{
    DmPlayer *s = &g->pl[p];
    s->cnd_static = DM_C_LOSE;
    g->effect_timer[p] = 180;
    if (!menu) {
        menu = 1;
        s->cnd = DM_C_LOSE_RETRY;
        s->mode = DM_M_LOSE_RETRY;
        retry_init(g, p, finish ? 3 : 5);
    } else {
        s->cnd = DM_C_LOSE;
        s->mode = DM_M_LOSE;
    }
    return menu;
}

static int set_draw(DmVs *g, int p, int menu) /* dm_set_draw_2p */
{
    DmPlayer *s = &g->pl[p];
    s->cnd_static = DM_C_DRAW;
    g->effect_timer[p] = 180;
    if (!menu) {
        menu = 1;
        s->cnd = DM_C_DRAW_RETRY;
        s->mode = DM_M_DRAW_RETRY;
        retry_init(g, p, 5);
    } else {
        s->cnd = DM_C_DRAW;
        s->mode = DM_M_DRAW;
    }
    return menu;
}

static int effect_calc(DmVs *g, int p) /* _effectX_calc */
{
    if (g->effect_timer[p]) g->effect_timer[p]--;
    return g->effect_timer[p] != 0;
}

/* ---- pause ---- */

static void query_pause(DmVs *g, int p) /* dm_query_pause_player */
{
    DmPlayer *s = &g->pl[p];
    if (g->query_pause >= 0) return;
    if (s->cnd_static != DM_C_WAIT && s->cnd_static != DM_C_PAUSE) return;
    if (s->cnd != DM_C_INIT && (g->pressed[p] & DM_BTN_START)) {
        g->query_pause_se = 1;
        g->query_pause = p;
    }
}

static void pause_on(DmVs *g, int p, int master) /* dm_set_pause_on */
{
    DmPlayer *s = &g->pl[p];
    g->effect_timer[p] = 180; /* effectPause_init; nothing counts it down in 2P */
    s->cnd_static = DM_C_PAUSE;
    s->cnd_old = s->cnd;
    s->mode_old = s->mode;
    if (p == master) {
        s->mode = DM_M_PAUSE_RETRY;
        s->cnd = DM_C_PAUSE_RE;
        retry_init(g, p, 1);
    } else {
        retry_init(g, p, 0);
        s->cnd = DM_C_PAUSE;
        s->mode = DM_M_PAUSE;
    }
}

static void pause_off(DmPlayer *s) /* dm_set_pause_off */
{
    s->cnd_static = DM_C_WAIT;
    s->cnd = s->cnd_old;
    s->mode = s->mode_old;
    if (s->mode == DM_M_TR_CHECK) s->cnd_static = DM_C_LOSE;
}

static void pause_requests(DmVs *g) /* dm_set_pause_and_volume */
{
    int sound = g->query_pause_se ? DM_SE_PAUSE : -1, who = g->query_pause;
    g->query_pause = -1;
    g->query_pause_se = 0;
    if (who < 0) return;
    if (g->pl[who].cnd_static == DM_C_WAIT) {
        for (int i = 0; i < 2; i++) pause_on(g, i, who);
        who = -1;
    } else if (g->pl[who].cnd_static == DM_C_PAUSE) {
        for (int i = 0; i < 2; i++) pause_off(&g->pl[i]);
        who = -1;
    }
    if (who < 0 && sound >= 0) se(g, sound);
}

/* ---- the per-player state machine: dm_game_main_cnt ---- */

static int player_step(DmVs *g, int pi)
{
    DmPlayer *p = &g->pl[pi];
    query_pause(g, pi);
    if (p->cnd_static != DM_C_WAIT && p->cnd_static != DM_C_PAUSE) black_up(p);
    if (g->cfg.mode == DM_MODE_TIME_ATTACK && g->game_time >= TA_TIME && p->cnd_static == DM_C_WAIT)
        return RET_GAME_OVER;

    switch (p->mode) {
    case DM_M_INIT:
        reveal_virus(g, p);
        return RET_VIRUS_WAIT;
    case DM_M_WAIT:
        return RET_VIRUS_WAIT;
    case DM_M_DOWN_WAIT:
        if (p->cnd_static == DM_C_GAME_OVER) return RET_GAME_OVER; /* dm_check_game_over */
        if (vertical_match(p) || horizontal_match(p)) {
            p->mode = p->game_over ? DM_M_TR_ERASE_CHECK : DM_M_ERASE_CHECK;
            p->cap_speed_count = 0;
        } else {
            p->mode = p->game_over ? DM_M_TR_CAP_SET : DM_M_CAP_SET;
        }
        break;
    case DM_M_ERASE_CHECK:
        if (++p->cap_speed_count >= 18) {
            p->mode = DM_M_ERASE_ANIME;
            p->cap_speed_count = 0;
            vertical_erase(p);
            horizontal_erase(p);
            orphan_halves(p);
            int erased = p->virus_count - update_virus_count(g, p, 1);
            p->total_erase += erased;
            make_score(p);
            if (p->virus_count == 0) {
                if (p->chain_line_max < p->chain_line) p->chain_line_max = p->chain_line;
                return RET_CLEAR;
            }
            if (g->last3_count >= p->virus_count) {
                if (!g->last3_flag) {
                    g->last3_flag = 1;
                    se(g, DM_SE_LAST3);
                }
                if (!g->bgm_bpm[0]) g->bgm_bpm[0] = g->bgm_bpm[1] = 1;
            }
            p->chain_count++;
            if (p->chain_line < 2) {
                if (p->chain_color[3] & 8) {
                    p->chain_color[3] &= (uint8_t)~8;
                    se(g, g->cfg.mode == DM_MODE_FLASH && erased ? DM_SE_FLASH_ERASE : DM_SE_VIRUS_ERASE);
                } else {
                    se(g, DM_SE_ERASE);
                }
            } else {
                int k = p->chain_line - 2;
                se(g, DM_SE_VOICE + (k <= 2 ? k : 2));
                p->chain_color[3] &= (uint8_t)~8;
            }
        }
        break;
    case DM_M_ERASE_ANIME:
        erase_anime(p);
        break;
    case DM_M_BALL_DOWN:
        go_down(g, p, 14);
        break;
    case DM_M_CAP_SET: {
        int next = 1;
        if (p->chain_count > 1) {
            int t = p->total_chain + p->chain_count - 1;
            p->total_chain = t < 99 ? t : 99;
        }
        attack_se(g, p);
        warning_line(p);
        if (g->cfg.mode != DM_MODE_TIME_ATTACK) {
            send_attack(g, p);
            if (drop_garbage(p)) {
                next = 0;
                se(g, DM_SE_VOICE + 3);
                p->mode = DM_M_BALL_DOWN;
            }
        }
        if (next) {
            set_capsule(g, p);
            speed_up(g, p);
            reset_chain(p);
            p->mode = DM_M_DOWN;
        }
        break;
    }
    case DM_M_CLEAR_WAIT:
    case DM_M_GOVER_WAIT:
    case DM_M_RETIRE_WAIT:
        effect_calc(g, pi);
        break;
    case DM_M_CLEAR_RESULT:
    case DM_M_GOVER_RESULT:
    case DM_M_RETIRE_RESULT:
        if (g->pressed[pi] & 0xFF3F) while (ta_update(g, &g->ta[pi], 0)) {}
        ta_update(g, &g->ta[pi], 1);
        add_score(p, g->ta[pi].delta);
        g->ta[pi].delta = 0;
        break;
    case DM_M_WIN_RETRY:
    case DM_M_LOSE_RETRY:
    case DM_M_DRAW_RETRY:
        if (effect_calc(g, pi)) break;
        switch (retry_input(g, pi)) {
        case 0: se(g, DM_SE_MENU_OK); return RET_REPLAY;
        case 1: se(g, DM_SE_MENU_OK); return RET_END;
        case 2: se(g, DM_SE_MENU_OK); return RET_RETRY;
        case 3: se(g, DM_SE_MENU_OK); return RET_GAME_END;
        default: break;
        }
        break;
    case DM_M_WIN:
    case DM_M_LOSE:
    case DM_M_DRAW:
        effect_calc(g, pi);
        break;
    case DM_M_TR_ERASE_CHECK:
        if (++p->cap_speed_count >= 18) {
            p->cap_speed_count = 0;
            p->mode = DM_M_ERASE_ANIME;
            vertical_erase(p);
            horizontal_erase(p);
            orphan_halves(p);
            p->chain_count++;
            update_virus_count(g, p, 1);
            if (p->chain_color[3] & 8) {
                p->chain_color[3] &= (uint8_t)~8;
                se(g, DM_SE_VIRUS_ERASE);
            } else {
                se(g, DM_SE_ERASE);
            }
        }
        break;
    case DM_M_TR_CAP_SET:
        warning_line(p);
        memset(p->attack, 0, sizeof p->attack); /* flg_training is never set in VS */
        set_capsule(g, p);
        speed_up(g, p);
        p->chain_line = p->chain_count = p->erase_virus = p->erase_virus_old = 0;
        memset(p->chain_color, 0, sizeof p->chain_color);
        p->mode = DM_M_DOWN;
        break;
    case DM_M_PAUSE_RETRY:
        switch (retry_input(g, pi)) {
        case 0:
            if (g->query_pause < 0) g->query_pause = pi; /* "continue" */
            se(g, DM_SE_MENU_OK);
            break;
        case 1: se(g, DM_SE_MENU_OK); return RET_RETRY;
        case 2: se(g, DM_SE_MENU_OK); return RET_GAME_END;
        default: break;
        }
        break;
    default:
        break;
    }
    return RET_NULL;
}

/* ---- dm_game_main_2p ---- */

static int set_ta_results(DmVs *g) /* dm_set_time_attack_result_2p */
{
    uint32_t r[2] = { g->ta[0].result, g->ta[1].result };
    int finish = 0, menu = 0;
    for (int i = 0; i < 2; i++)
        if (r[i] > r[i ^ 1]) finish = add_win(g, i);
    for (int i = 0; i < 2; i++)
        if (r[i] > r[i ^ 1]) menu = set_win(g, i, finish, menu);
    for (int i = 0; i < 2; i++)
        if (r[i] < r[i ^ 1]) menu = set_lose(g, i, finish, menu);
    for (int i = 0; i < 2; i++)
        if (r[i] == r[i ^ 1]) menu = set_draw(g, i, menu);
    return finish;
}

static int main_2p(DmVs *g)
{
    int ret[2], clear = 0, over = 0, menu = 0, finish = 0;
    DmMode mode = g->cfg.mode;

    pause_requests(g);
    for (int i = 0; i < 2; i++) ret[i] = player_step(g, i);

    /* dm_warning_h_line_se */
    int warn = 0;
    for (int i = 0; i < 2; i++)
        if (g->pl[i].cnd_static == DM_C_WAIT && g->pl[i].warning) warn++;
    if (!warn) {
        g->warning_se_count = 0;
    } else if (++g->warning_se_count == 1) {
        se(g, DM_SE_WARNING);
    } else if (g->warning_se_count >= 300) {
        g->warning_se_count = 0;
    }
    /* dm_play_count_down_se: the last ten seconds of TIME ATTACK */
    if (mode == DM_MODE_TIME_ATTACK) {
        int i = 0;
        while (i < 2 && g->pl[i].cnd_static != DM_C_WAIT) i++;
        if (i != 2) {
            int32_t diff = TA_TIME - (int32_t)g->game_time;
            if (diff % 60 == 0 && diff / 60 > 0 && diff / 60 < 11) se(g, DM_SE_COUNT);
        }
    }

    if (ret[0] == RET_VIRUS_WAIT && ret[1] == RET_VIRUS_WAIT) {
        if (g->countdown < 0 && g->pl[0].mode == DM_M_WAIT && g->pl[1].mode == DM_M_WAIT) {
            g->started = 1;
            g->pl[0].mode = g->pl[1].mode = DM_M_DOWN;
        }
        return RET_NULL;
    }

    for (int i = 0; i < 2; i++) {
        DmPlayer *p = &g->pl[i];
        if (ret[i] == RET_CLEAR) {
            if (mode == DM_MODE_TIME_ATTACK) {
                g->effect_timer[i] = 180;
                p->mode = DM_M_CLEAR_WAIT;
                p->cnd = p->cnd_static = DM_C_CLEAR_WAIT;
                ta_result(g, &g->ta[i], p->game_level, 1,
                          (int32_t)g->game_time > TA_TIME ? 0 : TA_TIME - g->game_time, p->total_chain,
                          p->total_erase, p->score);
            } else {
                clear++;
                p->cnd_static = DM_C_WIN;
            }
        } else if (ret[i] == RET_GAME_OVER) {
            if (mode == DM_MODE_TIME_ATTACK) {
                g->effect_timer[i] = 180;
                if (g->game_time >= TA_TIME) {
                    p->mode = DM_M_GOVER_WAIT;
                    p->cnd = p->cnd_static = DM_C_GOVER_WAIT;
                } else {
                    p->mode = DM_M_RETIRE_WAIT;
                    p->cnd = p->cnd_static = DM_C_RETIRE_WAIT;
                }
                ta_result(g, &g->ta[i], p->game_level, 0, 0, p->total_chain, p->total_erase, p->score);
            } else {
                over++;
                p->cnd_static = DM_C_LOSE;
            }
            p->virus_anime_speed = 1;
            p->black_up = 16;
            p->retire = 1;
        } else if (ret[i] == RET_RETRY) {
            return RET_RETRY;
        } else if (ret[i] == RET_REPLAY) {
            return RET_REPLAY;
        } else if (ret[i] == RET_END) {
            return RET_GAME_OVER;
        } else if (ret[i] == RET_GAME_END) {
            return RET_GAME_END;
        }
    }

    int waited = 0, shown = 0;
    for (int i = 0; i < 2; i++) {
        switch (g->pl[i].cnd) {
        case DM_C_CLEAR_WAIT:
        case DM_C_GOVER_WAIT:
        case DM_C_RETIRE_WAIT:
            if (g->effect_timer[i] == 0) waited++;
            break;
        case DM_C_CLEAR_RESULT:
        case DM_C_GOVER_RESULT:
        case DM_C_RETIRE_RESULT:
            if (g->ta[i].flow == 99) shown++;
            break;
        default:
            break;
        }
    }
    if (waited == 2) {
        for (int i = 0; i < 2; i++) {
            DmPlayer *p = &g->pl[i];
            if (p->cnd == DM_C_CLEAR_WAIT) {
                p->cnd_static = p->cnd = DM_C_CLEAR_RESULT;
                p->mode = DM_M_CLEAR_RESULT;
            } else if (p->cnd == DM_C_GOVER_WAIT) {
                p->cnd_static = p->cnd = DM_C_GOVER_RESULT;
                p->mode = DM_M_GOVER_RESULT;
            } else if (p->cnd == DM_C_RETIRE_WAIT) {
                p->cnd_static = p->cnd = DM_C_RETIRE_RESULT;
                p->mode = DM_M_RETIRE_RESULT;
            }
        }
    } else if (shown == 2) {
        finish = set_ta_results(g);
    } else if (clear == 2 || over == 2) {
        for (int i = 0; i < 2; i++) menu = set_draw(g, i, menu);
    } else if (clear) {
        for (int i = 0; i < 2; i++)
            if (g->pl[i].cnd_static == DM_C_WIN) finish = add_win(g, i);
        for (int i = 0; i < 2; i++)
            if (g->pl[i].cnd_static == DM_C_WIN) menu = set_win(g, i, finish, menu);
        for (int i = 0; i < 2; i++)
            if (g->pl[i].cnd_static != DM_C_WIN) menu = set_lose(g, i, finish, menu);
    } else if (over) {
        for (int i = 0; i < 2; i++)
            if (g->pl[i].cnd_static != DM_C_LOSE) finish = add_win(g, i);
        for (int i = 0; i < 2; i++)
            if (g->pl[i].cnd_static != DM_C_LOSE) menu = set_win(g, i, finish, menu);
        for (int i = 0; i < 2; i++)
            if (g->pl[i].cnd_static == DM_C_LOSE) menu = set_lose(g, i, finish, menu);
    }
    if (finish) g->saving = 1; /* dm_save_all: eep_rom_flg */
    return RET_NULL;
}

/* ---- key_control_main ---- */

static void key_control(DmVs *g, int pi)
{
    DmPlayer *p = &g->pl[pi];
    if (g->pressed[pi] & (DM_BTN_CUP | DM_BTN_CLEFT | DM_BTN_CRIGHT | DM_BTN_CDOWN)) g->ghost[pi] = !g->ghost[pi];
    if (p->mode == DM_M_THROW) {
        if (g->hold[pi] & DM_BTN_RIGHT) joy_fast(g, DM_BTN_RIGHT, pi);
        if (g->hold[pi] & DM_BTN_LEFT) joy_fast(g, DM_BTN_LEFT, pi);
    } else if (p->mode == DM_M_DOWN) {
        if (p->cnd_static == DM_C_WAIT) {
            uint16_t k = g->joygam[pi];
            if (k & DM_BTN_B) rotate(g, p, -1);
            else if (k & DM_BTN_A) rotate(g, p, 1);
            if (k & DM_BTN_LEFT) translate(g, p, -1, pi);
            else if (k & DM_BTN_RIGHT) translate(g, p, 1, pi);
            p->cap_speed_vec = 1;
            if ((k & DM_BTN_DOWN) && p->cap.y[0] > 0) {
                int v = fall_speed[p->cap_speed];
                p->cap_speed_vec = (v >> 1) + (v & 1); /* soft drop: half the fall wait, rounded up */
            }
        } else {
            if (g->hold[pi] & DM_BTN_RIGHT) joy_fast(g, DM_BTN_RIGHT, pi);
            if (g->hold[pi] & DM_BTN_LEFT) joy_fast(g, DM_BTN_LEFT, pi);
        }
        capsule_down(g, p);
    } else {
        joy_fast(g, DM_BTN_RIGHT, pi);
        joy_fast(g, DM_BTN_LEFT, pi);
    }
}

/* ---- dm_game_init ---- */

static void game_init(DmVs *g, int reinit, int32_t seed)
{
    if (!reinit) memset(g->win_count, 0, sizeof g->win_count);
    dm_rng_seed(&g->rng, seed);
    g->countdown = 0;
    g->bgm_bpm[0] = g->bgm_bpm[1] = 0;
    g->last3_flag = 0;
    g->last3_count = g->cfg.mode == DM_MODE_FLASH ? 1 : 3;
    g->started = 0;
    g->warning_se_count = 0;
    if (g->cfg.mode == DM_MODE_TIME_ATTACK) g->game_time = 0;
    dm_make_magazine(&g->rng, g->magazine);
    g->query_pause_se = 0;
    g->query_pause = -1;

    for (int i = 0; i < 2; i++) {
        DmPlayer *p = &g->pl[i];
        p->mode = DM_M_INIT;
        p->cnd = DM_C_INIT;
        p->cnd_static = DM_C_WAIT;
        p->virus_count = 0;
        p->virus_order = 0;
        p->warning = 0;
        p->retire = 0;
        p->game_over = 0;
        p->cap_speed = game_speed[p->speed_setting];
        p->cap_speed_max = 0;
        p->cap_speed_vec = 1;
        p->mag_cnt = 1;
        p->cap_count = 0;
        p->cap_speed_count = 0;
        p->move_se = 0;
        set_capsule(g, p);
        p->virus_anime = 0;
        p->virus_anime_count = 0;
        p->virus_anime_vec = 1;
        p->erase_anime = 0;
        p->erase_anime_count = 0;
        p->erase_virus = 0;
        p->erase_virus_old = 0;
        p->chain_line = 0;
        p->chain_count = 0;
        p->total_erase = 0;
        p->total_chain = 0;
        p->virus_anime_speed = 8;
        p->virus_anime_max = 2;
        memset(p->chain_color, 0, sizeof p->chain_color);
        memset(p->attack, 0, sizeof p->attack);
        init_map(p);
    }

    /* The layouts continue the same RNG (setup.c, dm_round_setup minus the seeding). */
    for (int i = 0; i < 2; i++) {
        DmPlayer *p = &g->pl[i];
        DmFlash fl;
        dm_virus_init(&g->rng, g->cfg.mode, &g->cfg.p[i], &p->vmap, NULL);
        if (g->cfg.mode == DM_MODE_FLASH) {
            dm_make_flash(&g->rng, g->cfg.p[i].game_level, &p->vmap, &fl);
            p->flash_count = 3; /* make_flash_virus_pos says 3 whatever it placed */
            for (int k = 0; k < fl.count; k++) {
                p->flash[k].column = fl.pos[k].column;
                p->flash[k].row = fl.pos[k].row;
                p->flash[k].color = p->flash_bak[k] = fl.pos[k].color;
            }
        } else {
            p->flash_count = 0;
        }
    }
    if (g->cfg.p[0].virus_level == g->cfg.p[1].virus_level) { /* dm_virus_map_copy */
        g->pl[1].vmap = g->pl[0].vmap;
        if (g->cfg.mode == DM_MODE_FLASH) {
            g->pl[1].flash_count = g->pl[0].flash_count;
            memcpy(g->pl[1].flash, g->pl[0].flash, sizeof g->pl[1].flash);
            memcpy(g->pl[1].flash_bak, g->pl[0].flash_bak, sizeof g->pl[1].flash_bak);
        }
    }
    g->effect_timer[0] = g->effect_timer[1] = 0; /* effectAll_init */
}

static int32_t next_seed(DmVs *g) /* randSeed2 = irandom() + osGetTime() */
{
    return (int32_t)((uint32_t)dm_rng_next(&g->rng) + g->salt);
}

void dm_vs_start(DmVs *g, const DmVsConfig *cfg, int32_t seed, uint32_t salt)
{
    memset(g, 0, sizeof *g);
    g->cfg = *cfg;
    g->salt = salt;
    for (int i = 0; i < 2; i++) {
        g->pl[i].virus_level = cfg->p[i].virus_level;
        g->pl[i].speed_setting = cfg->speed[i];
        g->pl[i].game_level = cfg->p[i].game_level;
    }
    joy_init(g);
    game_init(g, 0, seed);
}

void dm_vs_next_round(DmVs *g)
{
    if (g->cfg.mode == DM_MODE_TIME_ATTACK) g->pl[0].score = g->pl[1].score = 0;
    game_init(g, 1, next_seed(g));
}

void dm_vs_set_pill(DmVs *g, int player, int x, int y, int color)
{
    if (color < 0) clear_cell(&g->pl[player], x, y);
    else set_map(&g->pl[player], x, y, 4, color);
}

int dm_vs_round_state(const DmVs *g)
{
    for (int i = 0; i < 2; i++) {
        int c = g->pl[i].cnd_static;
        if (c != DM_C_WIN && c != DM_C_LOSE && c != DM_C_DRAW) return 0;
    }
    if (g->effect_timer[0] || g->effect_timer[1]) return 0;
    return g->win_count[0] == g->cfg.wins || g->win_count[1] == g->cfg.wins ? 2 : 1;
}

/* dm_game_graphic_effect's countdown: count_down_ctrl advances once per player per
 * drawn frame while either bottle is still in init/wait. */
static void countdown_frame(DmVs *g)
{
    for (int i = 0; i < 2; i++) {
        int c = g->pl[i].cnd;
        if ((c != DM_C_WAIT && c != DM_C_INIT) || g->countdown < 0) continue;
        int lo = g->countdown / 2, s = lo / 48;
        if (lo >= DM_COUNTDOWN_FRAMES) {
            g->countdown = -1;
            if (i == 0) se(g, DM_SE_GO);
        } else {
            if (i == 0 && s * 48 == lo - 20 && s < 3) se(g, DM_SE_COUNT);
            g->countdown++;
        }
    }
}

void dm_vs_tick(DmVs *g, uint16_t pad0, uint16_t pad1)
{
    g->se_n = 0;
    /* dm_game_main2 */
    g->bgm_bpm[1] = 0;
    for (int i = 0; i < 2; i++) /* dm_effect_make: the clock runs while someone plays */
        if (g->pl[i].cnd_static == DM_C_WAIT) {
            if (g->started && g->game_time < 5999 * 60) g->game_time++;
            break;
        }
    int ret = main_2p(g);
    int decided = g->win_count[0] == g->cfg.wins || g->win_count[1] == g->cfg.wins;
    if (ret == RET_GAME_OVER && !decided) {
        dm_vs_next_round(g);
        ret = RET_NULL;
    } else if (ret == RET_RETRY) {
        g->pl[0].score = g->pl[1].score = 0;
        game_init(g, 0, next_seed(g));
    }
    g->result = ret;

    /* dm_game_graphic_onDoneSawp. While the record is written (the frame a match is
     * decided, here) the pads are not read and the button state is cleared. */
    uint16_t pads[4] = { pad0, pad1, 0, 0 };
    if (g->saving) {
        memset(g->joycur, 0, sizeof g->joycur);
        memset(g->pressed, 0, sizeof g->pressed);
        memset(g->prev_hold, 0, sizeof g->prev_hold);
        memset(g->hold, 0, sizeof g->hold);
        g->saving = 0;
    } else {
        joy_read(g, pads);
    }
    make_key(g);
    for (int i = 0; i < 2; i++) key_control(g, i);

    /* the graphics thread */
    countdown_frame(g);
    for (int i = 0; i < 2; i++)
        if (g->pl[i].cnd_static != DM_C_PAUSE) virus_anime(&g->pl[i]);
}

/* ---- the trace dump (tools/oracle/glue.c dump_state, field for field) ---- */

typedef struct Out { char *buf; size_t size, len; } Out;

static void out(Out *o, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void out(Out *o, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(o->buf + (o->len < o->size ? o->len : o->size), o->len < o->size ? o->size - o->len : 0, fmt, ap);
    va_end(ap);
    if (n > 0) o->len += (size_t)n;
}

static void out_raw(Out *o, const char *s, size_t n)
{
    if (o->len + n < o->size) memcpy(o->buf + o->len, s, n + 1);
    o->len += n;
}

static void dump_cap(Out *o, const char *tag, const DmCap *c)
{
    out(o, "%s %d,%d %d,%d g%d,%d p%d,%d f%d%d%d\n", tag, c->x[0], c->x[1], c->y[0], c->y[1], c->g[0], c->g[1], c->p[0],
        c->p[1], c->live, c->falling, c->flg2);
}

size_t dm_vs_dump(const DmVs *g, char *buf, size_t size)
{
    Out o = { buf, size, 0 };
    out(&o, "G cd%d sg%d wc%d,%d et%d,%d ws%d l3%d,%d bpm%d gt%u\n", g->countdown, g->started, g->win_count[0],
        g->win_count[1], g->effect_timer[0], g->effect_timer[1], g->warning_se_count, g->last3_flag, g->last3_count,
        g->bgm_bpm[0], g->game_time);
    out(&o, "Q qp%d qs%d rt%d,%d rs%d,%d\n", g->query_pause, g->query_pause_se, g->retry_type[0], g->retry_type[1],
        g->retry_select[0], g->retry_select[1]);
    for (int i = 0; i < 2; i++) {
        const DmPlayer *s = &g->pl[i];
        out(&o, "P%d m%d c%d s%d mo%d co%d vo%d vn%d sp%d cn%d sc%d sv%d sm%d mg%d,%d mse%d ea%d ec%d ev%d,%d"
                " ch%d cl%d,%d col%d,%d,%d,%d bu%d w%d r%d go%d sco%u te%d tc%d\n",
            i, s->mode, s->cnd, s->cnd_static, s->mode_old, s->cnd_old, s->virus_order, s->virus_count, s->cap_speed,
            s->cap_count, s->cap_speed_count, s->cap_speed_vec, s->cap_speed_max, s->mag_cnt, s->mag_save, s->move_se,
            s->erase_anime, s->erase_anime_count, s->erase_virus, s->erase_virus_old, s->chain_count, s->chain_line,
            s->chain_line_max, s->chain_color[0], s->chain_color[1], s->chain_color[2], s->chain_color[3], s->black_up,
            s->warning, s->retire, s->game_over, s->score, s->total_erase, s->total_chain);
        const DmTaResult *t = &g->ta[i];
        out(&o, "TA l%d b%d f%d,%d n%d t%u c%d v%d s%u d%u r%u\n", t->level, t->bonus, t->flow, t->frame, t->cnt,
            t->time, t->combo, t->virus, t->score, t->delta, t->result);
        dump_cap(&o, "cap", &s->cap);
        dump_cap(&o, "nxt", &s->next);
        out(&o, "atk");
        for (int k = 0; k < 16; k++) out(&o, " %x:%x", s->attack[k][0], s->attack[k][1]);
        out(&o, "\nfl%d", s->flash_count);
        for (int k = 0; k < s->flash_count; k++)
            out(&o, " %d,%d,%d", s->flash[k].column, s->flash[k].row, s->flash[k].color);
        out(&o, "\nmap");
        for (int k = 0; k < DM_MAP_CELLS; k++) { /* " %d,%d,…" per cell, without printf: it is most of the dump */
            const DmCell *c = &s->map[k];
            int v[10] = { c->x, c->y, (c->used && c->virus >= 0 && !c->erasing) ? -1 : c->g, c->p, c->used, c->falls,
                          c->erasing, c->color, c->virus, c->flg5 };
            char tmp[64], *q = tmp;
            if (k % 8 == 0) *q++ = '\n';
            for (int f = 0; f < 10; f++) {
                int x = v[f];
                *q++ = f ? ',' : ' ';
                if (x < 0) { *q++ = '-'; x = -x; }
                if (x >= 100) *q++ = (char)('0' + x / 100);
                if (x >= 10) *q++ = (char)('0' + x / 10 % 10);
                *q++ = (char)('0' + x % 10);
            }
            *q = 0;
            out_raw(&o, tmp, (size_t)(q - tmp));
        }
        out(&o, "\nJ gam%x cur%x hold%x cnt", g->joygam[i], g->joycur[i], g->hold[i]);
        for (int k = 0; k < 16; k++) out(&o, " %d", g->joycnt[i][k]);
        out(&o, "\n");
    }
    out(&o, "SE");
    for (int k = 0; k < g->se_n; k++) out(&o, " %d", g->se[k]);
    out(&o, "\n");
    return o.len;
}
