# Third-party material

- **SDL2** (zlib licence) is bundled with the Linux and Windows downloads; its licence is
  next to it (`SDL2-license*`). Windows downloads also carry the toolchain runtime DLLs
  that SDL needs, each with its licence folder (and, for libiconv, its source).
- **libwarpzone** (jarvis-jhk/warpzone, MIT OR Apache-2.0) is linked statically for online
  play; its MIT licence ships as `warpzone-LICENSE-MIT`.
- **Install and launcher scripts** in `tools/linux` and `tools/windows` are adapted from
  jarvis-jhk/gtetrinet-ng and jarvis-jhk/s25coop and stay under GPL-2.0-or-later, as their
  headers say. They are separate programs run next to the game, not part of it; the game
  itself is MIT.
- **Game logic** is our own C code, written from reading the matching decompilation
  AngheloAlf/drmario64 (MIT). No data from the original ROM is part of this project; the
  sound effects are rendered at run time from the player's own copy.
