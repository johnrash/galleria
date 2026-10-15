#!/bin/bash
# Development helper (runs on Haiku): starts Galleria with the current
# settings file, prints the browser frame, presses the zoom button twice and
# prints the frame after each press. The settings file is left unchanged.
#   bash tests/zoom-check.sh [path/to/Galleria]
cd "$(dirname "$0")"
A=application/x-vnd.Galleria
APP=${1:-../objects.x86_64-cc13-release/Galleria}
SETTINGS="$(finddir B_USER_SETTINGS_DIRECTORY)/Galleria settings"
frame() { hey $A get Frame of Window 0 | sed -n 's/.*BRect(\(.*\))/\1/p'; }

cp -p "$SETTINGS" /tmp/zoom-check-settings
"$APP" >/tmp/zoom-check.log 2>&1 &
waitfor -m $A
sleep 1
echo "screen:        $(screenmode)"
echo "opened at:     $(frame)"
hey $A _WZM of Window 0 >/dev/null
echo "zoom pressed:  $(frame)"
hey $A _WZM of Window 0 >/dev/null
echo "zoom pressed:  $(frame)"
hey $A quit >/dev/null
while roster | grep -q "($A)"; do sleep 0.1; done
cp -p /tmp/zoom-check-settings "$SETTINGS"
rm /tmp/zoom-check-settings
