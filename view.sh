#!/bin/bash
# Open the browser preview for one camera. Run this after install.sh and a reboot.
# Usage:  bash view.sh imx586
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
SENSOR="${1:-}"
if [ -z "$SENSOR" ] || [ ! -d "$ROOT/cameras/$SENSOR" ]; then
  echo "Name the camera, for example:  bash view.sh imx586"
  exit 1
fi

if ! dmesg | grep -q "$SENSOR"; then
  echo "The kernel has not reported $SENSOR."
  echo "Check the flex is in the CAM/DISP 0 connector, then run:"
  echo "  bash install.sh $SENSOR"
  echo "  sudo reboot"
  exit 1
fi

if [ ! -x "$ROOT/cameras/$SENSOR/livecap" ]; then
  gcc -O2 -o "$ROOT/cameras/$SENSOR/livecap" "$ROOT/cameras/$SENSOR/livecap.c"
fi

IP="$(hostname -I 2>/dev/null | awk '{print $1}')"
echo
echo "Leave this window open. The picture is at:"
echo "  http://${IP:-<pi-address>}:8090/"
echo
echo "Stop the preview with Ctrl+C."
echo
cd "$ROOT/cameras/$SENSOR"
exec python3 -u live.py
