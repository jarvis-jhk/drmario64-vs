# Dr. Mario 64 VS

A faithful, frame-exact remake of the two-player local versus mode of Dr. Mario 64,
with sharp vector graphics for modern screens (up to 4K), for Linux, Windows and the
Steam Deck. Work in progress; not affiliated with Nintendo. No game data is included.

The game logic follows the behaviour documented by the matching decompilation
[AngheloAlf/drmario64](https://github.com/AngheloAlf/drmario64) (MIT); see
[docs/SPEC.md](docs/SPEC.md). Build: `cmake -S . -B build && cmake --build build && ctest --test-dir build`
(needs SDL2; `-DDM_BUILD_APP=OFF` builds only the headless core and its tests).

## Installing

From the [latest release](https://github.com/jarvis-jhk/drmario64-vs/releases/latest):

- **Steam Deck / Linux**: download `Install-DrMario64-VS.desktop` (in Desktop Mode) and
  open it. It installs the game under `~/.local/share/drmario64-vs`, adds a menu entry and
  a Steam shortcut with artwork (close Steam first), and the game updates itself at every
  start. Or unpack `drmario64-vs-linux-x86_64.tar.gz` and run `drmario64-vs.sh`.
- **Windows**: unpack `drmario64-vs-windows-x86_64.zip` and run `Play.cmd`.

Release builds send an anonymous one-line fault report (name, version, platform) when the
game crashes; set `DM_NO_REPORT=1` to turn that off.

## Playing

Both players set up their side on the start screen (mode NORMAL / FLASH / TIME ATTACK,
wins needed 1–3, virus level and speed per player) and press A when ready. In a match,
START pauses (the player who paused gets CONTINUE / RETRY / END); the round's winner
picks REPLAY / NEXT / END (RETRY after the deciding round).

| | Player 1 | Player 2 | Gamepad |
|---|---|---|---|
| Move / soft drop | A D / S | ← → / ↓ | D-pad or left stick |
| Rotate right / left | G (or Space) / F | L / K | A or Y / B or X |
| Start / pause | Tab | Enter | Start |
| Landing guide on/off | E | O | Back or right stick |

The first two gamepads connected are players 1 and 2; the keyboard always works too.
F11 or Alt+Enter toggles fullscreen (the Steam Deck starts fullscreen). Esc pauses, and
quits from the start screen.

**Sound effects** come from your own copy of the game: drop your Dr. Mario 64 (USA) ROM
(`.z64`) onto the window once, or start with `--rom <file>`. The effects are rendered
from it and cached in your user data directory; nothing from the ROM is part of this
project. Without a ROM the game plays silently. There is no music, by design.
