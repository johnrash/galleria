#!/bin/bash
# Scale test (runs on Haiku): a folder with many images. Measures how long
# the first screen of thumbnails takes, cold and from the cache, and checks
# that jumping to the end of the folder only makes thumbnails near the view.
#   bash tests/big-folder-test.sh [path/to/Galleria] [image folder] [count]

cd "$(dirname "$0")"
. ./hey-lib.sh
APP_BINARY=${1:-$(cd .. && pwd)/objects.x86_64-cc13-release/Galleria}
SOURCE=${2:-/boot/home/GalleriaTest/images}
COUNT=${3:-600}
A=application/x-vnd.Galleria
DIR=$(cd /tmp && pwd -P)/galleria-big
CACHE="$(finddir B_USER_CACHE_DIRECTORY)/Galleria"
SETTINGS="$(finddir B_USER_SETTINGS_DIRECTORY)/Galleria settings"

field() {
	hey $A Gsta of Window "$1" 2>/dev/null \
		| sed -n "s/^ *\"$2\" ([A-Z_0-9]*) : //p" | head -1 \
		| sed 's/^"\(.*\)"$/\1/; s/ (0x[0-9a-fA-F-]*)$//'
}
visible_done() {
	[ "$(field 0 scanning)" = "FALSE" ] && [ "$(field 0 visible)" -gt 0 ] \
		&& [ "$(field 0 visible)" = "$(field 0 visibleDone)" ]
}
now() { echo $(( $(date +%s%N) / 1000000 )); }
wait_visible() {
	local start=$(now)
	for ((i = 0; i < 600; i++)); do
		visible_done && break
		sleep 0.1
	done
	echo $(( $(now) - start ))
}
quit_app() {
	hey $A quit >/dev/null 2>&1
	for ((i = 0; i < 100; i++)); do
		roster | grep -q "($A)" || return
		sleep 0.1
	done
}

quit_app
[ -f "$SETTINGS" ] && mv -f "$SETTINGS" "$SETTINGS.big-backup"
rm -rf "$DIR" "$CACHE"
mkdir -p "$DIR"
files=($(cd "$SOURCE" && ls *.jpg *.png *.webp *.tiff *.bmp | grep -v broken))
for ((i = 0; i < COUNT; i++)); do
	f=${files[$((i % ${#files[@]}))]}
	cp "$SOURCE/$f" "$DIR/$(printf 'photo-%04d' $i).${f##*.}"
done
echo "$COUNT images in $DIR"

"$APP_BINARY" "$DIR" >/dev/null 2>&1 &
waitfor -m $A
echo "first screen, cold cache: $(wait_visible) ms ($(field 0 visible) thumbnails)"
echo "items: $(field 0 items)"

hey $A Gsli of Window 0 with "index=int32($((COUNT - 1)))" >/dev/null
echo "end of folder, cold cache: $(wait_visible) ms"
for ((i = 0; i < 300; i++)); do
	[ "$(field 0 pending)" = "0" ] && break
	sleep 0.1
done
echo "thumbnails in memory: $(field 0 loaded) of $COUNT"
echo "cache files: $(find "$CACHE" -type f | wc -l)"
quit_app

"$APP_BINARY" "$DIR" >/dev/null 2>&1 &
waitfor -m $A
echo "first screen, from cache: $(wait_visible) ms"
quit_app

rm -rf "$DIR" "$CACHE"
if [ -f "$SETTINGS.big-backup" ]; then
	mv -f "$SETTINGS.big-backup" "$SETTINGS"
else
	rm -f "$SETTINGS"
fi
