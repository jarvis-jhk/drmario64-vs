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
  **Online versus done 2026-10-16** (ahead of M4, Jan's roadmap order): lockstep 1v1
  through libwarpzone 0.1.1 — code rooms, launcher join/host, relay fallback, ranked 1v1
  queue with reports and state hash; CI plays real processes against warpzone-server.
- **M4 Polish** — input-latency measurement, options (window/fullscreen, rebinding).
- **macOS support** (Jan, 2026-10-19; combined item `drmario-macos-support`) — a macOS build
  (universal arm64+x86_64, or one per arch) built and smoke-tested on a GitHub `macos-*`
  runner in CI (unit + golden-trace tests, headless session test, packaged-tree smoke),
  attached to every release as a zip/dmg with an `.app` bundle; macOS install/start notes in
  README (unsigned is fine: document right-click → Open / `xattr -dr com.apple.quarantine`).
  Known risks: SDL2 renderer — we draw through `SDL_RenderGeometry`, so Metal backend and
  Retina/high-DPI scaling (`SDL_WINDOW_ALLOW_HIGHDPI`, drawable vs window size) need checking;
  controllers — macOS GameController/IOKit mapping, Xbox/PS pads, hotplug; paths — save/SFX
  cache via `SDL_GetPrefPath` is fine, but the ROM picker/launcher (Python + Tk, `launcher.py`)
  and the Linux installer/updater do not carry over (no zenity, no Steam helper; macOS
  Python/Tk is not guaranteed); auto-update must replace a whole `.app` atomically and keep
  quarantine in mind; Warpzone — CMake only fetches linux/windows libwarpzone packages, so a
  macOS (ideally universal) libwarpzone build is needed from the warpzone lane, or the mac
  build ships with online play switched off until then.
