/* SDL2 frontend skeleton (M0): opens a window, runs the fixed 60 Hz tick loop and
 * exits. The VS game itself arrives in M2; --headless/--frames keep CI honest. */
#include "fault.h"
#include "rng.h"
#include "version.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    int headless = 0;
    long frames = -1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--headless")) headless = 1;
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = strtol(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--version")) { printf("%s %s\n", DM_APP_NAME, DM_VERSION); return 0; }
    }
    dm_fault_install();
    if (SDL_Init(headless ? 0 : SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        dm_fault("SDL_Init", SDL_GetError());
        return 1;
    }
    SDL_Window *win = NULL;
    SDL_Renderer *ren = NULL;
    if (!headless) {
        win = SDL_CreateWindow("Dr. Mario 64 VS", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 960,
                               SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
        if (win) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC);
        if (!win || !ren) { dm_fault("window", SDL_GetError()); SDL_Quit(); return 1; }
    }
    /* Logic advances in whole 1/60 s ticks of the performance counter, independent of the
     * display rate; the renderer draws whatever the latest tick produced. */
    const Uint64 freq = SDL_GetPerformanceFrequency();
    Uint64 last = SDL_GetPerformanceCounter(), acc = 0;
    long ticks = 0;
    int running = 1;
    while (running && (frames < 0 || ticks < frames)) {
        SDL_Event e;
        while (SDL_PollEvent(&e))
            if (e.type == SDL_QUIT) running = 0;
        if (headless) { ticks++; continue; }
        Uint64 now = SDL_GetPerformanceCounter();
        acc += (now - last) * 60;
        last = now;
        while (acc >= freq) { acc -= freq; ticks++; }
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        SDL_RenderPresent(ren);
    }
    if (ren) SDL_DestroyRenderer(ren);
    if (win) SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
