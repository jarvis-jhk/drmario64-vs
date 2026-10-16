/* ROM sound extraction (src/app/romsfx.c). Needs the user's ROM, which is never in the
 * repo: with DM_ROM unset or unreadable the test is skipped (exit 0). With it, checks
 * the ROM validation and bank parsing, that every SE the VS mode plays renders to
 * plausible audio, the .v64 byte order, rejection of non-ROM input and the cache. */
#include "romsfx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(cond, ...)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);                                             \
            fprintf(stderr, __VA_ARGS__);                                                                    \
            fputc('\n', stderr);                                                                             \
            failures++;                                                                                      \
        }                                                                                                    \
    } while (0)

/* SndIndex values fired in VS (SPEC 03 §4.2), plus the voices of charNo 0 and 1. */
static const int vs_se[] = {
    55, 56, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 72, 73, 75, 78, 79, 80, 102,
    91, 92, 93, 94, 95, 81, 82, 83, 84, 85,
};

static uint8_t *read_all(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    size_t cap = 1 << 20, n = 0;
    uint8_t *buf = malloc(cap);
    while (buf) {
        n += fread(buf + n, 1, cap - n, f);
        if (n < cap) break;
        uint8_t *p = realloc(buf, cap * 2);
        if (!p) { free(buf); buf = NULL; break; }
        buf = p;
        cap *= 2;
    }
    fclose(f);
    *size = n;
    return buf;
}

static void temp_path(char *out, size_t len, const char *name)
{
    const char *dir = getenv("TMPDIR");
    if (!dir || !*dir) dir = getenv("TEMP");
    if (!dir || !*dir) dir = ".";
    snprintf(out, len, "%s/%s", dir, name);
}

static int same_bank(const DmSfxBank *a, const DmSfxBank *b)
{
    if (a->rate != b->rate) return 0;
    for (int i = 0; i < DM_SFX_COUNT; i++) {
        const DmSfxClip *x = &a->clip[i], *y = &b->clip[i];
        if (x->frames != y->frames || x->priority != y->priority || x->ticks != y->ticks || x->flags != y->flags)
            return 0;
        if (x->frames && memcmp(x->pcm, y->pcm, (size_t)x->frames * 4) != 0) return 0;
    }
    return 1;
}

static void check_clip(const DmSfxBank *b, int se)
{
    const DmSfxClip *c = &b->clip[se];
    CHECK(c->pcm != NULL, "SE %d: no pcm", se);
    if (!c->pcm) return;
    int ms = (int)((long)c->frames * 1000 / b->rate);
    CHECK(ms >= 15 && ms <= 5000, "SE %d: implausible length %d ms", se, ms);
    CHECK(c->ticks > 0 && c->ticks * 1000 / 60 + 20 >= ms, "SE %d: %d ticks for %d ms", se, c->ticks, ms);
    CHECK(!(c->flags & (DM_SFX_F_BADCMD | DM_SFX_F_CAPPED)), "SE %d: flags %d", se, c->flags);
    int peak = 0, clipped = 0, changes = 0;
    for (int k = 0; k < c->frames * 2; k++) {
        int a = abs(c->pcm[k]);
        if (a > peak) peak = a;
        if (a >= 32767) clipped++;
        if (k >= 2 && c->pcm[k] != c->pcm[k - 2]) changes++;
    }
    CHECK(peak >= 500, "SE %d: nearly silent (peak %d)", se, peak);
    CHECK(clipped < 16, "SE %d: %d clipped samples", se, clipped);
    CHECK(changes > c->frames / 2, "SE %d: not a waveform (%d changes)", se, changes);
}

int main(void)
{
    const char *rom_path = getenv("DM_ROM");
    size_t size = 0;
    uint8_t *rom = rom_path && *rom_path ? read_all(rom_path, &size) : NULL;
    if (!rom) {
        printf("skipped: no ROM (set DM_ROM to the Dr. Mario 64 US ROM)\n");
        return 0;
    }

    char err[256] = "";
    DmSfxBank b;
    int rc = dm_romsfx_load(rom_path, &b, err, sizeof err);
    CHECK(rc == 0, "load failed: %s", err);
    if (rc != 0) return 1;

    /* structure of the banks */
    CHECK(b.rate == DM_SFX_RATE, "rate %d", b.rate);
    CHECK(b.fx_effects == DM_SFX_COUNT, "fx effects %d", b.fx_effects);
    CHECK(b.fx_components > b.fx_effects, "fx components %d (one sub-effect expected)", b.fx_components);
    CHECK(b.fx_waves > 0 && b.ptr_waves >= b.fx_waves, "waves %d / %d", b.fx_waves, b.ptr_waves);
    for (int i = 0; i < DM_SFX_COUNT; i++) {
        CHECK(b.clip[i].pcm != NULL, "SE %d missing", i);
        CHECK(!(b.clip[i].flags & DM_SFX_F_BADCMD), "SE %d hit an unsupported command", i);
    }
    CHECK(b.clip[65].priority == 10 && b.clip[81].priority == 40, "priorities from gSndsEntries");
    CHECK(b.clip[74].flags & DM_SFX_F_CUT, "SE 74 is cut by its max length");

    for (size_t i = 0; i < sizeof vs_se / sizeof vs_se[0]; i++) check_clip(&b, vs_se[i]);

    /* the VADPCM decoder reproduces the encoder's stored loop states bit for bit */
    int looped = dm_romsfx_selfcheck(rom, size, err, sizeof err);
    CHECK(looped >= 8, "decoder selfcheck: %d (%s)", looped, err);

    /* .v64 (16-bit byte-swapped) dumps give the same result */
    uint8_t *swapped = malloc(size);
    if (swapped) {
        for (size_t i = 0; i + 1 < size; i += 2) {
            swapped[i] = rom[i + 1];
            swapped[i + 1] = rom[i];
        }
        DmSfxBank s;
        CHECK(dm_romsfx_load_mem(swapped, size, &s, err, sizeof err) == 0, ".v64 rejected: %s", err);
        CHECK(same_bank(&b, &s), ".v64 renders differently");
        dm_romsfx_free(&s);
        free(swapped);
    }

    /* rejection: truncated image, damaged sound bank header, a file that is no ROM */
    DmSfxBank x;
    CHECK(dm_romsfx_load_mem(rom, 1 << 20, &x, err, sizeof err) != 0, "truncated ROM accepted");
    rom[0x169BE0] ^= 0xFF;
    CHECK(dm_romsfx_load_mem(rom, size, &x, err, sizeof err) != 0, "bad PtrTables magic accepted");
    rom[0x169BE0] ^= 0xFF;
    char junk_path[1024];
    temp_path(junk_path, sizeof junk_path, "dm_romsfx_test_junk.bin");
    FILE *f = fopen(junk_path, "wb");
    if (f) {
        for (int i = 0; i < 1 << 16; i++) fputc("not a rom "[i % 10], f);
        fclose(f);
        err[0] = 0;
        CHECK(dm_romsfx_load(junk_path, &x, err, sizeof err) != 0 && err[0], "non-ROM file accepted");
        remove(junk_path);
    }
    CHECK(dm_romsfx_load("/nonexistent/dir/rom.z64", &x, err, sizeof err) != 0, "missing file accepted");

    /* cache round trip, then a truncated cache must be refused */
    char cache_path[1024];
    temp_path(cache_path, sizeof cache_path, "dm_romsfx_test_cache.bin");
    CHECK(dm_romsfx_save(&b, cache_path) == 0, "save failed");
    DmSfxBank c;
    CHECK(dm_romsfx_load_cache(cache_path, &c) == 0, "load_cache failed");
    CHECK(same_bank(&b, &c), "cache round trip differs");
    dm_romsfx_free(&c);
    size_t csize = 0;
    uint8_t *cbytes = read_all(cache_path, &csize);
    if (cbytes) {
        f = fopen(cache_path, "wb");
        if (f) {
            fwrite(cbytes, 1, csize / 2, f);
            fclose(f);
            CHECK(dm_romsfx_load_cache(cache_path, &c) != 0, "truncated cache accepted");
        }
        free(cbytes);
    }
    remove(cache_path);

    dm_romsfx_free(&b);
    free(rom);
    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("romsfx: ok (%d VS sound effects checked)\n", (int)(sizeof vs_se / sizeof vs_se[0]));
    return 0;
}
