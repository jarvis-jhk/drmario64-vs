/* A whole session through the frontend logic, headless: the setup screen, a match of
 * two wins between a playing bot and one that tops out, pause and CONTINUE, NEXT,
 * REPLAY of the deciding round (it must decide identically), and END back to setup. */
#include "bot.h"
#include "front.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static DmFront f;
static DmBot bots[2];
static int auto_play = 1;

/* One tick: the bots play unless a scripted pad is given (0xFFFF = bot). */
static void tick(uint16_t a, uint16_t b)
{
    uint16_t pa = a == 0xFFFF ? (auto_play ? dm_bot_pad(&bots[0], &f.vs, 0) : 0) : a;
    uint16_t pb = b == 0xFFFF ? (auto_play ? dm_bot_pad(&bots[1], &f.vs, 1) : 0) : b;
    dm_front_tick(&f, pa, pb);
}

static void press(int player, uint16_t btn) /* one press and release */
{
    tick(player == 0 ? btn : 0, player == 1 ? btn : 0);
    tick(0, 0);
}

static int run_until(int (*cond)(void), int limit)
{
    for (int i = 0; i < limit; i++) {
        if (cond()) return 1;
        tick(0xFFFF, 0xFFFF);
    }
    return cond();
}

static int round_decided(void) { return f.screen == DM_FRONT_GAME && dm_vs_round_state(&f.vs) != 0; }
static int started(void) { return f.vs.started && f.vs.pl[0].mode == DM_M_DOWN; }

int main(void)
{
    dm_front_init(&f, 12345);
    dm_bot_init(&bots[0], 2);
    dm_bot_init(&bots[1], 0);

    /* setup: P1 moves to WINS and lowers it to 2, P2 changes its level row */
    press(0, DM_BTN_DOWN);
    CHECK(f.panel[0].row == DM_ROW_WINS, "row %d", f.panel[0].row);
    press(0, DM_BTN_LEFT);
    CHECK(f.wins == 2, "wins %d", f.wins);
    press(1, DM_BTN_DOWN);
    press(1, DM_BTN_DOWN);
    for (int i = 0; i < 30; i++) tick(0, DM_BTN_LEFT); /* auto-repeat: the press, then ticks 12, 18, 24 */
    tick(0, 0);
    CHECK(f.panel[1].level == 10 - 4, "auto-repeat level %d", f.panel[1].level);
    press(0, DM_BTN_A);
    CHECK(f.screen == DM_FRONT_SETUP && f.panel[0].ready, "P1 ready");
    press(1, DM_BTN_START);
    CHECK(f.screen == DM_FRONT_GAME, "match started");
    CHECK(f.vs.cfg.wins == 2 && f.vs.cfg.p[1].virus_level == 6 && f.vs.cfg.p[0].virus_level == 10, "config");

    /* round 1: P2 stacks and tops out */
    CHECK(run_until(round_decided, 20000), "round 1 decided");
    CHECK(f.vs.win_count[0] == 1 && f.vs.win_count[1] == 0, "wins %d:%d", f.vs.win_count[0], f.vs.win_count[1]);
    CHECK(f.vs.pl[0].mode == DM_M_WIN_RETRY && f.vs.retry_type[0] == 5 && f.vs.retry_select[0] == 1, "NEXT menu");
    press(0, DM_BTN_A); /* NEXT */
    CHECK(dm_vs_round_state(&f.vs) == 0 && f.vs.pl[0].mode == DM_M_INIT, "round 2 begins (mode %d)", f.vs.pl[0].mode);

    /* round 2: P2 pauses, P1 sees the logo only, P2 continues */
    CHECK(run_until(started, 2000), "round 2 started");
    for (int i = 0; i < 30; i++) tick(0xFFFF, 0xFFFF);
    press(1, DM_BTN_START);
    tick(0, 0); /* read at the end of a tick, asked for in the next, applied in the one after */
    CHECK(f.vs.pl[0].cnd_static == DM_C_PAUSE && f.vs.pl[1].cnd_static == DM_C_PAUSE, "both paused");
    CHECK(f.vs.pl[1].mode == DM_M_PAUSE_RETRY && f.vs.retry_type[1] == 1, "P2 has the pause menu");
    uint32_t t = f.vs.game_time;
    for (int i = 0; i < 20; i++) tick(0, 0);
    CHECK(f.vs.game_time == t, "clock stopped while paused");
    press(1, DM_BTN_A); /* CONTINUE */
    tick(0, 0);
    CHECK(f.vs.pl[0].cnd_static == DM_C_WAIT && f.vs.pl[1].cnd_static == DM_C_WAIT, "unpaused");

    CHECK(run_until(round_decided, 20000), "round 2 decided");
    CHECK(dm_vs_round_state(&f.vs) == 2 && f.vs.win_count[0] == 2, "match decided");
    CHECK(f.match_wins[0] == 1 && f.match_wins[1] == 0, "session record %d:%d", f.match_wins[0], f.match_wins[1]);
    CHECK(f.vs.retry_type[0] == 3 && f.vs.retry_select[0] == 1, "RETRY menu");
    static char before[1 << 16], after[1 << 16];
    dm_vs_dump(&f.vs, before, sizeof before);
    uint32_t decided_time = f.vs.game_time;

    /* REPLAY: the round runs again from the recorded pads and ends the same way */
    press(0, DM_BTN_UP);
    CHECK(f.vs.retry_select[0] == 0, "cursor on REPLAY");
    press(0, DM_BTN_A);
    CHECK(f.replaying, "replaying");
    auto_play = 0; /* live pads are ignored anyway; prove it with noise */
    for (int i = 0; i < 20000 && !round_decided(); i++) tick((uint16_t)(i * 2654435761u), DM_BTN_START);
    CHECK(round_decided() && !f.replaying, "replay decided");
    dm_vs_dump(&f.vs, after, sizeof after);
    CHECK(f.vs.game_time == decided_time && !strcmp(before, after), "replay ends in the same state");
    CHECK(f.match_wins[0] == 1, "replay does not count twice (%d)", f.match_wins[0]);

    /* END: back to the setup screen, settings kept */
    tick(0, 0);
    press(0, DM_BTN_DOWN);
    CHECK(f.vs.retry_select[0] == 2, "cursor on END");
    press(0, DM_BTN_A);
    CHECK(f.screen == DM_FRONT_SETUP && f.wins == 2 && f.panel[1].level == 6, "back to setup");
    tick(DM_BTN_A, 0); /* held from the menu: not a new press */
    CHECK(!f.panel[0].ready, "held button does not ready");

    /* RETRY from the pause menu starts the match over */
    tick(0, 0);
    press(0, DM_BTN_A);
    press(1, DM_BTN_A);
    CHECK(f.screen == DM_FRONT_GAME, "second match");
    auto_play = 1;
    CHECK(run_until(started, 2000), "second match started");
    press(0, DM_BTN_START);
    tick(0, 0);
    press(0, DM_BTN_DOWN);
    press(0, DM_BTN_A);
    CHECK(f.screen == DM_FRONT_GAME && f.vs.pl[0].mode == DM_M_INIT && !f.vs.started, "RETRY restarts");

    /* FLASH and TIME ATTACK run to a decided round with a menu too */
    static const DmMode modes[2] = { DM_MODE_FLASH, DM_MODE_TIME_ATTACK };
    for (int m = 0; m < 2; m++) {
        CHECK(run_until(started, 2000), "mode %d: play before the pause", m);
        press(0, DM_BTN_START);
        tick(0, 0);
        press(0, DM_BTN_DOWN);
        press(0, DM_BTN_DOWN);
        press(0, DM_BTN_A); /* END */
        CHECK(f.screen == DM_FRONT_SETUP, "END from pause (mode %d)", m);
        tick(0, 0);
        f.mode = modes[m];
        press(0, DM_BTN_A);
        press(1, DM_BTN_A);
        CHECK(f.screen == DM_FRONT_GAME && f.vs.cfg.mode == modes[m] && f.vs.cfg.p[0].virus_level == 9, "mode %d", m);
        CHECK(run_until(round_decided, 40000), "mode %d decided", m);
        int menu = 0;
        for (int i = 0; i < 2; i++) menu += f.vs.pl[i].mode == DM_M_WIN_RETRY || f.vs.pl[i].mode == DM_M_LOSE_RETRY;
        CHECK(menu == 1 && f.vs.win_count[0] == 1, "mode %d: one menu, P1 won (%d)", m, f.vs.win_count[0]);
        for (int i = 0; i < 300 && !started(); i++) press(0, DM_BTN_A); /* NEXT */
        CHECK(started(), "mode %d next round", m);
    }

    dm_front_free(&f);
    printf("%s (%d failures)\n", failures ? "FAILED" : "ok", failures);
    return failures != 0;
}
