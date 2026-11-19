#!/bin/sh
# update_pack.sh: the small update package every release carries beside the full zip, for the
# game's own updater (miyoo/onion/update.sh): EndlessSky-Miyoo-update.tgz and its .sha256 in
# build-miyoo. It holds the binary, the data, the text files, the updater and the launcher, plus
# any image listed in miyoo/art/update-images.txt (paths in the game folder, one per line: the
# images upstream changed since the last release; clear the list after releasing).
# Run after stage.sh.
set -e
cd "$(dirname "$0")/../.."
P=build-miyoo/stage/Roms/PORTS
G="$P/Games/Endless Sky"
OUT=build-miyoo/EndlessSky-Miyoo-update.tgz
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
mkdir -p "$T/launcher"
cp -R "$G/data" "$T/"
for f in endless-sky areas.txt credits.txt keys.txt masks.txt sizes.txt copyright license.txt \
		update.sh cacert.pem port-version.txt; do
	cp "$G/$f" "$T/"
done
cp "$P/Shortcuts/Space/Endless Sky.port" "$T/launcher/"
if [ -f miyoo/art/update-images.txt ]; then
	grep -v '^#' miyoo/art/update-images.txt | while read -r f; do
		[ -n "$f" ] || continue
		mkdir -p "$T/$(dirname "$f")"
		cp "$G/$f" "$T/$f"
	done
fi
(cd "$T" && COPYFILE_DISABLE=1 tar -czf - .) > "$OUT"
(cd build-miyoo && shasum -a 256 "$(basename "$OUT")" > "$(basename "$OUT").sha256")
ls -l "$OUT"; cat "$OUT.sha256"
