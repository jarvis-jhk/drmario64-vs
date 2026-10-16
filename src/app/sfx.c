/* Silent until the ROM effect renderer (romsfx.c) is wired in. */
#include "sfx.h"

void dm_sfx_init(const char *rom_path) { (void)rom_path; }
int dm_sfx_use_rom(const char *path) { (void)path; return -1; }
const char *dm_sfx_status(void) { return "NO SOUND IN THIS BUILD"; }
void dm_sfx_begin_tick(void) {}
void dm_sfx_play(int se, int player) { (void)se; (void)player; }
void dm_sfx_quit(void) {}
