# Dr. Mario 64 VS

A faithful, frame-exact remake of the two-player local versus mode of Dr. Mario 64,
with sharp vector graphics for modern screens (up to 4K), for Linux, Windows and the
Steam Deck. Work in progress; not affiliated with Nintendo. No game data is included.

The game logic follows the behaviour documented by the matching decompilation
[AngheloAlf/drmario64](https://github.com/AngheloAlf/drmario64) (MIT); see
[docs/SPEC.md](docs/SPEC.md). Build: `cmake -S . -B build && cmake --build build && ctest --test-dir build`
(needs SDL2; `-DDM_BUILD_APP=OFF` builds only the headless core and its tests).
