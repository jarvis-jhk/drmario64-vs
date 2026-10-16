/* Two machines in one process: each runs its own DmFront from the same seed, owns one
 * pad (a bot plays it), and the pads cross a simulated link with delay, jitter, loss and
 * reordering. Both must play a whole match identically, the hashes must be compared and
 * agree, and a deliberately disturbed state must be reported as a desync. */
#include "bot.h"
#include "front.h"
#include "lockstep.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

typedef struct Packet { long due; size_t len; uint8_t data[DM_LS_PACKET_MAX]; } Packet;
typedef struct Link { Packet q[4096]; int n; } Link;

static uint32_t rnd = 12345;
static uint32_t next(void) { rnd = rnd * 1103515245u + 12345u; return rnd >> 8; }

static void send_to(Link *l, const uint8_t *d, size_t len, long now, int delay, int jitter, int loss)
{
    if ((int)(next() % 100) < loss || l->n == 4096) return;
    Packet *p = &l->q[l->n++];
    p->due = now + delay + (jitter ? (long)(next() % (unsigned)jitter) : 0);
    p->len = len;
    memcpy(p->data, d, len);
}

static void deliver(Link *l, DmLockstep *ls, long now)
{
    for (int i = 0; i < l->n;)
        if (l->q[i].due <= now) {
            dm_ls_receive(ls, l->q[i].data, l->q[i].len);
            l->q[i] = l->q[--l->n];
        } else i++;
}

typedef struct Machine { DmFront f; DmLockstep ls; DmBot bot; long stalls; int done_at; uint32_t final_hash; } Machine;
static Machine m[2];

static uint16_t local_pad(Machine *x, int side)
{
    DmFront *f = &x->f;
    if (f->screen == DM_FRONT_SETUP) return (f->frame & 8) && !f->panel[side].ready ? DM_BTN_A : 0;
    return dm_bot_pad(&x->bot, &f->vs, side);
}

/* Time runs in milliseconds; each machine ticks every 16 or 17 ms on its own clock. */
static int play(int delay_ticks, int delay_ms, int jitter, int loss, int disturb_at)
{
    static Link link[2];
    memset(link, 0, sizeof link);
    for (int i = 0; i < 2; i++) {
        dm_front_init(&m[i].f, 777);
        m[i].f.wins = 1;
        m[i].f.panel[0].level = m[i].f.panel[1].level = 3;
        dm_ls_init(&m[i].ls, i, delay_ticks);
        dm_bot_init(&m[i].bot, i ? 1 : 2);
        m[i].stalls = 0;
        m[i].done_at = 0;
    }
    long next_tick[2] = { 0, 5 };
    for (long now = 0; now < 60L * 60 * 1000; now++) {
        for (int i = 0; i < 2; i++) deliver(&link[i], &m[i].ls, now);
        for (int i = 0; i < 2; i++) {
            Machine *x = &m[i];
            if (now < next_tick[i] || x->done_at) continue;
            next_tick[i] += i ? 17 : 16;
            dm_ls_input(&x->ls, local_pad(x, i));
            uint16_t pads[2];
            if (dm_ls_ready(&x->ls, pads)) {
                dm_front_tick(&x->f, pads[0], pads[1]);
                if (disturb_at && i == 1 && x->ls.tick == (uint32_t)disturb_at) x->f.panel[1].rep ^= 1;
                dm_ls_advance(&x->ls, dm_front_hash(&x->f));
                if (x->f.counted && !x->done_at) { x->done_at = (int)x->ls.tick; x->final_hash = dm_front_hash(&x->f); }
            } else x->stalls++;
            uint8_t buf[DM_LS_PACKET_MAX];
            size_t n = dm_ls_packet(&x->ls, buf, sizeof buf);
            send_to(&link[!i], buf, n, now, delay_ms, jitter, loss);
        }
        if (m[0].ls.desync || m[1].ls.desync) return 2;
        if (m[0].done_at && m[1].done_at) {
            /* the faster side may be done first; keep the other's packets flowing */
            return 1;
        }
        if (m[0].done_at || m[1].done_at) { /* the finished one keeps sending its pads */
            for (int i = 0; i < 2; i++)
                if (m[i].done_at && now % 16 == 0) {
                    uint8_t buf[DM_LS_PACKET_MAX];
                    size_t n = dm_ls_packet(&m[i].ls, buf, sizeof buf);
                    send_to(&link[!i], buf, n, now, delay_ms, jitter, loss);
                }
        }
    }
    return 0;
}

int main(void)
{
    struct { int dt, ms, jitter, loss; } nets[] = { { 2, 1, 0, 0 }, { 4, 40, 30, 10 }, { 3, 25, 60, 30 } };
    for (size_t k = 0; k < sizeof nets / sizeof nets[0]; k++) {
        int r = play(nets[k].dt, nets[k].ms, nets[k].jitter, nets[k].loss, 0);
        CHECK(r == 1, "net %zu: result %d (desync %d/%d at %u)", k, r, m[0].ls.desync, m[1].ls.desync, m[0].ls.desync_tick);
        CHECK(m[0].done_at == m[1].done_at, "net %zu: decided at %d vs %d", k, m[0].done_at, m[1].done_at);
        CHECK(m[0].final_hash == m[1].final_hash, "net %zu: final %08x vs %08x", k, m[0].final_hash, m[1].final_hash);
        CHECK(m[0].f.counted == m[1].f.counted && m[0].f.counted, "net %zu: winner %d vs %d", k, m[0].f.counted, m[1].f.counted);
        CHECK(m[0].ls.checked > 1000, "net %zu: only %u ticks hash-checked", k, m[0].ls.checked);
        printf("net %zu: delay %d, %d ms +%d, loss %d%%: match decided at tick %d, winner P%d, hash %08x, checked to %u, stalls %ld/%ld\n",
               k, nets[k].dt, nets[k].ms, nets[k].jitter, nets[k].loss, m[0].done_at, m[0].f.counted, m[0].final_hash,
               m[0].ls.checked, m[0].stalls, m[1].stalls);
    }
    /* negative control: one machine's state drifts at tick 500 */
    int r = play(3, 20, 10, 5, 500);
    CHECK(r == 2, "disturbed state not reported (result %d)", r);
    uint32_t at = m[0].ls.desync ? m[0].ls.desync_tick : m[1].ls.desync_tick;
    CHECK(at >= 510 && at <= 600, "desync tick %u", at);
    printf("disturbed at 500: desync reported at tick %u\n", at);
    if (failures) printf("%d failure(s)\n", failures);
    else printf("lockstep OK\n");
    return failures != 0;
}
