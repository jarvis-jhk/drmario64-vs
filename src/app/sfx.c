#include "sfx.h"

#include "romsfx.h"
#include "vs.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The original's playback model (SPEC 03 §4.1): four effect voices; a new effect takes
 * a free voice, else steals the lowest-priority voice whose priority is not above its
 * own, else it is dropped. */
enum { VOICES = 4 };
typedef struct Voice { const DmSfxClip *clip; int pos, priority; } Voice;

static SDL_AudioDeviceID dev;
static DmSfxBank bank;
static int loaded;
static Voice voice[VOICES];
static int tick_se[16], tick_n;
static char status[96] = "NO SOUND: DROP YOUR DR. MARIO 64 (US) ROM ON THIS WINDOW";

/* _charSE_tbl for the default VS characters, charNo 0 and 1 (main1x.c:178). */
static const int voice_base[2] = { 91, 81 };

static void mix(void *user, Uint8 *out, int len)
{
    (void)user;
    int16_t *o = (int16_t *)out;
    int frames = len / 4;
    for (int i = 0; i < frames; i++) {
        int l = 0, r = 0;
        for (int v = 0; v < VOICES; v++) {
            Voice *s = &voice[v];
            if (!s->clip) continue;
            l += s->clip->pcm[2 * s->pos];
            r += s->clip->pcm[2 * s->pos + 1];
            if (++s->pos >= s->clip->frames) s->clip = NULL;
        }
        o[2 * i] = (int16_t)(l > 32767 ? 32767 : l < -32768 ? -32768 : l);
        o[2 * i + 1] = (int16_t)(r > 32767 ? 32767 : r < -32768 ? -32768 : r);
    }
}

static char *pref(const char *name)
{
    char *dir = SDL_GetPrefPath("jarvis-jhk", "drmario64-vs");
    if (!dir) return NULL;
    size_t n = strlen(dir) + strlen(name) + 1;
    char *p = malloc(n);
    if (p) snprintf(p, n, "%s%s", dir, name);
    SDL_free(dir);
    return p;
}

static void open_device(void)
{
    if (dev) SDL_CloseAudioDevice(dev);
    dev = 0;
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = bank.rate;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 512; /* ~11 ms at 44.1 kHz: an effect starts within the frame it is asked for */
    want.callback = mix;
    dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!dev) {
        snprintf(status, sizeof status, "NO SOUND: %s", SDL_GetError());
        return;
    }
    SDL_PauseAudioDevice(dev, 0);
}

static void loaded_bank(void)
{
    loaded = 1;
    snprintf(status, sizeof status, "SOUND EFFECTS FROM YOUR ROM");
    if (SDL_WasInit(SDL_INIT_AUDIO)) open_device();
}

int dm_sfx_use_rom(const char *path)
{
    DmSfxBank b;
    char err[160];
    memset(&b, 0, sizeof b);
    if (dm_romsfx_load(path, &b, err, sizeof err) != 0) {
        snprintf(status, sizeof status, "NOT USABLE AS ROM: %.60s", err);
        return -1;
    }
    if (dev) SDL_LockAudioDevice(dev);
    memset(voice, 0, sizeof voice);
    if (loaded) dm_romsfx_free(&bank);
    bank = b;
    if (dev) SDL_UnlockAudioDevice(dev);
    char *cache = pref("sfx-cache.bin");
    if (cache) dm_romsfx_save(&bank, cache);
    free(cache);
    loaded_bank();
    return 0;
}

/* The cache comes first: it is rendered from the one supported ROM, so re-reading a ROM
 * would give the same bank. That keeps a start cheap when the Warpzone launcher passes
 * --rom on every start. A dropped ROM (dm_sfx_use_rom) still re-imports. */
void dm_sfx_init(const char *rom_path)
{
    char *cache = pref("sfx-cache.bin");
    if (cache && dm_romsfx_load_cache(cache, &bank) == 0) {
        free(cache);
        loaded_bank();
        return;
    }
    free(cache);
    /* the launcher's find (WARPZONE_DATA_ROM, docs/integration.md of jarvis-jhk/warpzone) */
    const char *src[3] = { rom_path, getenv("WARPZONE_DATA_ROM"), getenv("DM_ROM") };
    for (int i = 0; i < 3; i++)
        if (src[i] && *src[i] && dm_sfx_use_rom(src[i]) == 0) return;
}

const char *dm_sfx_status(void) { return status; }

void dm_sfx_begin_tick(void) { tick_n = 0; }

void dm_sfx_play(int se, int player)
{
    if (se >= DM_SE_VOICE) se = voice_base[player == 1] + (se - DM_SE_VOICE);
    if (!loaded || !dev || se < 0 || se >= DM_SFX_COUNT || !bank.clip[se].frames) return;
    for (int i = 0; i < tick_n; i++)
        if (tick_se[i] == se) return;
    if (tick_n < 16) tick_se[tick_n++] = se;
    const DmSfxClip *c = &bank.clip[se];
    SDL_LockAudioDevice(dev);
    int pick = -1;
    for (int v = 0; v < VOICES && pick < 0; v++)
        if (!voice[v].clip) pick = v;
    if (pick < 0) /* steal the weakest that is not stronger */
        for (int v = 0; v < VOICES; v++)
            if (voice[v].priority <= c->priority && (pick < 0 || voice[v].priority < voice[pick].priority)) pick = v;
    if (pick >= 0) {
        voice[pick].clip = c;
        voice[pick].pos = 0;
        voice[pick].priority = c->priority;
    }
    SDL_UnlockAudioDevice(dev);
}

void dm_sfx_quit(void)
{
    if (dev) SDL_CloseAudioDevice(dev);
    dev = 0;
    if (loaded) dm_romsfx_free(&bank);
    loaded = 0;
}
