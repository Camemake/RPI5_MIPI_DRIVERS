# Sony IMX908 on Raspberry Pi 5

This folder is the Raspberry Pi 5 driver for the Camemake module that uses the Sony IMX908, an 8.39-megapixel sensor. Contact Camemake for the module.

The mode that streamed is 3856×2176, color RAW10, 2 MIPI lanes, 720 MHz link, on CAM/DISP 0. The clock is the crystal on the module, 27 MHz. A 1 second capture on the Pi held 21 frames. The browser preview shows that picture at half resolution.

## Use it

On the Raspberry Pi, from the top of this repository:

```bash
bash install.sh imx908
sudo reboot
bash view.sh imx908
```

Open the address the second command prints. It looks like `http://192.168.x.x:8090/`. The full steps, including how to plug the flex into CAM/DISP 0, are in the [main guide](../../README.md).

## What you should see

A color picture. The preview samples each 2×2 block down to one pixel, so the tint is only approximate, and the page is half of 3856×2176. It updates a few times a second.

## For someone wiring this into their own software

I2C address 0x1a, chip id 0x038c, 3856×2176, 2 lanes, RGGB RAW10, link 720 MHz. The packed format is `pRAA`. The receiver pads each line from 4820 bytes to 4832, so `sizeimage` is 10514432. The clock is the module crystal. Do not drive a clock into that pin.

The rest of the numbers, including the chip id and the link frequency, are in [DETAILS.md](../../DETAILS.md).

Every register this driver writes, and every register it names, is in [REGISTERS.md](REGISTERS.md).
