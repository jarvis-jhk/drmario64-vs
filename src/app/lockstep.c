#include "lockstep.h"

#include <string.h>

#define MAGIC 0x4C /* 'L' */
#define VERSION 1

void dm_ls_init(DmLockstep *ls, int side, int delay)
{
    memset(ls, 0, sizeof *ls);
    ls->side = side;
    ls->delay = delay < 1 ? 1 : delay > 30 ? 30 : delay;
    /* the first delay ticks run with released pads on both sides */
    ls->local_n = ls->remote_n = ls->peer_has = (uint32_t)ls->delay;
}

void dm_ls_input(DmLockstep *ls, uint16_t pad)
{
    if (ls->local_n > ls->tick + (uint32_t)ls->delay) return; /* already scheduled */
    ls->local[ls->local_n % DM_LS_RING] = pad;
    ls->local_n++;
}

int dm_ls_ready(const DmLockstep *ls, uint16_t pads[2])
{
    if (ls->desync || ls->tick >= ls->remote_n || ls->tick >= ls->local_n) return 0;
    uint16_t own = ls->local[ls->tick % DM_LS_RING], other = ls->remote[ls->tick % DM_LS_RING];
    if (ls->tick < (uint32_t)ls->delay) own = other = 0;
    pads[ls->side] = own;
    pads[!ls->side] = other;
    return 1;
}

static void compare(DmLockstep *ls)
{
    for (int i = 0; i < DM_LS_HASHES && i < ls->own_n; i++)
        for (int k = 0; k < DM_LS_HASHES && k < ls->peer_n; k++)
            if (ls->own_tick[i] == ls->peer_tick[k] && ls->own_tick[i]) {
                if (ls->own_hash[i] != ls->peer_hash[k]) {
                    if (!ls->desync) ls->desync_tick = ls->own_tick[i];
                    ls->desync = 1;
                } else if (ls->own_tick[i] > ls->checked) ls->checked = ls->own_tick[i];
            }
}

void dm_ls_advance(DmLockstep *ls, uint32_t hash)
{
    ls->tick++;
    if (ls->tick % DM_LS_HASH_EVERY) return;
    int slot = ls->own_n++ % DM_LS_HASHES;
    ls->own_tick[slot] = ls->tick;
    ls->own_hash[slot] = hash;
    compare(ls);
}

static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static uint32_t get32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* magic, version, ack u32, hash tick u32, hash u32, first u32, n u8, n pads u16 */
size_t dm_ls_packet(const DmLockstep *ls, uint8_t *buf, size_t cap)
{
    if (cap < 19) return 0;
    uint32_t first = ls->peer_has, n = ls->local_n > first ? ls->local_n - first : 0;
    if (n > 200) n = 200;
    if (19 + 2 * n > cap) n = (uint32_t)((cap - 19) / 2);
    buf[0] = MAGIC;
    buf[1] = VERSION;
    put32(buf + 2, ls->remote_n);
    uint32_t ht = 0, h = 0;
    if (ls->own_n) {
        int last = (ls->own_n - 1) % DM_LS_HASHES;
        ht = ls->own_tick[last];
        h = ls->own_hash[last];
    }
    put32(buf + 6, ht);
    put32(buf + 10, h);
    put32(buf + 14, first);
    buf[18] = (uint8_t)n;
    for (uint32_t i = 0; i < n; i++) {
        uint16_t pad = ls->local[(first + i) % DM_LS_RING];
        buf[19 + 2 * i] = (uint8_t)pad;
        buf[20 + 2 * i] = (uint8_t)(pad >> 8);
    }
    return 19 + 2 * n;
}

int dm_ls_receive(DmLockstep *ls, const uint8_t *d, size_t len)
{
    if (len < 19 || d[0] != MAGIC || d[1] != VERSION) return 0;
    uint32_t ack = get32(d + 2), ht = get32(d + 6), h = get32(d + 10), first = get32(d + 14), n = d[18];
    if (len < 19 + 2 * (size_t)n) return 0;
    if (ack > ls->peer_has && ack <= ls->local_n) ls->peer_has = ack;
    /* only what extends the contiguous run; anything older is a repeat, later ones come again */
    if (first <= ls->remote_n && first + n > ls->remote_n && first + n - ls->tick < DM_LS_RING) {
        for (uint32_t t = ls->remote_n; t < first + n; t++) {
            uint32_t i = t - first;
            ls->remote[t % DM_LS_RING] = (uint16_t)(d[19 + 2 * i] | d[20 + 2 * i] << 8);
        }
        ls->remote_n = first + n;
    }
    if (ht) {
        int seen = 0;
        for (int k = 0; k < DM_LS_HASHES && k < ls->peer_n; k++) seen |= ls->peer_tick[k] == ht;
        if (!seen) {
            int slot = ls->peer_n++ % DM_LS_HASHES;
            ls->peer_tick[slot] = ht;
            ls->peer_hash[slot] = h;
            compare(ls);
        }
    }
    return 1;
}
