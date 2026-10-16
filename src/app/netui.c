#include "netui.h"

#include <ctype.h>
#include <string.h>

static const char code_chars[] = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";
static const char name_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-";

const char *dm_netui_item(int i)
{
    static const char *items[DM_NU_ITEMS] = { "HOST A ROOM", "JOIN WITH A CODE", "RANKED 1 VS 1", "BACK" };
    return i >= 0 && i < DM_NU_ITEMS ? items[i] : "";
}

void dm_netui_open(DmNetUi *u)
{
    memset(u, 0, sizeof *u);
    u->screen = DM_NU_MENU;
    u->prev = 0xFFFF; /* the press that opened it is not a new one */
}

/* New presses, with up/down/left/right repeating at the frontend's 12/6 frames. */
static uint16_t presses(DmNetUi *u, uint16_t pad)
{
    uint16_t press = pad & ~u->prev;
    u->prev = pad;
    uint16_t dirs = pad & (DM_BTN_UP | DM_BTN_DOWN | DM_BTN_LEFT | DM_BTN_RIGHT);
    if (!dirs || (press & dirs)) u->rep = 0;
    else if (++u->rep >= 12) { press |= dirs; u->rep = 6; }
    return press;
}

static void entry(DmNetUi *u, const char *chars, size_t max)
{
    u->text[0] = 0;
    (void)chars;
    (void)max;
}

static int allowed(const char *chars, char c) { return c && strchr(chars, c) != NULL; }

/* 1 confirmed, -1 cancelled, 0 still typing */
static int edit(DmNetUi *u, const char *chars, size_t max, uint16_t k, const DmNetKeys *keys)
{
    size_t n = strlen(u->text);
    for (const char *t = keys->typed; *t; t++) {
        char c = chars == code_chars ? (char)toupper((unsigned char)*t) : *t;
        if (allowed(chars, c) && n < max) { u->text[n++] = c; u->text[n] = 0; }
    }
    if (keys->backspace && n) u->text[--n] = 0;
    if (keys->enter) return n ? 1 : 0;
    if (keys->escape) return -1;
    int dy = (k & DM_BTN_DOWN ? 1 : 0) - (k & DM_BTN_UP ? 1 : 0);
    if (dy) {
        size_t len = strlen(chars);
        if (!n) { u->text[0] = chars[0]; u->text[1] = 0; n = 1; dy = 0; }
        const char *at = strchr(chars, u->text[n - 1]);
        size_t i = at ? (size_t)(at - chars) : 0;
        u->text[n - 1] = chars[(i + len + (size_t)dy) % len];
    }
    if ((k & DM_BTN_RIGHT) && n && n < max) { u->text[n] = chars[0]; u->text[n + 1] = 0; n++; }
    if (k & (DM_BTN_LEFT | DM_BTN_B)) {
        if (n) u->text[--n] = 0;
        else if (k & DM_BTN_B) return -1;
    }
    if (k & (DM_BTN_A | DM_BTN_START)) return n ? 1 : 0;
    return 0;
}

void dm_netui_frame(DmNetUi *u, DmOnline *o, const DmFront *offline, uint16_t pad, const DmNetKeys *keys)
{
    uint16_t k = presses(u, pad);
    if (o->phase == DM_ON_PLAYING) { u->was_playing = 1; return; }
    if (u->was_playing) { /* the game ended: wait on in the room, or back to the menu */
        u->was_playing = 0;
        u->screen = o->phase == DM_ON_IDLE ? DM_NU_MENU : DM_NU_WAIT;
        u->prev = 0xFFFF;
        return;
    }
    switch (u->screen) {
    case DM_NU_MENU: {
        int dy = (k & DM_BTN_DOWN ? 1 : 0) - (k & DM_BTN_UP ? 1 : 0);
        u->sel = (u->sel + dy + DM_NU_ITEMS) % DM_NU_ITEMS;
        if (keys->escape || (k & DM_BTN_B)) { u->screen = DM_NU_OFF; return; }
        if (!(k & (DM_BTN_A | DM_BTN_START)) && !keys->enter) return;
        o->error[0] = 0;
        if (u->sel == DM_NU_BACK) { u->screen = DM_NU_OFF; return; }
        if (!o->available) return;
        if (u->sel == DM_NU_JOIN) { entry(u, code_chars, 6); u->screen = DM_NU_CODE; return; }
        if (u->sel == DM_NU_HOST) dm_online_host(o, offline);
        else dm_online_queue(o);
        u->screen = o->need_name ? DM_NU_NAME : DM_NU_WAIT;
        if (u->screen == DM_NU_NAME) entry(u, name_chars, 16);
        return;
    }
    case DM_NU_CODE: {
        int r = edit(u, code_chars, 6, k, keys);
        if (r < 0) u->screen = DM_NU_MENU;
        if (r > 0 && dm_online_is_code(u->text)) {
            dm_online_join_code(o, u->text);
            u->screen = o->need_name ? DM_NU_NAME : DM_NU_WAIT;
            if (u->screen == DM_NU_NAME) entry(u, name_chars, 16);
        }
        return;
    }
    case DM_NU_NAME: {
        int r = edit(u, name_chars, 16, k, keys);
        if (r < 0) { dm_online_leave(o); u->screen = DM_NU_MENU; }
        if (r > 0 && strlen(u->text) >= 3) {
            dm_online_register(o, u->text);
            o->need_name = 0; /* an error sets it again */
            u->screen = DM_NU_WAIT;
        }
        return;
    }
    case DM_NU_WAIT:
        if (o->need_name && o->pending) { u->screen = DM_NU_NAME; return; }
        if (keys->escape || (k & DM_BTN_B)) {
            dm_online_leave(o);
            u->screen = DM_NU_MENU;
        } else if (o->phase == DM_ON_IDLE && !o->pending && (k & (DM_BTN_A | DM_BTN_START))) u->screen = DM_NU_MENU;
        return;
    default: return;
    }
}
