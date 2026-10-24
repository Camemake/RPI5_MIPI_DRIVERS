# OmniVision OV5640 on Raspberry Pi 5

This folder is the Raspberry Pi 5 driver for the Camemake module that uses the OmniVision OV5640. The mode that streamed is 640×480 UYVY, 2 MIPI lanes, 192 MHz link, on CAM/DISP 0. The crystal on the module is 24 MHz. The driver builds the MIPI clock from that rate.

The preview is a color picture at about 30 frames per second.

## The power-down pin

Connector pin 6 is PWDN, and on the Pi 5 that pin is GPIO 34 (CAM_IO0). PWDN is active high. The chip has an internal pull-up, so a floating pin holds the sensor off and every I2C address NACKs.

GPIO 34 is also the pin the Pi calls `cam0_reg`. Enabling that regulator drives the pin high and puts this sensor into power-down. This overlay disables `cam0_reg` and claims the same GPIO as `powerdown-gpios`, held low. RESETB is pulled up on the module and is not on the connector, so the driver does not pulse PWDN high. GPIO 35 is the next camera pin and is open on this module.

If a probe fails with error `-121` on register `0x3008`, the pin is high or the flex is unseated. Power off, seat the flex in CAM/DISP 0, and boot with this overlay.

## Use it

On the Raspberry Pi, from the top of this repository:

```bash
bash install.sh ov5640
sudo reboot
bash view.sh ov5640
```

Open the address the second command prints. It looks like `http://192.168.x.x:8090/`. The full steps, including how to plug the flex into CAM/DISP 0, are in the [main guide](../../README.md).

## What you should see

A color picture, 640×480, updating at about 30 frames per second.

## For someone wiring this into their own software

I2C address `0x3c`. Chip id at `0x300a` is `0x5640`. A note that says write `0x40` and read `0x42` is the 7-bit address `0x20`. This module does not answer there.

640×480, UYVY, fourcc `UYVY`, 1280 bytes per line. 2 lanes, non-continuous clock. Link 192 MHz. The receiver logs `Using a link rate of 384 Mbps`.

The stored 12 MHz DVP register list is not this mode. The lists this driver writes, and the named registers, are in [REGISTERS.md](REGISTERS.md). The rest of the bring-up numbers are in [DETAILS.md](../../DETAILS.md).
