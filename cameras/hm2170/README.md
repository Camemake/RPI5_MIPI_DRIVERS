# HM2170 on Raspberry Pi 5

This folder is the driver for the Camemake [HM2170 2MP FF for Raspberry Pi](https://www.camemake.eu/shop/cm-mipi-hm2170-rpi-hm2170-2mp-ff-for-raspberry-pi-1082).

## Use it

On the Raspberry Pi, from the top of this repository:

```bash
bash install.sh hm2170
sudo reboot
bash view.sh hm2170
```

Open the address the second command prints. It looks like `http://192.168.x.x:8090/`. The full steps, including how to plug the flex into CAM/DISP 0, are in the [main guide](../../README.md).

## What you should see

A normal color picture.

## For someone wiring this into their own software

I2C address 0x25. The tested module is revision D. The mode is 1928×1088, 2 lanes, GRBG, link 540 MHz.

The rest of the numbers, including the chip id and the link frequency, are in [DETAILS.md](../../DETAILS.md).
