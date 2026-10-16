/* SDL2 frontend: window, input, the fixed 60 Hz tick loop around DmFront, and sound.
 * --headless/--frames keep CI honest; --shot renders a frame to a BMP without a
 * display (software renderer), with --demo letting two bots play first. */
#include "bot.h"
#include "fault.h"
#include "front.h"
#include "render.h"
#include "sfx.h"
#include "version.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CONTROLS_HINT "P1 WASD F/G TAB    P2 ARROWS K/L ENTER    F11 FULLSCREEN"

/* ---- input: keyboard maps for both players, and up to two gamepads ---- */

static SDL_GameController *pads[2];

static void pad_added(int index)
{
    if (!SDL_IsGameController(index)) return;
    SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(index);
    for (int i = 0; i < 2; i++)
        if (pads[i] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads[i])) == id) return;
    for (int i = 0; i < 2; i++)
        if (!pads[i]) { pads[i] = SDL_GameControllerOpen(index); return; }
}

static void pad_removed(SDL_JoystickID id)
{
    for (int i = 0; i < 2; i++)
        if (pads[i] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads[i])) == id) {
            SDL_GameControllerClose(pads[i]);
            pads[i] = NULL;
        }
}

static uint16_t keys_for(const Uint8 *k, int player)
{
    static const SDL_Scancode map[2][10] = {
        /* up, down, left, right, A (rotate right), B (rotate left), start, C, L, R */
        { SDL_SCANCODE_W, SDL_SCANCODE_S, SDL_SCANCODE_A, SDL_SCANCODE_D, SDL_SCANCODE_G, SDL_SCANCODE_F, SDL_SCANCODE_TAB,
          SDL_SCANCODE_E, SDL_SCANCODE_Q, SDL_SCANCODE_R },
        { SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT, SDL_SCANCODE_L, SDL_SCANCODE_K,
          SDL_SCANCODE_RETURN, SDL_SCANCODE_O, SDL_SCANCODE_I, SDL_SCANCODE_P },
    };
    static const uint16_t bit[10] = { DM_BTN_UP, DM_BTN_DOWN, DM_BTN_LEFT, DM_BTN_RIGHT, DM_BTN_A, DM_BTN_B,
                                      DM_BTN_START, DM_BTN_CUP, DM_BTN_L, DM_BTN_R };
    uint16_t m = 0;
    for (int i = 0; i < 10; i++)
        if (k[map[player][i]]) m |= bit[i];
    if (player == 0 && k[SDL_SCANCODE_SPACE]) m |= DM_BTN_A;
    if (player == 1 && (k[SDL_SCANCODE_KP_ENTER])) m |= DM_BTN_START;
    return m;
}

static uint16_t pad_bits(SDL_GameController *c)
{
    if (!c) return 0;
    uint16_t m = 0;
    struct { SDL_GameControllerButton b; uint16_t bit; } map[] = {
        { SDL_CONTROLLER_BUTTON_A, DM_BTN_A }, { SDL_CONTROLLER_BUTTON_Y, DM_BTN_A },
        { SDL_CONTROLLER_BUTTON_B, DM_BTN_B }, { SDL_CONTROLLER_BUTTON_X, DM_BTN_B },
        { SDL_CONTROLLER_BUTTON_START, DM_BTN_START }, { SDL_CONTROLLER_BUTTON_BACK, DM_BTN_CUP },
        { SDL_CONTROLLER_BUTTON_LEFTSHOULDER, DM_BTN_L }, { SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, DM_BTN_R },
        { SDL_CONTROLLER_BUTTON_DPAD_UP, DM_BTN_UP }, { SDL_CONTROLLER_BUTTON_DPAD_DOWN, DM_BTN_DOWN },
        { SDL_CONTROLLER_BUTTON_DPAD_LEFT, DM_BTN_LEFT }, { SDL_CONTROLLER_BUTTON_DPAD_RIGHT, DM_BTN_RIGHT },
    };
    for (size_t i = 0; i < sizeof map / sizeof map[0]; i++)
        if (SDL_GameControllerGetButton(c, map[i].b)) m |= map[i].bit;
    /* the stick acts as a D-pad (the N64 game reads the stick as directions too) */
    const int t = 16000;
    int x = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX), y = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY);
    if (x < -t) m |= DM_BTN_LEFT;
    if (x > t) m |= DM_BTN_RIGHT;
    if (y < -t) m |= DM_BTN_UP;
    if (y > t) m |= DM_BTN_DOWN;
    int rx = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_RIGHTX), ry = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_RIGHTY);
    if (rx < -t || rx > t || ry < -t || ry > t) m |= DM_BTN_CUP;
    return m;
}

/* ---- settings: each side's last choices, like the original's per-name config ---- */

static char *settings_path(void)
{
    char *dir = SDL_GetPrefPath("jarvis-jhk", "drmario64-vs");
    if (!dir) return NULL;
    size_t n = strlen(dir) + 16;
    char *p = malloc(n);
    if (p) snprintf(p, n, "%ssettings.ini", dir);
    SDL_free(dir);
    return p;
}

static char rom_path[1024];

static void load_settings(DmFront *f, int *fullscreen)
{
    char *path = settings_path();
    FILE *fp = path ? fopen(path, "r") : NULL;
    free(path);
    if (!fp) return;
    char line[128];
    int v;
    while (fgets(line, sizeof line, fp)) {
        if (sscanf(line, "mode=%d", &v) == 1 && (v == 0 || v == 1 || v == 3)) f->mode = (DmMode)v;
        else if (sscanf(line, "wins=%d", &v) == 1 && v >= 1 && v <= 3) f->wins = v;
        else if (sscanf(line, "fullscreen=%d", &v) == 1) *fullscreen = v != 0;
        else if (!strncmp(line, "rom=", 4)) {
            snprintf(rom_path, sizeof rom_path, "%s", line + 4);
            rom_path[strcspn(rom_path, "\r\n")] = 0;
        }
        for (int i = 0; i < 2; i++) {
            char key[16];
            snprintf(key, sizeof key, "level%d=%%d", i + 1);
            if (sscanf(line, key, &v) == 1 && v >= 0 && v <= 20) f->panel[i].level = v;
            snprintf(key, sizeof key, "glevel%d=%%d", i + 1);
            if (sscanf(line, key, &v) == 1 && v >= 0 && v <= 2) f->panel[i].glevel = v;
            snprintf(key, sizeof key, "speed%d=%%d", i + 1);
            if (sscanf(line, key, &v) == 1 && v >= 0 && v <= 2) f->panel[i].speed = v;
        }
    }
    fclose(fp);
}

static void save_settings(const DmFront *f, int fullscreen)
{
    char *path = settings_path();
    FILE *fp = path ? fopen(path, "w") : NULL;
    free(path);
    if (!fp) return;
    fprintf(fp, "mode=%d\nwins=%d\nfullscreen=%d\n", (int)f->mode, f->wins, fullscreen);
    if (rom_path[0]) fprintf(fp, "rom=%s\n", rom_path);
    for (int i = 0; i < 2; i++)
        fprintf(fp, "level%d=%d\nglevel%d=%d\nspeed%d=%d\n", i + 1, f->panel[i].level, i + 1, f->panel[i].glevel, i + 1,
                f->panel[i].speed);
    fclose(fp);
}

/* ---- demo players for --demo: the bots ready up, play, and confirm menus ---- */

static uint16_t demo_pad(DmBot *b, const DmFront *f, int i)
{
    if (f->screen == DM_FRONT_SETUP) return (f->frame & 8) && !f->panel[i].ready ? DM_BTN_A : 0;
    return dm_bot_pad(b, &f->vs, i) | (dm_vs_round_state(&f->vs) && (f->frame & 8) ? DM_BTN_A : 0);
}

/* until: NULL, "round" (stop once a round is decided and its menu is up) or "pause"
 * (player 2 pauses once play has started). */
static int shot(DmFront *f, const char *path, int w, int h, long frames, int demo, const char *until, const char *version)
{
    DmBot bots[2];
    dm_bot_init(&bots[0], 1);
    dm_bot_init(&bots[1], 1);
    bots[1].tick = 5; /* not in lockstep with the other bot */
    int paused = 0;
    for (long t = 0; t < frames; t++) {
        if (until && !strcmp(until, "round") && f->screen == DM_FRONT_GAME && dm_vs_round_state(&f->vs)) break;
        uint16_t a = demo ? demo_pad(&bots[0], f, 0) : 0, b = demo ? demo_pad(&bots[1], f, 1) : 0;
        if (until && !strcmp(until, "pause") && f->vs.started && f->screen == DM_FRONT_GAME) {
            if (paused++ > 4) break;
            a = 0;
            b = paused == 1 ? DM_BTN_START : 0;
        }
        dm_front_tick(f, a, b);
    }
    if (until && !strcmp(until, "round") && !(f->screen == DM_FRONT_GAME && dm_vs_round_state(&f->vs))) {
        fprintf(stderr, "shot: no round was decided in %ld frames\n", frames);
        return 1;
    }
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer *r = s ? SDL_CreateSoftwareRenderer(s) : NULL;
    if (!r) { fprintf(stderr, "shot: %s\n", SDL_GetError()); return 1; }
    DmDraw d;
    memset(&d, 0, sizeof d);
    d.ren = r;
    dm_render(&d, f, w, h, version, CONTROLS_HINT, "");
    SDL_RenderPresent(r);
    int ok = SDL_SaveBMP(s, path) == 0;
    dm_draw_free(&d);
    SDL_DestroyRenderer(r);
    SDL_FreeSurface(s);
    return ok ? 0 : 1;
}

/* --art DIR: the Steam library pictures, rendered by the vector renderer (BMP with alpha;
 * the release turns them into PNG). Names and sizes are Steam's grid conventions. */
static int art(const char *dir)
{
    static const struct { const char *kind, *file; int w, h; } pics[] = {
        { "portrait", "steamp.bmp", 600, 900 }, { "capsule", "steam.bmp", 920, 430 },
        { "hero", "steam_hero.bmp", 1920, 620 }, { "logo", "steam_logo.bmp", 1280, 320 },
        { "icon", "steam_icon.bmp", 256, 256 },
    };
    for (size_t i = 0; i < sizeof pics / sizeof pics[0]; i++) {
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", dir, pics[i].file);
        SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, pics[i].w, pics[i].h, 32, SDL_PIXELFORMAT_ARGB8888);
        SDL_Renderer *r = s ? SDL_CreateSoftwareRenderer(s) : NULL;
        if (!r) { fprintf(stderr, "art: %s\n", SDL_GetError()); return 1; }
        DmDraw d;
        memset(&d, 0, sizeof d);
        d.ren = r;
        int ok = dm_render_art(&d, pics[i].kind, pics[i].w, pics[i].h) == 0;
        SDL_RenderPresent(r);
        ok = ok && SDL_SaveBMP(s, path) == 0;
        dm_draw_free(&d);
        SDL_DestroyRenderer(r);
        SDL_FreeSurface(s);
        if (!ok) { fprintf(stderr, "art: %s failed\n", path); return 1; }
    }
    return 0;
}

int main(int argc, char **argv)
{
    int headless = 0, demo = 0, fullscreen = 0, w = 1920, h = 1080;
    long frames = -1;
    const char *shot_path = NULL, *arg_rom = NULL, *until = NULL, *art_dir = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--headless")) headless = 1;
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = strtol(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) shot_path = argv[++i];
        else if (!strcmp(argv[i], "--size") && i + 1 < argc) sscanf(argv[++i], "%dx%d", &w, &h);
        else if (!strcmp(argv[i], "--demo")) demo = 1;
        else if (!strcmp(argv[i], "--until") && i + 1 < argc) until = argv[++i];
        else if (!strcmp(argv[i], "--fullscreen")) fullscreen = 1;
        else if (!strcmp(argv[i], "--rom") && i + 1 < argc) arg_rom = argv[++i];
        else if (!strcmp(argv[i], "--art") && i + 1 < argc) art_dir = argv[++i];
        else if (!strcmp(argv[i], "--version")) { printf("%s %s\n", DM_APP_NAME, DM_VERSION); return 0; }
    }
    const char *version = "V" DM_VERSION;
    dm_fault_install();
    static DmFront f;
    dm_front_init(&f, (uint32_t)SDL_GetPerformanceCounter() ^ (uint32_t)(uintptr_t)&f);
    if (art_dir) {
        if (SDL_Init(0) != 0) return 1;
        int rc = art(art_dir);
        SDL_Quit();
        return rc;
    }
    if (shot_path) {
        dm_front_init(&f, 2001); /* reproducible pictures */
        if (SDL_Init(0) != 0) return 1;
        int rc = shot(&f, shot_path, w, h, frames < 0 ? 1 : frames, demo, until, version);
        SDL_Quit();
        return rc;
    }
    if (getenv("SteamDeck")) fullscreen = 1;
    if (!headless) load_settings(&f, &fullscreen);
    if (SDL_Init(headless ? 0 : SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO) != 0) {
        dm_fault("SDL_Init", SDL_GetError());
        return 1;
    }
    SDL_Window *win = NULL;
    SDL_Renderer *ren = NULL;
    if (!headless) {
        SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0");
        win = SDL_CreateWindow("Dr. Mario 64 VS", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 960,
                               SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | (fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
        if (win) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (win && !ren) ren = SDL_CreateRenderer(win, -1, 0);
        if (!win || !ren) { dm_fault("window", SDL_GetError()); SDL_Quit(); return 1; }
        SDL_ShowCursor(fullscreen ? SDL_DISABLE : SDL_ENABLE);
        if (arg_rom) snprintf(rom_path, sizeof rom_path, "%s", arg_rom);
        dm_sfx_init(rom_path[0] ? rom_path : NULL);
    }
    DmDraw draw;
    memset(&draw, 0, sizeof draw);
    DmBot bots[2];
    dm_bot_init(&bots[0], 1);
    dm_bot_init(&bots[1], 1);
    bots[1].tick = 5; /* not in lockstep with the other bot */
    /* Logic advances in whole 1/60 s ticks of the performance counter, independent of the
     * display rate; the renderer draws whatever the latest tick produced. Pads are read
     * at each tick, as the original reads them once per retrace. */
    const Uint64 freq = SDL_GetPerformanceFrequency();
    Uint64 last = SDL_GetPerformanceCounter(), acc = 0;
    long ticks = 0;
    int running = 1, was_screen = f.screen;
    while (running && (frames < 0 || ticks < frames)) {
        SDL_Event e;
        int escape = 0;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            else if (e.type == SDL_CONTROLLERDEVICEADDED) pad_added(e.cdevice.which);
            else if (e.type == SDL_CONTROLLERDEVICEREMOVED) pad_removed(e.cdevice.which);
            else if (e.type == SDL_DROPFILE) { /* the ROM for the sound effects */
                if (dm_sfx_use_rom(e.drop.file) == 0) {
                    snprintf(rom_path, sizeof rom_path, "%s", e.drop.file);
                    save_settings(&f, fullscreen);
                }
                SDL_free(e.drop.file);
            }
            else if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_F11 || (k == SDLK_RETURN && (e.key.keysym.mod & KMOD_ALT))) {
                    fullscreen = !fullscreen;
                    SDL_SetWindowFullscreen(win, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                    SDL_ShowCursor(fullscreen ? SDL_DISABLE : SDL_ENABLE);
                } else if (k == SDLK_ESCAPE) {
                    if (f.screen == DM_FRONT_SETUP) running = 0;
                    else escape = 1;
                }
            }
        }
        int due = 0;
        if (headless) due = 1;
        else {
            Uint64 now = SDL_GetPerformanceCounter();
            acc += (now - last) * 60;
            last = now;
            while (acc >= freq) { acc -= freq; due++; }
            if (due > 4) due = 4; /* after a stall, catch up a little and drop the rest */
            if (!due) SDL_Delay(1); /* without vsync the loop would spin */
        }
        for (int t = 0; t < due; t++) {
            uint16_t in[2] = { 0, 0 };
            if (demo) {
                in[0] = demo_pad(&bots[0], &f, 0);
                in[1] = demo_pad(&bots[1], &f, 1);
            } else if (!headless) {
                const Uint8 *k = SDL_GetKeyboardState(NULL);
                for (int i = 0; i < 2; i++) in[i] = keys_for(k, i) | pad_bits(pads[i]);
                if (escape) in[0] |= DM_BTN_START; /* Esc pauses, like START */
                escape = 0;
            }
            dm_front_tick(&f, in[0], in[1]);
            dm_sfx_begin_tick();
            for (int i = 0; i < f.se_n; i++) dm_sfx_play(f.se[i], f.se_player[i]);
            ticks++;
            if (f.screen != was_screen) {
                if (f.screen == DM_FRONT_GAME && !headless) save_settings(&f, fullscreen);
                was_screen = f.screen;
            }
        }
        if (!headless) {
            int dw, dh;
            SDL_GetRendererOutputSize(ren, &dw, &dh);
            draw.ren = ren;
            dm_render(&draw, &f, dw, dh, version, CONTROLS_HINT, dm_sfx_status());
            SDL_RenderPresent(ren);
        }
    }
    if (!headless) save_settings(&f, fullscreen);
    dm_sfx_quit();
    dm_draw_free(&draw);
    dm_front_free(&f);
    for (int i = 0; i < 2; i++)
        if (pads[i]) SDL_GameControllerClose(pads[i]);
    if (ren) SDL_DestroyRenderer(ren);
    if (win) SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
