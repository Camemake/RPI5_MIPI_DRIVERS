#!/bin/bash
# Install one Camemake camera driver on a Raspberry Pi 5.
# Usage, from this folder:  bash install.sh imx586
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

if [ "$(uname -m)" != "aarch64" ]; then
  echo "Run this on the Raspberry Pi 5, not on a Windows or Mac computer."
  exit 1
fi

SENSOR="${1:-}"
if [ -z "$SENSOR" ] || [ ! -d "$ROOT/cameras/$SENSOR" ]; then
  echo "Which camera is plugged in? Use one of these names:"
  echo
  for dir in "$ROOT"/cameras/*; do
    [ -d "$dir" ] || continue
    echo "  bash install.sh $(basename "$dir")"
  done
  exit 1
fi

CAM="$ROOT/cameras/$SENSOR"
echo "Installing $SENSOR"
echo "This will ask for your Raspberry Pi password."
echo

sudo apt-get update
sudo apt-get install -y build-essential device-tree-compiler v4l-utils \
  python3-numpy python3-pil

if [ ! -d "/lib/modules/$(uname -r)/build" ]; then
  sudo apt-get install -y "linux-headers-$(uname -r)" \
    || sudo apt-get install -y raspberrypi-kernel-headers
fi
if [ ! -d "/lib/modules/$(uname -r)/build" ]; then
  echo
  echo "The kernel headers for $(uname -r) are not installed."
  echo "Update the Pi, reboot, and run this script again:"
  echo "  sudo apt-get update && sudo apt-get full-upgrade"
  echo "  sudo reboot"
  exit 1
fi

make -C "$CAM" -j"$(nproc)"
dtc -@ -I dts -O dtb -o "$CAM/$SENSOR.dtbo" "$CAM/$SENSOR-overlay.dts"
gcc -O2 -o "$CAM/livecap" "$CAM/livecap.c"

sudo make -C "/lib/modules/$(uname -r)/build" M="$CAM" modules_install
sudo depmod -a

if [ -d /boot/firmware/overlays ]; then
  CFG=/boot/firmware/config.txt
  OVERLAYS=/boot/firmware/overlays
else
  CFG=/boot/config.txt
  OVERLAYS=/boot/overlays
fi

sudo cp "$CAM/$SENSOR.dtbo" "$OVERLAYS/$SENSOR.dtbo"
sudo cp "$CFG" "$CFG.bak-camemake"

sudo SENSOR="$SENSOR" CFG="$CFG" python3 - << 'PY'
import os
from pathlib import Path
sensor = os.environ["SENSOR"]
path = Path(os.environ["CFG"])
names = {
    "sc450ai", "ox05b1s", "gc4023", "hm2170",
    "sc233hgs", "imx675", "imx586",
}
kept = []
for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
    stripped = line.strip()
    if stripped.startswith("camera_auto_detect="):
        continue
    if stripped.startswith("dtoverlay="):
        value = stripped.split("=", 1)[1].split("#", 1)[0].strip()
        overlay = value.split(",", 1)[0]
        if overlay in names or any(overlay.startswith(name) for name in names):
            continue
    kept.append(line)
if kept and kept[-1] != "":
    kept.append("")
kept.append("camera_auto_detect=0")
kept.append(f"dtoverlay={sensor}")
path.write_text("\n".join(kept) + "\n", encoding="utf-8")
PY

echo
echo "Installed. Only one camera overlay can be active, and this one is $SENSOR."
echo "A copy of the old boot file is $CFG.bak-camemake"
echo
echo "Turn the Pi off if you still need to seat the flex. Otherwise reboot now:"
echo "  sudo reboot"
echo
echo "After the Pi is back, from this same folder run:"
echo "  bash view.sh $SENSOR"
echo "Then open http://<the-pi-address>:8090/ in a browser on the same network."
