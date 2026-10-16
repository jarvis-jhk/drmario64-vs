/* The screens, drawn as vectors in the original 2P layout (SPEC 03 §3). Positions that
 * the original takes from code are used as they are: bottle frames, cells, the
 * falling capsule's 1-px offset, the next-capsule spot, star columns and rows, the
 * virus counters and the timer. Our own art replaces the textures; where a texture
 * set the size of something, the size here is our choice. */
#include "render.h"

#include "draw.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static const DmRGBA pill_rgb[3] = { { 236, 54, 64, 255 }, { 252, 206, 36, 255 }, { 46, 122, 244, 255 } };
static const DmRGBA white = { 255, 255, 255, 255 };
static const DmRGBA text_dim = { 168, 182, 230, 255 };
static const DmRGBA side_rgb[2] = { { 255, 128, 110, 255 }, { 110, 186, 255, 255 } };
static const int map_x[2] = { 28, 212 };
enum { MAP_Y = 46 };

static DmRGBA mix(DmRGBA a, DmRGBA b, float t)
{
    DmRGBA c = { (unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t),
                 (unsigned char)(a.b + (b.b - a.b) * t), (unsigned char)(a.a + (b.a - a.a) * t) };
    return c;
}

static DmRGBA alpha(DmRGBA c, float a) { c.a = (unsigned char)(c.a * a); return c; }

/* Palette p: colour 0..2, +3 for the loser's darkened bottle. */
static DmRGBA pal(int p)
{
    DmRGBA c = pill_rgb[p % 3];
    return p >= 3 ? mix(c, dm_rgba(70, 72, 90, 255), 0.72f) : c;
}

/* One capsule half (g 0 top, 1 bottom, 2 left, 3 right, 4 single) in the 10×10 cell at (x, y). */
static void draw_half(DmDraw *d, float x, float y, int g, int p, float a)
{
    const float in = 0.55f, big = 4.4f, small = 1.1f;
    float r[4];
    switch (g) {
    case 0: r[0] = r[1] = big; r[2] = r[3] = small; break;
    case 1: r[0] = r[1] = small; r[2] = r[3] = big; break;
    case 2: r[0] = r[3] = big; r[1] = r[2] = small; break;
    case 3: r[0] = r[3] = small; r[1] = r[2] = big; break;
    default: r[0] = r[1] = r[2] = r[3] = big; break;
    }
    float bx = x + in, by = y + in, bw = 10 - 2 * in, bh = 10 - 2 * in;
    /* the halves of a pair meet: no gap on the joining side */
    if (g == 0) bh += in;
    if (g == 1) { by -= in; bh += in; }
    if (g == 2) bw += in;
    if (g == 3) { bx -= in; bw += in; }
    DmRGBA c = pal(p);
    dm_draw_rrect4(d, bx, by, bw, bh, r, alpha(mix(c, dm_rgba(0, 0, 0, 255), 0.45f), a));
    float ri[4];
    for (int k = 0; k < 4; k++) ri[k] = fmaxf(r[k] - 0.9f, 0.3f);
    dm_draw_rrect4(d, bx + 0.9f, by + 0.9f, bw - 1.8f, bh - 1.8f, ri, alpha(c, a));
    dm_draw_ellipse(d, bx + bw * 0.34f, by + bh * 0.3f, bw * 0.16f, bh * 0.11f, alpha(dm_rgba(255, 255, 255, 150), a));
}

/* A virus: a round body with feet and a face; frame 0..3 is the wobble. */
static void draw_virus(DmDraw *d, float x, float y, int p, int frame, float a, float scale)
{
    float cx = x + 5, cy = y + 5.2f;
    float wob = (frame & 1) ? 0.35f : -0.35f, lean = (frame & 2) ? 0.3f : -0.3f;
    DmRGBA c = pal(p), dark = mix(c, dm_rgba(0, 0, 0, 255), 0.5f);
    for (int k = 0; k < 6; k++) { /* feet and arms */
        float ang = (float)(k * 1.047 + 0.52) + (k & 1 ? wob : -wob) * 0.4f;
        dm_draw_circle(d, cx + cosf(ang) * 3.7f * scale, cy + sinf(ang) * 3.5f * scale, 1.25f * scale, alpha(dark, a));
    }
    dm_draw_ellipse(d, cx + lean * 0.4f, cy, (3.9f + wob * 0.4f) * scale, (3.7f - wob * 0.4f) * scale, alpha(dark, a));
    dm_draw_ellipse(d, cx + lean * 0.4f, cy, (3.2f + wob * 0.4f) * scale, (3.0f - wob * 0.4f) * scale, alpha(c, a));
    float ex = 1.35f * scale, ey = cy - 0.8f * scale;
    for (int s = -1; s <= 1; s += 2) {
        dm_draw_ellipse(d, cx + s * ex + lean * 0.5f, ey, 1.05f * scale, 1.25f * scale, alpha(white, a));
        dm_draw_circle(d, cx + s * ex + lean * 0.8f + 0.2f * s, ey + 0.25f * scale, 0.5f * scale, alpha(dm_rgba(10, 10, 20, 255), a));
    }
    dm_draw_line(d, cx - 1.2f * scale + lean, cy + 1.5f * scale, cx + 1.2f * scale + lean, cy + 1.5f * scale + wob * 0.6f, 0.55f * scale,
                 alpha(dm_rgba(30, 10, 20, 255), a));
}

static void draw_pop(DmDraw *d, float x, float y, int p, float t) /* an erasing cell, t 0..1 */
{
    DmRGBA c = pal(p);
    float r = 2.5f + t * 3.5f;
    float pts[2 * 24];
    for (int i = 0; i < 24; i++) {
        float ang = (float)(i * 2 * 3.14159265 / 24), rr = r * (i & 1 ? 0.55f : 1.0f);
        pts[2 * i] = x + 5 + cosf(ang) * rr;
        pts[2 * i + 1] = y + 5 + sinf(ang) * rr;
    }
    dm_draw_outline(d, pts, 24, 0.9f, alpha(c, 1 - t * 0.8f));
    dm_draw_circle(d, x + 5, y + 5, 2.0f * (1 - t), alpha(white, 1 - t));
}

static void bottle_path(float mx, float *p, int *n)
{
    /* The outer bottle spans (mx-8, 16)..(mx+88, 224) like the original's frame; the
     * neck holds columns 3 and 4. */
    const float pts[] = {
        mx + 22, 22, mx + 58, 22, mx + 58, 30, mx + 52, 30, mx + 52, 42, mx + 84, 48, mx + 86, 54, mx + 86, 214,
        mx + 80, 220, mx + 0, 220, mx - 6, 214, mx - 6, 54, mx - 4, 48, mx + 28, 42, mx + 28, 30, mx + 22, 30,
    };
    *n = (int)(sizeof pts / sizeof pts[0]) / 2;
    memcpy(p, pts, sizeof pts);
}

static void draw_bottle(DmDraw *d, int i)
{
    float mx = (float)map_x[i], p[64];
    int n;
    bottle_path(mx, p, &n);
    dm_draw_rrect(d, mx - 4.5f, 49.5f, 89, 169, 5, dm_rgba(8, 12, 34, 235));
    dm_draw_rrect(d, mx + 29, 29, 22, 22, 2, dm_rgba(8, 12, 34, 235));
    dm_draw_outline(d, p, n, 2.6f, dm_rgba(200, 222, 255, 230));
    dm_draw_outline(d, p, n, 0.8f, dm_rgba(255, 255, 255, 255));
    dm_draw_line(d, mx - 3, 60, mx - 3, 200, 0.9f, dm_rgba(255, 255, 255, 60)); /* glass shine */
}

static int landing_y(const DmPlayer *p) /* the fall-point guide: where the pair would stop */
{
    const DmCap *c = &p->cap;
    int y = c->y[0];
    for (;;) {
        int ny = y + 1, ok = ny <= DM_ROWS;
        for (int h = 0; h < 2 && ok; h++) {
            int hy = c->y[h] + (ny - c->y[0]);
            if (hy >= 1 && p->map[(hy - 1) * DM_COLS + c->x[h]].used) ok = 0;
        }
        if (!ok) return y;
        y = ny;
    }
}

static void draw_contents(DmDraw *d, const DmVs *g, int i, uint32_t frame)
{
    const DmPlayer *p = &g->pl[i];
    float mx = (float)map_x[i];
    for (int r = 0; r < DM_ROWS; r++)
        for (int x = 0; x < DM_COLS; x++) {
            const DmCell *c = &p->map[r * DM_COLS + x];
            if (!c->used) continue;
            float cx = mx + 10 * x, cy = MAP_Y + 10 * (r + 1);
            if (c->erasing) /* mark_erase: g runs 13, 14 for a virus and 5, 6 for a pill */
                draw_pop(d, cx, cy, c->p, (c->g - (c->virus >= 0 ? 13 : 5)) / 2.0f);
            else if (c->virus >= 0)
                draw_virus(d, cx, cy, c->p, p->virus_anime & 3, 1, 1);
            else {
                draw_half(d, cx, cy, c->g, c->p, 1);
            }
        }
    if (g->cfg.mode == DM_MODE_FLASH) /* the flashing targets */
        for (int k = 0; k < p->flash_count && k < DM_FLASH_MAX; k++) {
            int col = p->flash[k].column, row = p->flash[k].row;
            if (col < 0 || row < 1 || row > DM_ROWS) continue;
            const DmCell *c = &p->map[(row - 1) * DM_COLS + col];
            if (!c->used || c->virus < 0 || c->erasing) continue;
            float t = 0.5f + 0.5f * sinf(frame * 0.2f);
            dm_draw_circle(d, mx + 10 * col + 5, MAP_Y + 10 * row + 5.2f, 5.2f, dm_rgba(255, 255, 255, (int)(40 + 70 * t)));
        }
    const DmCap *c = &p->cap;
    if (p->mode == DM_M_DOWN && g->ghost[i] && c->live && c->y[0] > 0) {
        int ly = landing_y(p);
        for (int h = 0; h < 2; h++) {
            int hy = c->y[h] + ly - c->y[0];
            if (hy >= 1) dm_draw_rrect(d, mx + 10 * c->x[h] + 0.8f, MAP_Y + 10 * hy + 0.8f, 8.4f, 8.4f, 3.5f, dm_rgba(96, 96, 96, 150));
        }
    }
    if (c->live) /* dm_calc_capsel_pos: one pixel lower than the stack; the neck row at 36 */
        for (int h = 0; h < 2; h++)
            if (c->y[h] >= 0) {
                float cy = c->y[h] > 0 ? MAP_Y + 10.0f * c->y[h] + 1 : 36;
                draw_half(d, mx + 10 * c->x[h], cy, c->g[h], c->p[h], 1);
            }
    if (!c->live || c->y[0] > 0) /* the next capsule waits in the neck */
        if (p->mode != DM_M_INIT || p->virus_order > 0) {
            const DmCap *n = &p->next;
            draw_half(d, mx + 30, 36, 2, n->p[0], 1);
            draw_half(d, mx + 40, 36, 3, n->p[1], 1);
        }
}

static void panel(DmDraw *d, float cx, float cy, float w, float h, DmRGBA edge)
{
    dm_draw_rrect(d, cx - w / 2 - 1, cy - h / 2 - 1, w + 2, h + 2, 5, edge);
    dm_draw_rrect(d, cx - w / 2, cy - h / 2, w, h, 4, dm_rgba(14, 18, 52, 245));
}

static void logo(DmDraw *d, float cx, float cy, const char *s, DmRGBA c)
{
    float w = dm_text_width(s, 14) + 16;
    panel(d, cx, cy, w, 26, c);
    dm_draw_text(d, s, cx, cy - 7, 14, DM_ALIGN_CENTER, c);
}

static void star(DmDraw *d, float cx, float cy, float r, DmRGBA c, int filled)
{
    float p[20];
    for (int k = 0; k < 10; k++) {
        float a = (float)(-3.14159265 / 2 + k * 3.14159265 / 5), rr = k & 1 ? r * 0.45f : r;
        p[2 * k] = cx + cosf(a) * rr;
        p[2 * k + 1] = cy + sinf(a) * rr;
    }
    if (filled) /* a star is not convex: fill it as five triangles around a pentagon */
        for (int k = 0; k < 5; k++) {
            float t[6] = { p[4 * k], p[4 * k + 1], p[(4 * k + 2) % 20], p[(4 * k + 3) % 20], p[(4 * k + 18) % 20], p[(4 * k + 19) % 20] };
            dm_draw_poly(d, t, 3, c);
        }
    if (filled) {
        float pent[10];
        for (int k = 0; k < 5; k++) { pent[2 * k] = p[4 * k + 2]; pent[2 * k + 1] = p[4 * k + 3]; }
        dm_draw_poly(d, pent, 5, c);
    }
    dm_draw_outline(d, p, 10, 0.8f, filled ? mix(c, white, 0.5f) : alpha(text_dim, 0.7f));
}

static const char *speed_name[3] = { "LOW", "MED", "HI" };

static void draw_board(DmDraw *d, const DmFront *f)
{
    const DmVs *g = &f->vs;
    char buf[32];
    dm_draw_rrect(d, 121, 4, 78, 228, 6, dm_rgba(16, 22, 64, 200));
    const float col[2] = { 140, 180 };
    dm_draw_text(d, g->cfg.mode == DM_MODE_NORMAL ? "LEVEL" : "LEVEL", 160, 7, 4, DM_ALIGN_CENTER, text_dim);
    dm_draw_text(d, "SPEED", 160, 27, 4, DM_ALIGN_CENTER, text_dim);
    dm_draw_text(d, "SCORE", 160, 47, 4, DM_ALIGN_CENTER, text_dim);
    for (int i = 0; i < 2; i++) {
        const DmPlayer *p = &g->pl[i];
        if (g->cfg.mode == DM_MODE_NORMAL) snprintf(buf, sizeof buf, "%02d", p->virus_level);
        else snprintf(buf, sizeof buf, "%s", speed_name[p->game_level % 3]);
        dm_draw_text(d, buf, col[i], 14, 8, DM_ALIGN_CENTER, white);
        dm_draw_text(d, speed_name[p->speed_setting % 3], col[i], 34, 6, DM_ALIGN_CENTER, white);
        snprintf(buf, sizeof buf, "%07u", (unsigned)p->score);
        dm_draw_text(d, buf, 160, 54.0f + 9 * i, 6, DM_ALIGN_CENTER, side_rgb[i]);
    }
    /* stars: _posP2StarX / _posP2StarY by the wins needed */
    static const float star_y[3][3] = { { 72 }, { 80, 64 }, { 88, 72, 56 } };
    const float star_x[2] = { 128, 172 };
    int wins = g->cfg.wins < 1 ? 1 : g->cfg.wins > 3 ? 3 : g->cfg.wins;
    for (int i = 0; i < 2; i++)
        for (int k = 0; k < wins; k++)
            star(d, star_x[i], star_y[wins - 1][k] + 22, 5.5f, dm_rgba(255, 214, 64, 255), k < g->win_count[i]);
    dm_draw_text(d, "VS", 160, 124, 12, DM_ALIGN_CENTER, dm_rgba(52, 60, 110, 255));
    dm_draw_text(d, "WINS", 160, 145, 4, DM_ALIGN_CENTER, text_dim);
    for (int i = 0; i < 2; i++) {
        snprintf(buf, sizeof buf, "%d", f->match_wins[i] > 99 ? 99 : f->match_wins[i]);
        dm_draw_text(d, buf, col[i], 152, 6, DM_ALIGN_CENTER, side_rgb[i]);
    }
    uint32_t t = g->game_time;
    if (g->cfg.mode == DM_MODE_TIME_ATTACK) t = t >= 10800 ? 0 : 10800 - t + 59; /* counts down */
    snprintf(buf, sizeof buf, "%02u:%02u", (unsigned)(t / 3600), (unsigned)(t / 60 % 60));
    dm_draw_text(d, "TIME", 160, 163, 4, DM_ALIGN_CENTER, text_dim);
    dm_draw_text(d, buf, 160, 170, 7, DM_ALIGN_CENTER, white);
    const char *mode = g->cfg.mode == DM_MODE_FLASH ? "FLASH" : g->cfg.mode == DM_MODE_TIME_ATTACK ? "TIME ATTACK" : "NORMAL";
    dm_draw_text(d, mode, 160, 184, 4.5f, DM_ALIGN_CENTER, dm_rgba(255, 214, 64, 255));
    dm_draw_text(d, "VIRUS", 160, 197, 4, DM_ALIGN_CENTER, text_dim);
    for (int i = 0; i < 2; i++) {
        snprintf(buf, sizeof buf, "%d", g->pl[i].virus_count);
        dm_draw_text(d, buf, col[i], 205, 10, DM_ALIGN_CENTER, white); /* draw_virus_number centres it */
    }
}

static void draw_menu(DmDraw *d, const DmVs *g, int i, uint32_t frame)
{
    const char *const *items = dm_front_menu_items(g->retry_type[i]);
    if (!items) return;
    float cx = map_x[i] + 40.0f, y0 = 132;
    panel(d, cx, y0 + 22, 72, 50, dm_rgba(200, 222, 255, 255));
    for (int k = 0; k < 3; k++) {
        int sel = g->retry_select[i] == k;
        dm_draw_text(d, items[k], cx + 3, y0 + 6 + 14 * k, 6.5f, DM_ALIGN_CENTER, sel ? white : text_dim);
        if (sel && (frame & 16)) {
            float w = dm_text_width(items[k], 6.5f);
            dm_draw_text(d, ">", cx + 3 - w / 2 - 7, y0 + 6 + 14 * k, 6.5f, DM_ALIGN_LEFT, dm_rgba(255, 214, 64, 255));
        }
    }
}

static void draw_game(DmDraw *d, const DmFront *f)
{
    const DmVs *g = &f->vs;
    draw_board(d, f);
    for (int i = 0; i < 2; i++) {
        const DmPlayer *p = &g->pl[i];
        float cx = map_x[i] + 40.0f;
        draw_bottle(d, i);
        if (p->cnd_static != DM_C_PAUSE) draw_contents(d, g, i, f->frame);
        int lo = g->countdown >= 0 ? g->countdown / 2 : -1;
        if (lo >= 20) { /* SE 72 at display counts 20, 68, 116 */
            static const char *num[4] = { "3", "2", "1", "1" };
            const char *s = num[(lo - 20) / 48 > 3 ? 3 : (lo - 20) / 48];
            float ph = ((lo - 20) % 48) / 48.0f, h = 30 - ph * 8;
            dm_draw_text(d, s, cx, 112 - h / 2, h, DM_ALIGN_CENTER, alpha(white, 1 - ph * 0.6f));
        } else if (f->go_timer > 0 && p->cnd_static == DM_C_WAIT) {
            dm_draw_text(d, "GO!", cx, 100, 24, DM_ALIGN_CENTER, alpha(dm_rgba(255, 214, 64, 255), f->go_timer / 40.0f));
        }
        const char *s = NULL;
        DmRGBA c = white;
        switch (p->cnd_static) {
        case DM_C_WIN: s = "WIN"; c = dm_rgba(255, 214, 64, 255); break;
        case DM_C_LOSE: s = "LOSE"; c = dm_rgba(170, 180, 220, 255); break;
        case DM_C_DRAW: s = "DRAW"; break;
        case DM_C_PAUSE: s = "PAUSE"; break;
        case DM_C_CLEAR_WAIT: case DM_C_CLEAR_RESULT: s = "CLEAR!"; c = dm_rgba(255, 214, 64, 255); break;
        case DM_C_GOVER_WAIT: case DM_C_GOVER_RESULT: s = "TIME UP"; break;
        case DM_C_RETIRE_WAIT: case DM_C_RETIRE_RESULT: s = "GAME OVER"; c = dm_rgba(170, 180, 220, 255); break;
        default: break;
        }
        if (s) logo(d, cx, 108, s, c);
        int cnd = p->cnd_static;
        if (cnd == DM_C_CLEAR_RESULT || cnd == DM_C_GOVER_RESULT || cnd == DM_C_RETIRE_RESULT) {
            char buf[32];
            snprintf(buf, sizeof buf, "%u", (unsigned)g->ta[i].score);
            dm_draw_text(d, buf, cx, 130, 7, DM_ALIGN_CENTER, white);
        }
        int menu = (p->mode == DM_M_WIN_RETRY || p->mode == DM_M_LOSE_RETRY || p->mode == DM_M_DRAW_RETRY) ? g->effect_timer[i] == 0
                   : p->mode == DM_M_PAUSE_RETRY;
        if (menu) draw_menu(d, g, i, f->frame);
    }
    if (f->replaying) dm_draw_text(d, "REPLAY", 160, 228, 5, DM_ALIGN_CENTER, (f->frame & 32) ? white : text_dim);
}

static void draw_setup(DmDraw *d, const DmFront *f, const char *version, const char *hint, const char *status,
                       const char *const names[2])
{
    char buf[48];
    dm_draw_text(d, "DR. MARIO 64", 160, 14, 16, DM_ALIGN_CENTER, white);
    dm_draw_text(d, "VS", 160, 36, 10, DM_ALIGN_CENTER, dm_rgba(255, 214, 64, 255));
    /* a few pills and viruses as decoration */
    draw_half(d, 52, 20, 2, 0, 1);
    draw_half(d, 62, 20, 3, 2, 1);
    draw_virus(d, 254, 18, 1, (f->frame / 8) & 3, 1, 1.3f);
    draw_virus(d, 34, 36, 2, (f->frame / 8 + 1) & 3, 1, 1);
    draw_half(d, 268, 38, 0, 0, 1);
    draw_half(d, 268, 48, 1, 1, 1);

    static const char *label[DM_ROWS_SETUP] = { "MODE", "WINS", "LEVEL", "SPEED" };
    const char *mode = f->mode == DM_MODE_FLASH ? "FLASH" : f->mode == DM_MODE_TIME_ATTACK ? "TIME ATTACK" : "NORMAL";
    for (int i = 0; i < 2; i++) {
        const DmPanel *p = &f->panel[i];
        float cx = i ? 240 : 80, top = 62;
        panel(d, cx, top + 66, 136, 128, p->ready ? side_rgb[i] : alpha(side_rgb[i], 0.5f));
        if (names && names[i] && names[i][0]) snprintf(buf, sizeof buf, "%s", names[i]);
        else snprintf(buf, sizeof buf, "PLAYER %d", i + 1);
        dm_draw_text(d, buf, cx, top + 8, 8, DM_ALIGN_CENTER, side_rgb[i]);
        for (int r = 0; r < DM_ROWS_SETUP; r++) {
            float y = top + 26 + r * 22;
            const char *v;
            if (r == DM_ROW_MODE) v = mode;
            else if (r == DM_ROW_WINS) { snprintf(buf, sizeof buf, "%d", f->wins); v = buf; }
            else if (r == DM_ROW_LEVEL) {
                if (f->mode == DM_MODE_NORMAL) snprintf(buf, sizeof buf, "%02d", p->level);
                else snprintf(buf, sizeof buf, "%s", speed_name[p->glevel]);
                v = buf;
            } else v = speed_name[p->speed];
            int sel = p->row == r && !p->ready && !f->locked;
            if (sel) dm_draw_rrect(d, cx - 62, y - 3, 124, 18, 4, alpha(side_rgb[i], 0.22f));
            dm_draw_text(d, label[r], cx - 56, y + 2, 5, DM_ALIGN_LEFT, text_dim);
            dm_draw_text(d, v, cx + 22, y + 1, 7, DM_ALIGN_CENTER, white);
            if (sel) {
                dm_draw_text(d, "<", cx - 18, y + 1, 7, DM_ALIGN_CENTER, side_rgb[i]);
                dm_draw_text(d, ">", cx + 58, y + 1, 7, DM_ALIGN_RIGHT, side_rgb[i]);
            }
            if (r == DM_ROW_LEVEL && f->mode == DM_MODE_NORMAL) { /* the gauge */
                float gx = cx - 56, gw = 112;
                dm_draw_rrect(d, gx, y + 11, gw, 2, 1, dm_rgba(60, 70, 120, 255));
                dm_draw_rrect(d, gx, y + 11, fmaxf(gw * p->level / 20.0f, 2), 2, 1, side_rgb[i]);
            }
        }
        if (p->ready) dm_draw_text(d, "READY!", cx, top + 112, 9, DM_ALIGN_CENTER, (f->frame & 16) ? white : side_rgb[i]);
        else dm_draw_text(d, "PRESS A WHEN READY", cx, top + 114, 4.5f, DM_ALIGN_CENTER, text_dim);
    }
    dm_draw_text(d, status, 160, 196, 4.2f, DM_ALIGN_CENTER, dm_rgba(255, 214, 64, 255));
    dm_draw_text(d, hint, 160, 206, 4.2f, DM_ALIGN_CENTER, text_dim);
    dm_draw_text(d, f->locked ? "RANKED: SAME SETTINGS FOR BOTH   A READY   B BACK" : "UP/DOWN CHOOSE   LEFT/RIGHT CHANGE   B BACK", 160,
                 216, 4.2f, DM_ALIGN_CENTER, text_dim);
    dm_draw_text(d, version, 316, 232, 3.5f, DM_ALIGN_RIGHT, alpha(text_dim, 0.6f));
    dm_draw_text(d, "UNOFFICIAL FAN REMAKE", 4, 232, 3.5f, DM_ALIGN_LEFT, alpha(text_dim, 0.6f));
}

void dm_render(DmDraw *d, const DmFront *f, int w, int h, const char *version, const char *hint, const char *status,
               const char *banner, const char *const names[2])
{
    SDL_SetRenderDrawColor(d->ren, 0, 0, 0, 255);
    SDL_RenderClear(d->ren);
    SDL_SetRenderDrawBlendMode(d->ren, SDL_BLENDMODE_BLEND);
    dm_draw_begin(d, d->ren, w, h);
    dm_draw_backdrop(d, w, h, dm_rgba(20, 26, 78, 255), dm_rgba(6, 8, 26, 255));
    if (f->screen == DM_FRONT_SETUP) draw_setup(d, f, version, hint, status, names);
    else draw_game(d, f);
    if (banner && banner[0]) {
        /* top left, clear of the centre column's labels */
        float tw = dm_text_width(banner, 3.5f);
        dm_draw_rrect(d, 1, 1, tw + 6, 7, 2, dm_rgba(0, 0, 0, 170));
        dm_draw_text(d, banner, 4, 2.8f, 3.5f, DM_ALIGN_LEFT, dm_rgba(255, 214, 64, 255));
    }
    dm_draw_flush(d);
}

void dm_render_netui(DmDraw *d, const DmNetUi *u, const DmOnline *o, int w, int h, const char *version)
{
    SDL_SetRenderDrawColor(d->ren, 0, 0, 0, 255);
    SDL_RenderClear(d->ren);
    SDL_SetRenderDrawBlendMode(d->ren, SDL_BLENDMODE_BLEND);
    dm_draw_begin(d, d->ren, w, h);
    dm_draw_backdrop(d, w, h, dm_rgba(20, 26, 78, 255), dm_rgba(6, 8, 26, 255));
    const DmRGBA gold = dm_rgba(255, 214, 64, 255), red = dm_rgba(255, 120, 110, 255);
    dm_draw_text(d, "ONLINE VERSUS", 160, 18, 13, DM_ALIGN_CENTER, white);
    draw_virus(d, 40, 26, 0, 0, 1, 1.2f);
    draw_virus(d, 280, 26, 2, 2, 1, 1.2f);
    dm_draw_text(d, o->status, 160, 42, 5, DM_ALIGN_CENTER, text_dim);
    const char *help = "";
    if (u->screen == DM_NU_MENU) {
        for (int i = 0; i < DM_NU_ITEMS; i++) {
            float y = 70 + i * 24;
            int sel = u->sel == i, off = !o->available && i != DM_NU_BACK;
            if (sel) dm_draw_rrect(d, 70, y - 5, 180, 20, 5, alpha(side_rgb[1], 0.25f));
            dm_draw_text(d, dm_netui_item(i), 160, y, 8, DM_ALIGN_CENTER, off ? alpha(text_dim, 0.5f) : sel ? white : text_dim);
        }
        help = "UP/DOWN CHOOSE   A OK   B BACK";
    } else if (u->screen == DM_NU_CODE || u->screen == DM_NU_NAME) {
        dm_draw_text(d, u->screen == DM_NU_CODE ? "ROOM CODE" : "CHOOSE YOUR NAME", 160, 74, 8, DM_ALIGN_CENTER, gold);
        char shown[24];
        snprintf(shown, sizeof shown, "%s%s", u->text, (SDL_GetTicks() / 400) & 1 ? "_" : " ");
        panel(d, 160, 112, 220, 34, side_rgb[1]);
        dm_draw_text(d, shown, 160, 104, 14, DM_ALIGN_CENTER, white);
        if (u->screen == DM_NU_NAME) dm_draw_text(d, "3 TO 16 LETTERS, DIGITS, _ OR -  - SHARED WITH ALL WARPZONE GAMES", 160, 140, 4.2f, DM_ALIGN_CENTER, text_dim);
        help = "TYPE, OR UP/DOWN LETTER  RIGHT NEXT  LEFT DELETE  A OK  B BACK";
    } else {
        if (o->host && o->code[0] && !o->ranked) {
            dm_draw_text(d, "ROOM CODE", 160, 70, 7, DM_ALIGN_CENTER, gold);
            dm_draw_text(d, o->code, 160, 84, 22, DM_ALIGN_CENTER, white);
            dm_draw_text(d, "THE OTHER PLAYER CHOOSES JOIN WITH A CODE", 160, 116, 4.5f, DM_ALIGN_CENTER, text_dim);
        } else if (o->phase != DM_ON_IDLE) {
            draw_half(d, 150, 96, 2, (SDL_GetTicks() / 300) % 3, 1);
            draw_half(d, 160, 96, 3, (SDL_GetTicks() / 300 + 1) % 3, 1);
        }
        help = o->phase == DM_ON_IDLE ? "A OK   B BACK" : "B LEAVE";
    }
    if (o->error[0]) dm_draw_text(d, o->error, 160, 176, 5, DM_ALIGN_CENTER, red);
    if (!o->available) dm_draw_text(d, "THIS BUILD HAS NO ONLINE PLAY", 160, 166, 5, DM_ALIGN_CENTER, red);
    dm_draw_text(d, help, 160, 206, 4.2f, DM_ALIGN_CENTER, text_dim);
    dm_draw_text(d, "ONLINE THROUGH WARPZONE", 4, 232, 3.5f, DM_ALIGN_LEFT, alpha(text_dim, 0.6f));
    dm_draw_text(d, version, 316, 232, 3.5f, DM_ALIGN_RIGHT, alpha(text_dim, 0.6f));
    dm_draw_flush(d);
}

/* ---- Steam library artwork (--art): our own vector art, never anything from the ROM ---- */

/* Text sized to fit width wmax, at most cap high. */
static float fit(const char *s, float cap, float wmax)
{
    float w = dm_text_width(s, cap);
    return w > wmax ? cap * wmax / w : cap;
}

static void title(DmDraw *d, float cx, float cy, float cap, float wmax)
{
    float h = fit("DR. MARIO 64", cap, wmax);
    dm_draw_text(d, "DR. MARIO 64", cx + h * 0.06f, cy - h * 0.5f + h * 0.06f, h, DM_ALIGN_CENTER, dm_rgba(0, 0, 0, 150));
    dm_draw_text(d, "DR. MARIO 64", cx, cy - h * 0.5f, h, DM_ALIGN_CENTER, white);
    float v = h * 0.75f;
    dm_draw_text(d, "VS", cx, cy + h * 0.85f, v, DM_ALIGN_CENTER, dm_rgba(255, 214, 64, 255));
}

/* A scatter of capsules and viruses over a W×H unit canvas, deterministic. */
static void scatter(DmDraw *d, float W, float H, int n, float size)
{
    uint32_t r = 2001;
    for (int k = 0; k < n; k++) {
        r = r * 1103515245u + 12345u;
        float x = (float)((r >> 8) % 1000) / 1000.0f * (W - 2 * size);
        r = r * 1103515245u + 12345u;
        float y = (float)((r >> 8) % 1000) / 1000.0f * (H - 2 * size);
        int p = (int)(r >> 28) % 3;
        if (k & 1) draw_virus(d, x, y, p, k & 3, 0.9f, size / 10);
        else if (k & 2) { draw_half(d, x, y, 2, p, 0.9f); draw_half(d, x + 10, y, 3, (p + 1) % 3, 0.9f); }
        else { draw_half(d, x, y, 0, p, 0.9f); draw_half(d, x, y + 10, 1, (p + 2) % 3, 0.9f); }
    }
}

int dm_render_art(DmDraw *d, const char *kind, int w, int h)
{
    SDL_SetRenderDrawColor(d->ren, 0, 0, 0, 0);
    SDL_RenderClear(d->ren);
    SDL_SetRenderDrawBlendMode(d->ren, SDL_BLENDMODE_BLEND);
    dm_draw_begin(d, d->ren, w, h);
    /* A canvas 100 units high, as wide as the picture: ten-unit cells read at any size. */
    d->s = h / 100.0f;
    d->ox = d->oy = 0;
    float W = w / d->s, H = 100;
    int logo_only = !strcmp(kind, "logo");
    if (!logo_only) dm_draw_backdrop(d, w, h, dm_rgba(20, 26, 78, 255), dm_rgba(6, 8, 26, 255));
    if (!strcmp(kind, "capsule") || !strcmp(kind, "portrait")) {
        scatter(d, W, H, (int)(W * 0.12f), 10);
        dm_draw_rrect(d, W * 0.04f, H * 0.3f, W * 0.92f, H * 0.4f, 4, dm_rgba(8, 12, 34, 200));
        title(d, W / 2, H * 0.45f, 14, W * 0.84f);
    } else if (!strcmp(kind, "hero")) {
        scatter(d, W, H, 40, 9);
    } else if (logo_only) {
        title(d, W / 2, H * 0.38f, 40, W * 0.95f);
    } else if (!strcmp(kind, "icon")) {
        d->s = h / 40.0f; /* capsule halves are one cell: a smaller canvas makes them big */
        dm_draw_rrect(d, 1.5f, 1.5f, 37, 37, 7, dm_rgba(14, 18, 52, 255));
        draw_virus(d, 10, 18.5f, 0, 1, 1, 1.9f);
        draw_half(d, 25, 7, 0, 2, 1);
        draw_half(d, 25, 17, 1, 1, 1);
    } else return -1;
    dm_draw_flush(d);
    return 0;
}
