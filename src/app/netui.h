/* The local online screens around DmOnline: the menu (host a room, join with a code,
 * ranked 1v1), name and code entry, and the waiting screen. Driven by the merged pads of
 * this machine plus typed text; no SDL. While the shared game runs, the main loop shows
 * DmOnline's front instead. */
#pragma once
#include "online.h"

enum { DM_NU_OFF, DM_NU_MENU, DM_NU_NAME, DM_NU_CODE, DM_NU_WAIT };
enum { DM_NU_HOST, DM_NU_JOIN, DM_NU_RANKED, DM_NU_BACK, DM_NU_ITEMS };

typedef struct DmNetUi {
    int screen, sel;
    char text[20];
    uint16_t prev;
    int rep;
    int was_playing;
} DmNetUi;

/* Keys typed this frame (printable ASCII), and backspace/enter/escape presses. */
typedef struct DmNetKeys { char typed[16]; int backspace, enter, escape; } DmNetKeys;

void dm_netui_open(DmNetUi *u);
/* One frame. offline is the local setup whose settings a hosted room offers. */
void dm_netui_frame(DmNetUi *u, DmOnline *o, const DmFront *offline, uint16_t pad, const DmNetKeys *keys);
const char *dm_netui_item(int i);
