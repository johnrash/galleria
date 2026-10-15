#!/bin/bash
# Development helper (runs on Haiku): takes the README screenshots and prints
# the window frames for cropping.
cd "$(dirname "$0")"
. ./hey-lib.sh
A=application/x-vnd.Galleria
APP=${1:-/boot/system/apps/Galleria}
DIR=${2:-/boot/home/GalleriaTest/images}
SETTINGS="$(finddir B_USER_SETTINGS_DIRECTORY)/Galleria settings"
[ -f "$SETTINGS" ] && mv -f "$SETTINGS" "$SETTINGS.shots-backup"

"$APP" "$DIR" >/dev/null 2>&1 &
waitfor -m $A
hey $A set Frame of Window 0 to "BRect(120,60,1000,700)" >/dev/null
hey $A Gtss of Window 0 with "size=int32(144)" >/dev/null
sleep 3
hey $A Gsli of Window 0 with "index=int32(9)" >/dev/null
hey $A set Active of Window 0 to "bool(true)" >/dev/null
sleep 1
take_screenshot /tmp/shot-browser.png
hey $A get Frame of Window 0 | sed -n 's/.*BRect(\(.*\))/browser \1/p'

hey $A Gopi of Window 0 with "index=int32(18)" >/dev/null
sleep 2
hey $A set Frame of Window 1 to "BRect(200,90,1080,720)" >/dev/null
hey $A Gzfw of Window 1 >/dev/null
hey $A set Active of Window 1 to "bool(true)" >/dev/null
sleep 1
take_screenshot /tmp/shot-viewer.png
hey $A get Frame of Window 1 | sed -n 's/.*BRect(\(.*\))/viewer \1/p'

hey $A quit >/dev/null
sleep 1
rm -f "$SETTINGS"
[ -f "$SETTINGS.shots-backup" ] && mv -f "$SETTINGS.shots-backup" "$SETTINGS"
true
