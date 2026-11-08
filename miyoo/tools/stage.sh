#!/bin/sh
# stage.sh: rebuild the Miyoo package (build-miyoo/stage, laid out like the SD card) from this
# checkout: upstream data, sounds and texts plus the port's generated files. Run after every
# upstream update. The images are a separate step (miyoo/tools/art.sh): see miyoo/UPDATING.md.
set -e
cd "$(dirname "$0")/../.."
P=build-miyoo/stage/Roms/PORTS
G="$P/Games/Endless Sky"
VERSION=$(sed -n 's/.*ES_PORT_VERSION="\([^"]*\)".*/\1/p' CMakeLists.txt)
mkdir -p "$G" "$P/Shortcuts/Space" "$P/Imgs"

# Fresh copies, so files upstream removed don't linger (rsync isn't in every build image).
rm -rf "$G/data" "$G/sounds"
cp -R data "$G/data"
cp -R sounds "$G/sounds"
cp miyoo/data/_ui/*.txt "$G/data/_ui/"
python3 miyoo/tools/layout_miyoo.py data/_ui/interfaces.txt "$G/data/_ui/miyoo interfaces.txt" images
python3 miyoo/tools/help_miyoo.py data/_ui/help.txt "$G/data/_ui/miyoo help.txt"
python3 miyoo/tools/port_credits.py credits.txt "$G/credits.txt" "$VERSION"
cp keys.txt copyright license.txt "$G/"
# Sprite sizes, collision masks and areas (made from the art; see UPDATING.md).
cp miyoo/art/sizes.txt miyoo/art/masks.txt miyoo/art/areas.txt "$G/"
cp "miyoo/onion/Endless Sky.port" "$P/Shortcuts/Space/"
cp "miyoo/onion/Endless Sky.png" "$P/Imgs/"
[ -f build-miyoo/endless-sky ] && cp build-miyoo/endless-sky "$G/"
echo "stage ready: $G (port $VERSION)"
