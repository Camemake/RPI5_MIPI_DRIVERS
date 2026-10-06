# Himax HM2170 on Raspberry Pi 5

This folder is the Raspberry Pi 5 driver for the Camemake module that uses the [Himax HM2170](https://www.himax.com.tw/products/cmos-image-sensor/image-sensors/). The module is the [HM2170 2MP FF for Raspberry Pi](https://www.camemake.eu/shop/cm-mipi-hm2170-rpi-hm2170-2mp-ff-for-raspberry-pi-1082).

The mode that streamed is 1928×1088, RAW10, GRBG, 2 MIPI lanes, 540 MHz link, on CAM/DISP 0. The tested module is revision D. Himax lists the HM2170 as a 1920×1080 sensor. This folder uses the 1928×1088 readout that streamed.

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
