#!/bin/sh
# update.sh: keeps the Miyoo Mini Plus port of Endless Sky up to date from its GitHub releases.
#   update.sh check DIR [--daily]   ask GitHub for the latest release (--daily: at most once a day)
#   update.sh download DIR          fetch the update package and check its SHA-256
#   update.sh apply DIR             install a downloaded package (the launcher runs this before
#                                   the game starts, so no file is replaced under a running game)
# DIR (the game's config folder + /update) holds the download and a status file of key=value
# lines that the game's Port info page and main menu read: state= idle, checking, current,
# available, downloading, ready or error, then latest=, size=, error=.
# It runs on any Miyoo Mini or Mini Plus with Onion OS: it needs Onion's curl (or any curl on the
# PATH), Wi-Fi and a right clock (HTTPS checks dates), and says which is missing when one is.
# ES_LAUNCHER: the launcher that runs "apply", so a new launcher replaces it where it is.
# cacert.pem beside it: Mozilla's root certificates as curl publishes them
# (curl.se/docs/caextract.html, MPL 2.0); refresh it now and then when releasing.
# Every package must be signed with the port's release key: minisig (MiniDeck's verifier) checks
# its .minisig against release.pub, both from the installed game, so each update is accepted
# only by code and a key that were themselves verified. Nothing unsigned is ever installed.
REPO=zakijss/EndlessSky
ASSET=EndlessSky-Miyoo-update.tgz
G=$(cd "$(dirname "$0")" && pwd)
D=$2
[ -n "$D" ] || { echo "usage: update.sh check|download|apply DIR" >&2; exit 2; }
mkdir -p "$D" || exit 1
# Where a new launcher goes: beside the one that ran us, else the folder the full zip uses.
SHORTCUTS=${ES_SHORTCUTS:-/mnt/SDCARD/Roms/PORTS/Shortcuts/Space}
[ -n "${ES_LAUNCHER:-}" ] && [ -f "$ES_LAUNCHER" ] && SHORTCUTS=$(dirname "$ES_LAUNCHER")
# ES_CURL: for tests on another machine.
CURL=${ES_CURL:-}
[ -n "$CURL" ] || { [ -x /mnt/SDCARD/.tmp_update/bin/curl ] && CURL=/mnt/SDCARD/.tmp_update/bin/curl; }
[ -n "$CURL" ] || CURL=$(command -v curl 2>/dev/null)
export LD_LIBRARY_PATH="/mnt/SDCARD/.tmp_update/lib:/mnt/SDCARD/.tmp_update/lib/parasyte:${LD_LIBRARY_PATH:-}"

get() { # get URL FILE SECONDS: curl's exit code, as a sentence when it fails
	"$CURL" --cacert "$G/cacert.pem" -fsSL --connect-timeout 10 --max-time "$3" -o "$2" "$1"
}
why() { # why CURL-EXIT-CODE
	case "$1" in
	6|7|28) echo "GitHub couldn't be reached. Is Wi-Fi on?" ;;
	35|51|58|60|77|83) echo "Secure connection failed: is the date and time right?" ;;
	22) echo "GitHub didn't have the file. Try again later." ;;
	23) echo "The SD card couldn't be written. Is it full?" ;;
	*) echo "The connection failed (curl error $1). Try again later." ;;
	esac
}
online() { # some Wi-Fi interface is up (the Miyoo Mini has none; Onion can turn it off)
	for n in /sys/class/net/wlan*; do
		[ "$(cat "$n/operstate" 2>/dev/null)" = up ] && return 0
	done
	return 1
}
room() { # room DIR KB: at least KB kilobytes free where DIR is
	free=$(df -Pk "$1" 2>/dev/null | awk 'NR == 2 { print $4 }')
	case "$free" in ''|*[!0-9]*) return 0 ;; esac
	[ "$free" -ge "$2" ]
}
status() { # status STATE [key=value...]
	{ echo "state=$1"; shift; for kv in "$@"; do echo "$kv"; done; } > "$D/status.tmp" &&
		mv -f "$D/status.tmp" "$D/status"
}
field() { sed -n "s/^$1=//p" "$D/status" 2>/dev/null; }
current() { cat "$G/port-version.txt" 2>/dev/null || echo 0; }
newer() { # newer A B: version A is later than B
	awk -v a="$1" -v b="$2" 'BEGIN { n = split(a, x, "."); m = split(b, y, ".");
		for(i = 1; i <= (n > m ? n : m); i++) { p = x[i] + 0; q = y[i] + 0; if(p != q) exit !(p > q) }
		exit 1 }'
}
fail() { status error "latest=$(field latest)" "error=$1"; exit 1; }
# ES_MINISIG: for tests on another machine.
MINISIG=${ES_MINISIG:-$G/minisig}
signed() { # signed FILE SIGFILE: the release key signed FILE
	[ -x "$MINISIG" ] || chmod +x "$MINISIG" 2>/dev/null
	[ -f "$2" ] && [ -f "$G/release.pub" ] && "$MINISIG" -p "$G/release.pub" -m "$1" -x "$2" >/dev/null 2>&1
}

# One run at a time (the launcher's daily check can meet a check from the game). A lock left by a
# run that died (power off) is stale after 30 minutes, or once the clock has gone back past it.
if [ "$1" != apply ]; then
	now=$(date +%s)
	if ! mkdir "$D/lock" 2>/dev/null; then
		since=$(cat "$D/lock/since" 2>/dev/null)
		case "$since" in ''|*[!0-9]*) since=0 ;; esac
		[ "$since" -gt "$now" ] || [ $((now - since)) -gt 1800 ] || exit 0
	fi
	echo "$now" > "$D/lock/since"
	trap 'rm -rf "$D/lock"' EXIT
fi

case "$1" in
check)
	if [ "$3" = --daily ]; then
		# (a clock that went back, as after a flat battery, counts as due)
		last=$(cat "$D/checked" 2>/dev/null)
		case "$last" in ''|*[!0-9]*) last=0 ;; esac
		[ "$last" -le "$now" ] && [ $((now - last)) -lt 86400 ] && exit 0
		# no Wi-Fi: nothing to say on the menu, try again next start
		online || exit 0
	fi
	case "$(field state)" in downloading|ready) exit 0 ;; esac
	[ -n "$CURL" ] || fail "Updates need Onion OS 4.3 or newer (for its curl)."
	online || fail "Wi-Fi is off. Turn it on in Settings to check."
	status checking "latest=$(field latest)"
	get "https://api.github.com/repos/$REPO/releases/latest" "$D/latest.json" 30 || fail "$(why $?)"
	echo "$now" > "$D/checked"
	# The JSON one value per line; the asset's size and URL follow its name.
	tr ',{}' '\n\n\n' < "$D/latest.json" | awk -v asset="$ASSET" '
		/"tag_name":/ { sub(/.*"tag_name": *"/, ""); sub(/".*/, ""); print "tag " $0 }
		/"name":/ { inasset = index($0, "\"" asset "\"") > 0 }
		inasset && /"size":/ { sub(/.*"size": */, ""); print "size " $0 + 0 }
		inasset && /"browser_download_url":/ { sub(/.*"browser_download_url": *"/, ""); sub(/".*/, ""); print "url " $0 }
	' > "$D/latest.txt"
	rm -f "$D/latest.json"
	v=$(sed -n 's/^tag v*//p' "$D/latest.txt")
	size=$(sed -n 's/^size //p' "$D/latest.txt")
	url=$(sed -n 's/^url //p' "$D/latest.txt")
	rm -f "$D/latest.txt"
	case "$url" in "https://github.com/$REPO/releases/download/"*) ;; *) url= ;; esac
	if [ -n "$v" ] && [ -n "$url" ] && newer "$v" "$(current)"; then
		status available "latest=$v" "size=$size" "url=$url"
	else
		status current "latest=$(current)"
	fi
	;;
download)
	[ "$(field state)" = available ] || exit 0
	v=$(field latest); size=$(field size); url=$(field url)
	case "$size" in ''|*[!0-9]*) size=0 ;; esac
	# Room for the package now, and for it unpacked beside the game when it installs.
	room "$D" $((size / 1024 * 4 + 20480)) || fail "There isn't enough free space on the SD card."
	[ -n "$CURL" ] || fail "Updates need Onion OS 4.3 or newer (for its curl)."
	status downloading "latest=$v" "size=$size" "url=$url"
	rm -f "$D/update.part" "$D/update.tgz" "$D/update.minisig"
	# (after a failure, Check again finds the update again)
	get "$url.sha256" "$D/update.sha256" 30 || fail "$(why $?)"
	get "$url.minisig" "$D/update.minisig" 30 || {
		r=$?
		rm -f "$D/update.sha256"
		[ "$r" = 22 ] && fail "This update isn't signed, so it wasn't downloaded."
		fail "$(why $r)"
	}
	get "$url" "$D/update.part" 1800 || {
		r=$?
		rm -f "$D/update.part" "$D/update.sha256" "$D/update.minisig"
		fail "$(why $r)"
	}
	want=$(cut -d' ' -f1 "$D/update.sha256")
	got=$(sha256sum "$D/update.part" | cut -d' ' -f1)
	if [ -z "$want" ] || [ "$want" != "$got" ]; then
		rm -f "$D/update.part" "$D/update.sha256" "$D/update.minisig"
		fail "The download didn't match its checksum, so it was thrown away."
	fi
	if ! signed "$D/update.part" "$D/update.minisig"; then
		rm -f "$D/update.part" "$D/update.sha256" "$D/update.minisig"
		fail "The update's signature didn't check out, so it was thrown away."
	fi
	mv -f "$D/update.part" "$D/update.tgz" && sync
	status ready "latest=$v" "size=$size"
	;;
apply)
	# A power cut during an earlier apply can leave the old data aside: put it back first.
	[ -d "$G/data" ] || { [ -d "$G/data.old" ] && mv "$G/data.old" "$G/data"; }
	[ -f "$D/update.tgz" ] || exit 0
	v=$(field latest)
	want=$(cut -d' ' -f1 "$D/update.sha256" 2>/dev/null)
	got=$(sha256sum "$D/update.tgz" | cut -d' ' -f1)
	if [ -z "$want" ] || [ "$want" != "$got" ] || ! signed "$D/update.tgz" "$D/update.minisig"; then
		rm -f "$D/update.tgz" "$D/update.sha256" "$D/update.minisig"
		fail "The download was damaged on the card and was thrown away."
	fi
	N="$G/.update-new"
	# Not enough room to unpack: keep the download, try again on the next start.
	room "$G" $(( $(wc -c < "$D/update.tgz") / 1024 * 3 + 20480 )) ||
		fail "Not enough free space on the SD card to install it."
	rm -rf "$N" && mkdir "$N" || fail "The update couldn't be unpacked."
	# A package must bring the verifier and key for the next update too, or updates would stop.
	if ! tar -xzf "$D/update.tgz" -C "$N" || [ ! -f "$N/endless-sky" ] || [ ! -d "$N/data" ] ||
			[ ! -f "$N/minisig" ] || [ ! -f "$N/release.pub" ]; then
		rm -rf "$N" "$D/update.tgz" "$D/update.sha256" "$D/update.minisig"
		fail "The update couldn't be unpacked."
	fi
	# The version inside the signed package, not the release's tag: an old signed package can't
	# be passed off as new.
	if ! newer "$(cat "$N/port-version.txt" 2>/dev/null)" "$(current)"; then
		rm -rf "$N" "$D/update.tgz" "$D/update.sha256" "$D/update.minisig"
		fail "The update is older than the installed game, so it was thrown away."
	fi
	# Each file is replaced by a rename, so a running script or binary keeps its old copy.
	rm -rf "$G/data.old"
	mv "$G/data" "$G/data.old" && mv "$N/data" "$G/data" && rm -rf "$G/data.old"
	if [ -d "$N/launcher" ]; then
		# The launcher is running this: write a copy beside it, then rename over it.
		mkdir -p "$SHORTCUTS"
		for f in "$N/launcher/"*; do
			b=$(basename "$f")
			cp -f "$f" "$SHORTCUTS/.$b.new" && mv -f "$SHORTCUTS/.$b.new" "$SHORTCUTS/$b"
		done
		rm -rf "$N/launcher"
	fi
	( cd "$N" && find . -type f ) | while read -r f; do
		mkdir -p "$G/$(dirname "$f")" && mv -f "$N/$f" "$G/$f"
	done
	chmod +x "$G/endless-sky" "$G/update.sh" "$G/minisig" 2>/dev/null
	rm -rf "$N" "$D/update.tgz" "$D/update.sha256" "$D/update.minisig"
	sync
	status current "latest=$(current)"
	;;
*)
	echo "usage: update.sh check|download|apply DIR" >&2
	exit 2
	;;
esac
