# Roadmap

- **M0 Reference & spec** — decomp builds matching (done); docs/SPEC.md (done, UNVERIFIED points
  listed in NOTES); RNG ported with golden test (done); round-setup oracle from the decomp's own
  code + dmcore port of magazine, virus layouts and FLASH targets matching 280 golden
  rounds (done 2026-10-16). **M0 done.**
- **M1 Engine core** — per-tick trace oracle (input movies through the decomp's
  `dm_game_main_cnt`/`key_control_main`, docs/TRACES.md) and the headless
  deterministic VS engine in C; golden-trace tests
  match the original frame-for-frame (pill RNG, viruses, gravity, DAS, rotation,
  clears, garbage, win/lose). **Done 2026-10-16**: `src/core/vs.c` replays 29 oracle
  movies (~440k ticks, NORMAL/FLASH/TIME ATTACK) with every tick's full state identical.
- **M2 Playable 1v1** — SDL2 frontend, two controllers + keyboard, VS menu
  (level/speed per player), vector renderer in the original layout, 4K-crisp; SFX.
  **Done 2026-10-16**: setup screen, pause/retry/REPLAY menus, AA vector renderer,
  pads + keyboard, SFX rendered from the player's ROM; headless session test + bot round.
- **M3 Release & Deck** — Linux/Windows/Deck builds, installer with desktop file,
  artwork and auto-update, fault reporting; first release v0.1.0. **Done 2026-10-16**:
  release workflow on v* tags (tests + packaged-tree smoke), v0.1.0 preview. Open: Jan's
  hardware test (pads, Deck), and the repo going public (visibility gate, adhoc) so the
  installer and auto-update can download anonymously.
- **Warpzone** (Jan, 2026-10-16) — after going public: descriptor games/drmario64-vs.toml
  in jarvis-jhk/warpzone (tier 0: install/launch, `[[data]]` ROM with `--rom {path}`,
  Steam artwork) by PR. Our updater already stays silent with `.warpzone-managed`.
  Later, after M4: online 1v1 (tier 2, rollback-free lockstep fits the deterministic
  engine) and a 1v1 ladder.
- **M4 Polish** — input-latency measurement, options (window/fullscreen, rebinding).
