# OMNIVISION OV9281 on Raspberry Pi 5

This folder is the Raspberry Pi 5 driver for the Camemake module that uses the [OMNIVISION OV9281](https://www.ovt.com/products/ov9281/), a 1-megapixel monochrome global-shutter sensor. The module is the [OV9281 1MP FF for Raspberry Pi](https://www.camemake.eu/shop/cm-mipi-ov9281-rpi-ov9281-1mp-ff-for-raspberry-pi-1107).

The mode that streamed is 1280×800, mono RAW10, 2 MIPI lanes, 450 MHz link, on CAM/DISP 0. OMNIVISION lists 120 frames per second for this size at a 24 MHz clock. This module's crystal is 27 MHz, so the same register list runs at 450 MHz. The Pi kernel also ships an `ov9282` driver for this chip. That driver wants 24 MHz. `install.sh` keeps it from binding.

## Use it

On the Raspberry Pi, from the top of this repository:

```bash
bash install.sh ov9281
sudo reboot
bash view.sh ov9281
```

Open the address the second command prints. It looks like `http://192.168.x.x:8090/`. The full steps, including how to plug the flex into CAM/DISP 0, are in the [main guide](../../README.md).

## What you should see

A gray picture. This sensor has no color filters. The preview keeps the bright part of each pixel and draws it as gray.

## For someone wiring this into their own software

I2C address 0x60, chip id 0x9281, 1280×800, 2 lanes, mono RAW10, link 450 MHz. The packed format is `Y10P`, and each line is 1600 bytes.

The rest of the numbers, including the chip id and the link frequency, are in [DETAILS.md](../../DETAILS.md).

Every register this driver writes, and every register it names, is in [REGISTERS.md](REGISTERS.md).
