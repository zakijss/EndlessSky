#!/bin/sh
# update_test.sh: the updater's download and apply steps on this computer, with file:// URLs in
# place of GitHub: a good signed package installs (binary, data swapped whole, images, launcher,
# the updater and its verifier); damaged, unsigned or wrongly signed ones are refused and leave
# the game as it was.
#   sh miyoo/tests/update_test.sh            (Mac: needs minisign; Miyoo: needs MINISIG_BIN)
# Packages are signed with a throwaway key made here (no passphrase), never the release key.
set -e
cd "$(dirname "$0")/../.."
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
fail() { echo "FAIL: $1"; exit 1; }
# Runs on a Mac or on the Miyoo (busybox, Onion's curl).
export ES_CURL=$(command -v curl) ES_SHORTCUTS="$T/shortcuts"
# The verifier: MiniDeck's minisig on the device (MINISIG_BIN), or minisign standing in for it.
if [ -n "${MINISIG_BIN:-}" ]; then
	VERIFIER=$MINISIG_BIN
else
	command -v minisign >/dev/null || { echo "update_test: needs minisign (brew install minisign)"; exit 1; }
	VERIFIER="$T/minisig"
	printf '#!/bin/sh\nexec minisign -V -q -p "$2" -m "$4" -x "$6"\n' > "$VERIFIER"; chmod +x "$VERIFIER"
fi
# Test keys: made here when minisign is available, else taken from TEST_KEYS (a folder with
# good.key/good.pub/other.key made on a computer) to sign on the device.
KEYS=${TEST_KEYS:-$T/keys}
if [ ! -f "$KEYS/good.key" ]; then
	mkdir -p "$KEYS"
	minisign -G -W -p "$KEYS/good.pub" -s "$KEYS/good.key" >/dev/null
	minisign -G -W -p "$KEYS/other.pub" -s "$KEYS/other.key" >/dev/null
fi
sign() { # sign FILE KEY
	if [ -n "${SIGNER:-}" ]; then "$SIGNER" "$1" "$2"; else minisign -S -q -s "$KEYS/$2.key" -m "$1" -x "$1.minisig"; fi
}
command -v sha256sum >/dev/null || { mkdir "$T/bin"; printf '#!/bin/sh\nshasum -a 256 "$@"\n' > "$T/bin/sha256sum"; chmod +x "$T/bin/sha256sum"; PATH="$T/bin:$PATH"; }

# The installed game: 1.0, with a data file the new version drops.
G="$T/game"; D="$T/config/update"
mkdir -p "$G/data" "$G/images/ui" "$ES_SHORTCUTS" "$D"
cp miyoo/onion/update.sh miyoo/onion/cacert.pem "$G/"
cp "$VERIFIER" "$G/minisig"; cp "$KEYS/good.pub" "$G/release.pub"
echo old > "$G/endless-sky"; echo 1.0 > "$G/port-version.txt"; echo gone > "$G/data/removed.txt"
echo old > "$ES_SHORTCUTS/Endless Sky.port"

# The package: 1.1.
K="$T/pkg"; mkdir -p "$K/data" "$K/images/ui" "$K/launcher"
echo new > "$K/endless-sky"; echo 1.1 > "$K/port-version.txt"; echo kept > "$K/data/new.txt"
echo lock > "$K/images/ui/lock@1x.png"; echo new > "$K/launcher/Endless Sky.port"
cp "$G/update.sh" "$K/update.sh"; echo '# new' >> "$K/update.sh"
cp "$G/minisig" "$G/release.pub" "$K/"
pack() { # pack: the package from $K, its checksum, its signature by the good key
	(cd "$K" && tar -czf "$T/EndlessSky-Miyoo-update.tgz" .)
	(cd "$T" && sha256sum EndlessSky-Miyoo-update.tgz > EndlessSky-Miyoo-update.tgz.sha256)
	sign "$T/EndlessSky-Miyoo-update.tgz" good
}
pack
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

# An old signed package offered again as new (a replay): refused, the game stays as it is.
echo old > "$G/endless-sky"
available
sh "$G/update.sh" download "$D"
sh "$G/update.sh" apply "$D" || true
grep -q '^state=error' "$D/status" && grep -q 'older' "$D/status" || fail "old package reinstalled: $(cat "$D/status")"
[ "$(cat "$G/endless-sky")" = old ] || fail "game changed by an old package"
echo new > "$G/endless-sky"

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

# Unsigned: refused before downloading.
rm -f "$T/EndlessSky-Miyoo-update.tgz.minisig"
available
sh "$G/update.sh" download "$D" || true
grep -q '^state=error' "$D/status" || fail "unsigned package accepted"
[ ! -f "$D/update.tgz" ] || fail "unsigned package kept"

# Signed by another key: refused, nothing kept.
(cd "$T" && sha256sum EndlessSky-Miyoo-update.tgz > EndlessSky-Miyoo-update.tgz.sha256)
sign "$T/EndlessSky-Miyoo-update.tgz" other
available
sh "$G/update.sh" download "$D" || true
grep -q '^state=error' "$D/status" || fail "wrongly signed package accepted"
[ ! -f "$D/update.tgz" ] && [ ! -f "$D/update.part" ] || fail "wrongly signed package kept"

# A good package swapped for another one on the card before installing: refused.
pack
available
sh "$G/update.sh" download "$D"
grep -q '^state=ready' "$D/status" || fail "re-download: $(cat "$D/status")"
echo junk > "$D/swapped"; tar -czf "$D/update.tgz" -C "$D" swapped
sha256sum "$D/update.tgz" | sed 's#  .*#  EndlessSky-Miyoo-update.tgz#' > "$D/update.sha256"
sh "$G/update.sh" apply "$D" || true
grep -q '^state=error' "$D/status" || fail "swapped package installed"
[ "$(cat "$G/endless-sky")" = new ] || fail "game changed by a swapped package"

# A signed package without the verifier and key for the next update: refused.
mv "$K/minisig" "$T/minisig.kept"; pack
available
sh "$G/update.sh" download "$D"
sh "$G/update.sh" apply "$D" || true
grep -q '^state=error' "$D/status" || fail "package without a verifier installed"
mv "$T/minisig.kept" "$K/minisig"; pack

# A power cut mid-apply that left the data aside is mended on the next start.
mv "$G/data" "$G/data.old"
sh "$G/update.sh" apply "$D"
[ -d "$G/data" ] || fail "data not put back"

# Without a launcher path the new launcher goes to the zip's own folder.
echo 1.2 > "$K/port-version.txt"; pack
available
sh "$G/update.sh" download "$D"
sh "$G/update.sh" apply "$D"
[ "$(cat "$ES_SHORTCUTS/Endless Sky.port")" = new ] || fail "launcher not replaced"

echo "update ok"
