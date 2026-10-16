# Changelog

## v0.2.0 — online versus

- Play against someone on another machine through Warpzone: on the start screen press F2
  or the pad's Back button, then HOST A ROOM (shows a six-letter code) or JOIN WITH A CODE.
  The Warpzone launcher can also start the game straight into a room.
- RANKED 1 VS 1 finds an opponent and rates the match (both play NORMAL, level 10, MED,
  first to 3 wins).
- Both machines run the same game in lockstep, checked every half second, so a bad line
  shows as a short wait, never as a different game. Works through the Warpzone relay when
  a direct connection is not possible.
- The first time you go online you choose a name, shared with all Warpzone games.
- Sound: a ROM the Warpzone launcher found is used, and the cached sound effects need no
  ROM afterwards.

Not tested yet between two real home networks or on the public server's relay; please
report how the input delay feels online.

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
