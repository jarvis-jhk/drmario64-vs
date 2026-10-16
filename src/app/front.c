#include "front.h"

#include <stdlib.h>
#include <string.h>

/* Item labels per retry type, in cursor order, and what each item reports to the
 * engine (pause_table / cont_table in game_etc.c, entries 3..5). */
static const char *const menu_items[6][3] = {
    { NULL, NULL, NULL },
    { "CONTINUE", "RETRY", "END" },
    { "REPLAY", "END", "END" },
    { "REPLAY", "RETRY", "END" },
    { "REPLAY", "NEXT", "NEXT" },
    { "REPLAY", "NEXT", "END" },
};
static const int menu_result[6][3] = {
    { 0, 2, 2 }, { 0, 1, 2 }, { 0, 3, 3 }, { 0, 2, 3 }, { 0, 1, 1 }, { 0, 1, 3 },
};
static const int ta_levels[3] = { 6, 9, 12 }; /* _timeAttack_levelTable */

const char *const *dm_front_menu_items(int type)
{
    return type >= 1 && type <= 5 ? menu_items[type] : NULL;
}

static void se_by(DmFront *f, int id, int player)
{
    if (f->se_n >= (int)(sizeof f->se / sizeof f->se[0])) return;
    f->se_player[f->se_n] = (int8_t)player;
    f->se[f->se_n++] = id;
}

static void se(DmFront *f, int id) { se_by(f, id, -1); }

void dm_front_init(DmFront *f, uint32_t seed_base)
{
    memset(f, 0, sizeof *f);
    f->seed_base = seed_base;
    f->mode = DM_MODE_NORMAL;
    f->wins = 3;
    for (int i = 0; i < 2; i++) { /* dm_init_config_save: vm_lv 10, vm_sp 1, vm_fl_lv/vm_ta_lv 1 */
        f->panel[i].level = 10;
        f->panel[i].speed = 1;
        f->panel[i].glevel = 1;
    }
}

void dm_front_free(DmFront *f)
{
    free(f->round_start);
    free(f->log);
    f->round_start = NULL;
    f->log = NULL;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* The pad's new presses, plus left/right auto-repeat at joy.c's 12/6 frames. */
static uint16_t panel_keys(DmPanel *p, uint16_t pad)
{
    uint16_t press = pad & ~p->prev;
    p->prev = pad;
    uint16_t lr = pad & (DM_BTN_LEFT | DM_BTN_RIGHT);
    if (!lr || (press & lr)) p->rep = 0;
    else if (++p->rep >= 12) { press |= lr; p->rep = 6; }
    return press;
}

static void round_begins(DmFront *f)
{
    if (!f->round_start) f->round_start = malloc(sizeof *f->round_start);
    if (f->round_start) *f->round_start = f->vs;
    f->log_n = 0;
    f->replaying = 0;
}

static void start_match(DmFront *f)
{
    DmVsConfig c;
    memset(&c, 0, sizeof c);
    c.mode = f->mode;
    c.wins = f->wins;
    for (int i = 0; i < 2; i++) {
        if (f->mode == DM_MODE_NORMAL) {
            c.p[i].virus_level = f->panel[i].level;
        } else {
            c.p[i].virus_level = ta_levels[f->panel[i].glevel];
            c.p[i].game_level = f->panel[i].glevel;
        }
        c.speed[i] = f->panel[i].speed;
    }
    uint32_t t = f->seed_base + f->frame * 2654435761u;
    dm_vs_start(&f->vs, &c, (int32_t)t, t ^ 0x9e3779b9u);
    f->screen = DM_FRONT_GAME;
    f->counted = 0;
    f->go_timer = 0;
    round_begins(f);
}

static void setup_tick(DmFront *f, const uint16_t pads[2])
{
    int ready = 0;
    for (int i = 0; i < 2; i++) {
        DmPanel *p = &f->panel[i];
        uint16_t k = panel_keys(p, pads[i]);
        if (p->ready) {
            if (k & DM_BTN_B) { p->ready = 0; se(f, DM_SE_MENU_MOVE); }
            ready += p->ready;
            continue;
        }
        if (k & (DM_BTN_A | DM_BTN_START)) {
            p->ready = 1;
            se(f, DM_SE_MENU_OK);
            ready++;
            continue;
        }
        int dy = (k & DM_BTN_DOWN ? 1 : 0) - (k & DM_BTN_UP ? 1 : 0);
        if (dy) {
            p->row = (p->row + dy + DM_ROWS_SETUP) % DM_ROWS_SETUP;
            se(f, DM_SE_MENU_MOVE);
        }
        int dx = (k & DM_BTN_RIGHT ? 1 : 0) - (k & DM_BTN_LEFT ? 1 : 0);
        if (!dx) continue;
        int before, after;
        switch (p->row) {
        case DM_ROW_MODE: {
            static const DmMode order[3] = { DM_MODE_NORMAL, DM_MODE_FLASH, DM_MODE_TIME_ATTACK };
            int m = 0;
            while (order[m] != f->mode) m++;
            before = m;
            after = (m + dx + 3) % 3;
            f->mode = order[after];
            break;
        }
        case DM_ROW_WINS:
            before = f->wins;
            after = f->wins = clampi(f->wins + dx, 1, 3);
            break;
        case DM_ROW_LEVEL:
            if (f->mode == DM_MODE_NORMAL) {
                before = p->level;
                after = p->level = clampi(p->level + dx, 0, 20);
            } else {
                before = p->glevel;
                after = p->glevel = clampi(p->glevel + dx, 0, 2);
            }
            break;
        default:
            before = p->speed;
            after = p->speed = clampi(p->speed + dx, 0, 2);
            break;
        }
        if (before != after) se(f, DM_SE_MENU_MOVE);
    }
    if (ready == 2) {
        f->panel[0].ready = f->panel[1].ready = 0;
        start_match(f);
    }
}

static void to_setup(DmFront *f)
{
    f->screen = DM_FRONT_SETUP;
    for (int i = 0; i < 2; i++) {
        f->panel[i].ready = 0;
        f->panel[i].prev = 0xFFFF; /* the button that ended the match is not a new press here */
    }
}

static void game_tick(DmFront *f, uint16_t pad0, uint16_t pad1)
{
    DmVs *g = &f->vs;
    if (f->replaying) {
        if (f->replay_pos < f->log_n) {
            pad0 = f->log[f->replay_pos][0];
            pad1 = f->log[f->replay_pos][1];
            f->replay_pos++;
        }
    } else {
        if (f->log_n == f->log_cap) {
            int cap = f->log_cap ? f->log_cap * 2 : 4096;
            void *n = realloc(f->log, (size_t)cap * sizeof f->log[0]);
            if (n) { f->log = n; f->log_cap = cap; }
        }
        if (f->log_n < f->log_cap) {
            f->log[f->log_n][0] = pad0;
            f->log[f->log_n][1] = pad1;
            f->log_n++;
        }
    }
    for (int i = 0; i < 2; i++) { /* what the menu display reports for the cursor */
        int t = g->retry_type[i];
        if (t >= 0 && t <= 5) g->retry_result[i] = menu_result[t][clampi(g->retry_select[i], 0, 2)];
    }
    g->salt = f->seed_base ^ (f->frame * 0x85ebca6bu);
    int was = dm_vs_round_state(g), counting = g->countdown >= 0;
    dm_vs_tick(g, pad0, pad1);
    for (int i = 0; i < g->se_n; i++) se_by(f, g->se[i], g->se_player[i]);
    if (counting && g->countdown < 0) f->go_timer = 40;
    else if (f->go_timer > 0) f->go_timer--;

    int now = dm_vs_round_state(g);
    if (f->replaying && now) { /* the replayed round is decided again: back to live pads */
        f->replaying = 0;
        f->log_n = f->replay_pos;
    }
    if (now == 2 && !f->counted) {
        f->counted = 1 + (g->win_count[1] == g->cfg.wins);
        f->match_wins[f->counted - 1]++;
    }
    switch (g->result) {
    case DM_VS_MATCH_OVER:
    case DM_VS_QUIT:
        to_setup(f);
        return;
    case DM_VS_RESTARTED:
        f->counted = 0;
        f->go_timer = 0;
        round_begins(f);
        return;
    case DM_VS_REPLAY:
        if (f->round_start) {
            *g = *f->round_start;
            f->replaying = 1;
            f->replay_pos = 0;
            f->go_timer = 0;
            if (f->counted) { /* the replay decides the same match again */
                f->match_wins[f->counted - 1]--;
                f->counted = 0;
            }
        }
        return;
    default:
        break;
    }
    if (was && !now) round_begins(f); /* NEXT: dm_vs_tick started the next round */
}

void dm_front_tick(DmFront *f, uint16_t pad0, uint16_t pad1)
{
    f->se_n = 0;
    uint16_t pads[2] = { pad0, pad1 };
    if (f->screen == DM_FRONT_SETUP) setup_tick(f, pads);
    else game_tick(f, pad0, pad1);
    f->frame++;
}
