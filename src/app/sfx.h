/* Sound effects. Nintendo's samples are not shipped: they are rendered from the
 * player's own ROM (romsfx.c) the first time it is given, and cached in the user's
 * data directory. Without a ROM the game is silent. */
#pragma once

/* The cache wins; else rom_path (may be NULL), WARPZONE_DATA_ROM and DM_ROM are tried. */
void dm_sfx_init(const char *rom_path);
/* Render the effects from this ROM, cache them and switch sound on. 0 on success. */
int dm_sfx_use_rom(const char *path);
/* One line for the setup screen. */
const char *dm_sfx_status(void);
/* The original never queues one effect twice in the same tick. */
void dm_sfx_begin_tick(void);
/* An engine SE id; a character voice (DM_SE_VOICE + k) needs the player it belongs to. */
void dm_sfx_play(int se, int player);
void dm_sfx_quit(void);
