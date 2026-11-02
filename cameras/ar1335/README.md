# onsemi AR1335 on Raspberry Pi 5

This folder is the Raspberry Pi 5 driver for the Camemake module that uses the [onsemi AR1335](https://www.onsemi.com/products/sensors/image-sensors/ar1335). The module is the [AR1335 13MP FF for Raspberry Pi](https://www.camemake.eu/shop/cm-mipi-ar1335-rpi-ar1335-13mp-ff-for-raspberry-pi-1098).

The mode that streamed is 4208×3120, RAW10, GRBG, 4 MIPI lanes, 276 MHz link, on CAM/DISP 0. The crystal on the module is 24 MHz.

## Use it

On the Raspberry Pi, from the top of this repository:

```bash
bash install.sh ar1335
sudo reboot
bash view.sh ar1335
```

Open the address the second command prints. It looks like `http://192.168.x.x:8090/`. The full steps, including how to plug the flex into CAM/DISP 0, are in the [main guide](../../README.md).

## What you should see

A color picture of the scene in front of the lens, drawn at half of 4208×3120.

## For someone wiring this into their own software

I2C address `0x36`. Chip id at `0x0000` is `0x0153`. Registers are 16-bit address and 16-bit value.

4208×3120, RAW10, GRBG, fourcc `pgAA`. The receiver pads each line to 5264 bytes. 4 lanes. Link 276 MHz. The receiver logs `Using a link rate of 552 Mbps`.

GPIO 34 is XSHUTDOWN and it is active low. The chip answers only while that pin is high. Do not drive GPIO 35. The crystal is the clock.

Every register this driver writes is in [REGISTERS.md](REGISTERS.md). The rest of the numbers are in [DETAILS.md](../../DETAILS.md).
