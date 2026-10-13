# Dr. Mario 64 VS — brief

JARVIS owns this repo. Jan (2026-10-13) is the user and sets goals only.

## Goal
A standalone native game that is **only the 1v1 local versus mode** of Dr. Mario 64
(N64, 2001), playing and feeling **frame-perfect** like the original:
- identical game logic: RNG, virus generation, pill sequence, gravity/drop speeds per
  level and speed setting, lock/clear timing, cascade/combo garbage to the opponent,
  win/lose rules, the menu options of VS (virus level, speed, CPU off — 2 humans).
- identical **input feel**: per-frame polling at the original's rate, DAS/auto-repeat
  timings, rotation rules/wall kicks, soft drop, input latency no worse than the
  original on N64 at 60 Hz (NTSC). Verify with frame-by-frame traces, not by eye.
- **same screen layout**, but graphics redrawn as crisp vectors (or ≥4K assets) so it
  looks sharp at 3840×2160. No background art, **no music**, but the sound effects.
- Controllers: two gamepads (SDL GameController), keyboard fallback, Steam Deck works.

Not wanted now: story, marathon, score attack, 4P, CPU opponents, music, backgrounds.

## Source of truth
- ROM (US, NN6E, sha1 a130d3622ce40e0158db2da4247101f6e92206fc) lives OUTSIDE this
  repo at /app/agent/data/drmario/rom/drmario64-us.z64 (read-only). **Never commit
  ROM bytes, extracted graphics, audio or code from it.** It is Nintendo's.
- Matching decomp: github.com/AngheloAlf/drmario64 (MIT). Use it to read the exact
  logic; port the behaviour into our own clean C code, citing the function names.
  Build it locally against the ROM to run reference traces (e.g. via an emulator with
  input movie/trace support) and compare our engine frame-by-frame.
- Sound: the game cannot ship Nintendo's samples. Default plan: extract the SFX from
  the user's own ROM on first run (path picked in the launcher), cached locally; a
  ROM-less build plays silent or with own replacements. Decide and document in NOTES.

## Engineering
- C11 + SDL2 (like gtetrinet-ng), fixed 60 Hz logic tick decoupled from rendering,
  deterministic engine core with no SDL dependency so it is unit-testable headless.
- Golden tests: recorded input sequences → per-frame board state hashes, generated
  from the original (decomp build/emulator) and checked in as small text fixtures
  (state hashes, not ROM data).
- Renderer: vector shapes (pills, viruses, bottle, text) drawn at any resolution;
  layout coordinates from the original 320×240 screen, scaled with aspect kept.
- CI on GitHub Actions: Linux + native Windows, tests must pass. At most 2 compiler jobs locally.
- Releases: Linux tarball/AppImage, Windows zip, Steam Deck installer (desktop file,
  icon, Steam artwork grid/hero/logo, add-as-non-Steam-game helper) with auto-update
  from GitHub releases — copy the proven pattern from jarvis-jhk/gtetrinet-ng
  (versioned atomic installs) and jarvis-jhk/s25coop.
- Fault reporting: anonymous ntfy → GitHub Action → issue, endpoint only in the repo
  secret JARVIS_REPORT_URL; never in source. Same as gtetrinet-ng.
- Never put Jan's domains, name, LAN or home paths in the repo. Commit as jarvis-jhk
  noreply (326430845+jarvis-jhk@users.noreply.github.com).
- Repo is PRIVATE until a visibility check (adhoc lane) confirms no ROM-derived data.

Read ROADMAP.md and NOTES.md before working; update NOTES.md and the combined game
roadmap (`npx tsx /app/agent/scripts/roadmap.ts ...`) at the end of each session.
