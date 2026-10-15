#!/bin/bash
# End-to-end test of Galleria, driven with hey. Runs on Haiku:
#   bash tests/gui-test.sh [path/to/Galleria] [image folder]
#
# The image folder is made with tests/make-test-images.py. The test works on
# a copy in /tmp, saves and restores the settings file, and leaves the
# thumbnail cache empty.

cd "$(dirname "$0")"
. ./hey-lib.sh

APP_BINARY=${1:-$(cd .. && pwd)/objects.x86_64-cc13-release/Galleria}
SOURCE=${2:-/boot/home/GalleriaTest/images}
A=application/x-vnd.Galleria
HEY_APP=$A
TESTDIR=$(cd /tmp && pwd -P)/galleria-test
SETTINGS="$(finddir B_USER_SETTINGS_DIRECTORY)/Galleria settings"
CACHE="$(finddir B_USER_CACHE_DIRECTORY)/Galleria"
SCREENSHOTS=${SCREENSHOTS:-}
SENDKEY=/tmp/galleria-sendkey

PASSED=0
FAILED=0

check() {
	if [ "$2" = "$3" ]; then
		PASSED=$((PASSED + 1))
		echo "ok   $1"
	else
		FAILED=$((FAILED + 1))
		echo "FAIL $1"
		echo "     expected: $2"
		echo "     actual:   $3"
	fi
}

# field <window> <name>: one field of the window's state reply
field() {
	hey $A Gsta of Window "$1" 2>/dev/null \
		| sed -n "s/^ *\"$2\" ([A-Z_0-9]*) : //p" | head -1 \
		| sed 's/^"\(.*\)"$/\1/; s/ (0x[0-9a-fA-F-]*)$//'
}

names() {
	hey $A Gsta of Window 0 | sed -n 's/^ *"names" (B_STRING_TYPE) : "\(.*\)"$/\1/p' \
		| tr '\n' '|'
}

join() { local IFS='|'; echo "$*|"; }

# wait_until <seconds> <command...>: polls until the command succeeds
wait_until() {
	local limit=$(($1 * 10))
	shift
	for ((i = 0; i < limit; i++)); do
		"$@" && return 0
		sleep 0.1
	done
	return 1
}

scan_done() { [ "$(field 0 scanning)" = "FALSE" ]; }
thumbs_done() { [ "$(field 0 pending)" = "0" ] && scan_done; }
visible_done() {
	scan_done && [ "$(field 0 visible)" -gt 0 ] \
		&& [ "$(field 0 visible)" = "$(field 0 visibleDone)" ]
}
viewer_loaded() { [ "$(field 1 loading)" = "FALSE" ]; }
window_count() { hey $A count Window | hey_result; }
app_running() { roster | grep -q "($A)"; }
app_gone() { ! app_running; }
# frame <window>: "left top right bottom"
frame() {
	hey $A get Frame of Window "$1" | sed -n 's/.*BRect(\(.*\))/\1/p' \
		| tr -d ',' | awk '{ printf "%d %d %d %d", $1, $2, $3, $4 }'
}
width_of() { echo "$1" | awk '{ print $3 - $1 }'; }

quit_app() {
	hey $A quit >/dev/null 2>&1
	wait_until 10 app_gone
}

snapshot() {
	[ -n "$SCREENSHOTS" ] && take_screenshot "$SCREENSHOTS/$1.png"
}

cleanup() {
	quit_app
	rm -rf "$TESTDIR" "$CACHE"
	if [ -f "$SETTINGS.test-backup" ]; then
		mv -f "$SETTINGS.test-backup" "$SETTINGS"
	else
		rm -f "$SETTINGS"
	fi
}
trap cleanup EXIT

# --- set up -----------------------------------------------------------------

g++ -I/boot/system/develop/headers/private/app -o $SENDKEY sendkey.cpp -lbe \
	|| exit 1
key() { $SENDKEY $A "$@" && sleep 0.2; }

quit_app
[ -f "$SETTINGS" ] && mv -f "$SETTINGS" "$SETTINGS.test-backup"
rm -rf "$CACHE" "$TESTDIR"
mkdir -p "$TESTDIR"

NAME_ORDER=(alpha.png berry.webp broken.jpg desert.gif grey.ppm icon.ico
	IMG_1.jpg IMG_2.jpg IMG_10.jpg IMG_20.jpg lake.png large.jpg layers.psd
	meadow.tga night.jp2 pink.avif portrait-exif.jpg sunset.jpg tall.png
	teal.tiff tiny.png untitled violet.bmp)
IMAGES=${#NAME_ORDER[@]}

# created in reverse name order, so that creation order differs from names
for ((i = IMAGES - 1; i >= 0; i--)); do
	cp "$SOURCE/${NAME_ORDER[$i]}" "$TESTDIR/"
done
cp "$SOURCE/notes.txt" "$TESTDIR/"
cp -r "$SOURCE/More photos" "$TESTDIR/"

# modification times in yet another order
MTIME_ORDER=(tall.png sunset.jpg IMG_20.jpg alpha.png violet.bmp large.jpg
	berry.webp tiny.png broken.jpg night.jp2 IMG_2.jpg desert.gif lake.png
	untitled grey.ppm pink.avif IMG_1.jpg teal.tiff icon.ico meadow.tga
	portrait-exif.jpg layers.psd IMG_10.jpg)
for ((i = 0; i < ${#MTIME_ORDER[@]}; i++)); do
	touch -d "2021-03-01 10:00:$(printf %02d $i)" "$TESTDIR/${MTIME_ORDER[$i]}"
done

CREATION_ORDER=()
for ((i = IMAGES - 1; i >= 0; i--)); do
	CREATION_ORDER+=("${NAME_ORDER[$i]}")
done

SIZE_ORDER=($(for f in "${NAME_ORDER[@]}"; do
	echo "$(stat -c %s "$TESTDIR/$f") $f"; done | sort -n -k1,1 | cut -d' ' -f2))
SIZE_ORDER_DESC=()
for ((i = IMAGES - 1; i >= 0; i--)); do
	SIZE_ORDER_DESC+=("${SIZE_ORDER[$i]}")
done

TYPE_ORDER=(pink.avif violet.bmp desert.gif icon.ico untitled night.jp2
	broken.jpg IMG_1.jpg IMG_2.jpg IMG_10.jpg IMG_20.jpg large.jpg
	portrait-exif.jpg sunset.jpg alpha.png lake.png tall.png tiny.png
	grey.ppm layers.psd meadow.tga teal.tiff berry.webp)

FOLDER="More photos"

# --- browser ----------------------------------------------------------------

echo "== browser"
"$APP_BINARY" "$TESTDIR" >/tmp/galleria-test.log 2>&1 &
waitfor -m $A
wait_until 20 thumbs_done

check "window title" "Galleria: galleria-test" "$(field 0 title)"
check "folder path" "$TESTDIR" "$(field 0 path)"
check "items (images + 1 folder, no text file)" "$((IMAGES + 1))" "$(field 0 items)"
check "images" "$IMAGES" "$(field 0 images)"
check "status line" "$IMAGES images, 1 folder" "$(field 0 status)"
check "natural name order, folder first" "$(join "$FOLDER" "${NAME_ORDER[@]}")" "$(names)"
check "default sort labels" "Name Ascending" "$(field 0 sortLabel) $(field 0 orderLabel)"

# every readable image gets a thumbnail; the corrupt one does not
check "thumbnails made" "$((IMAGES - 1))" "$(field 0 loaded)"
# cached: all but the broken file and the two images smaller than 256 px
check "cache entries (256 px class)" "$((IMAGES - 3))" \
	"$(ls "$CACHE/256" 2>/dev/null | grep -c '\.png$')"
if [ "$(ls "$CACHE/256" 2>/dev/null | grep -c '\.png$')" != "$((IMAGES - 3))" ]; then
	ls -la "$CACHE/256"
	for f in "$CACHE"/256/*; do catattr -d Galleria:path "$f"; echo; done
fi
first=$(ls "$CACHE/256" | grep '\.png$' | head -1)
check "cache entry records its source" "$TESTDIR" \
	"$(dirname "$(catattr -d Galleria:path "$CACHE/256/$first" 2>/dev/null)")"
snapshot browser-name

echo "== sorting"
hey $A Gsby of Window 0 with "key=int32(3)" >/dev/null
check "size ascending" "$(join "$FOLDER" "${SIZE_ORDER[@]}")" "$(names)"
check "sort field label" "Size" "$(field 0 sortLabel)"
hey $A Gsor of Window 0 with "descending=bool(true)" >/dev/null
check "size descending (folder stays first)" \
	"$(join "$FOLDER" "${SIZE_ORDER_DESC[@]}")" "$(names)"
check "order field label" "Descending" "$(field 0 orderLabel)"
hey $A Gsor of Window 0 with "descending=bool(false)" >/dev/null
hey $A Gsby of Window 0 with "key=int32(1)" >/dev/null
check "modification date" "$(join "$FOLDER" "${MTIME_ORDER[@]}")" "$(names)"
hey $A Gsby of Window 0 with "key=int32(2)" >/dev/null
check "creation date" "$(join "$FOLDER" "${CREATION_ORDER[@]}")" "$(names)"
hey $A Gsby of Window 0 with "key=int32(4)" >/dev/null
check "type" "$(join "$FOLDER" "${TYPE_ORDER[@]}")" "$(names)"
hey $A Gsby of Window 0 with "key=int32(0)" >/dev/null
check "back to name order" "$(join "$FOLDER" "${NAME_ORDER[@]}")" "$(names)"

echo "== thumbnail size"
hey $A Gtss of Window 0 with "size=int32(300)" >/dev/null
check "size set" "300 300" "$(field 0 thumbnailSize) $(field 0 slider)"
# thumbnails are made for the visible rows and a few screens around them
wait_until 30 visible_done
check "large thumbnails for the visible images" "$(field 0 visible)" \
	"$(field 0 visibleDone)"
largest=$(python3 -c '
import struct, sys, glob
sizes = set()
for name in glob.glob(sys.argv[1] + "/*.png"):
    with open(name, "rb") as f:
        w, h = struct.unpack(">II", f.read(24)[16:24])
    sizes.add(max(w, h))
print(" ".join(str(s) for s in sorted(sizes)))' "$CACHE/512")
check "512 px class thumbnails are 512 px" "512" "$largest"
snapshot browser-large
hey $A Gtst of Window 0 with "delta=int32(-1)" >/dev/null
check "smaller step snaps to 32" "288 288" "$(field 0 thumbnailSize) $(field 0 slider)"
hey $A Gtss of Window 0 with "size=int32(1000)" >/dev/null
check "size is limited" "512" "$(field 0 thumbnailSize)"
hey $A Gtss of Window 0 with "size=int32(96)" >/dev/null
wait_until 30 thumbs_done
check "small thumbnails made" "$((IMAGES - 1))" "$(field 0 loaded)"
snapshot browser-small

echo "== selection and status"
hey $A Gsli of Window 0 with "index=int32(17)" >/dev/null
check "selected" "17" "$(field 0 selection)"
status=$(field 0 status)
case "$status" in
	"portrait-exif.jpg  •  600 × 800  •  "*) check "status shows EXIF corrected size" 1 1 ;;
	*) check "status shows EXIF corrected size" "portrait-exif.jpg  •  600 × 800  •  ..." "$status" ;;
esac

echo "== folders"
hey $A Gopi of Window 0 with "index=int32(0)" >/dev/null
wait_until 10 scan_done
check "opened subfolder" "$TESTDIR/$FOLDER" "$(field 0 path)"
check "subfolder items" "3" "$(field 0 items)"
hey $A Gpar of Window 0 >/dev/null
wait_until 10 scan_done
check "back in parent" "$TESTDIR" "$(field 0 path)"
check "came-from folder selected" "0" "$(field 0 selection)"
hey $A Ggto of Window 0 with "path=\"$TESTDIR/sunset.jpg\"" >/dev/null
wait_until 10 scan_done
check "location of a file selects it" "sunset.jpg" \
	"$(names | cut -d'|' -f$(( $(field 0 selection) + 1 )))"

echo "== live updates"
cp "$SOURCE/sunset.jpg" "$TESTDIR/zz-new.jpg"
wait_until 5 eval '[ "$(field 0 items)" = "$((IMAGES + 2))" ]'
check "new file appears" "$((IMAGES + 2))" "$(field 0 items)"
rm "$TESTDIR/zz-new.jpg"
wait_until 5 eval '[ "$(field 0 items)" = "$((IMAGES + 1))" ]'
check "deleted file disappears" "$((IMAGES + 1))" "$(field 0 items)"

# --- viewer -----------------------------------------------------------------

echo "== viewer"
hey $A Gopi of Window 0 with "index=int32(19)" >/dev/null
wait_until 10 eval '[ "$(window_count)" = "2" ]'
wait_until 10 viewer_loaded
check "viewer opened" "2" "$(window_count)"
check "viewer image" "$TESTDIR/tall.png" "$(field 1 path)"
check "position among images" "19 23" "$(( $(field 1 index) + 1 )) $(field 1 count)"
check "fits the window by default" "0" "$(field 1 zoomMode)"
check "title" "tall.png — 900 × 6000 — 12% — 19 / 23" "$(field 1 title)"
snapshot viewer-fit

hey $A Gz11 of Window 1 >/dev/null
check "original size" "2 1.000" "$(field 1 zoomMode) $(field 1 scale)"
hey $A Gscr of Window 1 with "y=float(-100000)" >/dev/null
check "scrolled to the top" "0.000" "$(field 1 scrollY)"
for n in 1 2 3; do
	hey $A _MWC of View image of Window 1 with "be:wheel_delta_y=float(1)" >/dev/null
done
check "mouse wheel scrolls down" "192.000" "$(field 1 scrollY)"
hey $A _MWC of View image of Window 1 with "be:wheel_delta_y=float(-1)" >/dev/null
check "mouse wheel scrolls up" "128.000" "$(field 1 scrollY)"
hey $A _MWC of View image of Window 1 with "be:wheel_delta_y=float(0.5)" >/dev/null
check "trackpad (fractional) scrolling" "160.000" "$(field 1 scrollY)"
snapshot viewer-100

hey $A Gzin of Window 1 >/dev/null
check "zoom in" "1.250" "$(field 1 scale)"
hey $A Gzou of Window 1 >/dev/null
hey $A Gzou of Window 1 >/dev/null
check "zoom out twice" "0.750" "$(field 1 scale)"
check "zoom is remembered" "2 0.750" "$(field 1 settingsZoomMode) $(field 1 settingsZoomFactor)"

hey $A Gnxt of Window 1 >/dev/null
wait_until 10 viewer_loaded
check "next image" "$TESTDIR/teal.tiff" "$(field 1 path)"
check "zoom sticks for the next image" "2 0.750" "$(field 1 zoomMode) $(field 1 scale)"
check "next image starts at the top" "0.000" "$(field 1 scrollY)"
check "browser follows the viewer" "20" "$(field 0 selection)"
hey $A Gprv of Window 1 >/dev/null
wait_until 10 viewer_loaded
check "previous image (from cache)" "$TESTDIR/tall.png 0.750" "$(field 1 path) $(field 1 scale)"

hey $A Gzfx of Window 1 >/dev/null
check "fit width" "1" "$(field 1 zoomMode)"
check "fit width does not enlarge small images" "1.000" "$(field 1 scale)"
hey $A Genl of Window 1 >/dev/null
check "enlarged to the window width" \
	"$(awk "BEGIN { printf \"%.3f\", $(field 1 viewWidth) / 900 }")" \
	"$(field 1 scale)"
hey $A Genl of Window 1 >/dev/null
hey $A Glst of Window 1 >/dev/null
wait_until 10 viewer_loaded
check "last image" "$TESTDIR/violet.bmp" "$(field 1 path)"
check "fit width sticks" "1" "$(field 1 zoomMode)"
hey $A Gfst of Window 1 >/dev/null
wait_until 10 viewer_loaded
check "first image" "$TESTDIR/alpha.png" "$(field 1 path)"

# a corrupt file shows a message instead of an image
hey $A Gopi of Window 0 with "index=int32(3)" >/dev/null
wait_until 10 viewer_loaded
check "corrupt image: shared viewer reused" "2" "$(window_count)"
check "corrupt image has no size" "$TESTDIR/broken.jpg 0" "$(field 1 path) $(field 1 imageWidth)"
snapshot viewer-broken

hey $A Gopi of Window 0 with "index=int32(17)" >/dev/null
wait_until 10 viewer_loaded
check "EXIF orientation applied" "600 800" "$(field 1 imageWidth) $(field 1 imageHeight)"

hey $A Gfsc of Window 1 >/dev/null
check "full screen" "TRUE" "$(field 1 fullScreen)"
screen=$(screenmode 2>/dev/null | sed -n 's/^Resolution: \([0-9]*\) \([0-9]*\),.*/\1 \2/p')
check "full screen covers the screen" "$screen" \
	"$(printf '%.0f %.0f' "$(field 1 viewWidth)" "$(field 1 viewHeight)")"
snapshot viewer-fullscreen
hey $A Gfsc of Window 1 >/dev/null
check "windowed again" "FALSE" "$(field 1 fullScreen)"
viewer_frame=$(frame 1)
hey $A _WZM of Window 1 >/dev/null
check "viewer zoom button maximizes" "yes" \
	"$([ "$(width_of "$(frame 1)")" -gt "$(width_of "$viewer_frame")" ] && echo yes || echo no)"
hey $A _WZM of Window 1 >/dev/null
check "viewer zoom button restores the size" "$viewer_frame" "$(frame 1)"

hey $A Gopi of Window 0 with "index=int32(18)" and "newWindow=bool(true)" >/dev/null
wait_until 10 eval '[ "$(window_count)" = "3" ]'
check "open in new window" "3" "$(window_count)"
hey $A quit Window 2 >/dev/null
hey $A quit Window 1 >/dev/null
wait_until 10 eval '[ "$(window_count)" = "1" ]'
check "viewers closed" "1" "$(window_count)"

echo "== keyboard"
hey $A Gsli of Window 0 with "index=int32(1)" >/dev/null
key 0 right
check "Right selects the next item" "2" "$(field 0 selection)"
key 0 down
below=$(field 0 selection)
key 0 up
check "Down and Up move by rows" "yes 2" \
	"$([ "$below" -gt 3 ] && echo yes || echo no) $(field 0 selection)"
key 0 end
check "End selects the last item" "$IMAGES" "$(field 0 selection)"
key 0 home
check "Home selects the first item" "0" "$(field 0 selection)"
key 0 enter
wait_until 10 scan_done
check "Enter opens a folder" "$TESTDIR/$FOLDER" "$(field 0 path)"
key 0 backspace
wait_until 10 scan_done
check "Backspace goes to the parent" "$TESTDIR 0" \
	"$(field 0 path) $(field 0 selection)"
key 0 right enter
wait_until 10 eval '[ "$(window_count)" = "2" ]'
wait_until 10 viewer_loaded
check "Enter opens an image" "$TESTDIR/alpha.png" "$(field 1 path)"

key 1 1
check "1: original size" "2 1.000" "$(field 1 zoomMode) $(field 1 scale)"
key 1 -
check "-: zoom out" "0.750" "$(field 1 scale)"
key 1 +
check "+: zoom in" "1.000" "$(field 1 scale)"
key 1 w
check "W: fit width" "1" "$(field 1 zoomMode)"
key 1 f
check "F: fit to window" "0" "$(field 1 zoomMode)"
key 1 right
wait_until 10 viewer_loaded
check "Right: next image" "berry.webp" "$(basename "$(field 1 path)")"
key 1 space
wait_until 10 viewer_loaded
check "Space: next image" "broken.jpg" "$(basename "$(field 1 path)")"
key 1 backspace
wait_until 10 viewer_loaded
check "Backspace: previous image" "berry.webp" "$(basename "$(field 1 path)")"
key 1 left
wait_until 10 viewer_loaded
check "Left: previous image" "alpha.png" "$(basename "$(field 1 path)")"
key 1 end
wait_until 10 viewer_loaded
check "End: last image" "violet.bmp" "$(basename "$(field 1 path)")"
key 1 home
wait_until 10 viewer_loaded
check "Home: first image" "alpha.png" "$(basename "$(field 1 path)")"

hey $A Gopi of Window 0 with "index=int32(19)" >/dev/null
wait_until 10 viewer_loaded
key 1 1
hey $A Gscr of Window 1 with "y=float(-100000)" >/dev/null
key 1 down
check "Down scrolls" "48.000" "$(field 1 scrollY)"
page=$(awk "BEGIN { printf \"%d\", $(field 1 viewHeight) * 0.9 }")
key 1 pagedown
check "Page Down scrolls a page" "$((48 + page)).000" "$(field 1 scrollY)"
key 1 up
check "Up scrolls back" "$((page)).000" "$(field 1 scrollY)"
key 1 enter
check "Enter: full screen" "TRUE" "$(field 1 fullScreen)"
key 1 escape
check "Escape leaves full screen" "FALSE" "$(field 1 fullScreen)"
key 1 w
key 1 escape
wait_until 10 eval '[ "$(window_count)" = "1" ]'
check "Escape closes the viewer" "1" "$(window_count)"

echo "== cache"
hey $A Gccc of Window 0 with "which=int32(1)" >/dev/null
check "cache cleared" "no" "$([ -e "$CACHE" ] && echo yes || echo no)"

# --- persistence ------------------------------------------------------------

echo "== zoom button"
SCREEN_WIDTH=$(screenmode | sed -n 's/^Resolution: \([0-9]*\) .*/\1/p')
hey $A set Frame of Window 0 to "BRect(100,100,800,600)" >/dev/null
check "normal frame" "100 100 800 600" "$(frame 0)"
hey $A _WZM of Window 0 >/dev/null
zoomed=$(frame 0)
check "zoom button maximizes" "yes" \
	"$([ "$(width_of "$zoomed")" -gt $((SCREEN_WIDTH * 3 / 4)) ] && echo yes || echo no)"
hey $A _WZM of Window 0 >/dev/null
check "zoom button restores the size" "100 100 800 600" "$(frame 0)"
hey $A _WZM of Window 0 >/dev/null
check "zoomed again" "$zoomed" "$(frame 0)"

echo "== settings"
hey $A Gsby of Window 0 with "key=int32(1)" >/dev/null
hey $A Gsor of Window 0 with "descending=bool(true)" >/dev/null
hey $A Gtss of Window 0 with "size=int32(224)" >/dev/null
quit_app
check "settings saved" "yes" "$([ -f "$SETTINGS" ] && echo yes || echo no)"

"$APP_BINARY" >/tmp/galleria-test.log 2>&1 &
waitfor -m $A
wait_until 20 scan_done
check "reopens the last folder" "$TESTDIR" "$(field 0 path)"
check "sorting restored" "1 TRUE" "$(field 0 sortKey) $(field 0 descending)"
check "thumbnail size restored" "224 224" "$(field 0 thumbnailSize) $(field 0 slider)"
check "reopens zoomed" "$zoomed" "$(frame 0)"
hey $A _WZM of Window 0 >/dev/null
check "zoom button still restores the size after a restart" \
	"100 100 800 600" "$(frame 0)"

# The thumbnail grid used to report its current size as its minimum size, so
# the window could not be made smaller than it had been (after a layout
# update, like a new status line). Resizing must still work after zooming.
hey $A _WZM of Window 0 >/dev/null
hey $A Gsli of Window 0 with "index=int32(3)" >/dev/null
hey $A Gsli of Window 0 with "index=int32(4)" >/dev/null
hey $A set Frame of Window 0 to "BRect(120,120,620,470)" >/dev/null
check "a zoomed window can be resized smaller by hand" "120 120 620 470" \
	"$(frame 0)"

# A window that has the zoomed size without being zoomed (how Galleria 1.0.0
# saved a zoomed window, or enlarged by hand) used to open at that size with
# a zoom button that could only maximize. The button must make it smaller.
hey $A set Frame of Window 0 to "BRect($(echo $zoomed | tr ' ' ','))" >/dev/null
quit_app
"$APP_BINARY" >/tmp/galleria-test.log 2>&1 &
waitfor -m $A
wait_until 20 scan_done
check "zoomed-size window reopens at that size" "$zoomed" "$(frame 0)"
hey $A _WZM of Window 0 >/dev/null
smaller=$(frame 0)
check "zoom button makes it smaller" "yes" \
	"$([ "$(width_of "$smaller")" -lt "$(width_of "$zoomed")" ] && echo yes || echo no)"
hey $A _WZM of Window 0 >/dev/null
check "and zooms it again" "$zoomed" "$(frame 0)"
hey $A _WZM of Window 0 >/dev/null
check "and back" "$smaller" "$(frame 0)"
quit_app

# --- launching with an image ------------------------------------------------

echo "== open an image from Tracker or the command line"
"$APP_BINARY" "$TESTDIR/IMG_10.jpg" >/tmp/galleria-test.log 2>&1 &
waitfor -m $A
wait_until 10 eval '[ "$(field 0 count)" = "$IMAGES" ]'
check "only the viewer opens" "1" "$(window_count)"
check "whole folder to browse" "$IMAGES" "$(field 0 count)"
check "image and zoom from last time" "$TESTDIR/IMG_10.jpg 1" \
	"$(field 0 path) $(field 0 zoomMode)"
# sorted like the browser was: modification date, descending
check "position in the folder's sort order" \
	"$(( IMAGES - 1 - 22 ))" "$(field 0 index)"
hey $A Gsib of Window 0 >/dev/null
wait_until 10 eval '[ "$(window_count)" = "2" ]'
wait_until 10 eval '[ "$(field 1 scanning)" = "FALSE" ]'
check "show in browser opens the folder" "$TESTDIR" "$(field 1 path)"
check "with the image selected" "IMG_10.jpg" \
	"$(hey $A Gsta of Window 1 | sed -n 's/^ *"status" (B_STRING_TYPE) : "\([^ ]*\) .*/\1/p')"

echo
echo "$PASSED passed, $FAILED failed"
[ $FAILED -eq 0 ]
