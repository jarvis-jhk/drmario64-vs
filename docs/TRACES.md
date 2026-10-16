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

- RNG oracle (calc.c compiled natively) → `tests/fixtures/rng.txt`.
- **Round-setup oracle** (`tools/oracle/`, M0 done 2026-10-16): `build.sh <decomp>`
  compiles the decomp's own `dm_virus_init.c` and `calc.c` plus `dm_make_magazine` cut
  verbatim from `dm_game_main.c`, natively on x86_64, and replays `dm_game_init`'s RNG
  order (seed → magazine → P1 virus/flash → P2 virus/flash → equal-level copy). It prints
  `tests/fixtures/setup.txt`: 280 rounds (NORMAL levels 0–20, FLASH and TIME ATTACK
  game levels 0–2, equal and different levels, 12 crowded level-20 seeds that restart
  the layout), each with the magazine hash, per-player colour counts + hash of bottle
  and reveal order, the FLASH targets and the next `irandom()` after setup. `test_core`
  checks `src/core/setup.c` against it and requires the scan and restart paths to stay
  covered. Generate: `tools/oracle/build.sh <decomp> > tests/fixtures/setup.txt`.
  ⚠️ ultralib's `s32`/`u32` are `long`; the script pins them to `int` with a shim
  header so the 32-bit wraparound matches the N64.
- **Per-tick trace oracle** (M1, 2026-10-16): `tools/oracle/build_trace.sh <decomp> tests/movies`
  compiles the decomp's whole `dm_game_main.c` (into `glue.c`, which reaches its private
  `watchGame`), `joy.c`, `main1x.c`, `calc.c`, `dm_virus_init.c`, plus `stubs.c` for the
  ~170 graphics/audio/OS/CPU symbols. Each tick runs the real frame order:
  `dm_game_main2()` → `dm_game_graphic_onDoneSawp()` (joyProcCore, dm_make_key,
  key_control_main) → the countdown step of `dm_game_graphic_effect`. Two scripted
  players (our code) produce the pads from the oracle's own state; one process per movie,
  so movies do not depend on each other.
  Format actually used (`.dmm`): `cfg …`, `i <tick> <p1> <p2>` (pads from that tick),
  `b <tick> <p> x,y,c …` (junk pills set after the tick), `n <tick>` (next round started
  after the tick), `h <tick> <fnv64>` every 60 ticks = rolling hash of every tick's full
  text state dump, `end <ticks> <hash> wins a b`. The dump (glue.c `dump_state`, engine
  `dm_vs_dump`) holds every logic field of both players, both bottles cell by cell, the
  capsule and next capsule, garbage queues, FLASH targets, the TIME ATTACK result
  counters, pad/auto-repeat state and the sound effects of the tick. Masked: a live
  virus's animation frame (graphics thread).
  `tests/test_vs.c` replays all movies through `src/core/vs.c`; `DM_DUMP=a:b` prints the
  engine's dumps, `build_trace.sh <decomp> <dir> <movie> a b` the original's, for a diff.
- Corpus (21 movies, ~280k ticks): NORMAL/FLASH/TIME ATTACK, levels 0–20, all speeds,
  1–3 wins, aimed play, taps, held DAS, mashing, soft drop on/off, pauses by both
  players, mirrored players (equal bottles → draws), junk-filled bottles for 2/3/4-line
  chains and attacks while garbage is pending, top-outs, TA time-outs and results.
- Two console details the oracle settles explicitly: the countdown length is asset data
  (`COUNTDOWN_FRAMES`/`DM_COUNTDOWN_FRAMES` = 164 display frames, UNVERIFIED, emulator
  item), and after a decided match the EEPROM write skips the pad read for its duration;
  both sides model that as exactly the one frame the match is decided in.
