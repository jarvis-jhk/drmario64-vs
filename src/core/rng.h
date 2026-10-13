/* The game's random number generator (decomp: calc.c randomseed/randomize00/irandom/
 * random). A 55-entry lagged subtractive generator modulo 0x10000; one global state. */
#pragma once
#include <stdint.h>

typedef struct DmRng {
    int32_t table[55];
    int32_t index;
} DmRng;

void dm_rng_seed(DmRng *r, int32_t seed); /* randomseed */
int32_t dm_rng_next(DmRng *r);            /* irandom: 0..0xFFFF */
int32_t dm_rng_below(DmRng *r, int32_t n); /* random(n) = (n * irandom()) >> 16 */
