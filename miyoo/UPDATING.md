# Updating the Miyoo Mini Plus port

This fork follows upstream Endless Sky. Upstream code, data and sounds come in by merge; the
port's own work lives in a few places, so most updates merge cleanly.

## Where the port lives

- `source/soft/`: software renderer, the Miyoo window (framebuffer, buttons, frame pacing,
  keyboard, status bar) and the Port info page. New files, so merges never touch them.
- Upstream source files changed for the port (about 40) carry their changes inside
  `#ifdef ES_SOFT` or `#ifdef ES_MIYOO` blocks. Merge conflicts, if any, are in those files.
- `miyoo/data/_ui/`: data that replaces upstream definitions (colours, messages, Port info text).
- `miyoo/tools/`: generators that read upstream files each time (layouts, help text, credits),
  the image tools, the uploader and `stage.sh`.
- `miyoo/onion/`: the Onion launcher (`Ports, Space`) and its list picture.
- `miyoo/tests/`: self-tests and the screen tours.

## Routine update (code, data, story)

1. `git fetch origin && git merge origin/master`. The first time, run `git fetch --unshallow`
   so the merge has history.
2. Build the images once: `docker build -t es-zig -f miyoo/Dockerfile miyoo` and
   `docker build -t es-host -f miyoo/Dockerfile.host miyoo`.
3. Build: `ninja -C build-soft` in es-host (the Mac check build), and `ninja -C build-arm -j2`
   in es-zig, then `arm-linux-gnueabihf-strip -o build-miyoo/endless-sky build-arm/endless-sky`.
4. Test in es-host: the integration tests (`--test <name>`, all must pass),
   `miyoo/tests/raster_test.cpp` and `miyoo/tests/sound_test.cpp` (build lines at their tops).
5. `miyoo/tools/stage.sh`. The layout and help generators stop with an error if upstream
   changed a layout or a help sentence they rewrite. Adjust the rule they name, then run it again.
6. Look at the screens: copy `miyoo/tests/tour.txt` into a test config's plugins and run the
   tours with `--debug` and `ES_SHOT_EVERY` (see the top of `tour.txt`).
7. Bump `ES_PORT_VERSION` in `CMakeLists.txt` (the upstream version stays as it is).
8. Upload with `miyoo/tools/push.sh` (one large file at a time, resumable), or copy the stage
   to the SD card.
9. Release: `cd build-miyoo/stage && zip -qr -X ../EndlessSky-Miyoo-<version>.zip Roms`, then
   `gh release create v<version> build-miyoo/EndlessSky-Miyoo-<version>.zip -R zakijss/EndlessSky`
   with a few lines on what changed. The zip unpacks onto the SD card root as it is.

## When upstream art changes

Only new or changed images need this, in this order:

1. `shrink_images.py images <out> 0.625` for all art.
2. With a full-size (scale 1) host build: `ES_WRITE_MASKS=<file>` for `masks.txt` and
   `ES_WRITE_AREAS=<file>` for `areas.txt` (collision shapes and areas come from full-size art).
3. With the 0.625 art staged: `ES_WRITE_SIZES=<file>` on the host soft build for `sizes.txt`
   (the size each sprite is drawn at).
4. `shrink_images.py` at 0.8 for the folders in `FIXED_SIZE`, over the staged copies.
5. `cap_images.py <stage images> <stage sizes.txt>` for the few huge backgrounds.

Order matters: `sizes.txt` must come from the 0.625 art, because the game keeps those sizes when
it later loads the 0.8 and capped files.

## Device notes

- Test runs: copy `miyoo/tools/device/run.sh` to `/tmp/es-prof/` on the device and run
  `run.sh "<test name>" [seconds]` (or `run.sh -` to play normally). It pauses MainUI and
  restores it afterwards.
- Press buttons only with `pressif.sh`, which checks the game is running (it calls `press`,
  built from `miyoo/probe/press.c`). Simulated holds don't work on this hardware, only taps.
- Leave the Menu button to Onion: it opens the game switcher.
