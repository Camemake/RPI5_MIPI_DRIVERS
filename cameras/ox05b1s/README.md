# OX05B1S on Raspberry Pi 5

This folder is the driver for the Camemake [OX05B1S 5MP FF for Raspberry Pi](https://www.camemake.eu/shop/cm-mipi-ox05b1s-rpi-ox05b1s-5mp-ff-for-raspberry-pi-1088).

## Use it

On the Raspberry Pi, from the top of this repository:

```bash
bash install.sh ox05b1s
sudo reboot
bash view.sh ox05b1s
```

Open the address the second command prints. It looks like `http://192.168.x.x:8090/`. The full steps, including how to plug the flex into CAM/DISP 0, are in the [main guide](../../README.md).

## What you should see

A picture with a strong magenta cast. This sensor sees infrared as well as color, and the preview does not remove the infrared. Magenta means it is streaming.

## For someone wiring this into their own software

I2C address 0x36, chip id 0x580542, 2592×1944, 4 lanes, GRBG. The link is 450 MHz. Each line is padded to 3248 bytes.

The rest of the numbers, including the chip id and the link frequency, are in [DETAILS.md](../../DETAILS.md).
