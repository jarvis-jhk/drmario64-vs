/* Deterministic lockstep for two machines: each side owns one pad, sends it a few
 * ticks ahead (the input delay), and a tick runs only once both pads for it are known.
 * The whole frontend (DmFront) is deterministic from its seed and the two pads, so both
 * machines then compute the same game. Inputs travel as unreliable datagrams that repeat
 * everything the peer has not acknowledged yet; a periodic state hash catches a desync.
 * No SDL and no network here: packets in and out as bytes (tests/test_lockstep.c). */
#pragma once
#include <stddef.h>
#include <stdint.h>

enum { DM_LS_RING = 1024, DM_LS_HASHES = 16, DM_LS_HASH_EVERY = 30, DM_LS_PACKET_MAX = 512 };

typedef struct DmLockstep {
    int side;            /* the pad index this machine owns: 0 or 1 */
    int delay;           /* input delay in ticks: the pad read now is used delay ticks later */
    uint32_t tick;       /* the next tick to run */
    uint16_t local[DM_LS_RING], remote[DM_LS_RING];
    uint32_t local_n;    /* own pads are known for ticks < local_n */
    uint32_t remote_n;   /* the peer's pads are known for ticks < remote_n (contiguous) */
    uint32_t peer_has;   /* the peer acknowledged our pads below this tick */
    uint32_t own_tick[DM_LS_HASHES], own_hash[DM_LS_HASHES];
    uint32_t peer_tick[DM_LS_HASHES], peer_hash[DM_LS_HASHES];
    int own_n, peer_n;   /* hashes recorded so far (rings) */
    uint32_t checked;    /* hashes compared and equal */
    int desync;          /* 1 once a hash differed */
    uint32_t desync_tick;
} DmLockstep;

void dm_ls_init(DmLockstep *ls, int side, int delay);
/* Give this tick's own pad; it is scheduled for tick + delay. Call once before each try. */
void dm_ls_input(DmLockstep *ls, uint16_t pad);
/* 1 if tick can run now; fills pads[0..1] in pad order. */
int dm_ls_ready(const DmLockstep *ls, uint16_t pads[2]);
/* The tick ran; hash is the state after it (only every DM_LS_HASH_EVERY ticks used). */
void dm_ls_advance(DmLockstep *ls, uint32_t hash);
/* The datagram to send now (every frame, also while waiting). */
size_t dm_ls_packet(const DmLockstep *ls, uint8_t *buf, size_t cap);
/* A datagram from the peer; 0 if it was not one of ours. */
int dm_ls_receive(DmLockstep *ls, const uint8_t *data, size_t len);
