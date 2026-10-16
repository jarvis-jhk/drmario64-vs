#pragma once
#include "draw.h"
#include "front.h"

/* Draw the current screen into a w×h device. The setup screen shows hint (the controls)
 * and status (whether there is sound). */
void dm_render(DmDraw *d, const DmFront *f, int w, int h, const char *version, const char *hint, const char *status);
