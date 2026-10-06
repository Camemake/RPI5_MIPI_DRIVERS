# SmartSens SC233HGS on Raspberry Pi 5

This folder is the Raspberry Pi 5 driver for the Camemake module that uses the [SmartSens SC233HGS](https://www.smartsenstech.com/en/mpage?id=212), a global-shutter sensor. The same part is in SmartSens's [global-shutter list](https://www.smartsenstech.com/en/gs_products). The module is the [SC233HGS 2MP FF for Raspberry Pi](https://www.camemake.eu/shop/cm-mipi-sc233hgs-rpi-sc233hgs-2mp-ff-for-raspberry-pi-1092).

The mode that streamed is 1920×1200, RAW10, BGGR, 4 MIPI lanes, 270 MHz link, on CAM/DISP 0. SmartSens lists a higher catalog frame rate. This folder is the mode that streamed on the Pi.

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
