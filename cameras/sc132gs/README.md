# SmartSens SC132GS on Raspberry Pi 5

This folder is the Raspberry Pi 5 driver for the Camemake module that uses the [SmartSens SC132GS](https://www.smartsenstech.com/en/gs_products). The module is the [SC132GS 1MP FF for Raspberry Pi](https://www.camemake.eu/shop/cm-mipi-sc132gs-rpi-sc132gs-1mp-ff-for-raspberry-pi-1101).

The mode that streamed is 1080×1280, RAW10, BGGR, 2 MIPI lanes, 202.5 MHz link, on CAM/DISP 0. The crystal on the module is 27 MHz.

## Use it

On the Raspberry Pi, from the top of this repository:

```bash
bash install.sh sc132gs
sudo reboot
bash view.sh sc132gs
```

Open the address the second command prints. It looks like `http://192.168.x.x:8090/`. The full steps, including how to plug the flex into CAM/DISP 0, are in the [main guide](../../README.md).

## What you should see

A color picture of the scene in front of the lens, drawn at half of 1080×1280.

## For someone wiring this into their own software

I2C address `0x30`. Chip id at `0x3107` is `0x0132`. Registers are 16-bit address and 8-bit value.

1080×1280, RAW10, BGGR, fourcc `pBAA`, 1360 bytes per line. 2 lanes. Link 202.5 MHz. The receiver logs `Using a link rate of 405 Mbps`.

GPIO 34 is PWDN and it is active low. The chip answers only while that pin is high. Do not drive GPIO 35. The crystal is the clock.

Every register this driver writes is in [REGISTERS.md](REGISTERS.md). The rest of the numbers are in [DETAILS.md](../../DETAILS.md).
