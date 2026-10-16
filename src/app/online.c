#include "online.h"
#include "version.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHANNEL_GAME 0  /* lockstep datagrams, unreliable */
#define CHANNEL_SETUP 1 /* the start handshake, reliable */
#define PROTOCOL 1

int dm_online_is_code(const char *text)
{
    static const char alphabet[] = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";
    int n = 0;
    for (const char *c = text; *c; c++) {
        if (*c == '-' || *c == ' ') continue;
        if (!strchr(alphabet, toupper((unsigned char)*c))) return 0;
        n++;
    }
    return n == 6;
}

static void upper(char *s)
{
    for (; *s; s++) *s = (char)toupper((unsigned char)*s);
}

static void set_status(DmOnline *o, const char *s)
{
    snprintf(o->status, sizeof o->status, "%s", s);
    upper(o->status);
}

#ifdef DM_WARPZONE
#include "warpzone.h"

#define C(o) ((wz_client *)(o)->client)

/* The few fields read from the library's JSON, which has fixed shapes. */
static const char *json_key(const char *json, const char *key)
{
    char pattern[48];
    snprintf(pattern, sizeof pattern, "\"%s\":", key);
    const char *p = strstr(json, pattern);
    if (!p) return NULL;
    p += strlen(pattern);
    while (*p == ' ') p++;
    return p;
}

static int json_str(const char *json, const char *key, char *out, size_t cap)
{
    const char *p = json_key(json, key);
    out[0] = 0;
    if (!p || *p != '"') return 0;
    size_t n = 0;
    for (p++; *p && *p != '"' && n + 1 < cap; p++) {
        if (*p == '\\' && p[1]) p++;
        out[n++] = *p;
    }
    out[n] = 0;
    return 1;
}

static int json_int(const char *json, const char *key, int64_t *out)
{
    const char *p = json_key(json, key);
    if (!p || !(isdigit((unsigned char)*p) || *p == '-')) return 0;
    *out = strtoll(p, NULL, 10);
    return 1;
}

static int json_bool(const char *json, const char *key)
{
    const char *p = json_key(json, key);
    return p && !strncmp(p, "true", 4);
}

int dm_online_open(DmOnline *o, const char *version, uint32_t now_ms)
{
    memset(o, 0, sizeof *o);
    o->mode = DM_MODE_NORMAL;
    o->wins = 3;
    o->verbose = getenv("DM_WZ_VERBOSE") != NULL;
    const char *loss = getenv("DM_NET_LOSS");
    o->loss = loss ? atoi(loss) : 0;
    o->loss_rng = now_ms | 1;
    if (getenv("DM_NO_WARPZONE")) { set_status(o, "online play switched off"); return 0; }
    /* Matches are joined by hand: a fresh client is also told about matches made before
     * it started, whose rooms are gone (seen with libwarpzone 0.1.0). */
    wz_config cfg = { WZ_ABI_VERSION, NULL, "drmario64-vs", version, NULL, 0 };
    o->client = wz_create(&cfg);
    if (!o->client) {
        fprintf(stderr, "warpzone: %s\n", wz_last_error());
        set_status(o, "online play is not available");
        return 0;
    }
    o->available = 1;
    o->opened_ms = o->now_ms = now_ms;
    set_status(o, "connecting to warpzone");
    return 1;
}

static void end_session(DmOnline *o)
{
    if (o->front) {
        dm_front_free(o->front);
        free(o->front);
        o->front = NULL;
    }
}

static void forget_room(DmOnline *o)
{
    end_session(o);
    o->phase = DM_ON_IDLE;
    o->host = o->ranked = o->peer_connected = o->peer_relayed = o->reported = 0;
    o->peer = 0;
    o->peer_account = -1;
    o->peer_name[0] = o->code[0] = o->room[0] = 0;
}

void dm_online_close(DmOnline *o)
{
    end_session(o);
    if (o->client) wz_destroy(C(o));
    o->client = NULL;
    o->available = 0;
}

int dm_online_launch(DmOnline *o, int argc, char **argv)
{
    if (!o->client) return 0;
    int intent = wz_handle_launch(C(o), argc, (const char *const *)argv);
    if (intent == 1) {
        o->phase = DM_ON_JOINING;
        set_status(o, "joining the room");
    }
    return intent;
}

void dm_online_register(DmOnline *o, const char *name)
{
    if (!o->client) return;
    o->error[0] = 0;
    set_status(o, "checking the name");
    wz_register(C(o), name);
}

static void run(DmOnline *o, int action, const char *arg_in)
{
    char arg[16];
    snprintf(arg, sizeof arg, "%s", arg_in ? arg_in : ""); /* may be pending_arg itself */
    o->error[0] = 0;
    if (!o->has_account || (action == DM_ON_QUEUE && o->now_ms - o->login_ms < 3000)) {
        /* after the login: the event stream first replays old matches, so the queue waits a moment */
        o->pending = action;
        snprintf(o->pending_arg, sizeof o->pending_arg, "%s", arg);
        set_status(o, o->need_name ? "choose a name for online play" : "connecting to warpzone");
        return;
    }
    o->pending = DM_ON_NONE;
    if (action == DM_ON_HOST) {
        static const char *mode[] = { "Normal", "Flash", "", "Time attack" };
        char options[300];
        snprintf(options, sizeof options,
                 "{\"max_players\":2,\"visibility\":\"unlisted\",\"entry\":\"code\",\"preset\":\"1v1\","
                 "\"metadata\":{\"mode\":\"%s\",\"wins\":\"%d\"}}",
                 mode[o->mode], o->wins);
        o->phase = DM_ON_OPENING;
        o->host = 1;
        wz_host(C(o), options);
        set_status(o, "opening a room");
    } else if (action == DM_ON_JOIN) {
        o->phase = DM_ON_JOINING;
        o->host = 0;
        snprintf(o->code, sizeof o->code, "%.6s", arg);
        wz_join_code(C(o), arg);
        char s[40];
        snprintf(s, sizeof s, "joining room %s", arg);
        set_status(o, s);
    } else if (action == DM_ON_QUEUE) {
        o->phase = DM_ON_QUEUED;
        wz_queue(C(o), "1v1", NULL);
        set_status(o, "looking for an opponent");
    }
}

void dm_online_host(DmOnline *o, const DmFront *from)
{
    o->mode = from->mode;
    o->wins = from->wins;
    for (int i = 0; i < 2; i++) { /* the host's own choices; the guest changes its panel in the game */
        o->level[i] = from->panel[0].level;
        o->glevel[i] = from->panel[0].glevel;
        o->speed[i] = from->panel[0].speed;
    }
    run(o, DM_ON_HOST, NULL);
}

void dm_online_join_code(DmOnline *o, const char *code)
{
    char clean[8];
    int n = 0;
    for (const char *c = code; *c && n < 6; c++)
        if (*c != '-' && *c != ' ') clean[n++] = (char)toupper((unsigned char)*c);
    clean[n] = 0;
    run(o, DM_ON_JOIN, clean);
}

void dm_online_queue(DmOnline *o) { run(o, DM_ON_QUEUE, NULL); }

void dm_online_leave(DmOnline *o)
{
    if (!o->client) return;
    if (o->phase == DM_ON_QUEUED) wz_leave_queue(C(o));
    if (o->phase != DM_ON_IDLE && o->phase != DM_ON_QUEUED) wz_leave(C(o));
    o->pending = DM_ON_NONE;
    forget_room(o);
    o->match_state[0] = 0;
    set_status(o, o->has_account ? "online" : "connecting to warpzone");
}

/* The host's offer: seed, input delay, ranked, settings, and the game version. */
static void send_offer(DmOnline *o)
{
    uint8_t m[64];
    int rtt = wz_peer_rtt_ms(C(o), o->peer);
    /* a pad must cross before it is due: half the round trip in ticks, plus slack */
    o->delay = rtt >= 0 ? 2 + (rtt / 2 + 16) / 17 : 4;
    const char *forced = getenv("DM_NET_DELAY");
    if (forced && atoi(forced) > 0) o->delay = atoi(forced);
    if (o->delay > 10) o->delay = 10;
    o->seed = (uint32_t)o->now_ms * 2654435761u ^ (uint32_t)o->account * 40503u ^ (uint32_t)(uintptr_t)o;
    m[0] = 'S';
    m[1] = PROTOCOL;
    for (int i = 0; i < 4; i++) m[2 + i] = (uint8_t)(o->seed >> (8 * i));
    m[6] = (uint8_t)o->delay;
    m[7] = (uint8_t)o->ranked;
    m[8] = (uint8_t)o->mode;
    m[9] = (uint8_t)o->wins;
    for (int i = 0; i < 2; i++) {
        m[10 + i] = (uint8_t)o->level[i];
        m[12 + i] = (uint8_t)o->glevel[i];
        m[14 + i] = (uint8_t)o->speed[i];
    }
    int n = snprintf((char *)m + 16, sizeof m - 16, "%s", DM_VERSION);
    wz_send(C(o), o->peer, CHANNEL_SETUP, m, (size_t)(16 + n), 1);
}

static void start_session(DmOnline *o, int side)
{
    end_session(o);
    o->front = calloc(1, sizeof *o->front);
    if (!o->front) return;
    dm_front_init(o->front, o->seed);
    DmFront *f = o->front;
    f->mode = o->mode;
    f->wins = o->wins;
    for (int i = 0; i < 2; i++) {
        f->panel[i].level = o->level[i];
        f->panel[i].glevel = o->glevel[i];
        f->panel[i].speed = o->speed[i];
    }
    f->locked = o->ranked;
    dm_ls_init(&o->ls, side, o->delay);
    o->phase = DM_ON_PLAYING;
    o->stalled = 0;
    o->decided_winner = 0;
    o->reported = 0;
    o->last_rx_ms = o->now_ms;
    if (o->host && !o->ranked) wz_set_closed(C(o), 1);
    set_status(o, "playing online");
    if (o->verbose) printf("online start side=%d seed=%08x delay=%d ranked=%d\n", side, o->seed, o->delay, o->ranked), fflush(stdout);
}

static void ranked_settings(DmOnline *o)
{
    const char *w = getenv("DM_RANKED_WINS");
    o->mode = DM_MODE_NORMAL;
    o->wins = w && atoi(w) >= 1 && atoi(w) <= 3 ? atoi(w) : DM_ON_RANKED_WINS;
    for (int i = 0; i < 2; i++) { /* everyone plays the same: level 10, MED */
        o->level[i] = 10;
        o->glevel[i] = 1;
        o->speed[i] = 1;
    }
}

static void fail(DmOnline *o, const char *why)
{
    snprintf(o->error, sizeof o->error, "%s", why);
    upper(o->error);
    if (o->verbose) printf("online ended: %s\n", why), fflush(stdout);
}

static void on_setup(DmOnline *o, const uint8_t *d, size_t len)
{
    if (o->host) {
        if (len >= 1 && d[0] == 'R' && o->phase == DM_ON_STARTING) start_session(o, 0);
        return;
    }
    if (len < 16 || d[0] != 'S') return;
    if (d[1] != PROTOCOL || len - 16 != strlen(DM_VERSION) || memcmp(d + 16, DM_VERSION, len - 16)) {
        char v[40];
        size_t n = len - 16 < sizeof v - 1 ? len - 16 : sizeof v - 1;
        memcpy(v, d + 16, n);
        v[n] = 0;
        char why[96];
        snprintf(why, sizeof why, "the host plays version %s, you %s", v, DM_VERSION);
        fail(o, why);
        dm_online_leave(o);
        return;
    }
    o->seed = d[2] | (uint32_t)d[3] << 8 | (uint32_t)d[4] << 16 | (uint32_t)d[5] << 24;
    o->delay = d[6];
    o->ranked = d[7];
    o->mode = (DmMode)d[8];
    o->wins = d[9];
    for (int i = 0; i < 2; i++) {
        o->level[i] = d[10 + i];
        o->glevel[i] = d[12 + i];
        o->speed[i] = d[14 + i];
    }
    uint8_t r = 'R';
    wz_send(C(o), o->peer, CHANNEL_SETUP, &r, 1, 1);
    start_session(o, 1);
}

static void report(DmOnline *o)
{
    int64_t acc[2] = { o->host ? o->account : o->peer_account, o->host ? o->peer_account : o->account };
    if (acc[0] < 0 || acc[1] < 0) return;
    int w = o->decided_winner - 1;
    char json[160];
    snprintf(json, sizeof json, "{\"placements\":[[%lld],[%lld]],\"state_hash\":\"%08x\"}", (long long)acc[w],
             (long long)acc[!w], o->decided_hash);
    wz_report_match(C(o), o->match_id, json);
    o->reported = 1;
    if (o->verbose) printf("warpzone report match=%s %s\n", o->match_id, json), fflush(stdout);
}

static void send_game(DmOnline *o)
{
    uint8_t buf[DM_LS_PACKET_MAX];
    size_t n = dm_ls_packet(&o->ls, buf, sizeof buf);
    if (!n) return;
    if (o->loss > 0) {
        o->loss_rng = o->loss_rng * 1103515245u + 12345u;
        if ((int)((o->loss_rng >> 8) % 100) < o->loss) return;
    }
    wz_send(C(o), o->peer, CHANNEL_GAME, buf, n, 0);
}

void dm_online_update(DmOnline *o, uint32_t now_ms)
{
    if (!o->client) return;
    o->now_ms = now_ms;
    wz_poll(C(o));
    wz_event e;
    while (wz_next_event(C(o), &e)) {
        const char *j = e.json ? e.json : "{}";
        if (o->verbose && e.type != WZ_EVENT_PEER_DATA) printf("warpzone event %d %s\n", (int)e.type, j), fflush(stdout);
        switch (e.type) {
        case WZ_EVENT_LOGGED_IN:
        case WZ_EVENT_REGISTERED: {
            o->online = 1;
            int64_t id;
            if (json_int(j, "id", &id)) {
                o->account = id;
                o->has_account = 1;
                o->need_name = 0;
                o->login_ms = now_ms;
                json_str(j, "username", o->username, sizeof o->username);
                char s[48];
                snprintf(s, sizeof s, "online as %s", o->username);
                set_status(o, s);
            } else {
                o->need_name = 1;
                const char *name = getenv("DM_WZ_NAME");
                if (name && *name) dm_online_register(o, name);
                else set_status(o, "choose a name for online play");
            }
            break;
        }
        case WZ_EVENT_ERROR: {
            char op[16], message[96];
            json_str(j, "op", op, sizeof op);
            json_str(j, "message", message, sizeof message);
            fprintf(stderr, "warpzone %s: %s\n", op, message);
            if (!strcmp(op, "login")) {
                o->online = 0;
                set_status(o, "warpzone is not reachable");
                break;
            }
            if (!strcmp(op, "udp") || !strcmp(op, "report") || !strcmp(op, "list")) break;
            if (!strcmp(op, "join") && o->ranked && o->phase == DM_ON_JOINING) {
                /* a replayed old match: its room is gone and we are still queued */
                o->ranked = 0;
                o->phase = DM_ON_QUEUED;
                set_status(o, "looking for an opponent");
                break;
            }
            fail(o, message);
            if (!strcmp(op, "register")) o->need_name = 1;
            if (!strcmp(op, "host") || !strcmp(op, "join") || !strcmp(op, "queue")) forget_room(o);
            break;
        }
        case WZ_EVENT_ROOM_HOSTED:
            json_str(j, "code", o->code, sizeof o->code);
            printf("warpzone code=%s\n", o->code);
            fflush(stdout);
            break;
        case WZ_EVENT_ROOM_JOINED:
            o->host = json_bool(j, "host");
            json_str(j, "id", o->room, sizeof o->room);
            o->phase = o->host && !o->ranked ? DM_ON_ROOM : DM_ON_STARTING;
            set_status(o, o->host && !o->ranked ? "waiting for the opponent" : "connecting to the opponent");
            break;
        case WZ_EVENT_KNOCKING: set_status(o, "waiting for the host to let you in"); break;
        case WZ_EVENT_KNOCK_DENIED: fail(o, "the host did not let you in"); forget_room(o); break;
        case WZ_EVENT_KNOCK: {
            /* our rooms use codes; a knock only comes into a ranked room the server filled */
            char knock[64];
            if (json_str(j, "knock", knock, sizeof knock)) wz_answer_knock(C(o), knock, o->ranked);
            break;
        }
        case WZ_EVENT_PEER_JOINED:
            if (o->peer_connected || o->phase == DM_ON_PLAYING) break; /* one opponent */
            o->peer = e.peer;
            o->peer_account = -1;
            json_int(j, "account", &o->peer_account);
            json_str(j, "username", o->peer_name, sizeof o->peer_name);
            if (o->phase == DM_ON_ROOM) {
                o->phase = DM_ON_STARTING;
                set_status(o, "connecting to the opponent");
            }
            break;
        case WZ_EVENT_PEER_CONNECTED:
        case WZ_EVENT_PEER_PATH:
            if (e.peer != o->peer && o->peer) break;
            if (!o->peer) { /* the guest learns of the host this way */
                o->peer = e.peer;
                o->peer_account = wz_peer_account(C(o), e.peer);
                wz_peer_name(C(o), e.peer, o->peer_name, sizeof o->peer_name);
            }
            if (!o->peer_name[0]) wz_peer_name(C(o), e.peer, o->peer_name, sizeof o->peer_name);
            if (o->peer_account < 0) o->peer_account = wz_peer_account(C(o), e.peer);
            o->peer_relayed = json_bool(j, "relayed");
            if (e.type == WZ_EVENT_PEER_CONNECTED) {
                o->peer_connected = 1;
                if (o->host && o->phase == DM_ON_STARTING) send_offer(o);
            }
            break;
        case WZ_EVENT_PEER_DATA:
            if (e.peer != o->peer) break;
            if (e.channel == CHANNEL_SETUP) on_setup(o, e.data, e.len);
            else if (e.channel == CHANNEL_GAME && o->phase == DM_ON_PLAYING && dm_ls_receive(&o->ls, e.data, e.len))
                o->last_rx_ms = now_ms;
            break;
        case WZ_EVENT_PEER_LEFT:
            if (e.peer != o->peer) break;
            if (o->phase == DM_ON_PLAYING || o->phase == DM_ON_STARTING) {
                if (!o->decided_winner || o->phase == DM_ON_STARTING) fail(o, "the opponent left");
                if (o->host && !o->ranked) { /* the room stays: wait for the next opponent */
                    end_session(o);
                    o->phase = DM_ON_ROOM;
                    o->peer = 0;
                    o->peer_connected = 0;
                    o->peer_name[0] = 0;
                    wz_set_closed(C(o), 0);
                    set_status(o, "waiting for the opponent");
                } else if (!o->ranked) dm_online_leave(o);
            }
            break;
        case WZ_EVENT_ROOM_CLOSED:
        case WZ_EVENT_KICKED:
            if (o->phase != DM_ON_IDLE && o->phase != DM_ON_QUEUED && !(o->ranked && o->decided_winner)) fail(o, "the room was closed");
            if (!(o->ranked && o->decided_winner && !o->match_state[0])) forget_room(o);
            break;
        case WZ_EVENT_QUEUED:
            if (json_bool(j, "queued") && o->phase == DM_ON_QUEUED) {
                int64_t waiting = 0;
                json_int(j, "waiting", &waiting);
                char s[64];
                snprintf(s, sizeof s, "looking for an opponent - %d waiting", (int)waiting);
                set_status(o, s);
                o->ranked = 2; /* confirmed: from now on a MATCH_FOUND is ours */
            }
            break;
        case WZ_EVENT_MATCH_FOUND: {
            if (o->phase != DM_ON_QUEUED || o->ranked != 2) break;
            char room[48];
            json_str(j, "room", room, sizeof room);
            json_str(j, "match_id", o->match_id, sizeof o->match_id);
            o->ranked = 1;
            o->match_state[0] = 0;
            o->phase = DM_ON_JOINING;
            ranked_settings(o);
            wz_join(C(o), room, NULL, NULL);
            set_status(o, "match found");
            break;
        }
        case WZ_EVENT_MATCH_FINISHED: {
            char id[48], state[16];
            json_str(j, "match_id", id, sizeof id);
            json_str(j, "state", state, sizeof state);
            if (!strcmp(id, o->match_id)) {
                snprintf(o->match_state, sizeof o->match_state, "%s", state);
                upper(o->match_state);
                printf("ranked match=%s tick=%u hash=%08x winner=%d result=%s\n", o->match_id, o->decided_tick, o->decided_hash,
                       o->decided_winner, o->match_state);
                fflush(stdout);
            }
            break;
        }
        default: break;
        }
    }
    if (o->pending && o->has_account) run(o, o->pending, o->pending_arg);
    /* the library holds back every event, the login error too, until the server first
     * answered, so an unreachable server only shows as silence */
    if (!o->online && !o->has_account && now_ms - o->opened_ms > 10000) {
        set_status(o, "warpzone is not reachable");
        if (o->pending && !o->error[0]) fail(o, "warpzone is not reachable - still trying");
    }
    if (o->phase == DM_ON_PLAYING) {
        send_game(o);
        if (now_ms - o->last_rx_ms > 15000) {
            fail(o, "the connection to the opponent was lost");
            dm_online_leave(o);
        } else if (o->ls.desync) {
            char why[64];
            snprintf(why, sizeof why, "the games went out of step at tick %u", o->ls.desync_tick);
            fail(o, why);
            dm_online_leave(o);
        }
    }
}

void dm_online_line(const DmOnline *o, char *out, size_t cap)
{
    out[0] = 0;
    if (o->phase != DM_ON_PLAYING) return;
    char name[24];
    snprintf(name, sizeof name, "%s", o->peer_name[0] ? o->peer_name : "opponent");
    upper(name);
    int rtt = wz_peer_rtt_ms(C(o), o->peer);
    if (o->stalled > 30) snprintf(out, cap, "WAITING FOR %s", name);
    else if (rtt >= 0) snprintf(out, cap, "%s %s  %d MS %s", o->ranked ? "RANKED VS" : "VS", name, rtt, o->peer_relayed ? "RELAYED" : "DIRECT");
    else snprintf(out, cap, "%s %s  %s", o->ranked ? "RANKED VS" : "VS", name, o->peer_relayed ? "RELAYED" : "DIRECT");
}

int dm_online_step(DmOnline *o, uint16_t pad)
{
    if (o->phase != DM_ON_PLAYING || !o->front) return 0;
    uint16_t pads[2];
    dm_ls_input(&o->ls, pad);
    if (!dm_ls_ready(&o->ls, pads)) {
        o->stalled++;
        return 0;
    }
    o->stalled = 0;
    dm_front_tick(o->front, pads[0], pads[1]);
    uint32_t h = dm_front_hash(o->front);
    dm_ls_advance(&o->ls, h);
    if (o->front->counted && !o->decided_winner) { /* the session's first decided match is the one that counts */
        o->decided_winner = o->front->counted;
        o->decided_tick = o->ls.tick;
        o->decided_hash = h;
        printf("online match decided tick=%u winner=%d hash=%08x checked=%u\n", o->decided_tick, o->decided_winner, h, o->ls.checked);
        fflush(stdout);
    }
    if (o->ranked && o->decided_winner && !o->reported) report(o);
    return 1;
}

#else

int dm_online_open(DmOnline *o, const char *version, uint32_t now_ms)
{
    (void)version;
    (void)now_ms;
    memset(o, 0, sizeof *o);
    (void)set_status;
    return 0;
}
void dm_online_close(DmOnline *o) { (void)o; }
int dm_online_launch(DmOnline *o, int argc, char **argv) { (void)o; (void)argc; (void)argv; return 0; }
void dm_online_register(DmOnline *o, const char *name) { (void)o; (void)name; }
void dm_online_host(DmOnline *o, const DmFront *from) { (void)o; (void)from; }
void dm_online_join_code(DmOnline *o, const char *code) { (void)o; (void)code; }
void dm_online_queue(DmOnline *o) { (void)o; }
void dm_online_leave(DmOnline *o) { (void)o; }
void dm_online_update(DmOnline *o, uint32_t now_ms) { (void)o; (void)now_ms; }
int dm_online_step(DmOnline *o, uint16_t pad) { (void)o; (void)pad; return 0; }
void dm_online_line(const DmOnline *o, char *out, size_t cap) { (void)o; if (cap) out[0] = 0; }

#endif
