# Reference traces — plan

Goal: for a recorded input movie (two pads, one entry per 60 Hz tick) and a known seed,
the original game's per-tick logical state, so the M1 engine is checked frame by frame
instead of by eye. Fixtures in this repo are **only hashes and our own input movies** —
never ROM bytes, decomp code or extracted assets.

## Route chosen: native oracle first, emulator second (SPEC part 03 §5)

1. **Oracle (route b)** — the decomp's own game-logic sources (`dm_game_main.c`,
   `calc.c`, `dm_virus_init.c`, `vr_init.c`, the tables in `main1x.c`, the auto-repeat
   in `joy.c`) compiled natively with stubs for graphics, audio (logs SE ids), EEPROM and
   OS calls. It lives OUTSIDE this repo, next to the decomp checkout, and is rebuilt from
   a pinned decomp commit. Each tick replays the real order:
   `dm_game_main2()` → input half (`dm_make_key` + `key_control_main` per player, the
   movie in place of `joyProcCore`) → fake render step (countdown, virus animation).
   Probe: those files already compile on x86_64 with 0 errors; ~212 symbols to stub.
2. **Emulator (route a)** — mupen64plus-core built from source without OpenCV/OSD, a
   tiny frontend with a frame callback reading RDRAM, and an input plugin serving the
   movie. Used for what the oracle cannot know: countdown length, the frame play
   actually starts, input→display latency, and a spot check that the oracle matches
   real code compiled by KMC GCC. RAM addresses are in SPEC part 03 §5.1.

## Formats (draft)

Input movie `tests/movies/<name>.dmi` (text):
```
seed2 <int32> level <p1> <p2> speed <p1> <p2> wins <1|2|3>
<tick> <p1 buttons hex> <p2 buttons hex>      # only lines where something changes
```
Buttons are the N64 bitmask (A, B, Z, START, D-pad, L, R, C). One line per change keeps
movies small and diffable.

Golden trace `tests/fixtures/<name>.trace`:
```
<tick> <fnv1a-64 of P1 logical state> <fnv1a-64 of P2> <SE ids fired this tick>
```
Logical state = per cell (occupied, colour, capsule half / virus, erasing), falling
capsule, next capsule, state-machine mode, chain fields, speed counters, pending
garbage, virus count, RNG index. Excluded (graphics-thread driven): virus animation
frame, countdown counter. The M1 test runner computes the same hash from the engine and
reports the first diverging tick with a readable dump of both boards.

## Corpus to record (M1 entry gate)

- seeds × levels: L0/L10/L20 each speed, same level both sides (P2 copies P1's bottle)
  and different levels;
- DAS: tap, hold 11/12/18/24 frames, charge during lock and clear;
- rotation at both walls, at the top row, blocked kicks, A+B same frame;
- soft drop at each speed, lock at the floor and on a stack;
- 2-, 3-, 4-line chains → garbage, both players attacking on the same tick, a second
  attack while one is pending;
- top-out, clear-all, simultaneous clear/top-out (draw rules), full best-of-3 match;
- pause by either player.

## Status

M0: plan only. Built and verified the decomp; RNG oracle (calc.c functions compiled
natively) produced `tests/fixtures/rng.txt`, which `test_core` checks.
