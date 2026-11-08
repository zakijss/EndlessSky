#!/bin/sh
# art.sh [images folder]: make the Miyoo images from upstream art (default: the stage's images).
# Run in the es-host container from the repository root. All art is shrunk to 0.625; art that is
# always drawn at its own size is redone at 0.8; then the few huge backgrounds are capped.
# miyoo/art/sizes.txt must already describe this art (see miyoo/UPDATING.md).
set -e
cd "$(dirname "$0")/../.."
OUT=${1:-"build-miyoo/stage/Roms/PORTS/Games/Endless Sky/images"}
python3 miyoo/tools/shrink_images.py images "$OUT" 0.625
for d in land scene thumbnail outfit ui _menu icon; do
	rm -rf "$OUT/$d"
	python3 miyoo/tools/shrink_images.py "images/$d" "$OUT/$d" 0.8
done
python3 miyoo/tools/cap_images.py "$OUT" miyoo/art/sizes.txt
