# IMX675 on Raspberry Pi 5

This folder is the driver for the Camemake [IMX675 5MP FF for Raspberry Pi](https://www.camemake.eu/shop/cm-mipi-imx675-rpi-imx675-5mp-ff-for-raspberry-pi-1105).

## Use it

On the Raspberry Pi, from the top of this repository:

```bash
bash install.sh imx675
sudo reboot
bash view.sh imx675
```

Open the address the second command prints. It looks like `http://192.168.x.x:8090/`. The full steps, including how to plug the flex into CAM/DISP 0, are in the [main guide](../../README.md).

## What you should see

A normal color picture.

## For someone wiring this into their own software

I2C 0x1a, module id 0x96, 2608×1960, 2 lanes, GRBG, link 800 MHz.

The rest of the numbers, including the chip id and the link frequency, are in [DETAILS.md](../../DETAILS.md).
