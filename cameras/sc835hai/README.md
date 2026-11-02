# SmartSens SC835HAI on Raspberry Pi 5

This folder is the Raspberry Pi 5 driver for the Camemake module that uses the [SmartSens SC835HAI](https://www.smartsenstech.com/en/ai_products).

The mode that streamed is 3840×2160, RAW10, BGGR, 2 MIPI lanes, 396 MHz link, on CAM/DISP 0. The crystal on the module is 24 MHz.

## Use it

On the Raspberry Pi, from the top of this repository:

```bash
bash install.sh sc835hai
sudo reboot
bash view.sh sc835hai
```

Open the address the second command prints. It looks like `http://192.168.x.x:8090/`. The full steps, including how to plug the flex into CAM/DISP 0, are in the [main guide](../../README.md).

## What you should see

A color picture of the scene in front of the lens, drawn at half of 3840×2160.

## For someone wiring this into their own software

I2C address `0x30`. Chip id at `0x3107` is `0xc170`. Registers are 16-bit address and 8-bit value.

3840×2160, RAW10, BGGR, fourcc `pBAA`, 4800 bytes per line. 2 lanes. Link 396 MHz. The receiver logs `Using a link rate of 792 Mbps`.

GPIO 34 is the reset and it is active low. The chip answers only while that pin is high. Do not drive GPIO 35. The crystal is the clock.

Every register this driver writes is in [REGISTERS.md](REGISTERS.md). The rest of the numbers are in [DETAILS.md](../../DETAILS.md).
