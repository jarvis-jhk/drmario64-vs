# Dr. Mario 64 — 2P VS behaviour spec

What the remake has to reproduce, read from the matching decomp
[AngheloAlf/drmario64](https://github.com/AngheloAlf/drmario64) (MIT, US version,
commit b5526094), which we built locally and which produced a ROM byte-identical to
the original (`build/us/drmario64.us.z64: OK`). Function names are the decomp's;
`file:line` refers to `src/main_segment/` of that commit unless stated otherwise.
Pseudocode is our own wording. Everything marked **UNVERIFIED** is inferred, not
read, and must be confirmed by a reference trace (docs/TRACES.md) before M1 relies on it.

Scope: human vs human only. CPU players, story, 4P, music and backgrounds are out.

Parts:
1. Randomness, virus placement, pill sequence, match setup
2. Per-frame loop, input and timing
3. VS rules (garbage, win/lose), layout, sound effects, trace feasibility


---

---

## 01 — Randomness, virus placement, capsule sequence, 2P VS match setup

Source: AngheloAlf/drmario64 decomp (US), checked out at
`<decomp>/`. All paths below are relative to that
checkout's `src/main_segment/` unless stated otherwise. Pseudocode is our own
wording; function names are the original names recovered by the decomp.

Conventions used here:
- Board is 8 columns x 16 playable rows. Columns `x = 0..7` (left to right).
  Virus/row coordinate `y = 1..16`, **y = 1 is the top playable row, y = 16 the
  bottom** (`init_map_all`, dm_virus_init.c:13 sets `pos_m_y = row + 1`). Cell
  index into the 128-entry virus map is `idx = (y - 1) * 8 + x`
  (`GAME_MAP_GET_INDEX_ALT2`, include/gamemap.h).
- Colour index `0, 1, 2` = red, yellow, blue (names from
  `get_virus_color_count`, dm_virus_init.c:110). Same indices are used for capsule
  halves.
- "s32 wrap" means two's-complement 32-bit arithmetic with wraparound (MIPS
  `addu/subu/mult` low word). A port must use `int32_t` via `uint32_t` casts.

---

### 1. Random number generators

There are **three** generators. Only one of them (the calc.c "random" table) affects
gameplay in human-vs-human 2P.

#### 1.1 Gameplay RNG: subtractive lagged-Fibonacci table (calc.c)

State: `static s32 randomtable[55]; static s32 randomindex;` (calc.c:11-12).
Values are nominally 16-bit, stored in s32.

`randomize00()` (calc.c:705) — refill, in place, s32 wrap:
```
for i in 0..23:   t[i] = t[i] - t[i+31]; if t[i] < 0: t[i] += 0x10000
for i in 24..54:  t[i] = t[i] - t[i-24]; if t[i] < 0: t[i] += 0x10000
```
(second loop uses the already-updated low entries).

`randomseed(s32 seed)` (calc.c:735):
```
t[54] = seed
value = 1
for i in 1..54:
    idx = (21*i mod 55) - 1          # covers 0..53 exactly once
    t[idx] = value
    value = seed - value             # s32 wrap
    if value < 0: value += 0x10000
    seed = t[idx]
randomize00() x4
randomindex = 0
```
Note: `seed` is **not** masked to 16 bits; for seeds outside 0..0xFFFF the table
holds values outside 16-bit range until they are mixed. Port it with exact s32
semantics (do not "simplify" to u16), otherwise sequences from emulator traces will
not match.

`irandom()` (calc.c:760) — pre-increment:
```
randomindex += 1
if randomindex >= 55: randomize00(); randomindex = 0
return t[randomindex] & 0xFFFF
```
So the first draw after seeding returns `t[1]`; `t[0]` is only returned right after
a refill.

`random(n)` (calc.c:773): `return (s32)(n * irandom()) >> 16` (s32 multiply,
arithmetic shift). For every call site except `random(0xFFFF)` the product fits in
31 bits, so it is simply `floor(n * r / 65536)` in `0..n-1`. For `random(0xFFFF)`
the product can overflow to negative; only its low 8 bits are ever used (see 3.1),
and those equal `(r - 1) & 0xFF` for `r >= 1`, and `0` for `r == 0`.

Before the first `randomseed` the table is BSS-zero, so `irandom()` returns 0.

#### 1.2 Mersenne Twister (vr_init.c) — not gameplay-relevant in human 2P

`mt[624]`, `mti` initialised to 625 (vr_init.c:12-17).
- `sgenrand(seed)` (vr_init.c:30): old 1998 MT init, `mt[0] = seed;
  mt[i] = mt[i-1] * 69069` (u32 wrap).
- `genrand(limit)` (vr_init.c:41): standard MT19937 generate (N=624, M=397,
  matrix 0x9908B0DF, standard tempering 11 / 7&0x9D2C5680 / 15&0xEFC60000 / 18);
  if never seeded it self-seeds with `0x1105`. Result is **rejection-sampled**:
  `mask = 2^bitlen(limit) - 1`; repeat `x = tempered & mask` until `x < limit`.
- Seeded once at the title screen with `osGetCount()` (dm_title_main.c:90) and
  again every round in `dm_game_init` (see 1.4).
- Consumers: CPU AI (`aiset.c`: 2520, 2533, 2535, 2822, 3459, 3464, 3522), demo
  character choice (main1x.c:613, 645) and the per-round seed formula. In
  GSL_2PLAY with two humans **nothing in the game logic draws from MT**; it only
  feeds the next round's seed. A remake can omit it.

#### 1.3 libnustd `rand()` — cosmetic only

`rand_data = rand_data * 20077 + 12345; return (rand_data >> 16) % (RAND_MAX+1)`
(src/libnustd/srand.c:15). Used only for attack-sprite jitter (game_etc.c:1126-1128),
character animation branching (char_anime.c:135), title/demo music choice. No effect
on board state. A remake may use any RNG for these.

#### 1.4 Seeding for a 2P VS round

`dm_game_init(bool reinit)` (dm_game_main.c:7032), lines 7040-7047:
```
if (!reinit || !st->replayFlag):
    st->replayFlag = 0
    st->randSeed  = (s32)((genrand(0xFFFF) + osGetTime()) * 0x10001)   # truncated to 32 bits
    st->randSeed2 = (s32)(irandom() + osGetTime())                     # truncated to 32 bits
sgenrand(st->randSeed)
randomseed(st->randSeed2)
```
- `osGetTime()` is the 64-bit CPU-counter time, so in practice the seed is
  unpredictable. A remake should pick any 32-bit `randSeed2` (e.g. from the OS
  clock) and **must store it** so a round can be replayed / golden-tested.
- `dm_game_init` is called for **every round** (dm_game_main.c:8077-8167, GSL_2PLAY
  branch at ~8110): `dm_game_init(false)` on first entry (7874) and on "retry",
  `dm_game_init(true)` for the next round of a match. Because `replayFlag` is 0 in
  normal play, **each round gets a fresh seed**, fresh capsule magazine and fresh
  virus layout.
- The in-game instant replay (`set_replay_state` dm_game_main.c:2197,
  `dm_ret_replay`) re-enters `dm_game_init(true)` with `replayFlag` set, re-using the
  same `randSeed/randSeed2`: this proves a round is a pure function of
  (`randSeed2`, per-frame inputs) for two humans. Good basis for golden tests.

#### 1.5 Complete list of gameplay RNG (`random`/`irandom`) consumers in 2P VS

In call order within a round:

| # | When | Function (file:line) | Draws |
|---|---|---|---|
| 1 | `dm_game_init`, once | `dm_make_magazine` (dm_game_main.c:777) | `random(0xFFFF)` for magazine[0], then one per candidate until 255 accepted (rejections consume draws) |
| 2 | `dm_game_init`, player 0 | `_dm_virus_init` via `dm_virus_init` (dm_virus_init.c:339/500) | see section 2 |
| 3 | FLASH mode only, player 0 | `make_flash_virus_pos` (dm_virus_init.c:628) | exactly one `random(3)` (line 653) |
| 4 | player 1 | `_dm_virus_init` | as #2 (draws are consumed even if the layout is later overwritten by the copy) |
| 5 | FLASH only, player 1 | `make_flash_virus_pos` | one `random(3)` |
| 6 | in game, whenever a player's capsule settles and that player's chain had >= 2 lines (`dm_mode_cap_set` in `dm_game_main_cnt`, dm_game_main.c:3803-3815; not in TIME ATTACK) | `dm_set_attack_2p` (2405) -> `dm_make_attack_pattern` (2389) | 0 or 1 pattern draw (`random(2)` or `random(4)`), then per garbage column a loop of `random(3)` |

Nothing else in a human 2P round calls `random`/`irandom`. (`set_bottom_up_virus`
dm_game_main.c:2831 uses `random` but only runs in 1P score-attack "TaiQ";
`dm_set_attack_4p` is 4P only; dm_manual_main.c is the tutorial.)

Per-frame order: `dm_game_main_2p` (dm_game_main.c:4671) calls
`dm_game_main_cnt(state[i], map[i], i)` for `i = 0` then `i = 1` each logic tick
(line 4702), so when both players trigger an attack on the same tick, **player 1P
(index 0) consumes RNG first**. Both players share the single RNG.

Garbage RNG details (exact draw order, needed for reproduction; the garbage rules
themselves belong to the garbage/attack spec part):
```
dm_set_attack_2p(state):
    if state.chain_line < 2: return 0                 # no draws
    enemy = player[state.player_no ^ 1]
    n = min(4, state.chain_line)
    if enemy.cap_attack_work[0].unk_0 != 0:           # enemy already has a pending row
        occupied = set of slots s (0..7) with 2-bit field s nonzero
        par = parity of the highest occupied slot
        pattern = occupied XOR {all slots with parity par}   # = free slots of that parity
        if pattern empty: return 1                    # no draws
    else:
        pattern = dm_make_attack_pattern(n):
            n>=4: 0x55 << random(2)     # slots {0,2,4,6} or {1,3,5,7}
            n==3: 0x15 << random(4)     # 3 slots, stride 2, shift 0..3
            n==2: 0x11 << random(4)     # 2 slots, 4 apart, shift 0..3
    enemy.cap_attack_work[0].unk_2 = state.player_no
    for s in 0..7 (ascending) where pattern bit s set:
        while chain_color[0] or chain_color[1] or chain_color[2] nonzero:
            c = random(3)
            if chain_color[c] != 0:
                chain_color[c] -= 1
                enemy.cap_attack_work[0].unk_0 |= (c+1) << (2*s)
                break
    return 1
```
`chain_color[c]` = number of cleared lines of colour c during the current chain
(`dm_h_erase_chack_set` 1089 / `dm_w_erase_chack_set` 1228). When the garbage is
dropped (`dm_broken_set` 1391), slot `s` lands in **column `7 - s`**, row y=1.

---

### 2. Virus generation

Entry: `dm_game_init` -> for each player `i < evs_playcnt`:
`dm_virus_init(evs_gamemode, &game_state_data[i], virus_map_data[i],
virus_map_disp_order[i])` (dm_game_main.c:7254-7261), which calls
`_dm_virus_init(..., special = 0)` (dm_virus_init.c:500). Output: a 128-cell map
`{virus_type (-1 = empty), x_pos, y_pos}` and `order[96]`, the list of cell indices
in generation order (used for the pop-in animation, `dm_set_virus`
dm_game_main.c:928, one virus per call).

#### 2.1 Tables

`limit_table` (dm_virus_init.c:103), 25 entries:
`6 x15 (idx 0-14), 5,5 (15-16), 4,4 (17-18), 3 x6 (19-24)`.
Lookup index = `level < 24 ? level : 23`.
Viruses may occupy rows `y = limit_line+1 .. 16`.

Virus count (`dm_get_first_virus_count`, dm_virus_init.c:295), mode NORMAL:
`count = (min(23, virus_level) + 1) * 4`.

| virus_level | count | limit_line | rows allowed | free rows on top |
|---|---|---|---|---|
| 0 | 4 | 6 | 7-16 (10 rows) | 6 |
| 1 | 8 | 6 | 7-16 | 6 |
| n (0-14) | 4(n+1) | 6 | 7-16 | 6 |
| 14 | 60 | 6 | 7-16 | 6 |
| 15 | 64 | 5 | 6-16 (11 rows) | 5 |
| 16 | 68 | 5 | 6-16 | 5 |
| 17 | 72 | 4 | 5-16 (12 rows) | 4 |
| 18 | 76 | 4 | 5-16 | 4 |
| 19 | 80 | 3 | 4-16 (13 rows) | 3 |
| 20 | 84 | 3 | 4-16 | 3 |
| (21-23, not selectable in VS) | 88/92/96 | 3 | 4-16 | 3 |

VS menu allows levels 0..20 only (section 4).

FLASH / TIME ATTACK modes use `game_level` (0..2) instead:
count = `_n_343[game_level]` = **30 / 40 / 50** (dm_virus_init.c:316);
limit index = `_l_359[game_level]` = 6 / 9 / 12 -> limit_line = 6 for all three
(rows 7..16). (`state->virus_level` is set to 6/9/12 from `_timeAttack_levelTable`
in main_menu.c:646 but the count comes from `_n_343`.)

#### 2.2 Placement rules (predicates)

`occupied(x,y)` = `dm_virus_check` (dm_virus_init.c:189): map cell has a virus.

`ok1(x,y,c)` = `dm_check_color` (dm_virus_init.c:198). Returns false (reject) if any
of these hold (only previously placed viruses exist in the map):
1. `x >= 2` and cells (x-1,y), (x-2,y) both colour c
2. `1 <= x <= 6` and cells (x-1,y), (x+1,y) both colour c
3. `x < 6` and cells (x+1,y), (x+2,y) both colour c
4. `y < 15` and cells (x,y+1), (x,y+2) both colour c
5. `3 < y < 16` and cells (x,y+1), (x,y-1) both colour c
6. `y > 5` and cells (x,y-1), (x,y-2) both colour c

`ok2(x,y,c)` = `dm_check_color_2` (dm_virus_init.c:244). Reject if:
1. `x >= 2` and cell (x-2,y) is colour c
2. `x < 6` and cell (x+2,y) is colour c
3. `y >= 6` and cell (x,y-2) is colour c
4. `y < 15` and cell (x,y+2) is colour c

(`ok2` subsumes most of `ok1`, but both are evaluated in this order; only the
result matters, neither consumes RNG.) Net effect: no same colour at distance 2
horizontally/vertically and never 3 in a row.

#### 2.3 Algorithm (`_dm_virus_init`, dm_virus_init.c:339-497)

```
RESTART:                                         # label loop_1, line 362
  clear map (all virus_type = -1), order[*] = 0xFF
  limit = limit_line (2.1); count = virus count (2.1)
  left[0..2] = count / 3                         # integer division
  # distribute the remainder (count % 3) one by one to a colour that is currently minimal
  k = 0
  while k < count % 3:
      c = random(3)
      if left[c] <= left[(c+1)%3] and left[c] <= left[(c+2)%3]:
          left[c] += 1; k += 1
  for n in 0 .. count-1:
      scans = 0; tries = 0
      repeat:                                     # one "attempt"
          do c = random(3) while left[c] == 0     # colour drawn EVERY attempt
          if tries == 2:                          # 3rd attempt -> deterministic scan
              tries = 0                           # (post-increment: compare then ++)
              scans += 1
              if scans > 2: goto RESTART          # full restart, RNG continues
              found = false
              for x in 0..7:
                  for y in 16 down to limit+1:
                      if not occupied(x,y) and ok1(x,y,c) and ok2(x,y,c):
                          found = true; break both loops
          else:
              tries += 1
              do:
                  x = random(8)
                  do y = random(17) while y < limit+1      # y in 0..16, reject above limit
              while occupied(x,y)                           # new x AND y on collision
              found = ok1(x,y,c) and (ok2(x,y,c))           # ok2 only evaluated if ok1
      until found
      left[c] -= 1
      map[(y-1)*8 + x] = {type=c, x, y}
      order[n] = (y-1)*8 + x
```
Attempt pattern per virus: random, random, scan, random, random, scan, random,
random, (9th attempt: colour drawn, then restart). The restart re-runs the whole
function including the remainder-colour draws, with the RNG continuing from where
it is.

Exact draw order inside one random attempt: colour draws (`random(3)` until
`left[c] != 0`), then `random(8)`, then one or more `random(17)`, repeating
x and y draws if the cell is occupied.

`y_point_set` (dm_virus_init.c:175) is unused.

#### 2.4 Same level => same layout (unlike NES)

After both players are generated (dm_game_main.c:7263-7276):
```
for i in 0 .. playcnt-2:
    for j in i+1 .. playcnt-1:
        if player[i].virus_level == player[j].virus_level:
            copy map + order from i to j (dm_virus_map_copy, dm_virus_init.c:276)
            FLASH: also copy flash_virus_count/pos/bak
            break
```
So in 2P: **if both players choose the same virus level, player 2 gets an exact
copy of player 1's bottle** (and the same reveal order); speed is irrelevant to this
test. Player 2's own generation still runs first and consumes RNG. In FLASH / TIME
ATTACK, `virus_level` is 6/9/12 derived from `game_level`, so the same game level
gives the same layout.

#### 2.5 FLASH mode flashing viruses (`make_flash_virus_pos`, dm_virus_init.c:628)

Only for `evs_gamemode == GMD_FLASH`. Chooses 3 "flash" viruses out of the layout.
```
_makeFlash_checkOrdre (546):
  ymax = max y over viruses with x not in {0,7}; ymax = min(ymax, 14); y3 = ymax - 3
  for each virus in generation order (order[0..count-1]):
      if x in {0,7}: side[c].append(idx)
      else:
          if y >= ymax: under[c]  elif y >= y3: under2[c]  else: under3[c]
          center[c].append(idx)
remaining = 3
c = random(3)                                  # the only RNG draw
quota table _n_564[game_level] (line 505), categories under, under2, under3, center, side:
  level 0: 1,0,2,96,96   level 1: 1,1,1,96,96   level 2: 1,2,0,96,96
for cat in those 5 categories:
  q = min(remaining, quota[cat]); fails = 0
  while fails < 3 and q > 0:
      c = (c+1) % 3
      if decided[c] <= decided[(c+1)%3] and decided[c] <= decided[(c+2)%3]:
          pick first not-yet-selected idx in cat[c] (else none)
      else none
      fails += 1
      if picked: decided[c].append(idx); remaining -= 1; q -= 1; fails = 0
flash list = decided[0] then decided[1] then decided[2]; column = idx & 7, row = idx >> 3
```

---

### 3. Capsule (pill) sequence

#### 3.1 Magazine generation (`dm_make_magazine`, dm_game_main.c:777)

Single global `u8 CapsMagazine[256]` (dm_game_main.c:229), **shared by all
players**, regenerated once per round inside `dm_game_init` (line 7145) — after
seeding, before the per-player init and before virus generation.

```
byte() = random(0xFFFF) mod 256 stored as u8  ==  (r - 1) & 0xFF if r >= 1 else 0,
         where r = irandom()
left(b)  = (b >> 4) % 3        # CAPSMAGAZINE_GET_A, line 771
right(b) = b % 3               # CAPSMAGAZINE_GET_B
mag[0] = byte(); prev = (left(mag[0]), right(mag[0]))
i = 1
while i < 256:
    mag[i] = byte()
    if (left(mag[i]), right(mag[i])) != prev:
        prev = that pair; i += 1
    # else: slot i is overwritten by the next draw
```
Properties:
- No two consecutive magazine entries have the same colour pair (ordered; R-Y and
  Y-R count as different).
- Distribution is **not uniform**. Over the 256 byte values the pair counts are
  (left,right): RR 36, RY 30, RB 30, YR 25, YY 30, YB 25, BR 25, BY 25, BB 30
  (before the no-repeat rejection).

#### 3.2 Dealing (`dm_set_capsel`, dm_game_main.c:831)

Each player has their own index `cap_magazine_cnt` (u8, struct field at
include/dm_game_main.h:217), set to 1 in `dm_game_init` (line ~7210), then
`dm_set_capsel` is called once there (so the first capsule in hand is `mag[1]` and
the preview is `mag[2]`).
```
dm_set_capsel(p):
    p.cap_move_se_flg = false
    p.now_cap  = new capsule colours (left(mag[cnt]), right(mag[cnt])), "go" flag set
    p.cap_magazine_save = cnt
    cnt += 1; if cnt >= 254: cnt = 1
    p.next_cap = (left(mag[cnt]), right(mag[cnt]))
```
- Sequence dealt is `mag[1], mag[2], ..., mag[253], mag[1], ...` (period 253).
  `mag[0]`, `mag[254]`, `mag[255]` are never dealt. Across the wrap (253 -> 1) a
  repeated pair is possible.
- **Both players receive the identical capsule sequence** (same array, separate
  counters), regardless of their level/speed.
- New capsule spawn position (`dm_init_capsel`, line 806): left half x=3, right
  half x=4, both at "row 0" (above the bottle), horizontal; `capsel_p[0]` = left
  colour, `capsel_p[1]` = right colour.
- `cap_magazine_save` is used to re-create the in-hand capsule after the throw
  animation (dm_game_main.c:3263-3265).

---

### 4. 2P VS match setup

#### 4.1 Menu path (main_menu.c)

Main menu -> 2P (`MODE_PLAY2`, main_menu.c:5262-5295). Three choices
(`mode_5557`, line 4961):

| choice | menu mode | `evs_gamemode` |
|---|---|---|
| 0 | MODE_VSMAN_NS | GMD_NORMAL (classic) |
| 1 | MODE_VSMAN_FL_NS | GMD_FLASH |
| 2 | MODE_VSMAN_TA_NS | GMD_TIME_ATTACK |

On confirm: `evs_playcnt = 2`, `evs_story_flg = 0`, `evs_gamesel = GSL_2PLAY`,
`game_state_data[0/1].player_type = PLAYERTYPE_0` (human). (`*_NS` = name select,
then character select `*_CH`, then the level panel `MODE_VSMAN[_FL|_TA]`.)
main1x.c:521-527 re-asserts both players human for GSL_2PLAY.

Human-vs-human only: TIME ATTACK exists only in VSMAN; VS COM (`GSL_VSCPU`) offers
NORMAL and FLASH only and adds CPU strength/character, which are out of scope.

#### 4.2 Per-player level panel (`menuPlay2_init` 7917, `menuPlay2Panel_init` 3634,
`menuPlay2Panel_input` 3799, `menuPlay2_input` 8268)

Each player has an independent panel driven by their own controller:

| item | NORMAL | FLASH / TIME ATTACK | source |
|---|---|---|---|
| row 0 | virus level gauge 0..20, D-pad L/R with key repeat, clamped | game level 0..2 (3-item selector) | `menuLvGauge_init` 1343 (`levelMax = 20`), `menuSpeedItem_input` 1845 (3 labels, `miLabel[3]`) |
| row 1 | speed 0..2 | speed 0..2 | `menuSpeedItem` |

Speed index 0/1/2 = LOW/MED/HI (UNVERIFIED label mapping, inferred from
`GameSpeed[] = {0, 10, 15, 55}` main1x.c:148 indexed by `cap_def_speed`; drop-speed
semantics belong to the gravity spec part).

Defaults come from the per-name saved config (`evs_mem_data[evs_select_name_no[i]].config`),
initialised by `dm_init_config_save` (record.c:147): `vm_lv = 10`, `vm_sp = 1`
(MED), `vm_fl_lv = 1`, `vm_ta_lv = 1`. The chosen values are written back to that
config on confirm (main_menu.c:8343-8358), i.e. the game remembers each player's last
choice.

Navigation: Up/Down (or A / B) moves between the two rows; A/Start on the speed row
marks the player ready (`flow = 1`); B on the level row backs out to character
select. When both players are ready, a shared sub-panel (stage background + music —
irrelevant for us) is confirmed by controller 1. "Master" convenience: while a
player holds **R**, their cursor and value changes are mirrored onto all
not-yet-ready panels, and R+A/Start readies everyone (`menuPlay2_checkInputMaster`
8079, `menuPlay2_applyMasterConfig` 8089, 8280-8290).

There is no other handicap option: handicap = choosing different level/speed per
player.

#### 4.3 Landing in game state (main_menu.c:8402-8418)

For each player i:
```
NORMAL:       game_state_data[i].virus_level = lvGauge.level           (0..20)
FLASH / TA:   game_state_data[i].virus_level = {6,9,12}[glv]; game_state_data[i].game_level = glv (0..2)
all:          game_state_data[i].cap_def_speed = speedItem.select      (0..2)
evs_seqnumb = music choice; evs_game_time = 0; next main = MAIN_12 (main1x) -> MAIN_GAME
```
Struct fields (include/dm_game_main.h): `virus_level` u8 @0x026 (line 205),
`cap_def_speed` u8 @0x02C (211), `cap_magazine_cnt` @0x032 (217),
`cap_magazine_save` @0x033 (218), `chain_line` (225), `chain_color[4]` (227),
`player_no` (233), `player_type` (234), `cap_attack_work[16]` (238),
`flash_virus_*` (241-243), `game_level` s32 @0x16C (245).
Per-player init in `dm_game_init` (dm_game_main.c:7193-7245) sets
`cap_speed = GameSpeed[cap_def_speed]`, `cap_magazine_cnt = 1`, calls
`dm_set_capsel`, zeroes chain/attack state.

#### 4.4 Match length and rounds

- `evs_vs_count` (main1x.c:127, default **3**; reset to 3 in record.c:304) = wins
  needed to take the match. It is a global option, not part of the VS panel:
  Options -> Misc -> "Count" (`MODE_COUNT`, main_menu.c:5671-5675), selector
  `evs_vs_count = 3 - select`, so values **3, 2, 1**. Saved to EEPROM
  (record.c:961).
- `st->win_count[]` reset only by `dm_game_init(false)` (dm_game_main.c:7128-7132).
  After a round, if no player has `win_count == evs_vs_count`, the game calls
  `dm_game_init(true)` -> new seeds, new magazine, new viruses, same
  level/speed/mode (dm_game_main.c:8110-8140). TIME ATTACK additionally zeroes both
  scores between rounds.
- TIME ATTACK 2P: round runs until `evs_game_time >= 10800` ticks (3 minutes at 60
  Hz) (dm_game_main.c:3667, 4750); no garbage is sent (`dm_set_attack_2p` skipped
  when `evs_gamemode == GMD_TIME_ATTACK`, line 3812). Winner rules: UNVERIFIED here
  (belongs to the win/lose spec part).
- `evs_score_flag` (main1x.c:82, default 1; Options -> Misc -> Score) toggles score
  display only (dm_game_main.c:6503) — UNVERIFIED that it has no logic effect.

---

### 5. Remake checklist (determinism)

1. Seed: pick `randSeed2` (s32). Call `randomseed(randSeed2)` at each round start.
2. `dm_make_magazine` (3.1).
3. For each player, set `cap_magazine_cnt = 1` and call `dm_set_capsel` (3.2) — no RNG.
4. `dm_virus_init` player 0, (FLASH: flash pick player 0), player 1, (FLASH: player 1).
5. If levels equal, copy player 0's layout/order (and flash data) to player 1.
6. During play, the only RNG consumer is garbage creation (1.5), player 0 before
   player 1 within a tick.
7. Golden test: record `randSeed2` from the emulator (watchGame->randSeed2, struct at
   dm_game_main.c:133-134) and compare magazine, both virus maps and all garbage
   rows given the same input movie.

UNVERIFIED items: speed label mapping (4.2); exact TIME ATTACK win rule; whether
`dm_mode_training` (re-deal from `cap_magazine_cnt = 1`, dm_game_main.c:3966-3977)
can be reached in GSL_2PLAY human VS.

---

## 02 — Per-frame gameplay loop, input and timing (2P human vs human VS)

Source: AngheloAlf/drmario64 decomp, US build. All paths are relative to
`<decomp>/`. "dgm" means `src/main_segment/dm_game_main.c`.
Line numbers refer to that checkout. Anything not read directly from code is marked
**UNVERIFIED**.

Coordinate conventions used in this section:
- The bottle is 8 columns (x = 0..7) by 16 rows (y = 1..16, with y = 16 at the bottom). `y = 0` is
  the spawn row above the bottle neck. Map storage: `GAME_MAP_ROWS = 17`, `GAME_MAP_COLUMNS = 8`
  (`include/gamemap.h:24-27`). Logical row y is stored at array row `y-1`
  (`get_map_info`/`set_map`/`clear_map`, `src/main_segment/dm_virus_init.c:31-150`). Array row 16
  (logical y = 17) exists but is never written, so it always reads as empty.
- The falling capsule `now_cap` has two halves [0] and [1]. Horizontal: [0] is left, [1] is right.
  Vertical: [0] is bottom, [1] is top (`pos_y[1] = pos_y[0] - 1`).
- Cell graphic codes (`capsel_m_g`): 0 = top half of a vertical pill, 1 = bottom half, 2 = left
  half of a horizontal pill, 3 = right half, 4 = single ball (no partner), 5 = pill cell being
  erased, 0xD = virus being erased. Viruses are marked by `capsel_m_flg[4] >= 0`; capsule cells
  have `capsel_m_flg[4] = -1`. Colour is in `capsel_m_flg[3]` (0..2). `capsel_m_p` is the palette:
  colour, or colour + 3 for "blackened" cells.

---

### 1. Frame rate, main loop and polling

#### 1.1 Logic rate: one tick per 60 Hz retrace
- `mainproc` creates the scheduler with `retraceCount = 1` for NTSC (`main.c:46`). The VI event
  fires on every retrace (`nnsched.c:89`). PAL boots into an error screen (`main.c:55-57`).
- On each `VIDEO_MSG`, `nnScEventHandler` broadcasts `NN_SC_RETRACE_MSG` (= 1) to every client
  and resets the CPU timer with `osSetTime(0)` (`nnsched.c:147-156`).
- `dm_game_main` (dgm:7853) is the game-screen loop. It blocks on its client queue and ignores
  any message that is not retrace (dgm:7898). For each retrace it does the following:
  1. Runs `dm_game_main2()` `evs_gamespeed` times (dgm:7920). `evs_gamespeed = 1`
     (`main1x.c:77`) and only the debug menu changes it, so this is **exactly one logic tick per
     retrace**. `dm_game_main2` dispatches to `dm_game_main_2p()` for `GSL_2PLAY` (dgm:8113).
  2. Calls `dm_audio_update()`.
  3. Calls `dm_game_graphic_onDoneSawp()` (dgm:7939). Despite its name, this runs on the game
     thread and does the input half of the tick (§1.2).
  4. Sets `graphic_no = GRAPHIC_NO_4` so the graphics thread draws at the next retrace (dgm:7947).
- The client queue holds `NN_SC_MAX_MESGS = 8` messages (`include/nnsched.h:16`). If a tick
  overruns a frame, the queued retraces are processed back to back (catch-up), so no logic tick
  is lost unless more than 8 queue up. When a backlog is detected, `D_80088105` is set
  (dgm:~7894).
- There are no per-player update multipliers. Each player gets exactly one
  `dm_game_main_cnt` call and one `key_control_main` call per tick.

#### 1.2 Exact order of work inside one tick (2P)
1. `dm_game_main_2p` (dgm:4671):
   - `dm_set_pause_and_volume(state, 2)` applies any pause or unpause that was requested on the
     previous tick (dgm:4151).
   - `dm_game_main_cnt(P1)`, then `dm_game_main_cnt(P2)` (dgm:4702). This is the state machine of
     §4, without the falling capsule.
   - `dm_warning_h_line_se`, `dm_play_count_down_se`, then start-of-round and win/lose resolution.
2. `dm_game_graphic_onDoneSawp` (dgm:8807):
   - Sleeps until **13 500 µs after the retrace**: `13500 - osGetTime()`, and only if more than
     50 µs remain (dgm:8817-8828). The controllers are therefore read about 13.5 ms into the
     frame, i.e. late, to minimise latency.
   - `joyProcCore()` polls all 4 pads (dgm:8838). If `eep_rom_flg` is set (EEPROM write in
     progress), all input arrays are zeroed instead.
   - `dm_make_key()` builds per-player `joygam[]` (dgm:8849, §2.1).
   - `key_control_main(P1, main_joy[0])`, then `key_control_main(P2, main_joy[1])`
     (dgm:8887-8891). This does rotate, then translate, then soft-drop speed, then **gravity
     (`dm_capsel_down`)** for the falling capsule.
3. Consequences for a port:
   - Within a tick, the board state machine runs **before** the input is polled, and the capsule
     movement and gravity run **after** it.
   - Button edges seen by `dm_game_main_cnt` come from the previous tick's poll. This covers
     START for pause (`dm_query_pause_player`, dgm:3195) and the retry menus.
   - P1 is always processed before P2 in both halves. This matters for garbage (§4.6).

#### 1.3 Input → display latency (**UNVERIFIED**, derived from thread structure)
- The graphics thread has priority `THREAD_PRI_GRAPHIC = 15`, the game thread 10
  (`include/dm_thread.h:17-18`).
- On retrace R, `gfxproc_onRetrace` → `dm_game_graphic` (dgm:8348, `graphic.c:218-221`, only if
  `pendingGFX == 0`) submits the display list built on the previous retrace, then builds a new
  one from the current state (`dm_game_graphic2`).
- Input polled at R + 13.5 ms is therefore drawn into the list built at R+1 and submitted at R+2,
  and appears on screen at about R+2 to R+3.
- Measure this on hardware or an emulator before claiming parity. The remake should not exceed
  it.

#### 1.4 Controller polling and derivation (`joy.c`)
- `joyProcCore` (`joy.c:114`) does the following:
  - Raw read: `osContStartReadData` → blocking receive → `osContGetReadData`.
  - For each port i:
    - `hold[i] = button`
    - `pressed[i] = ~prevHold[i] & button` (rising edge since the last poll, `joy.c:162-165`)
    - `prevHold[i] = hold[i]`
  - Repeat (`joycur`), for each of the 16 bit positions j (mask = 0x8000 >> j) that is enabled in
    `joyflg[i]` (`joy.c:177-189`):
    - If held: `joycnt[i][j]++`. The repeat bit fires when `joycnt == 1`, or when
      `joycnt >= joycur1 && (joycnt - joycur1) % joycur2 == 0`.
    - If not held: `joycnt[i][j] = 0`.
  - The US build has **no** opposite-direction filtering. That block (`joy.c:131-149`) is
    `VERSION_GW` only.
- `key_cntrol_init` (dgm:6784) runs at the start of every game screen (`dm_game_main`, dgm:7862):
  - `joyflg = ANY_BUTTON & ~(Z | START | L | R)`. Repeat counters run for A, B, D-pad and C
    buttons.
  - `joygmf = true`, `joygam = 0`.
  - **`joycur1 = evs_keyrept[0] = 12`, `joycur2 = evs_keyrept[1] = 6`** (`main1x.c:72`). These
    replace joyInit's defaults of 20/4 (`joy.c:106-107`).
- `joyCursorFastSet(mask, port)` (`joy.c:212`) sets the counter for that button to
  `joycur1 + joycur2 - 1 = 17`. If the button is still held at the next poll, the counter becomes
  18, and 18 fires because (18 - 12) % 6 == 0. It then repeats every 6 polls (§2.2).
- Controller to player mapping: `joyResponseCheck` (`joy.c:231`) fills
  `main_joy[0..]` with the port numbers of connected *standard* controllers, in port order. It
  is called at boot, title and menu. **P1 uses `main_joy[0]`, P2 uses `main_joy[1]`**, which is
  normally ports 1 and 2. Player state and `joygam` are indexed by player. `joycnt`,
  `gControllerHoldButtons` and so on are indexed by port.

---

### 2. Input feel

#### 2.1 `dm_make_key` (dgm:6800): what the game sees each tick
For `GSL_2PLAY`, the loop runs for players 0 and 1 when `player_type == PLAYERTYPE_0` (human):
```
joygam[p]  = pressed & (A|B)          // rotation: edge only, no auto-repeat
           | joycur  & (L_JPAD|R_JPAD) // movement: DAS/auto-repeat (§2.2)
           | pressed & (L_TRIG|R_TRIG) // not used by 2P gameplay
if (!(hold & (L_JPAD|R_JPAD)))
    joygam[p] |= hold & D_JPAD          // soft drop: level-triggered, only if neither L nor R is held
```
- **Up does nothing.** There is **no hard drop**.
- C buttons are not part of `joygam`. In `key_control_main`, any C-button *press* toggles that
  player's fall-point guide (ghost) `visible_fall_point[p]`. The US default is on (dgm:269-273,
  6844-6848).

#### 2.2 DAS / auto-repeat (left/right)
- A new press fires on the poll where it is first seen (counter = 1). The next fires come at
  counter 12, then 18, 24, 30, and so on. So the moves land on hold-frames
  **1, 12, 18, 24, …**: an **11-frame** delay after the first move, then a move **every 6
  frames**.
- **"Charged" DAS (`joyCursorFastSet`, counter set to 17):**
  - (a) In every tick where the player is *not* in `dm_mode_down` with `cnd_static == dm_cnd_wait`
    (lock, erase, cascade, garbage, spawn bookkeeping), `key_control_main` calls
    `joyCursorFastSet(R)` and `joyCursorFastSet(L)` unconditionally (dgm:6922-6925). In
    `dm_mode_down` with `cnd_static != wait` it does so only for held directions (dgm:6900-6907).
  - (b) Whenever `translate_capsel` is **blocked** (wall or cell), it charges the direction that
    was attempted (dgm:694-699).
  - Effect: if the direction is held, the counter goes 17 → 18 at the next poll, so it fires
    *every* 6 frames from there. In case (b), the blocked move is retried **on the very next
    frame** and then every 6 frames while held.
  - If the button is released, the counter resets to 0 at the next poll. Charging never creates a
    move by itself.
- Phase while holding through a spawn:
  - Let S be the tick in which the new capsule is created (`dm_mode_cap_set`). At S the counter
    reaches 18 and fires, but `translate_capsel` returns early because `pos_y[0] <= 0`. Early
    return does not re-charge.
  - Fires continue at S+6, S+12, …, S+30. The capsule enters y = 1 at the end of S+29 (§3.4), so
    **a held direction moves the new capsule on S+30, the first tick it is movable**, and then
    every 6 ticks.
- Down is **not** repeat-based. It is the held level (§2.6).

#### 2.3 Order of actions inside `key_control_main` (dgm:6838) for the falling capsule
Only when `mode_now == dm_mode_down && cnd_static == dm_cnd_wait`:
1. Rotation. `B` → `rotate_capsel(-1)`, **else** `A` → `rotate_capsel(+1)`. B wins if both are
   pressed on the same tick.
2. Translation. `L` → `translate_capsel(-1)`, **else** `R` → `translate_capsel(+1)`. Left wins.
3. `cap_speed_vec = 1`. If D is in `joygam` and `pos_y[0] > 0`:
   `cap_speed_vec = ceil(FallSpeed[cap_speed] / 2)`, computed as `(v >> 1) + (v & 1)`.
4. `dm_capsel_down` (gravity / lock, §3).

`dm_capsel_down` is also called (with the previous `cap_speed_vec`) in `dm_mode_down` when
`cnd_static != wait`. In 2P, `cnd_static` is normally `wait` during play.

Rotation and translation both early-return if `pos_y[0] <= 0` (capsule still in the spawn row) or
`capsel_flg_0 == 0` (capsule already locked).

#### 2.4 `translate_capsel` (dgm:647)
"Occupied" below means `get_map_info(...) == 1`, i.e. any cell present (virus or pill).

- **Right (+1):**
  - Horizontal: allowed if `pos_x[1] < 7` and cell (`pos_x[1]+1`, `pos_y[0]`) is free.
  - Vertical: allowed if `pos_x < 7` and (x+1, `pos_y[0]`) is free, and either `pos_y[1] == 0`
    (top half is in the spawn row, unchecked) or (x+1, `pos_y[1]`) is free.
- **Left (−1):** mirror image, using `pos_x[0] > 0` and cell `pos_x[0]-1`.
- On success:
  - SE `SND_INDEX_65` plays and `cap_move_se_flg` is cleared.
  - Both x values change by ±1.
  - Gravity counter, lock and fall timing are **not** affected (no lock reset).
- On failure:
  - SE 65 plays once, guarded by `cap_move_se_flg`. The flag resets after a successful move or a
    new capsule (`dm_set_capsel`).
  - The direction is charged (§2.2).

#### 2.5 `rotate_capsel` (dgm:707). A = +1 = clockwise, B = −1 = counter-clockwise

**Vertical → horizontal.** The capsule is at column x, with bottom [0] at y0 and top [1] at y0−1.
- If x == 7 or (x+1, y0) is occupied, try a **kick left**: if x != 0 and (x−1, y0) is free, set
  `pos_x[0] = x−1` and `pos_y[1] = y0`. [0] moves to x−1 and [1] lands at (x, y0). Otherwise the
  rotation fails.
- Otherwise: `pos_x[1] = x+1`, `pos_y[1] = y0`, so [1] goes to the right of [0].
- Only the bottom row y0 is checked. The horizontal result always lies on the old bottom row.
- Colours: for **B (−1)**, the two halves' colours are swapped. For A they are not.
  - Result for A: old top colour ends up on the right, old bottom on the left. That is a
    clockwise rotation.
  - Result for B: old top on the left, old bottom on the right.

**Horizontal → vertical.** [0] is at (x, y0) and [1] is at (x+1, y0).
- If y0 == 1 (top bottle row), always succeed: [1] moves to (x, 0), in the spawn row. This is an
  unchecked special case. `get_map_info` with row 0 would index out of bounds.
- Else if (x, y0−1) is occupied:
  - If (x+1, y0−1) is free, **kick right**: `pos_x[0] = x+1`, giving a vertical pill in column x+1
    (bottom (x+1, y0), top (x+1, y0−1)).
  - Otherwise the rotation fails.
- Otherwise: [1] moves to (x, y0−1), giving a vertical pill in the left column.
- Colours: for **A (+1)** the colours are swapped, so the old left colour ends on top (clockwise).
  For B the old left colour stays on the bottom.

On any success:
- SE `SND_INDEX_67` plays.
- The sprite codes `casel_g[i]` are remapped with `rotate_table_474 = {1,3,4,2}` and
  `rotate_mtx_475 = {2,0,3,1,2,0}`: `g' = mtx[table[g] + vec]` (dgm:266-267, 761-767). The result
  is always top = 0 / bottom = 1 / left = 2 / right = 3.
- The gravity counter is untouched.

Rotation and translation in the same tick are allowed. Rotation is applied first.

No other kick exists:
- A vertical capsule at column 0 with a blocked right side cannot rotate.
- A horizontal capsule does not kick upward or downward.

#### 2.6 Soft drop
- Down held (and neither L nor R held) with `pos_y[0] > 0`:
  `cap_speed_vec = ceil(FallSpeed[cap_speed] / 2)` per tick, instead of 1. The capsule falls when
  the accumulated counter reaches `cap_speed_max` (§3.2).
- Rows per tick depend on `cap_speed_max`:
  - In the common case (no bonus, not resting on anything) it takes **2 ticks per row**, because
    `2*ceil(f/2) >= f`.
  - When `cap_speed_max` exceeds `2*vec` (bonus rows or touch-down wait), it takes 3 ticks.
  - At `FallSpeed = 1` it takes 1 tick per row (vec = 1).
- There is **no special lock delay for soft drop**. A capsule resting on something locks at the
  next counter overflow, typically 2 or 3 ticks with down held.
- The counter is **not** reset when down is pressed or released. Partial progress carries over.

#### 2.7 Buttons during lock / clear / cascade / garbage / spawn phases
- A/B presses outside `dm_mode_down && cnd_wait`, or while `pos_y[0] == 0`, are **discarded**.
  They are edge-only and there is no buffer.
- L/R held are "charged" (§2.2). D held takes effect as soon as the capsule reaches y >= 1, with
  no re-press needed.
- START: `dm_query_pause_player` (dgm:3195) records the request in `dm_game_main_cnt`. It is
  honoured only while `cnd_static` is `wait` or `pause` and `cnd_now != init`. It is applied at
  the start of the *next* tick by `dm_set_pause_and_volume` (dgm:4151).
- Pause has these effects:
  - Both players' `mode_now` and `cnd_now` are saved and replaced by pause modes
    (`dm_set_pause_on`, dgm:4098). The pausing player gets the retry menu, the other gets the
    pause logo.
  - Gravity stops. `key_control_main` takes its else-branch, which charges L/R.
  - Unpausing restores the saved modes (dgm:4139). The gravity counter is preserved.

---

### 3. Gravity, speed and spawn

#### 3.1 Tables (`src/main_segment/main1x.c`)
- `GameSpeed[4] = { 0, 10, 15, 55 }` (`main1x.c:144`). The starting `cap_speed` is
  `GameSpeed[cap_def_speed]`, where `cap_def_speed` comes from the menu speed selector:
  - 0 = LOW, 1 = MED, 2 = HI.
  - The order of the menu labels is **UNVERIFIED** but consistent with the table.
  - `GameSpeed[3] = 55` is the cap.
- `FallSpeed[56]` (`main1x.c:153`): ticks per row, indexed by `cap_speed`:

| idx | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|---|
| 0–7   | 39 | 37 | 35 | 33 | 31 | 29 | 27 | 25 |
| 8–15  | 23 | 21 | 19 | 18 | 17 | 16 | 15 | 14 |
| 16–23 | 13 | 12 | 11 | 10 | 9 | 9 | 8 | 8 |
| 24–31 | 7 | 7 | 6 | 6 | 5 | 5 | 5 | 5 |
| 32–39 | 5 | 5 | 5 | 5 | 5 | 5 | 5 | 5 |
| 40–47 | 4 | 4 | 4 | 4 | 4 | 3 | 3 | 3 |
| 48–55 | 3 | 3 | 2 | 2 | 2 | 2 | 2 | 1 |

- `BonusWait[3][3]` (`main1x.c:135`), indexed `[pos_y[0]-1][cap_def_speed]`. These are extra
  ticks while the capsule is in the top three rows:

| row y | LOW | MED | HI |
|---|---|---|---|
| 1 | 2 | 2 | 3 |
| 2 | 1 | 1 | 1 |
| 3 | 1 | 0 | 1 |

- `touch_down_wait = 2`: a constant set in `dm_game_init_static` (dgm:7336) and never changed.
- `FlyingCnt = {40, 20, 20}` is the 1P throw animation only (`dm_mode_throw`). It is **not used
  in 2P VS**.

#### 3.2 `dm_capsel_down` (dgm:2301): fall counter, called once per tick from `key_control_main`
```
if pos_y[0] > 0:
    max = FallSpeed[cap_speed]
    if 1 <= pos_y[0] <= 3: max += BonusWait[pos_y[0]-1][cap_def_speed]
    if cell (pos_x[0], pos_y[0]+1) occupied: max += touch_down_wait (2)
else:
    max = 30                                  // spawn row
cap_speed_max = max                           // recomputed every tick
cap_speed_count += cap_speed_vec
if cap_speed_count < max: return
cap_speed_count = 0
if !capsel_flg_0: return
// try to step down
if pos_y[0] > 0:
    vertical:   blocked if (x, pos_y[0]+1) occupied
    horizontal: blocked if either (pos_x[i], pos_y[i]+1) occupied
if any pos_y[i] == 16: blocked
if not blocked:
    pos_y[0..1] += 1 (capped at 16)
    if either new cell is occupied -> cnd_static = dm_cnd_game_over, next_cap hidden, lock
    else return
LOCK: SE 66; mode_now = dm_mode_down_wait; capsel_flg_0 = 0;
      write both halves with pos_y != 0 into the map
      (palette + 3 if flg_game_over, i.e. blackened)
```
Rules a port must reproduce exactly:
- **The touch-down wait only looks under half [0].** For a horizontal capsule that is the left
  half. A horizontal capsule supported only under its right half gets no +2.
- **On the floor (y = 16) there is no +2**, because cell y = 17 is the always-empty sentinel row.
- **Lock = the next counter overflow while blocked.** After the tick in which the capsule steps
  into its resting row, it stays movable for one more full interval. During that interval
  `max` includes +2 if it is resting on something under [0]. If it is slid off the ledge in that
  time, it simply falls at the next overflow.
- Moving or rotating never resets the counter. There is no infinite stalling beyond normal
  gravity.
- `max` is re-evaluated every tick, so sliding onto or off a support changes the remaining wait
  immediately.
- A half with `pos_y == 0` (top of a vertical pill rotated at y = 1) is **not written** to the map
  on lock. It simply vanishes.

#### 3.3 Speed-up (`dm_capsel_speed_up`, dgm:859)
- It is called after every `dm_set_capsel` in `dm_mode_cap_set`, but not for the round's first
  capsule, which is spawned in `dm_game_init`.
- `cap_count++`. When it reaches 10: SE `SND_INDEX_58`, `cap_count = 0`,
  `cap_speed = min(cap_speed + 1, 55)`.
- Result: capsules 1 to 10 of a round fall at the start speed. The speed rises by one table step
  **as capsule 11 spawns** (it applies to capsule 11 itself), then at capsules 21, 31, and so on.
- `cap_count` and `cap_speed` are re-initialised every round by `dm_game_init` (dgm:7207-7211).
- (`GMD_TaiQ` clamps the speed with `_speed_561 = {15,20,25}`. That mode is not VS.)

#### 3.4 Spawn, entry and game over
- `dm_set_capsel` (dgm:831) → `dm_init_capsel_go`:
  - Position: [0] at (3, 0), [1] at (4, 0), horizontal, `casel_g = {2,3}`.
  - `capsel_flg_0 = 1` (active), `capsel_flg_1 = 1` (falling).
  - Colours come from `CapsMagazine[cap_magazine_cnt]`: left = `(m >> 4) % 3`, right = `m % 3`.
  - Then `cap_magazine_cnt++`, wrapping from 254 to 1. The next capsule is previewed from the new
    index.
  - Both players share the single `CapsMagazine[256]`, each with its own index starting at 1
    (dgm:7206-7213). RNG and magazine generation are covered in a separate section.
- There is **no throw animation in VS**. The capsule appears in the spawn row (y = 0) and
  `dm_capsel_down` uses `max = 30` there.
  - Down, rotate and move are ignored while `pos_y[0] == 0`.
  - `cap_speed_count` is always 0 at spawn. It is reset by the lock overflow, and
    `dm_mode_erase_chack` also resets it.
  - Let S be the cap_set tick. `key_control_main` runs in the same tick S and counts 1..30 over
    ticks S..S+29. **At the end of tick S+29 the capsule steps to y = 1**, and the player has
    control from tick S+30 (rotation and translation are evaluated before gravity within a tick).
  - The round's first capsule: S is the tick where `dm_game_main_2p` switches both players to
    `dm_mode_down` (dgm:4709-4724).
- **Game over** (`dm_check_game_over`, dgm:882, just tests `cnd_static == dm_cnd_game_over`):
  - The flag is set by `dm_capsel_down` when the capsule's step from y = 0 to y = 1 lands on an
    occupied cell. This can only happen on that first step, because every later step is checked
    for collision before moving. In practice: **game over iff cell (3,1) or (4,1) is occupied
    when the capsule enters the bottle**, 30 ticks after spawn.
  - The capsule locks over the occupied cells (overwriting them).
  - On the next tick, `dm_mode_down_wait` returns `dm_ret_game_over`.

---

### 4. Settling, clears and the per-player state machine

#### 4.1 `dm_game_main_cnt` (dgm:3649): per tick, before the switch
- `dm_query_pause_player`, `animeState_update`, `scoreNums_update`.
- If `cnd_static` is neither `wait` nor `pause`: `dm_black_up` (§4.7).

#### 4.2 States relevant to VS (`include/dm_game_main.h:60-94`) and their durations
Here "tick" means one 60 Hz logic frame. L is the tick in which the capsule locked (inside
`key_control_main`).

| mode | handler | what happens | duration |
|---|---|---|---|
| `dm_mode_init` | `dm_set_virus` (dgm:928) | Places **one virus per tick** in display order (skipping ones already placed). When all are placed: `cnd_now = wait`, `mode_now = dm_mode_wait`. Returns `dm_ret_virus_wait`. | N ticks for N viruses |
| `dm_mode_wait` | — | Returns `dm_ret_virus_wait`. The round starts when **both** players return `virus_wait`, both are in `dm_mode_wait`, and `count_down_ctrl < 0` (countdown finished). Then both get `dm_mode_down` in the same tick (dgm:4708-4726). | until countdown ends |
| `dm_mode_down` | (`key_control_main`) | Falling capsule (§2, §3). `dm_game_main_cnt` does nothing for this mode. | until lock |
| `dm_mode_down_wait` | dgm:3679 | Tick L+1: if game over → `dm_ret_game_over`. Else, if a line exists (`dm_h_erase_chack \|\| dm_w_erase_chack`), go to `erase_chack` with `cap_speed_count = 0`. Else go to `cap_set`. | 1 tick |
| `dm_mode_erase_chack` | dgm:3699 | `cap_speed_count++`. On the 18th tick: mark lines (`dm_h_erase_chack_set`, `dm_w_erase_chack_set`), break partners (`dm_h_ball_chack`, `dm_w_ball_chack`), recount viruses, score. **If viruses == 0 → `dm_ret_clear` (win)** in this tick. Else `chain_count++`, SE, `mode = erase_anime`. | **18 ticks** (pre-flash pause) |
| `dm_mode_erase_anime` | `dm_capsel_erase_anime` (dgm:999) | `erase_anime_count++`. At 10: `erase_anime()` advances the erase sprite (5→6, 0xD→0xE). At >26 (27th tick): `erase_anime()` again; cells reach 7 / 0xF and are cleared. Then `set_down_flg`, `mode = ball_down`, `erase_anime_count = 14`. | **27 ticks** |
| `dm_mode_ball_down` | `go_down(state,map,14)` (dgm:517) | See §4.5. The first step is immediate after an erase (count preset to 14). Then **one row every 14 ticks**. When nothing can fall, it re-checks lines → `erase_chack` (cascade) or `cap_set`, in the same tick as the last step. | 1 + 14·(rows−1) ticks after an erase; 14·rows after garbage |
| `dm_mode_cap_set` | dgm:3799 | Records combos. Then attack SE, warning flag, `dm_set_attack_2p` (send garbage), `dm_broken_set` (receive garbage → `ball_down`, no spawn). Otherwise spawn: `dm_set_capsel`, `dm_capsel_speed_up`, reset chain counters, `mode = down`. | 1 tick |
| `dm_mode_win/lose/draw[_retry]` | dgm:3902-3957 | End-of-round effects and retry menu. Covered in another section. | — |
| `dm_mode_pause[_retry]` | dgm:4063 | Pause menu. | — |

The `tr_*` / `training` modes only run when `flg_game_over` is set (a player who lost and keeps
playing in training) and are not needed for 2P VS. **UNVERIFIED** whether they are reachable in
2P: `dm_mode_tr_chaeck` is never assigned in the 2P paths read.

#### 4.3 Worked timelines (no input, in ticks)
- **Lock without a clear:**
  - L: lock. L+1: `down_wait` → `cap_set`. L+2: spawn.
  - L+31: the capsule enters y = 1. L+32: first tick with control.
  - So the **spawn-to-spawn overhead after a lock is 2 ticks, plus 30 ticks in the spawn row.**
- **Lock with a single clear and nothing falling:**
  - L+1: `down_wait` → `erase_chack`.
  - L+19: lines marked and viruses counted (win is decided here).
  - L+20 … L+46: `erase_anime`. Sprite step at L+29, cells removed at L+46.
  - L+47: `ball_down`. Nothing falls → `cap_set`.
  - L+48: spawn. L+77: the capsule enters y = 1.
- **Cascade:** each further link adds 18 (check) + 27 (anim) + fall time. Fall time is 1 tick for
  the first row, then 14 per additional row (§4.5).

#### 4.4 Line detection
`dm_h_erase_chack` / `dm_w_erase_chack` are the boolean tests. The `*_set` variants mark cells and
count lines. Note the naming: **"h" scans each column top to bottom (vertical lines); "w" scans
each row left to right (horizontal lines).**

- A line is a maximal run of **≥ 4 consecutive occupied cells with the same colour**
  (`capsel_m_flg[3]`), regardless of virus or pill. The internal counter counts matches *after*
  the first cell, so the test is `>= 3`.
- Early-out optimisations exist: no new run is started at array row ≥ 13 or column ≥ 5. These are
  behaviour-neutral, since such a run cannot reach length 4.
- Runs touching the bottom row (array row 15) or column 7 are closed explicitly.
- Both scans run in the same tick, vertical first and then horizontal. A cell can belong to both
  a horizontal and a vertical line. `dm_make_erase_*_line` skips cells already flagged
  (`capsel_m_flg[2] == 1`), so it is erased and counted once.
- For each marked cell:
  - `capsel_m_flg[2] = 1`.
  - Pill cells get `g = 5`.
  - Virus cells get `g = 0xD`, `erase_virus_count++`, `chain_color[3] |= 8 | (0x10 << colour)`.
- For each line found:
  - `chain_color[colour]++` and `chain_line++`.
  - If `chain_count == 0` (first link of this capsule's chain), also `chain_color[3] |= 1 << colour`.
- Run lengths 5, 6 and so on count as **one** line.
- `dm_h_ball_chack` / `dm_w_ball_chack` (dgm:1300, 1336) run after marking. Any half whose
  partner is no longer its matching half (because the partner is being erased) becomes a single
  ball (g = 4). A g = 1 cell at y = 1 is also converted.

#### 4.5 Falling of unsupported pieces (`set_down_flg` dgm:440, `go_down` dgm:517)
`set_down_flg` recomputes the "will fall" flag `capsel_m_flg[1]` for the whole map:
- Start with all cells in logical rows 1..15 marked as falling. Bottom row 16 is never marked.
- Scan array rows 14 → 0 (bottom to top), columns 0 → 7:
  - Empty cell → not falling. Virus → not falling.
  - Horizontal half (g = 2 at x with partner at x+1, or g = 3 with partner at x−1): the pair falls
    only if **for both halves** the cell below is empty or itself falling. Otherwise both are
    cleared.
  - Any other cell (vertical half or ball): falls only if the cell below is empty or falling.
    Vertical pills are thus naturally evaluated bottom first and fall together.
- It returns the number of falling cells.

`go_down(cout = 14)`:
- `erase_anime_count++`. If it is below 14, return.
- Otherwise reset it to 0 and **move every falling cell down exactly one row**, scanning bottom
  row first so cells don't collide. SE `SND_INDEX_55` plays if anything moved.
- Then call `set_down_flg`. If nothing will fall any more: run the line test → `erase_chack`
  (`cap_speed_count = 0`) or `cap_set`.

Timing:
- After an erase, `erase_anime_count` is preset to 14, so the first `go_down` tick steps
  immediately. Each later step comes **14 ticks** after the previous one. All falling pieces fall
  together, one row per step.
- If nothing is unsupported after an erase, `ball_down` lasts exactly 1 tick.
- After garbage drop (`cap_set` → `ball_down`), `erase_anime_count` is 0. It is always left at 0
  by the preceding `go_down`, assuming `dm_black_up` is not running. So the first step happens on
  the 14th tick.

#### 4.6 Chain / cascade counting and garbage timing
- `chain_count` counts erase steps (links) since the last spawn. It is incremented on each
  `erase_chack` completion that doesn't win.
- `chain_line` counts **all lines cleared since the last spawn, summed across the cascade**.
- `chain_color[0..2]` holds lines per colour.
- At `cap_set`:
  - If `chain_count > 1`: `total_chain_count += chain_count - 1` (stat, capped at 99).
  - `dm_set_attack_2p` (dgm:2405) runs when `chain_line >= 2` (two or more lines in the whole
    turn). It writes up to 4 coloured balls into the opponent's `cap_attack_work[0]`. The garbage
    pattern and colours are covered in a separate section.
  - Then `dm_broken_set` (dgm:1391): if *this* player has pending `cap_attack_work[0]`, the balls
    are placed in row **y = 1** at columns given by bit pairs (bits 0–1 → x = 7, …, bits 14–15
    → x = 0). `set_map` **overwrites** whatever is there. The queue shifts by one, chain
    counters reset, SE plays, and `mode = ball_down` instead of spawning.
  - After the garbage settles, `cap_set` runs again. A second queued entry is dropped in that
    pass, otherwise the capsule spawns.
- P1's `dm_game_main_cnt` runs before P2's in the same tick. So garbage sent by P1 in tick t can
  be received by P2 in the **same** tick t, if P2 is in `cap_set` at t. Garbage from P2 to P1 is
  seen at P1's next `cap_set`. Reproduce this ordering exactly.

#### 4.7 End of round
- `dm_ret_clear` (all viruses cleared, decided at the erase-mark tick) and `dm_ret_game_over` are
  resolved in `dm_game_main_2p` (dgm:4729-4877) in the same tick:
  - Both clear, or both top out → draw.
  - One clear (even if the other tops out in the same tick) → the clearer wins.
  - Otherwise the player who did not top out wins.
  - The win/lose modes stop both boards immediately. `key_control_main` then takes the
    else-branch, so no further gravity.
- Loser board: `flg_retire = true`, `black_up_count = 16`, `virus_anime_spead = 1`.
  `dm_black_up` (dgm:1360) then blackens one row every **6 ticks** (palette + 3), starting at the
  bottom row (array row 15) and moving up: 16 rows in 96 ticks. After that `flg_game_over = true`.
  It reuses `erase_anime_count` as its timer.
- Match length is `evs_vs_count` wins (default 3, `main1x.c:127`). Retry and next-round flow is
  covered in another section.

#### 4.8 Start-of-round countdown (**UNVERIFIED length**)
- `count_down_ctrl` is advanced in the graphics callback `dm_game_graphic_effect` (dgm:6689-6710),
  not in logic. It is incremented once per player per drawn frame, and the frame index is
  `count_down_ctrl / evs_playcnt`.
- SE `SND_INDEX_72` plays at frame indices 20, 68 and 116 (`idx % 48 == 20`, for the first three
  periods).
- When the LWS animation `etcLwsTbl[ETC_LWS_INDEX_9]` reports done (`disp_count_logo`,
  `game_etc.c:364`), SE `SND_INDEX_73` plays and `count_down_ctrl = -1`. The game starts on the
  next logic tick where both players are in `dm_mode_wait`.
- The animation length lives in ROM data and was not read. It also depends on the graphics thread
  actually drawing each frame (it skips when `pendingGFX != 0`, `graphic.c:218`). Measure it in
  an emulator. A plausible value is about 3×48 + 20 frames, also **UNVERIFIED**.
- Virus placement (§4.2) runs at the same time, at one virus per tick.

---

### 5. Sound effects referenced here (indices only)
| SE | trigger |
|---|---|
| 65 | move, or first blocked move attempt |
| 67 | rotate |
| 66 | capsule lock |
| 58 | speed up (every 10 capsules) |
| 55 | a falling-piece step in `go_down` |
| 56 / 61 | erase with viruses / without viruses (single line). With ≥ 2 lines a character SE `_charSE_tbl[charNo]+min(lines−2,2)` replaces them |
| 79 | warning: something in rows 1–3 (`dm_warning_h_line`). Repeats every 300 ticks while active |
| 80 | three or fewer viruses left (once per round) |
| 60 | pause toggle |
| 72 / 73 | countdown beep / go |

### 6. Summary of facts a port must hard-code
- Logic is 60 Hz, 1 tick per NTSC retrace. Order per tick: P1 state machine, P2 state machine,
  poll pads at about 13.5 ms, P1 input + gravity, P2 input + gravity.
- DAS: first move on the press frame, again at hold-frame 12, then every 6. A blocked move retries
  next frame, then every 6. Hold is "charged" during non-control phases.
- Rotation: A = CW, B = CCW, B has priority, edge-only. Only kicks: vertical→horizontal kicks left
  one column; horizontal→vertical shifts to the right column if the cell above the left half is
  blocked. Rotating horizontal→vertical at y = 1 is always allowed.
- Gravity: `max = FallSpeed[speed] + BonusWait[y−1][def] (y ≤ 3) + 2 if a block is under half
  [0]`, or 30 in the spawn row. Soft-drop adds `ceil(FallSpeed/2)` per tick. Lock happens at the
  next overflow while blocked. No hard drop.
- Start speeds 0 / 10 / 15. +1 at every 10th spawn. Cap 55.
- Phases: down_wait 1, erase pause 18, erase animation 27, fall 1st step immediate then 14 per
  row, cap_set 1, spawn row 30.

---

## 03 — VS rules (garbage, win/lose), screen layout, sound effects, reference-trace feasibility

Scope: 2-player human vs human VS (`evs_gamesel == GSL_2PLAY`, `evs_gamemode == GMD_NORMAL`),
US build. Source: decomp at `<decomp>/` (MIT). `dgm:N` means
`src/main_segment/dm_game_main.c` line N. Pseudocode is paraphrased, not copied. Items not
verifiable from the C code alone are marked **UNVERIFIED**.

Coordinate conventions used below (matching parts 01/02):
- Bottle = 8 columns (x 0..7, left to right) × 16 rows. The code's row number `y` is 1-based:
  y=1 is the top visible row and y=16 the bottom row. y=0 is the spawn spot in the bottle neck.
  Map array index = `(y-1)*8 + x` (`set_map`/`get_map_info`, `dm_virus_init.c:61,137`).
- Colour index 0/1/2 = red/yellow/blue (`get_virus_color_count`, `dm_virus_init.c:113`).
- Screen coordinates are in the original 320×240 framebuffer, with the origin at top-left.

---

### 1. Garbage / attack

#### 1.1 What counts as an attack

These per-player counters build up over one whole "turn": from the moment a capsule locks,
through every step of the cascade it causes. They are reset only in `dm_mode_cap_set`, when the
next capsule is dealt (dgm:3830-3848), or when incoming garbage is dropped (`dm_broken_set`).
- `chain_line` is the number of 3+-in-a-row lines erased during the turn. One line is counted
  for each run found by `dm_h_erase_chack_set` (vertical runs, dgm:1089) and
  `dm_w_erase_chack_set` (horizontal runs, dgm:1228). A cross-shaped clear therefore counts as 2.
- `chain_color[c]` (c = 0..2) is the number of lines erased in colour c this turn.
- `chain_color[3]` is a bit mask. Bit c is set by lines on the *first* cascade step only
  (`chain_count == 0`). Bit 3 means "a virus was erased in this step", and bits 4-6 record the
  virus colours. The mask only drives the 4P SFX and the 2P virus-clear SE (see §4).
- `chain_count` is the cascade step index. It is incremented once per erase step (dgm:3738).

**An attack is sent when `chain_line >= 2` at the end of the turn.** This covers both two lines
erased by one capsule and lines spread over cascade steps (dm_set_attack_2p dgm:2420).

#### 1.2 When it is evaluated (dm_game_main_cnt, case `dm_mode_cap_set`, dgm:3790)

The order, per player and per tick, is fixed. Player 0 is processed before player 1 in the same
tick (dm_game_main_2p dgm:4699).
1. `dm_attack_se(state, p)`: if `chain_line >= 2`, play the attack SE for this player (§4).
2. `dm_warning_h_line`: update this player's danger flag (§4).
3. If the mode is not TIME_ATTACK:
   a. `dm_set_attack_2p(state)` writes garbage into the **opponent's** pending slot. If it
      returns 1, play attacker animation ANIMENO_1 (cosmetic).
   b. `dm_broken_set(state, map)`: if **this** player has pending garbage, drop it now (§1.4).
      In that case: animation ANIMENO_2; SE `_charSE_tbl[charNo]+3` (the "hit" voice);
      `mode_now = dm_mode_ball_down`; and the capsule is **not** dealt this tick.
4. If no garbage was dropped: deal the next capsule (`dm_set_capsel`), run
   `dm_capsel_speed_up`, reset chain_line/chain_count/erase counters/chain_color, and set
   `mode_now = dm_mode_down`.

Consequences:
- Garbage is received **only at the receiver's own cap_set**. That is after the receiver's
  current capsule has locked and its own cascade has fully resolved. It is never dropped
  mid-fall.
- There is **no offsetting**. A player who is owed garbage and makes a 2-line combo sends a full
  attack and still receives the pending one.
- When garbage lands and the resulting fall/clears form lines, those lines start a new
  `chain_line` count, because dm_broken_set reset it. That can produce a counter-attack at the
  following cap_set. So garbage-caused combos *do* count.

#### 1.3 `dm_set_attack_2p` (dgm:2405) and `dm_make_attack_pattern` (dgm:2389)

Each player has 16 pending slots, `cap_attack_work[16]` ({u16 unk_0, u16 unk_2}). **2P only ever
uses slot 0**: the loop runs `i < 1`, so in practice 2P has a single merged slot.
`unk_0` holds 8 two-bit fields. Field k (bits 2k..2k+1) = colour+1 (0 = empty) for **column
7-k** (see the mapping in dm_broken_set below). `unk_2` = attacker's player_no (cosmetic only).

Pseudocode (attacker A, receiver E):
```
if A.chain_line < 2: return 0
n = min(4, A.chain_line)
if E.slot0 != 0:                        # merge with garbage already pending
    occ = bitmask of fields k that are non-empty
    par = parity of the highest non-empty k (all pending pieces share one parity)
    pattern = occ XOR (all 8 fields whose k has parity par)
            # = empty fields of the same parity as the existing pieces
            # (assuming the existing pieces are all one parity, which is always true)
    if pattern == 0: return 1           # slot full: the attack is lost (SE already played)
else:
    pattern = make_attack_pattern(n):
        n>=4: 0x55 << random(2)     -> k in {0,2,4,6} or {1,3,5,7}
        n==3: 0x15 << random(4)     -> k in {r, r+2, r+4}, r = 0..3
        n==2: 0x11 << random(4)     -> k in {r, r+4},      r = 0..3
E.slot0.unk_2 = A.player_no
for k = 0..7 ascending (i.e. column 7 down to column 0):
    if pattern bit k set:
        while any A.chain_color[0..2] > 0:
            c = random(3)                # rejection-sample a colour that still has count
            if A.chain_color[c] > 0:
                A.chain_color[c] -= 1
                E.slot0.unk_0 |= (c+1) << (2k)
                break
return 1
```
- Count: 2 pieces for 2 lines, 3 for 3, 4 for 4 or more. The colours come from the colours of
  the erased lines, drawn without replacement and weighted by the number of lines per colour.
  Example: 2 red lines + 1 blue line gives 2 red pieces and 1 blue piece in random columns.
- Fresh-attack columns (column = 7-k):
  - 4+ lines: {7,5,3,1} or {6,4,2,0}
  - 3 lines: r=0 → {7,5,3}, r=1 → {6,4,2}, r=2 → {5,3,1}, r=3 → {4,2,0}
  - 2 lines: r=0 → {7,3}, r=1 → {6,2}, r=2 → {5,1}, r=3 → {4,0}
- Merged attack: it can only fill the *free same-parity* columns. With 2 pieces pending at
  {7,3} and a new 2-line attack, the new pieces go to {5,1} (2 more colours are drawn). Total
  pending pieces never exceed 4. When the slot is already full, the new attack is discarded.
- RNG: the gameplay `random(n) = (n * irandom()) >> 16` (calc.c:773). It is the single
  lagged-Fibonacci stream shared by both players; see part 01. Consumption is one call for the
  pattern (fresh attack only), plus ≥1 call per piece because of the rejection loop.
  Within a tick, P0's cap_set consumes before P1's. In human-vs-human NORMAL VS, after round
  init this is the **only** consumer of `random()` (the AI uses `genrand`, aiset.c; and
  `aiCOM_MissTake` only counts time).
- Edge case (faithful port): `var_t1` (parity) is uninitialised if slot0 != 0 but no field is set.
  That cannot happen, because slot0 != 0 implies some field is set.

#### 1.4 Dropping garbage: `dm_broken_set` (dgm:1391)

At the receiver's cap_set, when `slot0.unk_0 != 0`:
1. `chain_line_max = max(chain_line_max, chain_line)`. Then reset chain_line, chain_count,
   erase_virus_count(_old) and chain_color[0..3] to 0.
2. For k = 0..7, if field k is non-empty, call `set_map(col = 7-k, y = 1, item = 4 (single
   ball), colour = field-1)`. This **unconditionally overwrites** whatever is in that top-row cell.
3. Run `dm_h_ball_chack` + `dm_w_ball_chack` (dgm:1300/1336). They turn any capsule half that
   lost its partner into a single ball (graphic 4). This matters when garbage overwrote half of a
   capsule lying in row 1.
4. `set_down_flg` marks everything unsupported as falling.
5. Shift the pending slots down by one (slot0 = old slot1, …). In 2P slot1+ are always 0, so
   slot0 becomes empty.
6. `mode_now = dm_mode_ball_down`. Pieces fall via `go_down(state, map, 14)`: one row per 14
   ticks, with SE 55 on each step that moves something. `erase_anime_count` is **not** preset
   here, unlike after an erase, where it is set to 14 for an immediate first step. The first
   garbage step therefore happens 14 − (current count) ticks later; normally the count is 0, so
   14 ticks. **UNVERIFIED** against a trace.
7. When nothing is falling any more: if lines exist, go to `dm_mode_erase_chack` (a normal
   cascade, whose lines count toward a new attack); otherwise go to `dm_mode_cap_set` again.
   That deals the capsule, because the slot is now empty.

#### 1.5 Score

Score is computed and **displayed** in VS: the two 7-digit totals on the board (§3). The
floating per-clear popups `scoreNums` are drawn only if Options→Score is on
(`evs_score_flag`, dgm:6503, default 1). Score does not affect the rules.
- At every erase step, `dm_make_score` (dgm:323) calls `dm_calc_score(state,
  level = cap_def_speed)` (dgm:292). For each newly erased virus, with running index i
  (`erase_virus_count_old .. erase_virus_count-1`, cumulative over the whole turn):
  `score += Score1p[speed][min(i,5)] * 100`.
- `Score1p` (main1x.c:162): LOW {1,2,4,8,16,32}, MED {2,4,8,16,32,64}, HI {3,6,12,24,48,96}.
  So with LOW speed, the 1st virus of a turn gives 100, the 2nd 200, … and the 6th and later
  3200 each.
- `dm_add_score`: the total is capped at 9,999,900. Scores are reset to 0 only on "retry" (a new
  match, dm_game_main2) or in time attack. **They carry over between rounds of a match**, because
  `dm_game_init(true)` does not clear `game_score`.

---

### 2. Win / lose / draw, rounds, menus

#### 2.1 Per-player round-end detection (dm_game_main_cnt)
- **Clear**: in `dm_mode_erase_chack` (18 ticks after lock or landing), after the lines are
  erased and the virus count is updated. If `virus_number == 0`, return `dm_ret_clear` (dgm:3720).
  This is decided at the moment the erase starts, so the erase animation is not waited for.
- **Top-out**: `dm_capsel_down` (dgm:2301) moves the capsule one row down. If either cell it
  moves into is occupied, it sets `cnd_static = dm_cnd_game_over`. In practice this is the
  y=0 → y=1 entry step into columns 3/4 of row 1, because the below-check protects every later
  step. The capsule is then written into the map anyway (overwriting). Next tick,
  `dm_mode_down_wait` → `dm_check_game_over` → return `dm_ret_game_over`. (Part 02 §3.4 has the
  tick-exact path.)

#### 2.2 Resolution in `dm_game_main_2p` (dgm:4671), same tick, after both players ran
```
clears = number of players returning dm_ret_clear   (each gets cnd_static = win)
overs  = number returning dm_ret_game_over           (each: cnd_static = lose,
         virus_anime_spead = 1, black_up_count = 16, flg_retire = 1)
if clears == 2 or overs == 2:    both -> draw (dm_set_draw_2p), play SEQ_INDEX_17 jingle
elif clears >= 1:                each clearer: dm_add_win_2p; then dm_set_win_2p;
                                 every other player: dm_set_lose_2p
elif overs >= 1:                 each non-loser: dm_add_win_2p, dm_set_win_2p;
                                 the loser: dm_set_lose_2p
if match finished: dm_save_all() (EEPROM records; remake: ignore or save locally)
```
- One player clearing on the same tick the other tops out: the **clearer wins**.
- Simultaneous clear, or simultaneous top-out: **draw**. A draw awards no star.
- `flg_retire` (top-out loser only) drives `dm_black_up` (dgm:1360). Every 6 ticks, starting
  with the bottom row (y=16) and going up, every occupied cell's palette gets +3 ("greyed").
  That is 16 rows × 6 = 96 ticks. It runs while `cnd_static` is not wait/pause. It is cosmetic
  but is part of the map state.

#### 2.3 `dm_add_win_2p` (dgm:4486)
- `win_count[p]++`. The star target is `x = _posP2StarX[p]` and
  `y = _posP2StarY[vs_count-1][win_count-1]` (§3), stored in `star_pos[star_count++]`.
- `finish = (win_count[p] == evs_vs_count)`. When finished:
  `vs_win_total[p] = min(99, +1)` and `vs_win_count[p] = min(999, +1)`. These are per-name
  persistent records shown on the board.
- Jingle: `SEQ_INDEX_15` if the match is won, otherwise `SEQ_INDEX_14` (a sequence, not an SFX;
  see §4.3).

#### 2.4 `dm_set_win_2p` / `dm_set_lose_2p` / `dm_set_draw_2p` (dgm:4535/4569/4607)
- Each sets `cnd_static` (win/lose/draw), starts a 180-tick effect timer (`effectX_init(p, 0xB4)`)
  and the character animation (win ANIMENO_3, lose/draw ANIMENO_4). Win also plays the
  character voice SE `_charSE_tbl[charNo]+4`. Lose and draw play no SE.
- **Only one player gets the menu**: the first *human* player in call order. Calls run in the
  order winners → losers (or P0 → P1 for a draw), so in human-vs-human the **winner** (P0 on a
  draw) gets `mode = *_retry` and a retry menu. The other player just shows the logo
  (`dm_mode_win`/`lose`/`draw`).
- Menu type: if the match is finished, `RETRYTYPE_3`. If it is not finished, or on a draw,
  `RETRYTYPE_5`.
- In `*_retry` mode the menu accepts input only after the effect timer reaches 0 (180 ticks).

#### 2.5 Retry menu (`retryMenu_input` dgm:2750; tables `cont_table` game_etc.c:144)
- Up/Down on the D-pad (from `joycur`, i.e. auto-repeat) move the cursor with wrap-around and
  play SE 64. A or START confirms with SE 62. The initial cursor is item 1 for RETRYTYPE_2..5
  and item 0 otherwise.
- RETRYTYPE_5 (round over, match continues), 3 items:
  0 = REPLAY (`dm_ret_replay`); **1 = NEXT (default)**, which returns `dm_ret_end`, mapped to
  game_over; 2 = END (`dm_ret_game_end`, back to the menu).
- RETRYTYPE_3 (match over), 3 items:
  0 = REPLAY; **1 = RETRY (default, `dm_ret_retry`)**; 2 = END.
- The on-screen labels come from textures (`etc_06`/`etc_09` and similar): **UNVERIFIED wording**.
- Remake: REPLAY (re-running the round from the recorded inputs) is optional and out of scope
  unless wanted.

#### 2.6 Between rounds (dm_game_main2 dgm:8109)
- `dm_ret_game_over` while no player has `win_count == evs_vs_count` leads to
  `dm_game_init(true)`, a **new round**:
  - new seeds (unless replaying), so a new virus layout and a new pill magazine;
  - **same virus_level and cap_def_speed for each player: no automatic level-up in VS**;
  - cap_speed is reset to `GameSpeed[cap_def_speed]`;
  - win_count, stars and score are kept. The countdown and virus build-up repeat.
- `dm_ret_retry` (RETRY on the match-over menu, or Retry in the pause menu) resets score to 0
  and calls `dm_game_init(false)`. That is a **new match**: win_count and stars are reset, and
  levels are unchanged.
- `dm_ret_game_end`, or game_over with a finished match, leaves the game and returns to
  MAIN_MENU (`dm_game_main3`).

#### 2.7 Wins needed per match
- `evs_vs_count` (main1x.c:127) defaults to **3**. It is set in Options → Misc → "count" menu
  (main_menu.c:5674, `evs_vs_count = 3 - select`), so the values are 3, 2 or 1. It is not saved
  to EEPROM (no reference in record.c). The remake should expose 1/2/3, defaulting to 3.

#### 2.8 Pause (VS)
- `dm_query_pause_player` (dgm:3195): a human pressing START (new press) while their
  `cnd_static` is wait or pause and `cnd_now != init` requests pause/unpause and flags SE 60.
  The request is **ignored during the countdown/virus build-up** (`cnd_now == init`) and after
  the round has ended.
- `dm_set_pause_and_volume` (dgm:4151, at the start of every tick):
  - If the requester is in wait, **both** players pause. The requester becomes the "master" and
    gets `dm_mode_pause_retry` with menu RETRYTYPE_1. The other player gets the pause logo only.
  - If the requester is already paused, both unpause and restore their saved mode/cnd.
  - Either player's START therefore toggles pause, and SE 60 plays on both pause and unpause.
- RETRYTYPE_1 menu (`pause_table[1]`, game_etc.c:73), cursor starts at 0:
  0 = CONTINUE (requests unpause, SE 62); 1 = RETRY (new match, `dm_ret_retry`, game_retry++);
  2 = END (`dm_ret_game_end`).
- Pressing START on the menu both confirms the cursor item and re-requests pause via
  `dm_query_pause_player` in the same tick. Exact interplay: **UNVERIFIED**, verify in trace.
- While paused, the bottles' contents are not drawn (`dm_game_graphic_p` returns early if
  `cnd_static == pause`).

#### 2.9 Round start (summary; tick details in part 02 §4.8)
- `dm_mode_init`: `dm_set_virus` places one virus per tick per player in display order. The
  player then enters `dm_mode_wait`.
- The countdown is driven from the graphics callback (`dm_game_graphic_effect` dgm:6689).
  - `count_down_ctrl` is incremented once per player per rendered frame; the displayed value is
    `count_down_ctrl / 2`.
  - SE 72 ("tick") plays at display counts 20, 68 and 116.
  - SE 73 ("go") plays when the count animation (`disp_count_logo`, LWS asset
    ETC_LWS_INDEX_9) ends; then `count_down_ctrl = -1`.
  - The animation length is asset data: **UNVERIFIED**, measure it.
- Play starts on the first tick where both players are in `dm_mode_wait` and
  `count_down_ctrl < 0`: both switch to `dm_mode_down` in the same tick.

---

### 3. Screen layout, 2P VS (320×240)

All values come from the code; textures are only referenced. In the final steady state
(`frame_move_count = 20`), the board offset is 0.

#### 3.1 Bottles and cells
- `dm_calc_bottle_2p` (dgm:6187): `map_x = 28 (P1) / 212 (P2)`, `map_y = 46`, cell size
  `map_item_size = 10` (dgm:7021-7027).
- The cell (x, y), with y = 1..16, is drawn at **screen (map_x + 10·x, map_y + 10·y)** and is
  10×10 (`dm_map_draw` dgm:5575; `pos_m_y = row+1`).
  - Playfield P1: x 28..108, y 56..216. Playfield P2: x 212..292, y 56..216.
  - The map is drawn with a scissor of (map_x, map_y+10, 80×160), so nothing above row 1 is
    drawn from the map.
- The **falling capsule** is drawn 1 px lower than the stacked cells: at y = map_y + 10·y + 1
  (`dm_calc_capsel_pos` dgm:1563). At y = 0 (in the neck) it is drawn at (map_x + 10·x, 36),
  i.e. cells (58,36) and (68,36) for P1, and (242,36) and (252,36) for P2.
- **Next-capsule preview** (`dm_game_graphic_p` dgm:6380): the next pill is drawn at its spawn
  spot in the neck, at (map_x + 30, 36) and (map_x + 40, 36). It is only drawn when the current
  capsule is inside the bottle (`now_cap.pos_y[0] > 0`). While the current pill sits in the
  neck, it occupies that same spot and the preview is hidden.
- **Fall-point guide** (ghost): each human toggles it with any C button (`visible_fall_point`,
  key_control_main dgm:6845, saved per player name). It is drawn at the landing position with
  colour (96,96,96) and alpha 150 (dgm:6343-6372), only in `dm_mode_down` and once the capsule
  is inside the bottle. It is an original feature, so keep it (it defaults per profile;
  **UNVERIFIED** default).
- **Bottle frame** (`dm_draw_bottle_2p` dgm:6202, `_draw_bottle_10` dgm:6148): the top-left is
  (map_x − 8, map_y − 30), i.e. (20,16) for P1 and (204,16) for P2. The frame is 96 wide and is
  assembled from rects (`_rect_4752`):
  - neck/top: 96×20 at +0, and 96×20 at +20 (screen y 16..56);
  - left wall: 8×160 at (+0,+40); right wall: 8×160 at (+88,+40) (screen y 56..216);
  - bottom: 96×8 at (+0,+200) (screen y 216..224).
  - A translucent glass texture (alpha 192) is drawn behind the frame.
  - So the outer bottle is P1 x 20..116 and P2 x 204..300, y 16..224.
- Slide-in at round start: `frame_move_count` goes 0→20, +1 per tick (dm_effect_make). Bottle
  x = 28 − 8·(20 − n) for P1 and 212 + 8·(20 − n) for P2, so they slide in from the sides.
  The board y is offset by −6·(20 − n). The bottle frame and board are drawn only during the
  slide; afterwards they are baked into a snapshot background (`dm_game_draw_snapped_bg`).

#### 3.2 Centre board and HUD (`draw_vsmode_board` dgm:6050 with x=0, y=0; `dm_game_graphic2` dgm:8516-8560)

| Element | Position (x,y) | Notes |
|---|---|---|
| "LEVEL" label (texP2[0x14]) | (104, 9) | Normal mode |
| Virus level numbers | P1 (113,11), P2 (189,11) | 2 digits, `draw_count_number`, 9 px digit pitch |
| "SPEED" label (texP2[0xD]) | (104, 25) | |
| Speed word (LOW/MED/HI strip, item `2 - cap_def_speed`) | P1 (108,27), P2 (184,27) | texP2[0xC] has 3 rows |
| Score label (texP2[0xB]) | (88, 41) | |
| Scores | P1 (94,42), P2 (162,42) | 7 digits, font colour 1 (P1) / 2 (P2) |
| Record wins (`vs_win_total`) | P1 (119,152), P2 (182,152) | 2 digits; persistent per profile. Remake: session match wins. |
| Characters (animated) | P1 (138,149) mirrored, P2 (181,149) | `_posP2CharFrm`; not needed (no art) |
| Game timer `evs_game_time` (mm:ss) | (154,167) | min at +0, ':' at +17, sec at +26. Counts while any player is in wait and the game has started; capped at 5999:00. |
| Mode logo (texP2[0xF], normal) | (131,181) | |
| Virus counters | P1 (138,210), P2 (182,210) | `draw_virus_number`: centred on (x,y), 14 px digit pitch; digit glyph size from asset (**UNVERIFIED**) |
| Win stars | x: P1 128, P2 172 | `_posP2StarX` |
| Win star y by wins needed (1st win, 2nd, 3rd) | 1 → (72); 2 → (80, 64); 3 → (88, 72, 56) | `_posP2StarY`. Empty star bases are drawn for the wins still missing; won stars fly in (`starForce`). |
| WIN / LOSE / DRAW / PAUSE / count logos | horizontally offset by `x2p = ∓92` from the screen centre (centre 160 → 68 / 252 = bottle centres) | game_etc.c:205 |
| Retry/continue menu | x = map-area centre − 31; y = bottom of the logo window − 50 − 2 (3-item menus); 16 px row pitch, cursor rows at +(-1, 15, 30) | `disp_continue_logo` game_etc.c:832. Window origin from `initEtcWork` (**UNVERIFIED** numbers). |

- Score popups (`scoreNums`, if evs_score_flag): spawned at the centroid of the erased cells
  (`dm_calc_erase_score_pos`), i.e. map_x + 5 + pos.x and map_y + 5 + pos.y.
- Danger: there is no visual element in 2P beyond the SE.

---

### 4. Sound effects

#### 4.1 Playback model
- `dm_snd_play_in_game(i)` plays only when `evs_gamespeed < 6`; it calls `dm_snd_play`
  (`audio/sound.c`).
- There are 4 FX voices.
  - The same effect number is never double-queued in the same tick.
  - Otherwise a free voice is taken. Failing that, the request steals a voice with priority
    ≤ the new one; failing that, it replaces a queued request of lower priority.
- `gSndsEntries[104]` (sound.c:57) holds, per SE: {fx number, signed pitch offset (×0.125,
  passed to `MusHandleSetFreqOffset`, semitones **UNVERIFIED**), unk, priority, max-length
  (in units of 6 ticks, 0 = none), volume}.
- Character voices: `_charSE_tbl[charNo]` (sound.c:29) is a base index. +0/+1/+2 = combo voice
  (chain_line 2 / 3 / ≥4), +3 = hit by garbage, +4 = win. The bases per charNo 0..14 are
  91, 81, 35, 40, 45, 15, 25, 20, 0, 5, 10, 30, 50, 86, 96. Default VS characters are charNo 0
  and 1 (main1x.c:178), otherwise taken from the VS character select (main_menu.c:7702). Which
  base belongs to which character is **UNVERIFIED**. The remake has no characters, so pick a
  fixed voice set per side or omit voices (decision for NOTES).

#### 4.2 SFX fired during a VS game

| Event | SE id | Call site | Condition / notes |
|---|---|---|---|
| Countdown tick (3,2,1) | SND_INDEX_72 | dgm:6704 (graphics cb) | display counts 20, 68, 116 |
| Countdown "go" | SND_INDEX_73 | dgm:6699 | end of the count animation |
| Move left/right | SND_INDEX_65 | translate_capsel dgm:686/693 | on every successful move. On a blocked move, only once until the next successful move or new capsule (`cap_move_se_flg`) |
| Rotate | SND_INDEX_67 | rotate_capsel dgm:761 | only if the rotation succeeded |
| Capsule lands/locks | SND_INDEX_66 | dm_capsel_down dgm:2374 | also on the top-out lock |
| Pieces fall one row (cascade or garbage) | SND_INDEX_55 | go_down dgm:542 | once per step in which something moved |
| Erase, with virus, chain_line < 2 | SND_INDEX_56 | dgm:3749 | at the erase step (18 ticks after settle) |
| Erase, no virus, chain_line < 2 | SND_INDEX_61 | dgm:3752 | |
| Erase with cumulative chain_line ≥ 2 | `_charSE_tbl[charNo] + min(chain_line-2, 2)` | dgm:3763 | character combo voice **instead of** 56/61 |
| Attack sent (turn end, chain_line ≥ 2) | P1: SND_INDEX_59, P2: SND_INDEX_75 | dm_attack_se dgm:390 (`dm_chaine_se_table_vs_178`) | at the attacker's cap_set; plays even if the attack was lost to a full slot |
| Garbage received | `_charSE_tbl[charNo] + 3` | dgm:3821 | when dm_broken_set drops pieces |
| Speed up (every 10 capsules) | SND_INDEX_58 | dm_capsel_speed_up dgm:864 | |
| Danger: stack in top 3 rows | SND_INDEX_79 | dm_warning_h_line_se dgm:2253 | `warning_se_flag` is updated at each cap_set (rows y 1..3 occupied). The global counter plays the SE on the first tick any waiting player is flagged, then every 300 ticks while flagged |
| ≤ 3 viruses left (first time in the round, either player) | SND_INDEX_80 | dgm:3730 | one shot per round (shared flag); the original also speeds up the BGM (ignore) |
| Pause / unpause | SND_INDEX_60 | dm_set_pause_and_volume dgm:4193 | |
| Menu cursor move | SND_INDEX_64 | retryMenu_input dgm:2783 | |
| Menu confirm | SND_INDEX_62 | dgm:3922-3939, 4069-4080 | |
| Win (round) | `_charSE_tbl[charNo] + 4` | dm_set_win_2p dgm:4541 | |
| Round won / match won jingle | SEQ_INDEX_14 / SEQ_INDEX_15 | dm_add_win_2p dgm:4528 | **sequences (music)**, not FX |
| Draw jingle | SEQ_INDEX_17 | dgm:4842 | sequence |
| (BGM) | SEQ `evs_seqnumb*2` (+1 for fast) | dgm:7144, 7990 | not wanted |

- Lose plays no SE. Out of VS scope: SE 57/74 (1P), 63/78 (time-attack result), 72 as the
  time-attack 10-second countdown, 101 (1P), 102 (flash mode).
- The jingles are Music-Tools songs. Note that with music turned off (`evs_seqence == 0`),
  `_dm_seq_play_in_game` still plays sequences ≥ SEQ_INDEX_10, so the original plays the
  win/draw jingles even with BGM off. Decide in NOTES whether the remake renders them (they come
  from the same banks, but they are songs).

#### 4.3 Where the sounds live in the ROM (for runtime extraction)
- The driver is libmus ("Music Tools", © Software Creations 1997/98; its source is vendored in
  `lib/libmus`, **not MIT**, so do not copy it). It is set up in `dm_audio_init_driver`
  (sound.c:268), with segment addresses from `_romDataTbl` (`src/boot/boot_data.c:62-64`).
- Offsets in the **retail US ROM** (sha1 a130d362…). I checked them by byte-matching the
  uncompressed build against `<your ROM>`. These segments are
  stored **uncompressed**; only code and graphics segments are gzip-compressed.

| Segment | Retail ROM start..end | Size | Header / content |
|---|---|---|---|
| `n64_wave_tables` (sample data, VADPCM) | 0x04F980..0x169BE0 | 0x11A260 | ASCII "N64 WaveTables" |
| `n64_ptr_tables_v2` (sample bank `ptr_bank_t`) | 0x169BE0..0x172130 | 0x8550 | ASCII "N64 PtrTablesV2" |
| `fxbank` (`fx_header_t`) | 0x172130..0x172D60 | 0xC30 | BE ints: components = 0x69, effects = 0x68 (= 104 = SND_INDEX_MAX), waves = 0x5B |
| songs (BGM/jingles) | other `_romDataTbl` entries via `RO_800ACA20` | – | not needed |

(The uncompressed decomp-build offsets are different: 0x9B460 / 0x1B56C0 / 0x1BDC10.)

- Format notes, from the libmus headers (`lib/libmus/src/player.h:60-80`, `libmus_data.h:20-29`):
  - `fx_header_t` = {num_components, num_effects, num_waves, flags, ptr_addr,
    wave_table (u16*), effects[] {fxdata ptr, priority}}.
  - Pointers are offsets relative to the bank start and are remapped at init
    (`__MusIntRemapPtrBank`, player.c:1099).
  - `ptr_bank_t` = {16-byte name, flags, 12-byte name, count, basenote*, detune*,
    wave_list[] → ALWaveTable}. ALWaveTable base offsets are relative to the WaveTables
    segment, and the samples are N64 VADPCM with codebooks in the wave entries.
  - Each effect is a Music-Tools command stream (the same command set as songs,
    `player_commands.c`): wave select, notes, envelopes, pitch, and so on.
- Extraction plan (recommended):
  1. Validate the ROM: sha1, or at least the "N64 WaveTables"/"N64 PtrTablesV2" magic at the
     offsets above.
  2. Parse the ptr bank and decode the needed waves from VADPCM to PCM.
  3. **Render** each of the ~20 used SFX once, by interpreting its fx command stream (a
     clean-room re-implementation of the Music-Tools semantics we need), applying the
     `gSndsEntries` pitch offset and volume.
  4. Cache the result as WAV in the user's data directory.

  A cheaper fallback is to play the primary wave of each effect with the pitch offset; this is
  an audible approximation. **UNVERIFIED** how complex the used effects are; inspect the fx
  streams first. Output rate: libmus default; **UNVERIFIED**.

---

### 5. Reference traces: feasibility and recommendation

#### 5.1 RAM map (US build, from `build/us/drmario64.us.map` + `nm` on the ELF)
KSEG0 addresses; for the RDRAM offset, use `addr & 0x1FFFFFFF`. Data is big-endian.

| Symbol | Address | Size / layout |
|---|---|---|
| `game_state_data[4]` | 0x80123700 | stride 0x3C4 (P2 at 0x80123AC4). Total 0xF10 |
| `game_map_data[4][136]` | 0x800EBEF0 | `GameMapCell` 10 bytes, 17 rows × 8, stride 0x550 (P2 at 0x800EC440) |
| `watchGame` (pointer) | 0x800F3E50 | deref → struct_watchGame (0xB60) |
| `CapsMagazine[256]` | 0x800EF448 | u8 |
| `randomindex` / `randomtable[55]` | 0x800E97C0 / 0x800E97C8 | static in calc.c |
| `mt[624]` / `mti` | 0x800E4640 / 0x80088480 | Mersenne twister (seeding only) |
| `evs_gamesel` / `evs_gamemode` / `evs_playcnt` | 0x800EFCD0 / 0x801236F0 / 0x80088403 | |
| `evs_vs_count` / `evs_game_time` / `evs_gamespeed` | 0x80088418 / 0x800FB3A4 / 0x80088406 | |
| `gControllerPressedButtons[5]` / `gControllerHoldButtons` | 0x800FAF88 / 0x800EBCF8 | u16 |
| `joycur` / `joygam[4]` / `main_joy` | 0x800F48C4 / 0x800F48B8 / 0x800EBD16 | |
| `FallSpeed` / `GameSpeed` / `BonusWait` | 0x80088434 / 0x80088430 / 0x80088420 | tables |

`struct_game_state_data` field offsets. I verified these by compiling the decomp header with
gcc -m32 -std=gnu89, where `bool` is a 4-byte enum. The result matches the header comments and
the size 0x3C4.

| Field | Offset |
|---|---|
| game_score | 0x00 |
| map_x / map_y / map_item_size | 0x06 / 0x08 / 0x0A |
| mode_now | 0x0C |
| cnd_now | 0x14 |
| cnd_static | 0x20 |
| virus_number | 0x25 |
| virus_level | 0x26 |
| cap_def_speed / cap_speed / cap_count / cap_speed_count / cap_speed_vec / cap_speed_max / cap_magazine_cnt | 0x2C..0x32 |
| erase_anime_count | 0x36 |
| erase_virus_count | 0x37 |
| chain_count / chain_line | 0x39 / 0x3A |
| chain_color[4] | 0x3C |
| warning_se_flag | 0x44 |
| flg_retire / flg_game_over | 0x48 / 0x49 |
| player_no | 0x4B |
| cap_attack_work[16] | 0x50 |
| game_level | 0x16C |
| now_cap {pos_x[2], pos_y[2], casel_g[2], capsel_p[2], flg0, flg1, flg2} | 0x178 |
| next_cap | 0x184 |

`GameMapCell` = {pos_m_x, pos_m_y, capsel_m_g (graphic), capsel_m_p (palette: colour, +3 when
greyed), flg[6]}. In flg: [0] occupied, [1] falling, [2] erasing, [3] colour, [4] virus type
(<0 for capsule).

`watchGame` offsets (from the struct comments at dgm:131-214; **UNVERIFIED** individually):
randSeed +0x004, randSeed2 +0x008, star_count +0x06C, frame_move_count +0x394,
count_down_ctrl +0x3AC, started_game_flg +0x3C4, warning_se_count +0x428,
effect_timer[4] +0x888, touch_down_wait +0x898 (= 2 in VS), win_count[4] +0x89C.

**Hash guidance.**
- Hash only logical state: for each cell, occupied, colour, item/graphic for capsules (0-4),
  virus flag and erasing; plus now_cap, next_cap, mode_now, cnd_static, the chain fields,
  cap_speed*, cap_attack_work[0], virus_number, randomindex.
- **Exclude virus `capsel_m_g`.** `dm_virus_anime` rewrites it from the *graphics* path
  (dm_game_graphic2 dgm:8448), so it depends on render timing.
- Likewise exclude `count_down_ctrl`, which is also advanced in the graphics callback.

#### 5.2 Route (a): emulator + input movie + RAM reads
- Debian 12 provides mupen64plus 2.5.9 (`libmupen64plus2`, `mupen64plus-ui-console`,
  `-rsp-hle`, `-input-sdl`, `-video-glide64mk2`) and `ares` 126 (a GUI app with no scripting or
  RAM-dump API, so it is unsuitable).
- I downloaded and extracted the debs into `<toolchain dir>/m64p/`, without
  installing them.
  - The Debian core links against OpenCV, gstreamer, gdal, gphoto2 and similar libraries. The
    dependency tail could not be satisfied in a few minutes, so the stock binary does not start
    here.
  - It does export the debugger API (`DebugMemGetPointer`, `DebugMemRead32`,
    `DebugSetCallbacks`). Whether these work without a DBG build is **UNVERIFIED**.
- Workable plan: build `mupen64plus-core` 2.5.9 from source with `OPENCV=0 OSD=0`
  (`DEBUGGER=1` if needed) into `<toolchain dir>/`. SDL2, libpng and zlib headers are
  present on this host; minizip headers need `libminizip-dev` (download only). Then add:
  - a ~200-line C frontend: CoreStartup → load ROM → attach rsp-hle and the dummy gfx/audio
    plugins → `M64CMD_SET_FRAME_CALLBACK`. In the callback, read RDRAM via `DebugMemGetPointer`
    (or save a state per frame and parse the RDRAM from the state file). mupen stores RDRAM as
    host-endian 32-bit words, so XOR byte addresses with 3.
  - a tiny input plugin that serves controller 1/2 buttons per `GetKeys` from a movie file
    (the game polls once per retrace).
  - menu navigation scripted as inputs, or a boot savestate at the VS level-select screen;
    seeds read from `watchGame->randSeed/randSeed2` and logged in the trace.

Risks:
- The build and glue effort is about half a day.
- The countdown, rendering and `osGetTime`-based seeds depend on emulator timing. Logic
  ticks are robust, but the frame where play starts and the countdown length may differ from
  hardware if the RDP is emulated slower or faster.
- dummy-gfx changes when the graphics thread runs, and the countdown advances only when a
  frame is rendered (`pendingGFX == 0`).
- Mapping traces to logic ticks needs care because of the logic/graphics split.
- Non-determinism from seeds is handled by reading them (or by poking `osGetTime`).

#### 5.3 Route (b): compile the decomp logic natively as an oracle
- Probe done: `dm_game_main.c`, `calc.c`, `dm_virus_init.c` and `vr_init.c` **compile with 0
  errors** on x86_64 using
  `gcc -std=gnu89 -nostdinc -isystem <gcc include> -I lib/ultralib/include{,/PR,/gcc} -I lib/libmus/include -I include -I build/us -I src/main_segment -DVERSION_US=1 -D_LANGUAGE_C -DF3DEX_GBI_2 -D_MIPS_SZLONG=32 -D_MIPS_SZINT=32`
  (objects in /tmp/offs).
  - The only pointer-size warnings come from the gbi.h graphics macros.
  - `dm_game_main.o` has 212 undefined symbols: graphics, anime, msgWnd, EEPROM, audio, AI
    and OS calls, plus data from main1x.c.
- Harness:
  - link `main1x.c` (tables) and the logic files; stub everything else, with the audio stub
    logging the SE ids;
  - set `evs_*`, levels and speeds;
  - call `dm_game_init(false)` with chosen seeds (stub `osGetTime`, or patch
    `randSeed`/`randSeed2` before `sgenrand`/`randomseed`);
  - then per tick replicate the real order: `dm_game_main2()`, then the onDoneSawp part
    (`dm_make_key` + `key_control_main` per player, with `joyProcCore` replaced by the movie);
  - drive the countdown and `dm_virus_anime` from a fake "render" call once per tick;
  - dump state hashes per tick.

Risks:
- It is the same source, not an independent check of hardware timing; the tick order and the
  graphics-thread pieces (countdown, virus anim) are our reconstruction.
- KMC-vs-gcc semantic differences in the logic (signed char, `bool` size, uninitialised
  locals, float→int in drawing only) are low risk, but verify a few cases against (a).
- `joy.c` auto-repeat (`joyCursorFastSet`, `joycur`) must be included faithfully; compile
  `joy.c` too.

#### 5.4 Recommendation
- Use **(b) as the golden-fixture generator**: deterministic, fast, seedable and CI-friendly,
  and it produces no ROM bytes, only hashes. Build it in `<oracle dir>/`,
  outside our repo, since it compiles Nintendo-derived decomp code. Only the text hash
  fixtures go into the repo.
- Then use **(a) sparingly as the timing referee**: a handful of emulator traces (spawn and
  entry, DAS, soft drop, countdown length, garbage drop timing, round end) to confirm the
  harness's tick order and the UNVERIFIED items above.
- Build (a) only after (b) works.
