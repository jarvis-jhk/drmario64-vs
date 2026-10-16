#pragma once
#include "draw.h"
#include "front.h"
#include "netui.h"

/* Draw the current screen into a w×h device. The setup screen shows hint (the controls)
 * and status (whether there is sound). Online, banner is the connection line at the top
 * and names (or NULL) label the two sides. */
void dm_render(DmDraw *d, const DmFront *f, int w, int h, const char *version, const char *hint, const char *status,
               const char *banner, const char *const names[2]);
/* The local online screens (menu, name/code entry, waiting). */
void dm_render_netui(DmDraw *d, const DmNetUi *u, const DmOnline *o, int w, int h, const char *version);

/* Steam artwork of the given kind (capsule, portrait, hero, logo, icon) over w×h; -1 if unknown. */
int dm_render_art(DmDraw *d, const char *kind, int w, int h);
