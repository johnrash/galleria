#!/bin/bash
# Development helper (runs on Haiku): start Galleria with the given
# arguments if it is not running, wait until the browser has read the folder
# and made all visible thumbnails, then take a screenshot.
#   tests/snap.sh /tmp/shot.png [folder or image ...]
cd "$(dirname "$0")"
. ./hey-lib.sh
HEY_APP=application/x-vnd.Galleria
APP=../objects.x86_64-cc13-release/Galleria
OUT=${1:-/tmp/galleria.png}
shift

if ! roster | grep -q Galleria; then
	"$APP" "$@" >/tmp/galleria.log 2>&1 &
	waitfor -m $HEY_APP
elif [ $# -gt 0 ]; then
	"$APP" "$@" >>/tmp/galleria.log 2>&1
fi

# wait for scanning and thumbnails (up to 30 s)
for i in $(seq 1 60); do
	state=$(hey $HEY_APP Gsta of Window 0 2>/dev/null)
	scanning=$(echo "$state" | sed -n 's/.*"scanning" (B_BOOL_TYPE) : //p')
	pending=$(echo "$state" | sed -n 's/.*"pending" (B_INT32_TYPE) : \([0-9]*\).*/\1/p')
	[ "$scanning" = "FALSE" ] && [ "$pending" = "0" ] && break
	sleep 0.5
done
sleep 0.5
take_screenshot "$OUT"
echo "screenshot: $OUT"
