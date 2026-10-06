# SC450AI on Raspberry Pi 5

This folder is the driver for the Camemake [SC450AI 4MP FF for Raspberry Pi](https://www.camemake.eu/shop/cm-mipi-sc450ai-rpi-sc450ai-4mp-ff-for-raspberry-pi-1096).

## Use it

On the Raspberry Pi, from the top of this repository:

```bash
bash install.sh sc450ai
sudo reboot
bash view.sh sc450ai
```

Open the address the second command prints. It looks like `http://192.168.x.x:8090/`. The full steps, including how to plug the flex into CAM/DISP 0, are in the [main guide](../../README.md).

## What you should see

A normal color picture of the scene in front of the lens.

## For someone wiring this into their own software

The register list matches this module's 27 MHz crystal. The mode is 2688×1520, 4 lanes, BGGR.

The rest of the numbers, including the chip id and the link frequency, are in [DETAILS.md](../../DETAILS.md).
