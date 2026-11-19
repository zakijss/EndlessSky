#!/bin/sh
# update_test.sh: the updater's download and apply steps on this computer, with file:// URLs in
# place of GitHub: a good package installs (binary, data swapped whole, images, launcher, the
# updater itself), a damaged one is refused and leaves the game as it was.
#   sh miyoo/tests/update_test.sh
set -e
cd "$(dirname "$0")/../.."
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
fail() { echo "FAIL: $1"; exit 1; }
# Runs on a Mac or on the Miyoo (busybox, Onion's curl).
export ES_CURL=$(command -v curl) ES_SHORTCUTS="$T/shortcuts"
command -v sha256sum >/dev/null || { mkdir "$T/bin"; printf '#!/bin/sh\nshasum -a 256 "$@"\n' > "$T/bin/sha256sum"; chmod +x "$T/bin/sha256sum"; PATH="$T/bin:$PATH"; }

# The installed game: 1.0, with a data file the new version drops.
G="$T/game"; D="$T/config/update"
mkdir -p "$G/data" "$G/images/ui" "$ES_SHORTCUTS" "$D"
cp miyoo/onion/update.sh miyoo/onion/cacert.pem "$G/"
echo old > "$G/endless-sky"; echo 1.0 > "$G/port-version.txt"; echo gone > "$G/data/removed.txt"
echo old > "$ES_SHORTCUTS/Endless Sky.port"

# The package: 1.1.
K="$T/pkg"; mkdir -p "$K/data" "$K/images/ui" "$K/launcher"
echo new > "$K/endless-sky"; echo 1.1 > "$K/port-version.txt"; echo kept > "$K/data/new.txt"
echo lock > "$K/images/ui/lock@1x.png"; echo new > "$K/launcher/Endless Sky.port"
cp "$G/update.sh" "$K/update.sh"; echo '# new' >> "$K/update.sh"
(cd "$K" && tar -czf "$T/EndlessSky-Miyoo-update.tgz" .)
(cd "$T" && sha256sum EndlessSky-Miyoo-update.tgz > EndlessSky-Miyoo-update.tgz.sha256)
URL="file://$T/EndlessSky-Miyoo-update.tgz"
available() { printf 'state=available\nlatest=1.1\nsize=100\nurl=%s\n' "$URL" > "$D/status"; }

# Download: checked and kept.
available
sh "$G/update.sh" download "$D"
grep -q '^state=ready' "$D/status" || fail "download: $(cat "$D/status")"
[ -f "$D/update.tgz" ] || fail "no package after the download"

# Apply, from a launcher the user moved to another genre: everything in place, the new
# launcher where theirs is (not a second copy in Space), the old data gone, the package removed.
mkdir -p "$T/moved"; echo old > "$T/moved/Endless Sky.port"
ES_LAUNCHER="$T/moved/Endless Sky.port" sh "$G/update.sh" apply "$D"
[ "$(cat "$T/moved/Endless Sky.port")" = new ] || fail "moved launcher not replaced"
[ "$(cat "$ES_SHORTCUTS/Endless Sky.port")" = old ] || fail "a second launcher was written"
[ "$(cat "$G/endless-sky")" = new ] || fail "binary not replaced"
[ -x "$G/endless-sky" ] || fail "binary not executable"
[ -f "$G/data/new.txt" ] && [ ! -f "$G/data/removed.txt" ] || fail "data not swapped"
[ -f "$G/images/ui/lock@1x.png" ] || fail "image not installed"
[ ! -d "$G/launcher" ] && [ ! -d "$G/.update-new" ] && [ ! -d "$G/data.old" ] || fail "leftovers in the game folder"
tail -1 "$G/update.sh" | grep -q '# new' || fail "updater not replaced"
[ ! -f "$D/update.tgz" ] || fail "package left behind"
grep -q '^state=current' "$D/status" && grep -q '^latest=1.1' "$D/status" || fail "status after apply: $(cat "$D/status")"

# A package damaged on the card is refused; the game stays as it is.
available
sh "$G/update.sh" download "$D"
echo junk >> "$D/update.tgz"
sh "$G/update.sh" apply "$D" || true
grep -q '^state=error' "$D/status" || fail "damaged package accepted"
[ ! -f "$D/update.tgz" ] || fail "damaged package kept"
[ "$(cat "$G/endless-sky")" = new ] || fail "game changed by a damaged package"

# A download that doesn't match its checksum is thrown away.
available
echo "0000  EndlessSky-Miyoo-update.tgz" > "$T/EndlessSky-Miyoo-update.tgz.sha256"
sh "$G/update.sh" download "$D" || true
grep -q '^state=error' "$D/status" || fail "bad checksum accepted"
[ ! -f "$D/update.tgz" ] && [ ! -f "$D/update.part" ] || fail "bad download kept"

# A power cut mid-apply that left the data aside is mended on the next start.
mv "$G/data" "$G/data.old"
sh "$G/update.sh" apply "$D"
[ -d "$G/data" ] || fail "data not put back"

# Without a launcher path the new launcher goes to the zip's own folder.
(cd "$T" && sha256sum EndlessSky-Miyoo-update.tgz > EndlessSky-Miyoo-update.tgz.sha256)
available
sh "$G/update.sh" download "$D"
sh "$G/update.sh" apply "$D"
[ "$(cat "$ES_SHORTCUTS/Endless Sky.port")" = new ] || fail "launcher not replaced"

echo "update ok"
