#!/bin/bash
# Development helper (runs on Haiku): starts Galleria on a folder with an
# empty thumbnail cache several times and lists images that got a
# thumbnail but no cache entry.
#   bash tests/cache-check.sh <folder> [runs]
cd "$(dirname "$0")"
A=application/x-vnd.Galleria
APP=../objects.x86_64-cc13-release/Galleria
DIR=$1
RUNS=${2:-5}
CACHE="$(finddir B_USER_CACHE_DIRECTORY)/Galleria"

field() {
	hey $A Gsta of Window 0 2>/dev/null \
		| sed -n "s/^ *\"$1\" ([A-Z_0-9]*) : //p" | head -1 \
		| sed 's/^"\(.*\)"$/\1/; s/ (0x[0-9a-fA-F-]*)$//'
}

for ((run = 1; run <= RUNS; run++)); do
	hey $A quit >/dev/null 2>&1
	while roster | grep -q "($A)"; do sleep 0.1; done
	rm -rf "$CACHE"
	"$APP" "$DIR" >/tmp/cache-check.log 2>&1 &
	waitfor -m $A
	for ((i = 0; i < 300; i++)); do
		[ "$(field scanning)" = "FALSE" ] && [ "$(field pending)" = "0" ] \
			&& [ "$(field loaded)" -gt 0 ] && break
		sleep 0.1
	done
	cached=$(for f in "$CACHE"/256/*.png; do
		catattr -d Galleria:path "$f"; done 2>/dev/null | xargs -n1 -d '\n' basename | sort)
	echo "run $run: loaded $(field loaded), cache files $(echo "$cached" | grep -c .)," \
		"temp files $(ls "$CACHE"/256 | grep -c tmp)"
	for f in "$DIR"/*; do
		name=$(basename "$f")
		echo "$cached" | grep -qx "$name" || echo "    not cached: $name"
	done
done
hey $A quit >/dev/null 2>&1
grep -v "JPEG Library" /tmp/cache-check.log
