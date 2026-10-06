# GalaxyCore GC4023 on Raspberry Pi 5

This folder is the Raspberry Pi 5 driver for the Camemake module that uses the GalaxyCore GC4023. GalaxyCore's public site is [gcoreinc.com](https://www.gcoreinc.com/). The module is the [GC4023 2.5MP FF for Raspberry Pi](https://www.camemake.eu/shop/cm-mipi-gc4023-rpi-gc4023-2-5mp-ff-for-raspberry-pi-1095).

The mode that streamed is 2560×1440, RAW10, RGGB, 2 MIPI lanes, 351 MHz link, on CAM/DISP 0. The chip answers at I2C 0x29.

## Use it

On the Raspberry Pi, from the top of this repository:

```bash
bash install.sh gc4023
sudo reboot
bash view.sh gc4023
```

Open the address the second command prints. It looks like `http://192.168.x.x:8090/`. The full steps, including how to plug the flex into CAM/DISP 0, are in the [main guide](../../README.md).

## What you should see

A normal color picture.

## For someone wiring this into their own software

This module answers at I2C 0x29, not 0x31. It is a 2-lane camera. The mode is 2560×1440, RGGB, link 351 MHz.

The rest of the numbers, including the chip id and the link frequency, are in [DETAILS.md](../../DETAILS.md).
