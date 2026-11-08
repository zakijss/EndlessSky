# Endless Sky for Miyoo Mini Plus

The full [Endless Sky](https://github.com/endless-sky/endless-sky), the open-source space trading and
combat game, running on the Miyoo Mini Plus under Onion OS. The story, ships, outfits, missions and
saves are the original game's. What changed is how it runs on a 640x480 handheld with no GPU,
mouse or keyboard: a software renderer, screen layouts that fit, and controls built for the
Miyoo's buttons.

Port by ZJSS, built on Endless Sky 0.11.5 (upstream development branch) and kept in step with it.

[![Release](https://img.shields.io/github/v/release/zakijss/EndlessSky?color=4f8fd8&labelColor=1e1d28)](https://github.com/zakijss/EndlessSky/releases/latest) [![Onion OS](https://img.shields.io/badge/Onion_OS-Ports-3cc8de?labelColor=1e1d28)](https://github.com/OnionUI/Onion) [![Miyoo](https://img.shields.io/badge/Miyoo-Mini_Plus-e2d03c?labelColor=1e1d28)](https://github.com/OnionUI/Onion) [![License](https://img.shields.io/badge/license-GPLv3-8a8a8a?labelColor=1e1d28)](../license.txt)

<p align="center"><img src="../miyoo/docs/menu.png" alt="The main menu: the Endless Sky title tagged Mini Plus by ZJSS, the pilot's ship, planet and credits, and a Port info button" width="49%"> <img src="../miyoo/docs/flight.png" alt="Flying in the Sol system on the Miyoo: radar, shields and energy bars, the frame counter at the top left and the time, Wi-Fi and battery at the top right" width="49%"></p>
<p align="center"><img src="../miyoo/docs/planet.png" alt="Landed on Earth: the city picture, the planet's description and buttons for trading, jobs, the bank, the spaceport, the outfitter and hiring crew" width="49%"> <img src="../miyoo/docs/outfitter.png" alt="The outfitter: weapons listed with their pictures on the left, the player's ship and the buy and sell buttons on the right" width="49%"></p>
<p align="center"><img src="../miyoo/docs/map.png" alt="The star map around Sol, with its planets and their services, the commodity prices and the map buttons" width="49%"> <img src="../miyoo/docs/ship.png" alt="The ship info page: the ship's model, cost, shields, hull, mass, cargo, speed, outfits and its energy and heat table" width="49%"></p>

## What's in it

- Everything in the original game: the campaigns, every ship and outfit, trading, mining,
  missions, fleets and the galaxy map. Saves are the same format as the PC game, so you can
  move a pilot between the two.
- A software renderer written for the Miyoo's Cortex-A7. Flight runs at about 30 frames a second
  at full game speed, hyperspace at about 20, menus redraw only when something changes.
- Screen layouts redone for 800x600 logical pixels on the 640x480 panel, with every screen
  checked so text fits. Text is drawn at full resolution so it stays sharp.
- Button controls for everything, including an on-screen keyboard and a pointer for menus. The
  game's help and tutorials name the Miyoo's buttons instead of keyboard keys.
- A status bar with the time, Wi-Fi and battery in the top-right corner, and a frame counter at
  the top left you can turn on (Preferences, Settings, Show frame rate).
- A Port info page in the main menu (Start in flight, then Port info) with the controls and tips.
- The game's sound effects and ambient loops. Endless Sky has no music.
- Works with Onion's game switcher. Menu opens it; land first to keep your progress, as quitting
  always loses what you did since the last landing.

## Install

1. Download `EndlessSky-Miyoo-<version>.zip` from
   [Releases](https://github.com/zakijss/EndlessSky/releases/latest).
2. Unzip it and copy its `Roms` folder to the root of the SD card, merging it with the one there.
3. On the Miyoo, open Games, Ports, and the Space genre. If Endless Sky isn't listed, open Ports
   and refresh the list (Select, then Refresh roms).

Saves go to `Saves/CurrentProfile/saves/EndlessSky` on the SD card, with the game's log next to
them in `last-run.log`. To update, copy a newer release over the old one; saves are kept.

The game needs about 200 MB on the card. It was made and tested on the Mini Plus; other Miyoo
models are untested.

## Controls

Flying:

| Button | Action | Button | Action |
| --- | --- | --- | --- |
| Up / Down | Thrust / reverse | L1 | Land |
| Left / Right | Turn | R1 | Hyperspace jump |
| A | Fire primary weapons | L2 | Star map |
| B | Fire secondary weapon | R2 | Board target |
| X | Afterburner | Start | Main menu |
| Y | Target nearest enemy | Select (tap) | Pointer mode |

Hold Select for:

| Button | Action | Button | Action |
| --- | --- | --- | --- |
| A | Hail target | Up | Fleet: fight my target |
| B | Next secondary weapon | Down | Fleet: gather around me |
| X | Deploy or recall fighters | Left | Fleet: hold position |
| Y | Next ship | Right | Fleet: toggle hold fire |
| L1 | Scan | L2 | Message log |
| R1 | Auto steer | R2 | Nearest asteroid |
| Start | Fast-forward | | |

Hold Start for: Y player info, A cloak, B fleet harvest, X fleet ammo use, L1 pause, R1 help.

Menus and planets use a pointer. Tap Select to switch between flying and the pointer; landing
and taking off switch for you. The D-pad moves the pointer (hold to speed up), A clicks, X
right-clicks, B goes back and Start confirms. L1 and R1 scroll, L2 and R2 page. Hold Select or
Start while clicking to buy or sell 5 or 20 at a time, or both for 100.

When the game asks for text, a keyboard opens at the top of the screen: the D-pad picks a key,
A types, B deletes, X is Shift, Start is done and Y hides it.

## Known limits

- Texture filtering and motion blur are off by default because they slow drawing a lot. Both
  are in Preferences, Settings.
- Big battles with many ships drop below 30 frames a second. The game keeps its speed by drawing
  fewer frames.

## Building

Builds run in Docker on any machine: an ARM cross-compiler image for the device and a host image
for checks and tests. [miyoo/UPDATING.md](../miyoo/UPDATING.md) has the steps, how upstream
updates are merged, and how the art is prepared. The port's code is in `source/soft/` and in
`#ifdef ES_SOFT` / `ES_MIYOO` blocks in the upstream files, so merges from upstream rarely
conflict.

Bug reports about the port go in this repo's issues. Problems with the game itself (missions,
balance, story) belong upstream at
[endless-sky/endless-sky](https://github.com/endless-sky/endless-sky/issues).

## License

Endless Sky is free software under the GNU General Public License version 3, created by Michael
Zahniser and many contributors. The port's code is under the same license. Art and sound
licenses are listed in [copyright](../copyright); the full list of contributors is in
[credits.txt](../credits.txt).
