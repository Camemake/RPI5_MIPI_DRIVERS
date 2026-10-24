# onsemi AR0234CS on Raspberry Pi 5

This folder is the Raspberry Pi 5 driver for the Camemake color module that uses the [onsemi AR0234CS](https://www.onsemi.com/products/sensors/image-sensors/ar0234cs), a 2.3-megapixel global-shutter sensor. The module is the [AR0234CS 2MP FF for Raspberry Pi](https://www.camemake.eu/shop/cm-mipi-ar0234cs-rpi-ar0234cs-2mp-ff-for-raspberry-pi-1106).

The mode that streamed is 1920×1200, color RAW10, 2 MIPI lanes, 506.25 MHz link, on CAM/DISP 0. onsemi lists 120 frames per second for this size. That figure is the sensor's catalog rate. This module's crystal is 27 MHz, and the register list was written for 24 MHz, so the same PLL ratios run at 506.25 MHz. A 3 second capture on the Pi held 160 frames.

## Use it

On the Raspberry Pi, from the top of this repository:

```bash
bash install.sh ar0234cs
sudo reboot
bash view.sh ar0234cs
```

Open the address the second command prints. It looks like `http://192.168.x.x:8090/`. The full steps, including how to plug the flex into CAM/DISP 0, are in the [main guide](../../README.md).

## What you should see

A color picture at 1920×1200. The preview fills in the missing color at each pixel, so the tint is only approximate. The browser page updates a few times a second because each frame is the full picture.

## For someone wiring this into their own software

I2C address 0x10, chip id 0x0a56, 1920×1200, 2 lanes, GRBG RAW10, link 506.25 MHz. The packed format is `pgAA`, and each line is 2400 bytes. Register values are 16 bits. The mono AR0234 answers with chip id 0x1a56, and this driver does not use that part.

The rest of the numbers, including the chip id and the link frequency, are in [DETAILS.md](../../DETAILS.md).

Every register this driver writes, and every register it names, is in [REGISTERS.md](REGISTERS.md).
