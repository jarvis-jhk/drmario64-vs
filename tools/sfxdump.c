/* sfxdump <rom> <outdir>: render every sound effect from the user's ROM (romsfx.c) and
 * write se_NNN.wav per SndIndex, for listening checks. The output is ROM-derived audio:
 * keep it outside the repository. */
#include "../src/app/romsfx.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void le(uint8_t *p, uint32_t v, int n)
{
    for (int i = 0; i < n; i++) p[i] = (uint8_t)(v >> (8 * i));
}

static int write_wav(const char *path, const DmSfxClip *c, int rate)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    uint32_t data = (uint32_t)c->frames * 4;
    uint8_t h[44];
    memcpy(h, "RIFF", 4);
    le(h + 4, 36 + data, 4);
    memcpy(h + 8, "WAVEfmt ", 8);
    le(h + 16, 16, 4);
    le(h + 20, 1, 2); /* PCM */
    le(h + 22, 2, 2);
    le(h + 24, (uint32_t)rate, 4);
    le(h + 28, (uint32_t)rate * 4, 4);
    le(h + 32, 4, 2);
    le(h + 34, 16, 2);
    memcpy(h + 36, "data", 4);
    le(h + 40, data, 4);
    int bad = fwrite(h, 1, 44, f) != 44;
    for (int k = 0; k < c->frames * 2 && !bad; k++) {
        uint8_t s[2];
        le(s, (uint16_t)c->pcm[k], 2);
        bad = fwrite(s, 1, 2, f) != 2;
    }
    bad |= fclose(f) != 0;
    return bad ? -1 : 0;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s <rom.z64> <outdir>\n", argv[0]);
        return 2;
    }
    DmSfxBank b;
    char err[256];
    if (dm_romsfx_load(argv[1], &b, err, sizeof err) != 0) {
        fprintf(stderr, "sfxdump: %s\n", err);
        return 1;
    }
    printf("rate %d, fx bank: %d components, %d effects, %d wave selects; sample bank: %d waves\n", b.rate,
           b.fx_components, b.fx_effects, b.fx_waves, b.ptr_waves);
    printf("  se   fx   ms  ticks   peak  peak_dBFS flags\n");
    int rc = 0;
    for (int i = 0; i < DM_SFX_COUNT; i++) {
        const DmSfxClip *c = &b.clip[i];
        int peak = 0;
        for (int k = 0; k < c->frames * 2; k++) {
            int a = abs(c->pcm[k]);
            if (a > peak) peak = a;
        }
        char path[4096];
        snprintf(path, sizeof path, "%s/se_%03d.wav", argv[2], i);
        if (write_wav(path, c, b.rate) != 0) {
            fprintf(stderr, "sfxdump: cannot write %s\n", path);
            rc = 1;
        }
        printf("  %3d  %3d %5d  %4d  %5d  %6.1f   %s%s%s\n", i, i, (int)((long)c->frames * 1000 / b.rate), c->ticks,
               peak, peak ? 20.0 * log10(peak / 32768.0) : -999.0, c->flags & DM_SFX_F_CUT ? "cut " : "",
               c->flags & DM_SFX_F_CAPPED ? "capped " : "", c->flags & DM_SFX_F_BADCMD ? "BADCMD" : "");
    }
    dm_romsfx_free(&b);
    return rc;
}
