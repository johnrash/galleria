#!/bin/bash
# Development helper (runs on Haiku): enlarges the browser window, causes a
# layout update, then makes the window small again and prints the frames.
# The settings file is left unchanged.
#   bash tests/resize-check.sh [path/to/Galleria]
cd "$(dirname "$0")"
A=application/x-vnd.Galleria
APP=${1:-../objects.x86_64-cc13-release/Galleria}
SETTINGS="$(finddir B_USER_SETTINGS_DIRECTORY)/Galleria settings"
frame() { hey $A get Frame of Window 0 | sed -n 's/.*BRect(\(.*\))/\1/p'; }

[ -f "$SETTINGS" ] && cp -p "$SETTINGS" /tmp/resize-check-settings
"$APP" /boot/home/GalleriaTest/images >/dev/null 2>&1 &
waitfor -m $A
sleep 1
hey $A set Frame of Window 0 to "BRect(5,29,1100,780)" >/dev/null
hey $A Gsli of Window 0 with "index=int32(3)" >/dev/null
sleep 0.5
echo "enlarged:          $(frame)"
hey $A set Frame of Window 0 to "BRect(120,120,620,470)" >/dev/null
echo "asked for (120, 120, 620, 470): $(frame)"
hey $A quit >/dev/null
while roster | grep -q "($A)"; do sleep 0.1; done
if [ -f /tmp/resize-check-settings ]; then
	cp -p /tmp/resize-check-settings "$SETTINGS"
	rm /tmp/resize-check-settings
fi
