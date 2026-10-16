/* Sound effects from the user's ROM (see romsfx.h and SPEC part 03 §4).
 *
 * Three uncompressed segments of the retail US ROM are read: the VADPCM wave data
 * ("N64 WaveTables"), the sample pointer bank ("N64 PtrTablesV2") and the effect bank.
 * Each effect is a short Music Tools command stream. We play it the way the game's
 * driver would, one 60 Hz player tick at a time, into a float mix at 32 kHz:
 *  - dm_audio_update / func_8002AB28 start an effect with default parameters, then set
 *    the handle's volume scale to gSndsEntries.volume and its pitch offset to
 *    gSndsEntries.offset * 0.125 semitones; func_8002AC64 stops it after maxlen*6
 *    frames when maxlen != 0.
 *  - InitMusicDriver: 32000 Hz output, one player call per retrace (60 Hz NTSC).
 * The command semantics, the envelope and the synthesizer's volume handling are our
 * own implementation of the documented behaviour of the Music Tools library and the
 * N64 audio library; the libmus source in the decomp is not MIT and is not copied.
 * The VADPCM decoder is written from the format description. */
#include "romsfx.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Renderer version: bump whenever the output changes, to invalidate caches. */
enum { RENDER_VERSION = 1 };

/* Retail US ROM layout (boot_data.c _romDataTbl, offsets checked in SPEC 03 §4.3). */
enum {
    WAVE_START = 0x04F980, WAVE_END = 0x169BE0,
    PTR_START = 0x169BE0, PTR_END = 0x172130,
    FX_START = 0x172130, FX_END = 0x172D60,
};

/* Timing. The player is called once per retrace: 1e6/60 = 16666 us, which the
 * synthesizer turns into 533 output samples (rounded to nearest); volume changes are
 * ramped over the same time rounded down to a multiple of 16 samples (528). */
enum { TICK_SAMPLES = 533, RAMP_SAMPLES = 528 };
enum { TEMPO_DEFAULT = 96 * 256 / 60 }; /* 96 length units per second, 8.8 fixed */
enum { LEN_HOLD = 0x7FFF };             /* note length meaning "until stopped" */
enum { NOTE_REST = 96 };
enum { MAX_TICKS = 1800 };              /* 30 s render cap for effects that never stop */
enum { MAX_CHANS = 8, FOR_DEPTH = 4, CMD_GUARD = 4096 };

/* gSndsEntries (sound.c): per SndIndex {pitch offset in 1/8 semitone, priority,
 * max length in units of 6 frames, volume scale}. The fx number equals the index for
 * all 104 entries; the unused unk_2 byte is dropped. */
typedef struct SndEntry { int8_t offset; uint8_t priority, maxlen, volume; } SndEntry;
static const SndEntry snd_entries[DM_SFX_COUNT] = {
    {-6, 40, 0, 108}, {-6, 40, 0, 108}, {-6, 40, 0, 108}, {-6, 40, 0, 108}, {-6, 40, 0, 108}, /* 0 */
    {-6, 40, 0, 108}, {-6, 40, 0, 108}, {-6, 40, 0, 108}, {-6, 40, 0, 108}, {-6, 40, 0, 108},
    {-12, 40, 0, 108}, {-12, 40, 0, 108}, {-12, 40, 0, 108}, {-12, 40, 0, 108}, {-12, 40, 0, 108}, /* 10 */
    {-6, 40, 0, 108}, {-6, 40, 0, 108}, {-6, 40, 0, 108}, {-6, 40, 0, 108}, {-6, 40, 0, 108},
    {-6, 40, 0, 108}, {-6, 40, 0, 108}, {-6, 40, 0, 108}, {-6, 40, 0, 108}, {-6, 40, 0, 108}, /* 20 */
    {-6, 40, 0, 108}, {-6, 40, 0, 108}, {-6, 40, 0, 108}, {-6, 40, 0, 108}, {-6, 40, 0, 108},
    {-6, 40, 0, 108}, {-6, 40, 0, 108}, {-6, 40, 0, 108}, {-6, 40, 0, 108}, {-6, 40, 0, 108}, /* 30 */
    {-12, 40, 0, 108}, {-12, 40, 0, 108}, {-12, 40, 0, 108}, {-12, 40, 0, 108}, {-12, 40, 0, 108},
    {-12, 40, 0, 108}, {-12, 40, 0, 108}, {-12, 40, 0, 108}, {-12, 40, 0, 108}, {-6, 40, 0, 108}, /* 40 */
    {-6, 40, 0, 108}, {-6, 40, 0, 108}, {-6, 40, 0, 108}, {-6, 40, 0, 108}, {-6, 40, 0, 108},
    {-12, 40, 0, 108}, {-12, 40, 0, 108}, {-12, 40, 0, 108}, {-12, 40, 0, 108}, {-12, 40, 0, 108}, /* 50 */
    {-12, 10, 0, 116}, {0, 10, 0, 116}, {-6, 10, 0, 116}, {-12, 10, 0, 120}, {-6, 10, 0, 120},
    {0, 10, 0, 120}, {-12, 10, 0, 116}, {0, 10, 0, 120}, {-6, 10, 0, 120}, {-6, 10, 0, 120}, /* 60 */
    {-12, 10, 0, 106}, {-12, 10, 0, 116}, {-6, 10, 0, 96}, {0, 10, 0, 120}, {5, 10, 0, 120},
    {-12, 10, 0, 120}, {-12, 10, 0, 120}, {-6, 10, 0, 120}, {-6, 10, 0, 120}, {0, 10, 20, 116}, /* 70 */
    {-6, 40, 0, 120}, {-6, 40, 0, 120}, {-6, 40, 0, 120}, {-6, 40, 0, 116}, {-6, 40, 0, 120},
    {-6, 40, 0, 120}, {-12, 40, 0, 108}, {-12, 40, 0, 108}, {-12, 40, 0, 108}, {-12, 40, 0, 108}, /* 80 */
    {-12, 40, 0, 108}, {-12, 40, 0, 108}, {-12, 40, 0, 108}, {-12, 40, 0, 108}, {-12, 40, 0, 108},
    {-12, 40, 0, 108}, {-6, 40, 0, 108}, {-12, 40, 0, 108}, {-6, 40, 0, 108}, {-12, 40, 0, 108}, /* 90 */
    {-6, 40, 0, 108}, {-6, 40, 0, 108}, {-12, 40, 0, 108}, {-6, 40, 0, 108}, {-12, 40, 0, 108},
    {-6, 40, 0, 108}, {0, 10, 0, 120}, {0, 10, 0, 120}, {0, 10, 0, 120}, /* 100 */
};

/* ---- big-endian readers with bounds ---- */

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static int16_t be16s(const uint8_t *p) { return (int16_t)(uint16_t)((unsigned)p[0] << 8 | p[1]); }
static int s8(int v) { v &= 0xFF; return v >= 0x80 ? v - 0x100 : v; } /* signed byte */
static int fits(size_t len, uint32_t off, size_t n) { return off <= len && n <= len - off; }

static void set_err(char *err, size_t errlen, const char *msg)
{
    if (err && errlen) snprintf(err, errlen, "%s", msg);
}

/* ---- sample bank ---- */

typedef struct Wave {
    uint32_t base;       /* offset into the wave data segment */
    uint32_t bytes;
    int type;            /* 0 = VADPCM, 1 = raw 16-bit */
    uint32_t loop_off, book_off; /* offsets into the pointer bank, 0 = none */
    float detune;        /* semitones added to the note: coarse (base note) + fine */
    int16_t *pcm;
    int n, loop_start, loop_end; /* loop_end 0 = no loop */
    uint32_t loop_count; /* 0xFFFFFFFF = forever */
    int state;           /* 0 = not decoded yet, 1 = ok, -1 = unusable */
} Wave;

typedef struct Rom {
    const uint8_t *wt, *pt, *fx;
    size_t wt_len, pt_len, fx_len;
    int ncomp, neff, nwaves;   /* fx bank header */
    uint32_t wtab;             /* fx wave table (u16 per wave select) */
    int ptr_count;
    Wave *waves;
} Rom;

static int floor_div2048(int64_t v) { return (int)(v >= 0 ? v / 2048 : -((-v + 2047) / 2048)); }

static int16_t clamp16(long v) { return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }

/* VADPCM: 9-byte frames of 16 samples. The header byte holds the residual scale
 * (high nibble, a power of two) and the predictor index (low nibble). Each half of a
 * frame is 8 samples predicted from the previous `order` outputs and the earlier
 * residuals of the same half, using that predictor's order x 8 coefficient table
 * (Q11): x[i] = (sum_k c[k][i]*h[k] + sum_{j<i} c[order-1][i-1-j]*r[j] + 2048*r[i]) >> 11. */
static int decode_vadpcm(const uint8_t *src, uint32_t bytes, const uint8_t *book, int order,
                         int npred, int16_t *out)
{
    int32_t hist[8] = { 0 }; /* last `order` outputs, oldest first */
    uint32_t frames = bytes / 9;
    for (uint32_t f = 0; f < frames; f++) {
        const uint8_t *fr = src + f * 9;
        int shift = fr[0] >> 4, pred = fr[0] & 15;
        if (pred >= npred) return -1;
        const uint8_t *cb = book + (size_t)pred * order * 8 * 2;
        for (int half = 0; half < 2; half++) {
            int32_t r[8];
            for (int i = 0; i < 8; i++) {
                int b = fr[1 + half * 4 + i / 2];
                int nib = (i & 1) ? (b & 15) : (b >> 4);
                if (nib >= 8) nib -= 16;
                r[i] = nib * (1 << shift);
            }
            int16_t *o = out + f * 16 + half * 8;
            for (int i = 0; i < 8; i++) {
                int64_t acc = (int64_t)r[i] * 2048;
                for (int k = 0; k < order; k++) acc += (int64_t)be16s(cb + (k * 8 + i) * 2) * hist[k];
                for (int j = 0; j < i; j++)
                    acc += (int64_t)be16s(cb + ((order - 1) * 8 + (i - 1 - j)) * 2) * r[j];
                o[i] = clamp16(floor_div2048(acc));
            }
            for (int k = 0; k < order; k++) hist[k] = o[8 - order + k];
        }
    }
    return 0;
}

/* Decode a ptr-bank wave the first time an effect uses it. */
static const Wave *get_wave(Rom *rom, int idx)
{
    if (idx < 0 || idx >= rom->ptr_count) return NULL;
    Wave *w = &rom->waves[idx];
    if (w->state) return w->state > 0 ? w : NULL;
    w->state = -1;
    if (!fits(rom->wt_len, w->base, w->bytes)) return NULL;
    const uint8_t *src = rom->wt + w->base;
    if (w->type == 0) {
        if (!w->book_off || !fits(rom->pt_len, w->book_off, 8)) return NULL;
        int order = (int)be32(rom->pt + w->book_off), npred = (int)be32(rom->pt + w->book_off + 4);
        if (order < 1 || order > 8 || npred < 1 || npred > 16) return NULL;
        if (!fits(rom->pt_len, w->book_off + 8, (size_t)order * npred * 16)) return NULL;
        w->n = (int)(w->bytes / 9) * 16;
        w->pcm = malloc((size_t)(w->n ? w->n : 1) * sizeof *w->pcm);
        if (!w->pcm) return NULL;
        if (decode_vadpcm(src, w->bytes, rom->pt + w->book_off + 8, order, npred, w->pcm) != 0) return NULL;
    } else if (w->type == 1) {
        w->n = (int)(w->bytes / 2);
        w->pcm = malloc((size_t)(w->n ? w->n : 1) * sizeof *w->pcm);
        if (!w->pcm) return NULL;
        for (int i = 0; i < w->n; i++) w->pcm[i] = be16s(src + 2 * i);
    } else {
        return NULL;
    }
    /* Loop {start, end, count} in samples (the ADPCM variant also stores the decoder
     * state at the start; we loop over our own decoded PCM, which already has it). */
    if (w->loop_off && fits(rom->pt_len, w->loop_off, 12)) {
        uint32_t s = be32(rom->pt + w->loop_off), e = be32(rom->pt + w->loop_off + 4);
        uint32_t c = be32(rom->pt + w->loop_off + 8);
        if (e > (uint32_t)w->n) e = (uint32_t)w->n;
        if (c && s < e) {
            w->loop_start = (int)s;
            w->loop_end = (int)e;
            w->loop_count = c;
        }
    }
    w->state = 1;
    return w;
}

/* ---- synthesizer voice ---- */

/* Equal-power pan law of the N64 audio library's envelope mixer: 128 steps of a
 * quarter cosine, index 0 = full gain. */
static float eqpower(int i)
{
    if (i < 0) i = 0;
    if (i > 127) i = 127;
    return (float)(int)(32767.0 * cos((double)i / 127.0 * 1.5707963267948966) + 0.5);
}

typedef struct Voice {
    const Wave *w;
    int on;
    double pos, ratio;
    int64_t loops_left;  /* -1 forever */
    float gl, gr;        /* current per-side gain, Q15 units (never below 1) */
    float tl, tr, ml, mr;
    int ramp;            /* samples left in the current volume segment */
    int vol;             /* mixer volume after the square law */
    int pan;
    float dry;
    int fresh;           /* no volume set yet: see voice_set_vol */
} Voice;

static void voice_retarget(Voice *v, int samples)
{
    v->tl = (float)(int)((v->vol * eqpower(v->pan)) / 32768.0f);
    v->tr = (float)(int)((v->vol * eqpower(127 - v->pan)) / 32768.0f);
    v->ramp = samples;
    if (samples <= 0) {
        v->gl = v->tl < 1 ? 1 : v->tl;
        v->gr = v->tr < 1 ? 1 : v->tr;
        return;
    }
    /* The mixer ramps geometrically from the current to the target gain. */
    v->ml = (float)pow((v->tl < 1 ? 1 : v->tl) / v->gl, 1.0 / samples);
    v->mr = (float)pow((v->tr < 1 ? 1 : v->tr) / v->gr, 1.0 / samples);
}

/* The library squares the volume (0..32767) to approximate loudness. */
static void voice_set_vol(Voice *v, unsigned vol)
{
    v->vol = (int)(((long)vol * (long)vol) >> 15);
    /* The mixer ramps from the gain the hardware voice had before. A voice used for
     * the first time starts near zero and would fade in over a whole frame, but in a
     * running game the voices are reused and keep the last effect's level, so an
     * effect's first note starts at once (approximation, see NOTES). */
    voice_retarget(v, v->fresh ? 0 : RAMP_SAMPLES);
    v->fresh = 0;
}

static void voice_set_pan(Voice *v, int pan)
{
    v->pan = pan;
    voice_retarget(v, v->ramp); /* finishes with the running volume segment */
}

static void voice_start(Voice *v, const Wave *w)
{
    v->w = w;
    v->on = w != NULL;
    v->pos = 0;
    v->loops_left = (w && w->loop_end) ? (w->loop_count == 0xFFFFFFFFu ? -1 : (int64_t)w->loop_count) : 0;
}

static float voice_sample(Voice *v)
{
    const Wave *w = v->w;
    int i = (int)v->pos;
    double fr = v->pos - i;
    int j = i + 1;
    if (w->loop_end && v->loops_left != 0 && j >= w->loop_end) j = w->loop_start;
    float a = i < w->n ? w->pcm[i] : 0.0f, b = j < w->n ? w->pcm[j] : 0.0f;
    v->pos += v->ratio;
    if (w->loop_end && v->loops_left != 0 && v->pos >= w->loop_end) {
        v->pos -= w->loop_end - w->loop_start;
        if (v->loops_left > 0) v->loops_left--;
    }
    if (v->pos >= w->n) v->on = 0;
    return (float)(a + (b - a) * fr);
}

static void voice_mix(Voice *v, float *out, int n)
{
    for (int k = 0; k < n; k++) {
        if (v->ramp > 0) {
            v->gl *= v->ml;
            v->gr *= v->mr;
            if (--v->ramp == 0) {
                v->gl = v->tl < 1 ? 1 : v->tl;
                v->gr = v->tr < 1 ? 1 : v->tr;
            }
        }
        if (!v->on) continue;
        float s = voice_sample(v) * (v->dry / 32768.0f);
        out[2 * k] += s * (v->gl / 32768.0f);
        out[2 * k + 1] += s * (v->gr / 32768.0f);
    }
}

/* ---- effect player channel ---- */

typedef struct Chan {
    int active;            /* command stream still running */
    uint32_t pc, base;     /* offsets into the fx bank */
    int bad;               /* hit an unknown/unsupported command */
    uint32_t frame, note_start, note_end, release_frame; /* 8.8 length units */
    int tempo, length, fixed_length, ignore_next_len;
    int wave_sel, pending; /* pending: ptr wave waiting to start, -1 none */
    int playing;
    int volume, pan, velocity, default_velocity, velocity_on;
    int transpose, ignore_transpose, trigger_off, env_trigger_off;
    int reverb, old_reverb;
    float freq_offset, distort, base_note, last_note, port_base;
    int port;
    /* envelope: phase 1 attack, 2 decay, 3 sustain, 4 release, 5 done */
    int env_phase, env_cur, env_init, env_peak, env_sustain;
    int env_speed_calc, env_attack, env_decay, env_release, env_release_start;
    float env_attack_step, env_decay_step, env_release_step;
    int cutoff, endit;
    int vib_delay, vib_speed;
    float vib_amount, vib_step, vibrato;
    int wobble_amount, wobble_on, wobble_off, wobble_count, wobble_cur;
    int sweep_speed, sweep_timer, sweep_dir;
    uint32_t sweep_frame;
    int count;             /* length units since the note started */
    int volscale, panscale, priority;
    int stopping, stopping_speed;
    unsigned old_volume;
    int old_pan;
    float old_freq;
    int for_n;
    uint32_t for_pc[FOR_DEPTH];
    int for_count[FOR_DEPTH];
    int for_volume[FOR_DEPTH];
    Voice voice;
} Chan;

typedef struct Render {
    Rom *rom;
    Chan ch[MAX_CHANS];
    int nch;
    uint32_t rand_state;
} Render;

static void env_set(Chan *c, const uint8_t a[7])
{
    int speed = a[0] ? a[0] : 1;
    c->env_speed_calc = 1024 / speed;
    c->env_init = a[1];
    c->env_attack = a[2] ? a[2] : 1;
    c->env_peak = a[3];
    c->env_attack_step = (float)(c->env_peak - c->env_init) / (float)c->env_attack;
    c->env_decay = a[4] ? a[4] : 1;
    c->env_sustain = a[5];
    c->env_decay_step = (float)(c->env_sustain - c->env_peak) / (float)c->env_decay;
    c->env_release = a[6] ? a[6] : 1;
    c->env_release_step = 1.0f / (float)c->env_release;
}

static void chan_init(Chan *c, Rom *rom, int fx, int volscale, int panscale, int priority)
{
    static const uint8_t default_env[7] = { 1, 0, 1, 127, 255, 127, 15 };
    memset(c, 0, sizeof *c);
    c->active = 1;
    c->pc = c->base = be32(rom->fx + 24 + 8 * (uint32_t)fx);
    c->tempo = TEMPO_DEFAULT;
    c->length = 1;
    c->pending = -1;
    c->default_velocity = c->volume = 127;
    c->pan = 64;
    c->old_volume = 0xFFFF;
    c->old_pan = 0xFF;
    c->old_reverb = 0xFF;
    c->old_freq = 99.9f;
    c->stopping = -1;
    c->volscale = volscale;
    c->panscale = panscale;
    c->priority = priority;
    env_set(c, default_env);
    c->env_init = 0;
    c->env_attack_step = 1.0f;
    c->voice.gl = c->voice.gr = 1;
    c->voice.fresh = 1;
    c->voice.pan = 64;
    c->voice.dry = 32767;
}

static int rd(Render *r, Chan *c, int *b)
{
    if (c->pc >= r->rom->fx_len) return 0;
    *b = r->rom->fx[c->pc++];
    return 1;
}

/* A value of one byte, or two when the first has its top bit set. */
static int rd_var(Render *r, Chan *c, int *v)
{
    int b, lo;
    if (!rd(r, c, &b)) return 0;
    if (b & 0x80) {
        if (!rd(r, c, &lo)) return 0;
        b = (b & 0x7F) << 8 | lo;
    }
    *v = b;
    return 1;
}

static int rd_n(Render *r, Chan *c, uint8_t *a, int n)
{
    for (int i = 0; i < n; i++) {
        int b;
        if (!rd(r, c, &b)) return 0;
        a[i] = (uint8_t)b;
    }
    return 1;
}

static int rnd(Render *r, int range) /* deterministic stand-in for the library's RNG */
{
    r->rand_state = r->rand_state * 1103515245u + 12345u;
    return (int)(((r->rand_state >> 16) & 0x7FFF) * (uint32_t)range / 0x8000u);
}

static void chan_stop(Chan *c)
{
    c->active = 0;
    c->pending = -1;
    if (c->playing) {
        c->playing = 0;
        c->voice.on = 0; /* volume 0 and stop at the same instant: a hard cut */
    }
}

static void chan_flush_pending(Render *r, Chan *c)
{
    voice_start(&c->voice, get_wave(r->rom, c->pending));
    c->playing = 1;
    c->pending = -1;
}

static void env_start(Chan *c)
{
    if (c->length != LEN_HOLD)
        c->release_frame = c->cutoff ? c->note_start + ((uint32_t)c->cutoff << 8)
                                     : c->note_end - ((uint32_t)c->endit << 8);
    else
        c->release_frame = c->note_start + 0x7FFFFFFFu;
    c->env_cur = c->env_init;
    c->env_phase = 1;
}

static void env_tick(Chan *c)
{
    if ((int32_t)(c->release_frame - c->frame) < 0 && c->env_phase < 4) {
        c->env_phase = 4;
        c->env_release_start = c->env_cur;
    }
    uint32_t since = (c->frame - c->note_start) >> 8;
    int k;
    switch (c->env_phase) {
    case 1:
        k = (int)((since * (uint32_t)c->env_speed_calc) >> 10);
        if (k < c->env_attack) {
            c->env_cur = (uint8_t)(c->env_init + (int)(c->env_attack_step * (float)k));
        } else {
            c->env_phase = 2;
            c->env_cur = c->env_peak;
        }
        break;
    case 2:
        k = (int)(((since - (uint32_t)c->env_attack) * (uint32_t)c->env_speed_calc) >> 10);
        if (k < c->env_decay) {
            c->env_cur = (uint8_t)(c->env_peak + (int)(c->env_decay_step * (float)k));
        } else {
            c->env_phase = 3;
            c->env_cur = c->env_sustain;
        }
        break;
    case 4:
        k = (int)((((c->frame - c->release_frame) >> 8) * (uint32_t)c->env_speed_calc) >> 10);
        if (k < c->env_release) {
            c->env_cur = (uint8_t)(c->env_release_start -
                                   (int)(c->env_release_step * (float)k * (float)c->env_release_start));
        } else {
            c->env_phase = 5;
            c->env_cur = 0;
        }
        break;
    default:
        break;
    }
}

static void start_sub_effect(Render *r, Chan *parent, int fx);

/* Run commands up to the next note or rest, then start it. */
static void chan_next_note(Render *r, Chan *c)
{
    Rom *rom = r->rom;
    int b, v, guard = 0;
    uint8_t a[7];
    for (;;) {
        if (!rd(r, c, &b)) goto bad;
        if (b < 0x80) break;
        if (++guard > CMD_GUARD) goto bad; /* a loop without any note */
        switch (b) {
        case 0x80: chan_stop(c); return;                                   /* stop */
        case 0x81: if (!rd_var(r, c, &v)) goto bad; c->wave_sel = v; break; /* wave */
        case 0x82: if (!rd(r, c, &v)) goto bad;                            /* portamento */
            c->port = v;
            if (v) c->port_base = c->base_note;
            break;
        case 0x83: c->port = 0; break;
        case 0x84: if (!rd_n(r, c, a, 7)) goto bad; env_set(c, a); break;  /* envelope */
        case 0x85: if (!rd(r, c, &v)) goto bad; c->tempo = v * 256 * 96 / 120 / 60; break;
        case 0x86: if (!rd_n(r, c, a, 2)) goto bad; c->cutoff = a[0] << 8 | a[1]; c->endit = 0; break;
        case 0x87: if (!rd(r, c, &v)) goto bad; c->endit = v; c->cutoff = 0; break;
        case 0x88: case 0x89:                                                /* vibrato up/down */
            if (!rd_n(r, c, a, 3)) goto bad;
            c->vib_delay = a[0];
            c->vib_speed = a[1];
            c->vib_amount = (b == 0x88 ? 1.0f : -1.0f) * (float)a[2] / 50.0f;
            c->vib_step = a[1] ? 6.2831852f / (float)a[1] : 0.0f;
            break;
        case 0x8A: c->vib_speed = 0; c->vibrato = 0; break;
        case 0x8B: if (!rd_var(r, c, &v)) goto bad; c->fixed_length = v; break;
        case 0x8C: c->ignore_next_len = 1; break;
        case 0x8D: if (!rd(r, c, &v)) goto bad; c->transpose = v; break;
        case 0x8E: c->ignore_transpose = 1; break;
        case 0x8F: if (!rd(r, c, &v)) goto bad;                            /* detune */
            c->freq_offset -= c->distort;
            c->distort = (float)s8(v) / 100.0f;
            c->freq_offset += c->distort;
            break;
        case 0x91: c->env_trigger_off = 1; break;
        case 0x92: c->env_trigger_off = 0; break;
        case 0x93: c->trigger_off = 1; break;
        case 0x94: c->trigger_off = 0; break;
        case 0x95:                                                          /* for */
            if (!rd(r, c, &v) || c->for_n >= FOR_DEPTH) goto bad;
            c->for_count[c->for_n] = v;
            c->for_pc[c->for_n] = c->pc;
            c->for_volume[c->for_n] = c->volume;
            c->for_n++;
            break;
        case 0x96:                                                          /* next */
            if (c->for_n <= 0) goto bad;
            if (c->for_count[c->for_n - 1] != 0xFF && --c->for_count[c->for_n - 1] == 0) {
                c->for_n--;
            } else {
                c->pc = c->for_pc[c->for_n - 1];
                c->volume = c->for_volume[c->for_n - 1];
            }
            break;
        case 0x97: if (!rd_n(r, c, a, 3)) goto bad;                        /* wobble */
            c->wobble_amount = s8(a[0]);
            c->wobble_on = a[1];
            c->wobble_off = a[2];
            break;
        case 0x98: c->wobble_on = 0; break;
        case 0x99: c->velocity_on = 1; break;
        case 0x9A: c->velocity_on = 0; break;
        case 0x9B: if (!rd(r, c, &v)) goto bad; c->default_velocity = v; c->velocity_on = 0; break;
        case 0x9C: if (!rd(r, c, &v)) goto bad; c->pan = v / 2; break;
        case 0x9D: if (!rd_n(r, c, a, 2)) goto bad; break;                 /* obsolete */
        case 0x9F: break;                                                   /* drums off */
        case 0xA0: if (!rd(r, c, &v)) goto bad; break;                     /* debug print */
        case 0xA1: if (!rd_n(r, c, a, 6)) goto bad;                        /* goto */
            /* bytes 2..5 address the song-only volume/pitchbend streams */
            c->pc = c->base + (uint32_t)(a[0] << 8 | a[1]);
            break;
        case 0xA2: if (!rd(r, c, &v)) goto bad; c->reverb = v; break;
        case 0xA3: if (!rd_n(r, c, a, 2)) goto bad; c->transpose = (uint8_t)(rnd(r, a[0]) + a[1]); break;
        case 0xA4: if (!rd_n(r, c, a, 2)) goto bad; c->volume = (uint8_t)(rnd(r, a[0]) + a[1]); break;
        case 0xA5: if (!rd_n(r, c, a, 2)) goto bad; c->pan = (uint8_t)(rnd(r, a[0]) + a[1]); break;
        case 0xA6: if (!rd(r, c, &v)) goto bad; c->volume = v; break;
        case 0xA7: if (!rd_var(r, c, &v)) goto bad; start_sub_effect(r, c, v); break;
        case 0xA8: if (!rd(r, c, &v)) goto bad; break; /* bend range: no pitchbend stream in effects */
        case 0xA9: if (!rd(r, c, &v)) goto bad; c->sweep_speed = v; break;
        case 0xAA: if (!rd(r, c, &v)) goto bad; break; /* reverb type for songs; not modelled */
        case 0xAB: if (!rd(r, c, &v) || !rd_var(r, c, &v)) goto bad; break; /* marker */
        case 0xAC: c->fixed_length = 0; break;
        default: goto bad; /* 0x90/0x9E need a song's tables; >= 0xAD unknown */
        }
    }

    /* a note (or rest): optional velocity byte, then the length unless fixed */
    int note = b;
    c->last_note = c->port_base;
    if (c->velocity_on) {
        if (!rd(r, c, &v)) goto bad;
        if (v & 0x80) {
            v &= 0x7F;
            c->velocity_on = 0;
            c->default_velocity = v;
        }
        if (note != NOTE_REST) c->velocity = v;
    } else {
        c->velocity = c->default_velocity;
    }
    if (c->fixed_length && !c->ignore_next_len) {
        c->length = c->fixed_length;
    } else {
        c->ignore_next_len = 0;
        if (!rd_var(r, c, &v)) goto bad;
        c->length = v;
    }
    c->note_start = c->note_end;
    c->note_end += (uint32_t)c->length * 256;
    c->count = 0;
    c->wobble_count = c->wobble_off;
    c->wobble_cur = 0;

    if (note != NOTE_REST) {
        if (!c->env_trigger_off) env_start(c);
        if (c->sweep_speed) {
            c->sweep_frame = c->note_start;
            c->sweep_timer = 0;
            c->sweep_dir = c->pan & 0x40;
        }
        if (c->wave_sel < 0 || c->wave_sel >= rom->nwaves) goto bad;
        int w = rom->fx[rom->wtab + 2 * (uint32_t)c->wave_sel] << 8 | rom->fx[rom->wtab + 2 * (uint32_t)c->wave_sel + 1];
        if (w >= rom->ptr_count) goto bad;
        if (!c->trigger_off) {
            c->pending = w;
            if (c->playing && c->old_volume) {
                /* fade the old note out for one tick, start the new one next tick */
                c->old_volume = 0;
                voice_set_vol(&c->voice, 0);
            } else {
                chan_flush_pending(r, c);
            }
        }
        c->base_note = (float)note + rom->waves[w].detune;
        c->base_note += c->ignore_transpose ? 0.0f : (float)s8(c->transpose);
        if (c->reverb != c->old_reverb) {
            int mix = c->reverb & 0xFF; /* reverb base level is 0 for effects */
            c->old_reverb = c->reverb;
            c->voice.dry = eqpower(mix); /* only the dry part: reverb itself is not modelled */
        }
    } else if (c->env_phase < 4) {
        c->env_phase = 4;
        c->release_frame = c->frame;
        c->env_release_start = c->env_cur;
    }
    return;
bad:
    c->bad = 1;
    chan_stop(c);
}

/* Cstartfx: a second effect on a free channel with the parent's volume/pan scale and
 * one more priority level. Its pitch offset stays 0: the game sets the offset only once,
 * right after starting the parent. Processed in the same tick (higher channel index). */
static void start_sub_effect(Render *r, Chan *parent, int fx)
{
    if (fx < 0 || fx >= r->rom->ncomp || r->nch >= MAX_CHANS) return;
    Chan *c = &r->ch[r->nch++];
    chan_init(c, r->rom, fx, parent->volscale, parent->panscale, parent->priority + 1);
    c->stopping = parent->stopping;
    c->stopping_speed = parent->stopping_speed;
}

static void sweep_tick(Chan *c)
{
    do {
        c->sweep_frame += 256;
        int t = c->sweep_timer + c->sweep_speed;
        if (t < 64) {
            c->sweep_timer = t;
            continue;
        }
        c->sweep_timer = t & 63;
        t >>= 6;
        if (!c->sweep_dir) {
            c->pan += t;
            if (c->pan > 0x7F) { c->pan = 0x7F; c->sweep_dir = 1; }
        } else {
            c->pan -= t;
            if (c->pan <= 0) { c->pan = 0; c->sweep_dir = 0; }
        }
    } while ((int32_t)(c->sweep_frame - c->frame) < 0);
}

static float wobble_tick(Chan *c)
{
    c->wobble_count = (c->wobble_count - 1) & 0xFF; /* a byte counter */
    if (c->wobble_count == 0) {
        if (c->wobble_cur == 0) {
            c->wobble_cur = c->wobble_amount;
            c->wobble_count = c->wobble_on;
        } else {
            c->wobble_cur = 0;
            c->wobble_count = c->wobble_off;
        }
    }
    return (float)c->wobble_cur;
}

static float vibrato_tick(Chan *c)
{
    int t = c->count - c->vib_delay;
    if (t <= 0) return 0.0f;
    c->vibrato = sinf((float)t * c->vib_step) * c->vib_amount;
    return c->vibrato;
}

static void set_pitch(Chan *c, float offset)
{
    float f = c->base_note;
    if (c->port) {
        if (c->count <= c->port) f = c->last_note + (f - c->last_note) / (float)c->port * (float)c->count;
        c->port_base = f;
    }
    f += offset;
    if (f == c->old_freq) return;
    c->old_freq = f;
    double ratio = exp2((double)f / 12.0);
    if (ratio > 2.0) { /* the library's limit: clamp and silence the note */
        ratio = 2.0;
        c->velocity = 0;
    }
    c->voice.ratio = ratio;
}

static void set_volume_pan(Chan *c)
{
    unsigned long vol = ((unsigned long)c->volume * (unsigned long)c->env_cur * (unsigned long)c->velocity *
                         (unsigned long)c->volscale) >> 13;
    if (vol > 32767) vol = 32767;
    vol = (vol * 0x7FFF) >> 15; /* effects master volume, set to maximum by the library */
    if (c->stopping != -1) vol = vol * (unsigned long)c->stopping / (unsigned long)c->stopping_speed;
    if (vol != c->old_volume) {
        c->old_volume = (unsigned)vol;
        voice_set_vol(&c->voice, (unsigned)vol);
    }
    int pan = ((c->pan * c->panscale) >> 7) & 0x7F;
    if (pan != c->old_pan) {
        c->old_pan = pan;
        voice_set_pan(&c->voice, pan);
    }
}

/* One player tick for one channel. */
static void chan_tick(Render *r, Chan *c)
{
    if (c->pending >= 0) chan_flush_pending(r, c);
    c->frame += (uint32_t)c->tempo;
    if (c->length != LEN_HOLD) {
        while ((int32_t)(c->note_end - c->frame) < 0 && c->active) chan_next_note(r, c);
        if (!c->active) return;
    }
    if (c->stopping != -1 && --c->stopping == -1) {
        chan_stop(c);
        return;
    }
    if (c->playing) {
        if (c->env_phase) env_tick(c);
        if (c->sweep_speed && (int32_t)(c->sweep_frame - c->frame) < 0) sweep_tick(c);
        float total = c->freq_offset;
        if (c->vib_speed) total += vibrato_tick(c);
        if (c->wobble_on) total += wobble_tick(c);
        if (c->pending < 0) {
            set_pitch(c, total);
            set_volume_pan(c);
        }
    }
    c->count = (int)((c->frame - c->note_start) >> 8);
}

/* Render SndIndex `idx` into clip. */
static int render_effect(Rom *rom, int idx, DmSfxClip *clip)
{
    const SndEntry *e = &snd_entries[idx];
    Render *r = calloc(1, sizeof *r);
    size_t cap = (size_t)TICK_SAMPLES * 2 * 64, used = 0;
    float *mix = calloc(cap, sizeof *mix);
    if (!r || !mix) {
        free(r);
        free(mix);
        return -1;
    }
    r->rom = rom;
    r->rand_state = 0x12345678u;
    r->nch = 1;
    /* MusStartEffect(number) with default scales, then MusHandleSetVolume and
     * MusHandleSetFreqOffset from func_8002AB28, all before the first tick. */
    chan_init(&r->ch[0], rom, idx, 0x80, 0x80, 0);
    r->ch[0].volscale = e->volume;
    r->ch[0].freq_offset = (float)e->offset * 0.125f;

    int cut_tick = e->maxlen ? e->maxlen * 6 + 1 : -1;
    int tick = 0, alive = 1;
    clip->flags = 0;
    for (; tick < MAX_TICKS; tick++) {
        if (tick == cut_tick) { /* func_8002AC64 -> MusHandleStop(handle, 0) */
            for (int i = 0; i < r->nch; i++)
                if (r->ch[i].active && r->ch[i].stopping == -1) {
                    r->ch[i].stopping = 0;
                    r->ch[i].stopping_speed = 1;
                }
            clip->flags |= DM_SFX_F_CUT;
        }
        alive = 0;
        for (int i = 0; i < r->nch; i++) {
            if (!r->ch[i].active) continue;
            chan_tick(r, &r->ch[i]);
            alive |= r->ch[i].active;
        }
        if (!alive) break;
        if (used + (size_t)TICK_SAMPLES * 2 > cap) {
            float *m = realloc(mix, cap * 2 * sizeof *mix);
            if (!m) {
                free(mix);
                free(r);
                return -1;
            }
            memset(m + cap, 0, cap * sizeof *m);
            mix = m;
            cap *= 2;
        }
        for (int i = 0; i < r->nch; i++) voice_mix(&r->ch[i].voice, mix + used, TICK_SAMPLES);
        used += (size_t)TICK_SAMPLES * 2;
    }
    if (alive) clip->flags |= DM_SFX_F_CAPPED;
    for (int i = 0; i < r->nch; i++)
        if (r->ch[i].bad) clip->flags |= DM_SFX_F_BADCMD;
    free(r);

    /* the voice stays allocated until the stop, but silence at the end is not needed */
    size_t frames = used / 2;
    while (frames > 0 && fabsf(mix[2 * frames - 2]) < 0.5f && fabsf(mix[2 * frames - 1]) < 0.5f) frames--;
    clip->pcm = malloc((frames ? frames : 1) * 2 * sizeof *clip->pcm);
    if (!clip->pcm) {
        free(mix);
        return -1;
    }
    for (size_t k = 0; k < frames * 2; k++) clip->pcm[k] = clamp16(lrintf(mix[k]));
    clip->frames = (int)frames;
    clip->priority = e->priority;
    clip->ticks = tick;
    free(mix);
    return 0;
}

/* ---- ROM parsing ---- */

static int parse_rom(const uint8_t *img, size_t size, Rom *rom, char *err, size_t errlen)
{
    memset(rom, 0, sizeof *rom);
    if (size < FX_END) {
        set_err(err, errlen, "file too small for a Dr. Mario 64 ROM");
        return -1;
    }
    if (memcmp(img + WAVE_START, "N64 WaveTables", 14) != 0 || memcmp(img + PTR_START, "N64 PtrTablesV2", 15) != 0) {
        set_err(err, errlen, "not the Dr. Mario 64 US ROM (sound bank headers not found)");
        return -1;
    }
    rom->wt = img + WAVE_START;
    rom->wt_len = WAVE_END - WAVE_START;
    rom->pt = img + PTR_START;
    rom->pt_len = PTR_END - PTR_START;
    rom->fx = img + FX_START;
    rom->fx_len = FX_END - FX_START;

    /* fx bank header: components, effects, waves, flags, sample bank, wave table, then
     * {fxdata offset, priority} per component */
    rom->ncomp = (int)be32(rom->fx);
    rom->neff = (int)be32(rom->fx + 4);
    rom->nwaves = (int)be32(rom->fx + 8);
    rom->wtab = be32(rom->fx + 20);
    if (rom->neff != DM_SFX_COUNT || rom->ncomp < rom->neff || rom->ncomp > 4096 || rom->nwaves < 1 ||
        !fits(rom->fx_len, 24, (size_t)rom->ncomp * 8) || !fits(rom->fx_len, rom->wtab, (size_t)rom->nwaves * 2)) {
        set_err(err, errlen, "effect bank header is not as expected");
        return -1;
    }
    for (int i = 0; i < rom->ncomp; i++)
        if (be32(rom->fx + 24 + 8 * (uint32_t)i) >= rom->fx_len) {
            set_err(err, errlen, "effect bank entry out of range");
            return -1;
        }

    /* pointer bank: name[16], flags, name[12], count, then offsets of the base note
     * bytes, the fine-tune words and the wave list */
    rom->ptr_count = (int)be32(rom->pt + 32);
    uint32_t bn = be32(rom->pt + 36), dt = be32(rom->pt + 40), wl = be32(rom->pt + 44);
    int n = rom->ptr_count;
    if (n < 1 || n > 4096 || !fits(rom->pt_len, bn, (size_t)n) || !fits(rom->pt_len, dt, (size_t)n * 4) ||
        !fits(rom->pt_len, wl, (size_t)n * 4)) {
        set_err(err, errlen, "sample bank header is not as expected");
        return -1;
    }
    rom->waves = calloc((size_t)n, sizeof *rom->waves);
    if (!rom->waves) {
        set_err(err, errlen, "out of memory");
        return -1;
    }
    for (int i = 0; i < n; i++) {
        Wave *w = &rom->waves[i];
        uint32_t o = be32(rom->pt + wl + 4 * (uint32_t)i);
        if (!fits(rom->pt_len, o, 20)) {
            set_err(err, errlen, "sample bank wave entry out of range");
            return -1;
        }
        /* ALWaveTable: base, len, type, flags, pad, loop, book (base is relative to
         * the wave data, loop/book to the pointer bank) */
        w->base = be32(rom->pt + o);
        w->bytes = be32(rom->pt + o + 4);
        w->type = rom->pt[o + 8];
        w->loop_off = be32(rom->pt + o + 12);
        w->book_off = be32(rom->pt + o + 16);
        /* A note is played at 2^((note + detune)/12) of the output rate, where the
         * coarse part is the signed base-note byte minus 48 (c4) and the fine part is the
         * first byte of the tune word in 1/100 semitone. */
        w->detune = (float)s8(rom->pt[bn + (uint32_t)i] - 48) + (float)s8(rom->pt[dt + 4 * (uint32_t)i]) / 100.0f;
    }
    return 0;
}

static void free_rom(Rom *rom)
{
    if (rom->waves)
        for (int i = 0; i < rom->ptr_count; i++) free(rom->waves[i].pcm);
    free(rom->waves);
    rom->waves = NULL;
}

int dm_romsfx_load_mem(const uint8_t *img, size_t size, DmSfxBank *out, char *err, size_t errlen)
{
    memset(out, 0, sizeof *out);
    if (size < 4 || size > 64u * 1024 * 1024) {
        set_err(err, errlen, "file size is not that of an N64 ROM");
        return -1;
    }
    /* normalise the byte order to big-endian (.z64) */
    uint8_t *own = NULL;
    uint32_t magic = be32(img);
    if (magic == 0x37804012u || magic == 0x40123780u) {
        own = malloc(size);
        if (!own) {
            set_err(err, errlen, "out of memory");
            return -1;
        }
        for (size_t i = 0; i + 3 < size; i += 4) {
            if (magic == 0x37804012u) { /* .v64: 16-bit byte swap */
                own[i] = img[i + 1]; own[i + 1] = img[i]; own[i + 2] = img[i + 3]; own[i + 3] = img[i + 2];
            } else {                    /* .n64: little-endian 32-bit words */
                own[i] = img[i + 3]; own[i + 1] = img[i + 2]; own[i + 2] = img[i + 1]; own[i + 3] = img[i];
            }
        }
        img = own;
    } else if (magic != 0x80371240u) {
        set_err(err, errlen, "not an N64 ROM image");
        return -1;
    }
    Rom rom;
    int rc = parse_rom(img, size, &rom, err, errlen);
    if (rc == 0) {
        out->rate = DM_SFX_RATE;
        out->fx_components = rom.ncomp;
        out->fx_effects = rom.neff;
        out->fx_waves = rom.nwaves;
        out->ptr_waves = rom.ptr_count;
        for (int i = 0; i < DM_SFX_COUNT && rc == 0; i++)
            if (render_effect(&rom, i, &out->clip[i]) != 0) {
                set_err(err, errlen, "out of memory while rendering");
                rc = -1;
            }
    }
    free_rom(&rom);
    free(own);
    if (rc != 0) dm_romsfx_free(out);
    return rc;
}

int dm_romsfx_load(const char *rom_path, DmSfxBank *out, char *err, size_t errlen)
{
    memset(out, 0, sizeof *out);
    FILE *f = fopen(rom_path, "rb");
    if (!f) {
        set_err(err, errlen, "cannot open the ROM file");
        return -1;
    }
    uint8_t *img = NULL;
    size_t size = 0, cap = 0;
    for (;;) {
        if (size == cap) {
            if (cap >= 64u * 1024 * 1024) { /* larger than any N64 cartridge */
                size = cap + 1;
                break;
            }
            cap = cap ? cap * 2 : 4u * 1024 * 1024;
            uint8_t *p = realloc(img, cap);
            if (!p) {
                free(img);
                fclose(f);
                set_err(err, errlen, "out of memory");
                return -1;
            }
            img = p;
        }
        size_t got = fread(img + size, 1, cap - size, f);
        size += got;
        if (got == 0) break;
    }
    int ioerr = ferror(f);
    fclose(f);
    if (ioerr) {
        free(img);
        set_err(err, errlen, "error reading the ROM file");
        return -1;
    }
    int rc = dm_romsfx_load_mem(img, size, out, err, errlen);
    free(img);
    return rc;
}

/* Every looped VADPCM wave stores the decoder output of the 16-sample frame that holds
 * the loop start (so the hardware can resume there). Our decoder must reproduce it
 * exactly, which checks the decoder against the original encoder without fixtures. */
int dm_romsfx_selfcheck(const uint8_t *img, size_t size, char *err, size_t errlen)
{
    if (size < 4 || be32(img) != 0x80371240u) {
        set_err(err, errlen, "selfcheck needs a .z64 image");
        return -1;
    }
    Rom rom;
    if (parse_rom(img, size, &rom, err, errlen) != 0) {
        free_rom(&rom);
        return -1;
    }
    int checked = 0;
    for (int i = 0; i < rom.ptr_count && checked >= 0; i++) {
        const Wave *w = &rom.waves[i];
        if (w->type != 0 || !w->loop_off || !fits(rom.pt_len, w->loop_off, 12 + 32)) continue;
        const Wave *d = get_wave(&rom, i);
        if (!d) continue;
        int frame = (int)(be32(rom.pt + w->loop_off) & ~15u);
        if (frame + 16 > d->n) continue;
        for (int k = 0; k < 16; k++)
            if (d->pcm[frame + k] != be16s(rom.pt + w->loop_off + 12 + 2 * k)) {
                char msg[96];
                snprintf(msg, sizeof msg, "VADPCM mismatch in wave %d at sample %d", i, frame + k);
                set_err(err, errlen, msg);
                checked = -1;
                break;
            }
        if (checked >= 0) checked++;
    }
    free_rom(&rom);
    return checked;
}

void dm_romsfx_free(DmSfxBank *b)
{
    for (int i = 0; i < DM_SFX_COUNT; i++) {
        free(b->clip[i].pcm);
        b->clip[i].pcm = NULL;
        b->clip[i].frames = 0;
    }
}

/* ---- cache: "DMSFXCH1", version, rate, count, then per clip frames, priority, ticks,
 * flags and the samples; all little-endian 32-bit / 16-bit ---- */

static const char cache_magic[8] = { 'D', 'M', 'S', 'F', 'X', 'C', 'H', '1' };

static int put32(FILE *f, uint32_t v)
{
    uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    return fwrite(b, 1, 4, f) == 4 ? 0 : -1;
}

static int get32(FILE *f, uint32_t *v)
{
    uint8_t b[4];
    if (fread(b, 1, 4, f) != 4) return -1;
    *v = (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
    return 0;
}

int dm_romsfx_save(const DmSfxBank *b, const char *cache_path)
{
    FILE *f = fopen(cache_path, "wb");
    if (!f) return -1;
    int bad = fwrite(cache_magic, 1, 8, f) != 8;
    bad |= put32(f, RENDER_VERSION) | put32(f, (uint32_t)b->rate) | put32(f, DM_SFX_COUNT);
    uint8_t buf[4096];
    for (int i = 0; i < DM_SFX_COUNT && !bad; i++) {
        const DmSfxClip *c = &b->clip[i];
        int frames = c->pcm ? c->frames : 0;
        bad |= put32(f, (uint32_t)frames) | put32(f, (uint32_t)c->priority) | put32(f, (uint32_t)c->ticks) |
               put32(f, (uint32_t)c->flags);
        size_t n = (size_t)frames * 2, k = 0;
        while (k < n && !bad) {
            size_t m = 0;
            for (; m < sizeof buf / 2 && k < n; m++, k++) {
                uint16_t s = (uint16_t)c->pcm[k];
                buf[2 * m] = (uint8_t)s;
                buf[2 * m + 1] = (uint8_t)(s >> 8);
            }
            bad |= fwrite(buf, 1, 2 * m, f) != 2 * m;
        }
    }
    bad |= fclose(f) != 0;
    return bad ? -1 : 0;
}

int dm_romsfx_load_cache(const char *cache_path, DmSfxBank *out)
{
    memset(out, 0, sizeof *out);
    FILE *f = fopen(cache_path, "rb");
    if (!f) return -1;
    char magic[8];
    uint32_t ver, rate, count;
    int bad = fread(magic, 1, 8, f) != 8 || memcmp(magic, cache_magic, 8) != 0;
    bad = bad || get32(f, &ver) || get32(f, &rate) || get32(f, &count);
    bad = bad || ver != RENDER_VERSION || rate != DM_SFX_RATE || count != DM_SFX_COUNT;
    for (int i = 0; i < DM_SFX_COUNT && !bad; i++) {
        DmSfxClip *c = &out->clip[i];
        uint32_t frames, pri, ticks, flags;
        if (get32(f, &frames) || get32(f, &pri) || get32(f, &ticks) || get32(f, &flags) ||
            frames > (uint32_t)DM_SFX_RATE * 60) {
            bad = 1;
            break;
        }
        c->frames = (int)frames;
        c->priority = (int)pri;
        c->ticks = (int)ticks;
        c->flags = (int)flags;
        c->pcm = malloc((frames ? frames : 1) * 2 * sizeof *c->pcm);
        if (!c->pcm) {
            bad = 1;
            break;
        }
        for (size_t k = 0; k < (size_t)frames * 2; k++) {
            uint8_t s[2];
            if (fread(s, 1, 2, f) != 2) {
                bad = 1;
                break;
            }
            c->pcm[k] = (int16_t)(uint16_t)(s[0] | s[1] << 8);
        }
    }
    fclose(f);
    if (bad) {
        dm_romsfx_free(out);
        return -1;
    }
    out->rate = (int)rate;
    return 0;
}
