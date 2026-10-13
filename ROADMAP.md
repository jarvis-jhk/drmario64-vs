# Roadmap

- **M0 Reference & spec** — build the decomp against the ROM, document the VS rules,
  timings, RNG and input handling (docs/SPEC.md) with decomp function references; a
  trace tool that records per-frame state from the original for given inputs.
- **M1 Engine core** — headless deterministic VS engine in C; golden-trace tests
  match the original frame-for-frame (pill RNG, viruses, gravity, DAS, rotation,
  clears, garbage, win/lose).
- **M2 Playable 1v1** — SDL2 frontend, two controllers + keyboard, VS menu
  (level/speed per player), vector renderer in the original layout, 4K-crisp; SFX.
- **M3 Release & Deck** — Linux/Windows/Deck builds, installer with desktop file,
  artwork and auto-update, fault reporting; first release v0.1.0.
- **M4 Polish** — input-latency measurement, options (window/fullscreen, rebinding).
