#pragma once
#include "draw.h"
#include "front.h"

/* Draw the current screen into a w×h device. The setup screen shows hint (the controls)
 * and status (whether there is sound). */
void dm_render(DmDraw *d, const DmFront *f, int w, int h, const char *version, const char *hint, const char *status);

/* Steam artwork of the given kind (capsule, portrait, hero, logo, icon) over w×h; -1 if unknown. */
int dm_render_art(DmDraw *d, const char *kind, int w, int h);
