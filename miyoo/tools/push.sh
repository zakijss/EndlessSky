#!/bin/sh
# Resumable upload of one large file to the Miyoo over a weak Wi-Fi link.
#   push.sh <local file> <remote file>        (ssh host "miyoo"; override with MIYOO_HOST)
# Splits into 512 KB pieces, sends each over one shared ssh connection with a timeout and
# retries, checks every piece's sha256 on the device, skips pieces already there, then joins
# them and checks the whole file. Rerun after a failure to continue where it stopped.
set -eu
SRC=$1
DST=$2
HOST=${MIYOO_HOST:-miyoo}
PIECE=524288
WORK=$(mktemp -d)
REMOTE_DIR=$(dirname "$DST")/.push-$(basename "$DST")
CTL=$WORK/ctl
trap 'ssh -o ControlPath="$CTL" -O exit "$HOST" 2>/dev/null; rm -rf "$WORK"' EXIT

sha() { shasum -a 256 "$1" | cut -d' ' -f1; }
rsh() { ssh -o ControlMaster=auto -o ControlPath="$CTL" -o ControlPersist=120 -o ConnectTimeout=10 \
	-o ServerAliveInterval=5 -o ServerAliveCountMax=3 "$HOST" "$@"; }

split -b $PIECE -a 4 "$SRC" "$WORK/p."
total=$(ls "$WORK"/p.* | wc -l | tr -d ' ')
rsh "mkdir -p '$REMOTE_DIR'" 2>/dev/null
# Pieces already on the device, with their sums, so a rerun only sends what is missing.
rsh "cd '$REMOTE_DIR' && sha256sum p.* 2>/dev/null" 2>/dev/null > "$WORK/have" || true

sent=0
skipped=0
for f in "$WORK"/p.*; do
	name=$(basename "$f")
	want=$(sha "$f")
	if grep -q "^$want  $name\$" "$WORK/have"; then
		skipped=$((skipped + 1))
		continue
	fi
	ok=0
	for attempt in 1 2 3 4 5; do
		got=$(timeout 60 ssh -o ControlMaster=auto -o ControlPath="$CTL" -o ControlPersist=120 \
			-o ConnectTimeout=10 "$HOST" \
			"cat > '$REMOTE_DIR/$name.tmp' && mv '$REMOTE_DIR/$name.tmp' '$REMOTE_DIR/$name' && sha256sum '$REMOTE_DIR/$name'" \
			< "$f" 2>/dev/null | cut -d' ' -f1) || true
		if [ "$got" = "$want" ]; then
			ok=1
			break
		fi
		sleep 2
	done
	if [ $ok != 1 ]; then
		echo "piece $name failed 5 times; rerun to continue ($sent sent, $skipped already there, of $total)" >&2
		exit 1
	fi
	sent=$((sent + 1))
	[ $((sent % 20)) = 0 ] && echo "$((sent + skipped)) / $total pieces"
done

want=$(sha "$SRC")
got=$(rsh "cat '$REMOTE_DIR'/p.* > '$DST.tmp' && mv '$DST.tmp' '$DST' && sha256sum '$DST'" 2>/dev/null | cut -d' ' -f1)
if [ "$got" != "$want" ]; then
	echo "joined file does not match (expected $want, got $got)" >&2
	exit 1
fi
rsh "rm -rf '$REMOTE_DIR'" 2>/dev/null
echo "uploaded $DST ($total pieces, $sent sent, $skipped resumed) sha256 $want"
