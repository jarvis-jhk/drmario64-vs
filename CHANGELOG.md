# Changelog

## v0.1.0 — first preview

- Two-player local VS of Dr. Mario 64: NORMAL, FLASH and TIME ATTACK, 1–3 wins, virus
  level and speed per player. The game logic replays 29 recorded matches of the original's
  own code tick for tick (pills, viruses, gravity, auto-repeat, rotation, clears, garbage).
- Crisp vector graphics in the original two-bottle layout, sharp up to 4K.
- Two gamepads (the first two connected) plus keyboard for both players; START pauses,
  the winner of a round picks REPLAY / NEXT / END.
- Sound effects rendered from your own Dr. Mario 64 (USA) ROM: drop the `.z64` onto the
  window once. Without it the game is silent. No music, by design.
- Linux and Steam Deck: download `Install-DrMario64-VS.desktop`, open it, and the game is
  installed with a menu entry, a Steam shortcut with artwork, and updates at every start.
  Windows: unzip and run `Play.cmd`.

Not tested on real hardware yet: two physical pads, the Steam Deck, the feel of the
default keyboard layout. Please report anything that feels different from the original.
