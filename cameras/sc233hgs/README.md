# SC233HGS on Raspberry Pi 5

This folder is the driver for the Camemake [SC233HGS 2MP FF for Raspberry Pi](https://www.camemake.eu/shop/cm-mipi-sc233hgs-rpi-sc233hgs-2mp-ff-for-raspberry-pi-1092).

## Use it

On the Raspberry Pi, from the top of this repository:

```bash
bash install.sh sc233hgs
sudo reboot
bash view.sh sc233hgs
```

Open the address the second command prints. It looks like `http://192.168.x.x:8090/`. The full steps, including how to plug the flex into CAM/DISP 0, are in the [main guide](../../README.md).

## What you should see

A normal color picture. The driver asks the sensor for each frame, because this cable has no separate sync wire. You do not set that up yourself.

## For someone wiring this into their own software

I2C 0x30, chip id 0xcb61, 1920×1200, 4 lanes, BGGR, link 270 MHz.

The rest of the numbers, including the chip id and the link frequency, are in [DETAILS.md](../../DETAILS.md).
