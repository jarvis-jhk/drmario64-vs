/* Resolution-free drawing: anti-aliased convex shapes, strokes and a stroke font, in
 * the original's 320×240 screen units, batched into one SDL_RenderGeometry call. */
#pragma once
#include <SDL.h>

typedef struct DmRGBA { unsigned char r, g, b, a; } DmRGBA;

typedef struct DmDraw {
    SDL_Renderer *ren;
    float s, ox, oy;            /* screen unit -> device pixel */
    SDL_Vertex *v;
    int *idx;
    int nv, ni, cv, ci;
} DmDraw;

void dm_draw_begin(DmDraw *d, SDL_Renderer *ren, int w, int h);
void dm_draw_flush(DmDraw *d);
void dm_draw_free(DmDraw *d);

/* A vertical gradient over the whole device, letterbox included. */
void dm_draw_backdrop(DmDraw *d, int w, int h, DmRGBA top, DmRGBA bottom);
void dm_draw_circle(DmDraw *d, float cx, float cy, float r, DmRGBA c);
void dm_draw_ellipse(DmDraw *d, float cx, float cy, float rx, float ry, DmRGBA c);
/* Rounded rectangle with per-corner radii: top-left, top-right, bottom-right, bottom-left. */
void dm_draw_rrect4(DmDraw *d, float x, float y, float w, float h, const float rad[4], DmRGBA c);
void dm_draw_rrect(DmDraw *d, float x, float y, float w, float h, float rad, DmRGBA c);
void dm_draw_line(DmDraw *d, float x0, float y0, float x1, float y1, float width, DmRGBA c);
/* A closed outline through n points, any shape. */
void dm_draw_outline(DmDraw *d, const float *xy, int n, float width, DmRGBA c);
void dm_draw_poly(DmDraw *d, const float *xy, int n, DmRGBA c); /* convex */

enum { DM_ALIGN_LEFT, DM_ALIGN_CENTER, DM_ALIGN_RIGHT };
/* Text in the stroke font; h is the cap height. Returns the width drawn. */
float dm_draw_text(DmDraw *d, const char *s, float x, float y, float h, int align, DmRGBA c);
float dm_text_width(const char *s, float h);

static inline DmRGBA dm_rgba(int r, int g, int b, int a) { DmRGBA c = { (unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)a }; return c; }
