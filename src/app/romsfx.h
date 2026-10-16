/* Sound effects rendered from the user's own ROM at first run, so the remake ships no
 * Nintendo audio. The ROM's effect bank (Music Tools "fxbank"), its sample pointer bank
 * and the VADPCM wave data are parsed here, and every effect is played once, offline,
 * through our own re-implementation of the driver behaviour the game relies on
 * (sound.c dm_audio_update / dm_snd_play, gSndsEntries; music_driver.c InitMusicDriver).
 * The result is cached in our own binary format. No SDL dependency. */
#pragma once
#include <stddef.h>
#include <stdint.h>

enum { DM_SFX_COUNT = 104 }; /* SndIndex 0..103 (SND_INDEX_MAX) */
enum { DM_SFX_RATE = 32000 }; /* musConfig.syn_output_rate in InitMusicDriver */

enum { /* DmSfxClip.flags */
    DM_SFX_F_CAPPED = 1,  /* effect never stopped by itself; cut at the render cap */
    DM_SFX_F_BADCMD = 2,  /* unknown or unsupported command; channel stopped there */
    DM_SFX_F_CUT = 4,     /* stopped by the gSndsEntries max length, like the game */
};

typedef struct DmSfxClip {
    int16_t *pcm;   /* interleaved stereo, DM_SFX_RATE, trailing silence trimmed */
    int frames;     /* stereo frames in pcm */
    int priority;   /* gSndsEntries priority (voice stealing in dm_snd_play) */
    int ticks;      /* 60 Hz frames the driver keeps the effect alive (voice busy) */
    int flags;      /* DM_SFX_F_* */
} DmSfxClip;

typedef struct DmSfxBank {
    int rate;
    DmSfxClip clip[DM_SFX_COUNT];
    /* what the ROM banks contained (0 when loaded from the cache) */
    int fx_components, fx_effects, fx_waves, ptr_waves;
} DmSfxBank;

/* Parse `rom_path` (.z64 big-endian; .v64/.n64 are byte-swapped), validate it and render
 * all effects. 0 on success; otherwise -1 with a message in err. */
int dm_romsfx_load(const char *rom_path, DmSfxBank *out, char *err, size_t errlen);
/* Same on a ROM image already in memory (any of the three byte orders; not modified). */
int dm_romsfx_load_mem(const uint8_t *rom, size_t size, DmSfxBank *out, char *err, size_t errlen);
void dm_romsfx_free(DmSfxBank *b);
/* Test hook: decode every looped wave of a .z64 image and compare the frame at the loop
 * start with the decoder state the encoder stored there. Returns the number of waves
 * checked, or -1 on a mismatch or a bad image (message in err). */
int dm_romsfx_selfcheck(const uint8_t *rom, size_t size, char *err, size_t errlen);

/* Cache: our own little-endian format, tagged with the renderer version so a newer
 * renderer re-extracts. 0 ok, -1 on I/O error or a missing/stale/corrupt cache. */
int dm_romsfx_save(const DmSfxBank *b, const char *cache_path);
int dm_romsfx_load_cache(const char *cache_path, DmSfxBank *out);
