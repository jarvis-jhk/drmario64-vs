#include "draw.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void dm_draw_begin(DmDraw *d, SDL_Renderer *ren, int w, int h)
{
    d->ren = ren;
    d->s = fminf(w / 320.0f, h / 240.0f);
    d->ox = (w - 320.0f * d->s) * 0.5f;
    d->oy = (h - 240.0f * d->s) * 0.5f;
    d->nv = d->ni = 0;
}

void dm_draw_flush(DmDraw *d)
{
    if (d->ni) SDL_RenderGeometry(d->ren, NULL, d->v, d->nv, d->idx, d->ni);
    d->nv = d->ni = 0;
}

void dm_draw_free(DmDraw *d)
{
    free(d->v);
    free(d->idx);
    memset(d, 0, sizeof *d);
}

static int reserve(DmDraw *d, int nv, int ni)
{
    if (d->nv + nv > d->cv) {
        int c = d->cv ? d->cv : 8192;
        while (c < d->nv + nv) c *= 2;
        SDL_Vertex *n = realloc(d->v, (size_t)c * sizeof *n);
        if (!n) return 0;
        d->v = n;
        d->cv = c;
    }
    if (d->ni + ni > d->ci) {
        int c = d->ci ? d->ci : 16384;
        while (c < d->ni + ni) c *= 2;
        int *n = realloc(d->idx, (size_t)c * sizeof *n);
        if (!n) return 0;
        d->idx = n;
        d->ci = c;
    }
    return 1;
}

static int vert(DmDraw *d, float x, float y, DmRGBA c, float alpha)
{
    SDL_Vertex *v = &d->v[d->nv];
    v->position.x = x;
    v->position.y = y;
    v->color.r = c.r;
    v->color.g = c.g;
    v->color.b = c.b;
    v->color.a = (Uint8)(c.a * alpha + 0.5f);
    v->tex_coord.x = v->tex_coord.y = 0;
    return d->nv++;
}

static void tri(DmDraw *d, int a, int b, int c)
{
    d->idx[d->ni++] = a;
    d->idx[d->ni++] = b;
    d->idx[d->ni++] = c;
}

/* Miter normals of a closed path in device pixels, pointing outwards whatever the winding. */
static void normals(const float *p, int n, float *nm)
{
    float area = 0;
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        area += p[2 * i] * p[2 * j + 1] - p[2 * j] * p[2 * i + 1];
    }
    float sgn = area >= 0 ? 1.0f : -1.0f; /* y points down */
    for (int i = 0; i < n; i++) {
        int a = (i + n - 1) % n, b = (i + 1) % n;
        float e1x = p[2 * i] - p[2 * a], e1y = p[2 * i + 1] - p[2 * a + 1];
        float e2x = p[2 * b] - p[2 * i], e2y = p[2 * b + 1] - p[2 * i + 1];
        float l1 = sqrtf(e1x * e1x + e1y * e1y), l2 = sqrtf(e2x * e2x + e2y * e2y);
        if (l1 < 1e-6f) l1 = 1;
        if (l2 < 1e-6f) l2 = 1;
        float n1x = e1y / l1 * sgn, n1y = -e1x / l1 * sgn, n2x = e2y / l2 * sgn, n2y = -e2x / l2 * sgn;
        float mx = n1x + n2x, my = n1y + n2y, ml = sqrtf(mx * mx + my * my);
        if (ml < 1e-6f) { mx = n1x; my = n1y; ml = 1; }
        mx /= ml;
        my /= ml;
        float dot = mx * n1x + my * n1y;
        float k = dot > 0.25f ? 1.0f / dot : 4.0f;
        nm[2 * i] = mx * k;
        nm[2 * i + 1] = my * k;
    }
}

/* A convex polygon already in device pixels, with a one-pixel soft edge. */
static void poly_dev(DmDraw *d, const float *p, int n, DmRGBA c)
{
    if (n < 3 || c.a == 0 || !reserve(d, 2 * n, 9 * n)) return;
    float nmb[2 * 260];
    float *nm = n <= 260 ? nmb : malloc(sizeof(float) * 2 * (size_t)n);
    if (!nm) return;
    normals(p, n, nm);
    int base = d->nv;
    for (int i = 0; i < n; i++) {
        vert(d, p[2 * i] - nm[2 * i] * 0.5f, p[2 * i + 1] - nm[2 * i + 1] * 0.5f, c, 1);
        vert(d, p[2 * i] + nm[2 * i] * 0.5f, p[2 * i + 1] + nm[2 * i + 1] * 0.5f, c, 0);
    }
    for (int i = 1; i + 1 < n; i++) tri(d, base, base + 2 * i, base + 2 * i + 2);
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        tri(d, base + 2 * i, base + 2 * i + 1, base + 2 * j);
        tri(d, base + 2 * j, base + 2 * i + 1, base + 2 * j + 1);
    }
    if (nm != nmb) free(nm);
}

static float dx(DmDraw *d, float x) { return d->ox + x * d->s; }
static float dy(DmDraw *d, float y) { return d->oy + y * d->s; }

static int segs(DmDraw *d, float r)
{
    int n = (int)(r * d->s * 1.1f) + 10;
    return n < 10 ? 10 : n > 160 ? 160 : n;
}

void dm_draw_poly(DmDraw *d, const float *xy, int n, DmRGBA c)
{
    float p[2 * 260];
    if (n > 260) return;
    for (int i = 0; i < n; i++) {
        p[2 * i] = dx(d, xy[2 * i]);
        p[2 * i + 1] = dy(d, xy[2 * i + 1]);
    }
    poly_dev(d, p, n, c);
}

void dm_draw_ellipse(DmDraw *d, float cx, float cy, float rx, float ry, DmRGBA c)
{
    float p[2 * 160];
    int n = segs(d, fmaxf(rx, ry));
    for (int i = 0; i < n; i++) {
        float a = (float)(2 * M_PI * i / n);
        p[2 * i] = dx(d, cx + cosf(a) * rx);
        p[2 * i + 1] = dy(d, cy + sinf(a) * ry);
    }
    poly_dev(d, p, n, c);
}

void dm_draw_circle(DmDraw *d, float cx, float cy, float r, DmRGBA c) { dm_draw_ellipse(d, cx, cy, r, r, c); }

/* Points of a rounded rectangle, clockwise from the top-left corner's start. */
static int rrect_path(DmDraw *d, float x, float y, float w, float h, const float rad[4], float *p, int dev)
{
    const float cxs[4] = { x, x + w, x + w, x }, cys[4] = { y, y, y + h, y + h };
    const float sx[4] = { 1, -1, -1, 1 }, sy[4] = { 1, 1, -1, -1 };
    const float a0[4] = { (float)M_PI, (float)(1.5 * M_PI), 0, (float)(0.5 * M_PI) };
    int n = 0;
    for (int k = 0; k < 4; k++) {
        float r = fminf(rad[k], fminf(w, h) * 0.5f);
        float ox = cxs[k] + sx[k] * r, oy = cys[k] + sy[k] * r;
        int m = r * d->s < 0.5f ? 1 : segs(d, r) / 4 + 2;
        for (int i = 0; i < m; i++) {
            float a = a0[k] + (float)(0.5 * M_PI) * (m == 1 ? 0.5f : (float)i / (m - 1));
            float px = m == 1 ? cxs[k] : ox + cosf(a) * r, py = m == 1 ? cys[k] : oy + sinf(a) * r;
            p[2 * n] = dev ? dx(d, px) : px;
            p[2 * n + 1] = dev ? dy(d, py) : py;
            n++;
        }
    }
    return n;
}

void dm_draw_rrect4(DmDraw *d, float x, float y, float w, float h, const float rad[4], DmRGBA c)
{
    float p[2 * 260];
    if (w <= 0 || h <= 0) return;
    poly_dev(d, p, rrect_path(d, x, y, w, h, rad, p, 1), c);
}

void dm_draw_rrect(DmDraw *d, float x, float y, float w, float h, float rad, DmRGBA c)
{
    const float r[4] = { rad, rad, rad, rad };
    dm_draw_rrect4(d, x, y, w, h, r, c);
}

void dm_draw_line(DmDraw *d, float x0, float y0, float x1, float y1, float width, DmRGBA c)
{
    float hw = width * 0.5f * d->s, alpha = 1;
    if (hw < 0.5f) { alpha = hw * 2; hw = 0.5f; }
    c.a = (unsigned char)(c.a * alpha);
    float ax = dx(d, x0), ay = dy(d, y0), bx = dx(d, x1), by = dy(d, y1);
    float ang = atan2f(by - ay, bx - ax);
    int m = (int)(hw * 1.2f) + 4;
    if (m > 64) m = 64;
    float p[2 * 132];
    int n = 0;
    for (int i = 0; i <= m; i++) { /* the cap around b, then the one around a */
        float a = ang - (float)(M_PI / 2) + (float)M_PI * i / m;
        p[2 * n] = bx + cosf(a) * hw;
        p[2 * n + 1] = by + sinf(a) * hw;
        n++;
    }
    for (int i = 0; i <= m; i++) {
        float a = ang + (float)(M_PI / 2) + (float)M_PI * i / m;
        p[2 * n] = ax + cosf(a) * hw;
        p[2 * n + 1] = ay + sinf(a) * hw;
        n++;
    }
    poly_dev(d, p, n, c);
}

void dm_draw_outline(DmDraw *d, const float *xy, int n, float width, DmRGBA c)
{
    if (n < 2 || n > 260 || !reserve(d, 4 * n, 18 * n)) return;
    float p[2 * 260], nm[2 * 260];
    for (int i = 0; i < n; i++) {
        p[2 * i] = dx(d, xy[2 * i]);
        p[2 * i + 1] = dy(d, xy[2 * i + 1]);
    }
    normals(p, n, nm);
    float hw = width * 0.5f * d->s, alpha = 1;
    if (hw < 0.5f) { alpha = hw * 2; hw = 0.5f; }
    const float off[4] = { hw + 0.5f, hw - 0.5f, -(hw - 0.5f), -(hw + 0.5f) };
    const float al[4] = { 0, alpha, alpha, 0 };
    int base = d->nv;
    for (int i = 0; i < n; i++)
        for (int k = 0; k < 4; k++)
            vert(d, p[2 * i] + nm[2 * i] * off[k], p[2 * i + 1] + nm[2 * i + 1] * off[k], c, al[k]);
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        for (int k = 0; k < 3; k++) {
            int a = base + 4 * i + k, b = base + 4 * j + k;
            tri(d, a, a + 1, b);
            tri(d, b, a + 1, b + 1);
        }
    }
}

void dm_draw_backdrop(DmDraw *d, int w, int h, DmRGBA top, DmRGBA bottom)
{
    if (!reserve(d, 4, 6)) return;
    int b = d->nv;
    vert(d, 0, 0, top, 1);
    vert(d, (float)w, 0, top, 1);
    vert(d, (float)w, (float)h, bottom, 1);
    vert(d, 0, (float)h, bottom, 1);
    tri(d, b, b + 1, b + 2);
    tri(d, b, b + 2, b + 3);
}

/* ---- the stroke font: polylines on a 4×6 grid, '|' starts a new stroke ---- */

static const char *glyph(char ch)
{
    switch (ch) {
    case '0': return "1,0 3,0 4,1 4,5 3,6 1,6 0,5 0,1 1,0";
    case '1': return "1,1 2,0 2,6|1,6 3,6";
    case '2': return "0,1 1,0 3,0 4,1 4,2 0,6 4,6";
    case '3': return "0,0 4,0 2,2.5 3,2.5 4,3.5 4,5 3,6 1,6 0,5";
    case '4': return "3,6 3,0 0,4 4,4";
    case '5': return "4,0 0,0 0,2.8 3,2.8 4,3.8 4,5 3,6 0,6";
    case '6': return "3.5,0 1.5,0 0,1.5 0,5 1,6 3,6 4,5 4,4 3,3 0,3";
    case '7': return "0,0 4,0 1.5,6";
    case '8': return "1,3 0,2 0,1 1,0 3,0 4,1 4,2 3,3 1,3 0,4 0,5 1,6 3,6 4,5 4,4 3,3";
    case '9': return "4,3 1,3 0,2 0,1 1,0 3,0 4,1 4,4.5 2.5,6 0.5,6";
    case 'A': return "0,6 0,2 2,0 4,2 4,6|0,4 4,4";
    case 'B': return "0,3 3,3 4,4 4,5 3,6 0,6 0,0 3,0 4,1 4,2 3,3";
    case 'C': return "4,1 3,0 1,0 0,1 0,5 1,6 3,6 4,5";
    case 'D': return "0,0 0,6 2,6 4,4 4,2 2,0 0,0";
    case 'E': return "4,0 0,0 0,6 4,6|0,3 3,3";
    case 'F': return "4,0 0,0 0,6|0,3 3,3";
    case 'G': return "4,1 3,0 1,0 0,1 0,5 1,6 3,6 4,5 4,3 2,3";
    case 'H': return "0,0 0,6|4,0 4,6|0,3 4,3";
    case 'I': return "1,0 3,0|2,0 2,6|1,6 3,6";
    case 'J': return "4,0 4,5 3,6 1,6 0,5";
    case 'K': return "0,0 0,6|4,0 0,4|1.3,3 4,6";
    case 'L': return "0,0 0,6 4,6";
    case 'M': return "0,6 0,0 2,3 4,0 4,6";
    case 'N': return "0,6 0,0 4,6 4,0";
    case 'O': return "1,0 3,0 4,1 4,5 3,6 1,6 0,5 0,1 1,0";
    case 'P': return "0,6 0,0 3,0 4,1 4,2 3,3 0,3";
    case 'Q': return "1,0 3,0 4,1 4,5 3,6 1,6 0,5 0,1 1,0|2.5,4.5 4,6";
    case 'R': return "0,6 0,0 3,0 4,1 4,2 3,3 0,3|2,3 4,6";
    case 'S': return "4,1 3,0 1,0 0,1 0,2 1,3 3,3 4,4 4,5 3,6 1,6 0,5";
    case 'T': return "0,0 4,0|2,0 2,6";
    case 'U': return "0,0 0,5 1,6 3,6 4,5 4,0";
    case 'V': return "0,0 2,6 4,0";
    case 'W': return "0,0 1,6 2,3 3,6 4,0";
    case 'X': return "0,0 4,6|4,0 0,6";
    case 'Y': return "0,0 2,3 4,0|2,3 2,6";
    case 'Z': return "0,0 4,0 0,6 4,6";
    case ':': return "2,1.6 2,1.7|2,4.4 2,4.5";
    case '-': return "1,3 3,3";
    case '_': return "0,6 4,6";
    case '+': return "0.5,3 3.5,3|2,1.5 2,4.5";
    case '.': return "2,5.9 2,6";
    case '!': return "2,0 2,4|2,5.9 2,6";
    case '/': return "4,0 0,6";
    case '<': return "3,0 0,3 3,6";
    case '>': return "1,0 4,3 1,6";
    case '?': return "0,1 1,0 3,0 4,1 4,2 2,3.5 2,4|2,5.9 2,6";
    case '\'': return "2,0 2,1.5";
    default: return NULL;
    }
}

float dm_text_width(const char *s, float h)
{
    size_t n = strlen(s);
    return n ? (n * 6 - 2) * h / 6 : 0;
}

float dm_draw_text(DmDraw *d, const char *s, float x, float y, float h, int align, DmRGBA c)
{
    float u = h / 6, w = dm_text_width(s, h), lw = u * 1.05f;
    if (align == DM_ALIGN_CENTER) x -= w / 2;
    else if (align == DM_ALIGN_RIGHT) x -= w;
    for (; *s; s++, x += 6 * u) {
        char ch = *s >= 'a' && *s <= 'z' ? (char)(*s - 32) : *s;
        const char *g = glyph(ch);
        if (!g) continue;
        float px = 0, py = 0;
        int have = 0;
        while (*g) {
            if (*g == '|') { have = 0; g++; continue; }
            if (*g == ' ') { g++; continue; }
            char *e;
            float gx = strtof(g, &e);
            if (*e != ',') break;
            float gy = strtof(e + 1, &e);
            g = e;
            float qx = x + gx * u, qy = y + gy * u;
            if (have) dm_draw_line(d, px, py, qx, qy, lw, c);
            px = qx;
            py = qy;
            have = 1;
        }
    }
    return w;
}
