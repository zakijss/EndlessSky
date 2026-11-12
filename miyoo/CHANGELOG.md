# Changelog

Port versions of Endless Sky for the Miyoo Mini Plus. Each one names the upstream version it is
built on.

## 1.2 (11 November 2026)

Built on Endless Sky 0.11.5 (upstream development branch, 10 November 2026).

Handheld features (Preferences, Settings, last page "Mini Plus"):
- In menus the D-pad jumps between buttons and list rows, outlined in orange; holding repeats. The
  map keeps the free pointer.
- Button hints: hold Select or Start for a moment to see what each button does; a notice when
  Select switches between flying and the pointer.
- Rumble on hull hits, failing shields, jumps and losing the flagship (off, light, strong).
- Battery: warnings at 15% and 5%; at Onion's cut-off (4% by default) the game quits as Quit does
  (saving on a planet) and Onion shows its low-battery screen and powers off.
- Battery saver: on planets and menus, 20 frames a second and the CPU on demand.
- Start in last save: skip the main menu at launch.
- Stay awake in flight: Onion's sleep timer no longer stops a long autopilot trip.
- Waking from sleep in flight opens the main menu (the game pauses).
- Save in flight: Start+R2 saves wherever you are, and leaving in flight (Menu, a flat battery,
  Onion's sleep, Quit) saves first; loading such a save starts in flight at that spot (other
  ships are placed anew). Off in Preferences gives the PC behaviour (only landing saves).
- Menu now leaves for Onion's game switcher, saving first.
- Slow pointer: hold Start while moving the pointer for fine aim (the map).
- Dim when idle: the screen dims after a minute without a button press (30 s to 5 min, or off);
  the next press only wakes it.
- Buttons for the commands upstream leaves unbound: fleet jump (Start+Up), fleet scan
  (Start+Down), turret tracking (Start+Left); Start+B centres the map on your system.
- Saves and settings are written to a temporary file and renamed into place.

Picture quality and speed:
- Drawing uses both CPU cores: big sprites, fills, the map fog, stars and other shapes are shared
  between them in small bands of rows.
- Blending and smooth sprite sampling use the CPU's NEON vector unit (four pixels at once).
- Sprite smoothing (Preferences, Settings, Mini Plus): "auto" smooths sprites whenever the frame
  has time for it, which is always on planets and in menus and often in flight, and backs off
  in heavy scenes instead of dropping frames. "off" and "always" as before.
- Ships, planets, asteroids and effects ship at three more sizes (0.4, 0.56 and 0.8), and the
  game loads the one drawn pixel for pixel at your view zoom.
- Faster starfield; the background haze stays sharp-filtered (it is soft art).
- Sprite memory follows the memory the system has free (up to 20 MB as before), so jumping to a
  new system no longer pushes the game into swap when other apps hold memory.

## 1.1 (11 November 2026)

Built on Endless Sky 0.11.5 (upstream development branch, 10 November 2026).

- Upstream changes: the game saves right after landing, fleet cargo use and capacity in Player
  Info, custom capacities in Ship Info, NPC placement and
  spawning options for missions, "unremovable" and "nondepreciable" outfits, game data
  conditions, and fixes for new-pilot gamerules and NPC ships loaded from saves.

## 1.0 (8 November 2026)

Built on Endless Sky 0.11.5 (upstream development branch, 7 November 2026).

First release.

- The whole game on the Miyoo Mini Plus under Onion OS (Games, Ports, Space).
- Software renderer: flight at 30 frames a second, full game speed in battles and hyperspace.
- Layouts for every menu and planet screen on the 640x480 screen.
- Button controls with Select and Start layers, a pointer for menus and an on-screen keyboard.
  Help messages and tutorials name the Miyoo's buttons.
- Status bar with the time, Wi-Fi and battery, and an optional frame counter (Preferences,
  Settings, Show frame rate).
- Port info page in the main menu.
- Sound effects and ambient loops.
- Menu opens Onion's game switcher.
