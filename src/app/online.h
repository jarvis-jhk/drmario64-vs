/* Online versus through Warpzone (libwarpzone): code rooms, the launcher's join/host
 * arguments, and the ranked 1v1 queue. Two machines run one DmFront in lockstep
 * (lockstep.h): the host owns pad 0, the guest pad 1. Built without -DDM_WARPZONE every
 * call is a no-op and `available` stays 0. No SDL in here; the caller gives the clock. */
#pragma once
#include "front.h"
#include "lockstep.h"

#include <stdint.h>

enum {
    DM_ON_IDLE,      /* not in a room */
    DM_ON_OPENING,   /* hosting: waiting for the room */
    DM_ON_ROOM,      /* hosting: the room is open, waiting for the opponent */
    DM_ON_JOINING,   /* joining a room */
    DM_ON_QUEUED,    /* in the ranked queue */
    DM_ON_STARTING,  /* in a room with the opponent, agreeing on the game */
    DM_ON_PLAYING,   /* the lockstep game runs: front is valid */
};
enum { DM_ON_NONE, DM_ON_HOST, DM_ON_JOIN, DM_ON_QUEUE };
enum { DM_ON_RANKED_WINS = 3 };

typedef struct DmOnline {
    void *client;          /* wz_client */
    int available;         /* the library is linked and a client exists */
    int online;            /* the server answered */
    int has_account, need_name;
    int64_t account, peer_account;
    char username[24], peer_name[24];
    int phase, host, ranked;
    int pending;           /* an action waiting for the login */
    char pending_arg[16];
    char code[8], room[48], match_id[48], match_state[16];
    uint32_t peer;
    int peer_connected, peer_relayed;
    uint32_t seed;
    int delay;
    DmLockstep ls;
    DmFront *front;        /* the shared game while DM_ON_PLAYING */
    uint32_t decided_tick, decided_hash;
    int decided_winner;    /* 1 + side once the match is decided, else 0 */
    int reported;
    uint32_t opened_ms, login_ms, last_rx_ms, now_ms;
    int stalled;           /* ticks in a row without the opponent's pad */
    int loss;              /* test hook: drop this percentage of outgoing game datagrams */
    uint32_t loss_rng;
    /* what the host offers: mode and rounds; each side's level and speed */
    DmMode mode;
    int wins, level[2], glevel[2], speed[2];
    char status[96], error[96];
    int verbose;
} DmOnline;

int dm_online_open(DmOnline *o, const char *version, uint32_t now_ms);
void dm_online_close(DmOnline *o);
/* --warpzone-join / --warpzone-host (and the environment): 1 joining, 2 host asked, 0 none. */
int dm_online_launch(DmOnline *o, int argc, char **argv);
void dm_online_register(DmOnline *o, const char *name);
/* Host offers the settings of `from` (mode, rounds; its own level/speed for both sides). */
void dm_online_host(DmOnline *o, const DmFront *from);
void dm_online_join_code(DmOnline *o, const char *code);
void dm_online_queue(DmOnline *o);
void dm_online_leave(DmOnline *o);
/* Network and events; call every frame. */
void dm_online_update(DmOnline *o, uint32_t now_ms);
/* While playing: give this tick's own pad; 1 if a tick of the shared game ran. */
int dm_online_step(DmOnline *o, uint16_t pad);
/* A short line about the connection ("VS ANNA  34 MS DIRECT"), for the screen. */
void dm_online_line(const DmOnline *o, char *out, size_t cap);
/* 1 if text looks like a room code (6 of Warpzone's code letters, dashes/spaces ignored). */
int dm_online_is_code(const char *text);
