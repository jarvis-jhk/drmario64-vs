#include "rng.h"

/* MIPS addu/subu wrap; do the arithmetic unsigned so C has no overflow UB. */
static int32_t sub32(int32_t a, int32_t b) { return (int32_t)((uint32_t)a - (uint32_t)b); }

/* randomize00. Only a negative difference is corrected: values above 0xFFFF that a
 * large seed puts in the table survive, exactly as in the original. */
static void refill(DmRng *r)
{
    for (int i = 0; i < 24; i++) {
        int32_t v = sub32(r->table[i], r->table[i + 31]);
        r->table[i] = v < 0 ? v + 0x10000 : v;
    }
    for (int i = 24; i < 55; i++) {
        int32_t v = sub32(r->table[i], r->table[i - 24]);
        r->table[i] = v < 0 ? v + 0x10000 : v;
    }
}

void dm_rng_seed(DmRng *r, int32_t seed)
{
    int32_t value = 1;
    r->table[54] = seed;
    for (int i = 1; i < 55; i++) {
        int idx = (21 * i) % 55 - 1;
        r->table[idx] = value;
        value = sub32(seed, value);
        if (value < 0) value += 0x10000;
        seed = r->table[idx];
    }
    for (int k = 0; k < 4; k++) refill(r);
    r->index = 0;
}

int32_t dm_rng_next(DmRng *r)
{
    if (++r->index >= 55) {
        refill(r);
        r->index = 0;
    }
    return (uint16_t)r->table[r->index];
}

int32_t dm_rng_below(DmRng *r, int32_t n)
{
    return (int32_t)((uint32_t)n * (uint32_t)dm_rng_next(r)) >> 16;
}
