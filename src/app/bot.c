/* A plain greedy player of our own (the original's CPU AI is not ported: VS here is
 * human against human). It drives the screenshots and the headless
 * playthrough test, so it only has to play sensibly, not well. */
#include "bot.h"

#include <string.h>

static int cell_color(const DmPlayer *p, int x, int y) /* -1 empty or outside */
{
    if (x < 0 || x >= DM_COLS || y < 1 || y > DM_ROWS) return -1;
    const DmCell *c = &p->map[(y - 1) * DM_COLS + x];
    return c->used ? c->p % 3 : -1;
}

static uint8_t is_virus[DM_ROWS + 1][DM_COLS];

/* Length of the same-colour run through (x, y) along (dx, dy), and the viruses in it. */
static int run(int8_t grid[DM_ROWS + 1][DM_COLS], int x, int y, int dx, int dy, int *viruses)
{
    int c = grid[y][x], n = 1;
    *viruses = is_virus[y][x];
    for (int s = -1; s <= 1; s += 2)
        for (int k = 1; k < 4; k++) {
            int xx = x + s * dx * k, yy = y + s * dy * k;
            if (xx < 0 || xx >= DM_COLS || yy < 1 || yy > DM_ROWS || grid[yy][xx] != c) break;
            n++;
            *viruses += is_virus[yy][xx];
        }
    return n;
}

/* Where the pair lands: half 0 at (x, y), half 1 at (x + 1, y) or (x, y - 1). */
static int landing(int8_t grid[DM_ROWS + 1][DM_COLS], int x, int vertical)
{
    int y = 1;
    if (grid[1][x] >= 0 || (!vertical && grid[1][x + 1] >= 0)) return 0;
    while (y < DM_ROWS && grid[y + 1][x] < 0 && (vertical || grid[y + 1][x + 1] < 0)) y++;
    return y;
}

static int score(int8_t grid[DM_ROWS + 1][DM_COLS], int x, int y, int vertical, int c0, int c1)
{
    int x1 = vertical ? x : x + 1, y1 = vertical ? y - 1 : y;
    if (y1 < 1) return -100000;
    grid[y][x] = (int8_t)c0;
    grid[y1][x1] = (int8_t)c1;
    int s = 0;
    for (int h = 0; h < 2; h++) {
        int hx = h ? x1 : x, hy = h ? y1 : y;
        for (int d = 0; d < 2; d++) {
            int v, n = run(grid, hx, hy, d ? 0 : 1, d ? 1 : 0, &v);
            s += (n >= 4 ? 400 : n * n * 10) + v * (n >= 4 ? 300 : 40 * n);
        }
        if (hy < DM_ROWS && grid[hy + 1][hx] >= 0 && grid[hy + 1][hx] != grid[hy][hx] &&
            !(vertical && h == 1))
            s -= 25; /* covers another colour */
    }
    s += y * 6;
    if (y1 <= 4) s -= 300; /* near the neck */
    grid[y][x] = grid[y1][x1] = -1;
    return s;
}

void dm_bot_init(DmBot *b, int skill) { memset(b, 0, sizeof *b); b->skill = skill; b->tx = -1; }

uint16_t dm_bot_pad(DmBot *b, const DmVs *g, int pi)
{
    const DmPlayer *p = &g->pl[pi];
    b->tick++;
    if (b->skill == 0) return DM_BTN_DOWN; /* the stacker: tops out fast */
    if (p->mode != DM_M_DOWN || p->cnd_static != DM_C_WAIT || !p->cap.live) { b->tx = -1; return 0; }
    const DmCap *c = &p->cap;
    int vertical = c->x[0] == c->x[1];
    if (b->tx < 0 || b->serial != g->pl[pi].cap_count) {
        int8_t grid[DM_ROWS + 1][DM_COLS];
        for (int y = 0; y <= DM_ROWS; y++)
            for (int x = 0; x < DM_COLS; x++) {
                grid[y][x] = (int8_t)(y ? cell_color(p, x, y) : -1);
                is_virus[y][x] = y && grid[y][x] >= 0 && p->map[(y - 1) * DM_COLS + x].virus >= 0;
            }
        int best = -1000000;
        for (int v = 0; v < 2; v++)
            for (int swap = 0; swap < 2; swap++)
                for (int x = 0; x < DM_COLS - (v ? 0 : 1); x++) {
                    int y = landing(grid, x, v);
                    if (!y) continue;
                    int c0 = swap ? c->p[1] % 3 : c->p[0] % 3, c1 = swap ? c->p[0] % 3 : c->p[1] % 3;
                    int s = score(grid, x, y, v, c0, c1) + (int)((b->tick * 7 + x * 13) % 5);
                    if (s > best) { best = s; b->tx = x; b->tv = v; b->tc = c0; }
                }
        b->serial = g->pl[pi].cap_count;
        if (b->tx < 0) { b->tx = 3; b->tv = 0; b->tc = c->p[0] % 3; }
    }
    if (b->tick & 1) return 0; /* release between presses so each one is new */
    if (vertical != b->tv || c->p[0] % 3 != b->tc) {
        if (c->p[0] % 3 == c->p[1] % 3 && vertical == b->tv) {} /* same colours: orientation is enough */
        else return DM_BTN_A;
    }
    if (c->x[0] < b->tx) return DM_BTN_RIGHT;
    if (c->x[0] > b->tx) return DM_BTN_LEFT;
    return b->skill >= 2 ? DM_BTN_DOWN : (b->tick & 2 ? DM_BTN_DOWN : 0);
}
