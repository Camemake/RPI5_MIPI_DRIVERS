# Sony IMX586 on Raspberry Pi 5

This folder is the Raspberry Pi 5 driver for the Camemake module that uses the [Sony IMX586](https://www.sony.com/en/SonyInfo/News/Press/201807/18-060E/), the 48-megapixel stacked sensor Sony announced for phones. The module is the [IMX586 48MP FF for Raspberry Pi](https://www.camemake.eu/shop/cm-mipi-imx586-rpi-imx586-48mp-ff-for-raspberry-pi-1104).

The mode that streamed is 4000×3000, RAW10, RGGB, 4 MIPI lanes, 450 MHz link, on CAM/DISP 0. That is the 2×2 bin of the 8000×6000 array on Sony's page. The full array is not in this package.

## Use it

On the Raspberry Pi, from the top of this repository:

```bash
bash install.sh imx586
sudo reboot
bash view.sh imx586
```

Open the address the second command prints. It looks like `http://192.168.x.x:8090/`. The full steps, including how to plug the flex into CAM/DISP 0, are in the [main guide](../../README.md).

## What you should see

A normal color picture, updating at about 2 frames per second. The preview is slow because each frame is about 15 MB. The mode is 4000×3000, the binned picture, not the full 8000×6000 array.

## For someone wiring this into their own software

I2C 0x1a, chip id 0x0586, 4 lanes, RGGB, link 450 MHz. The driver raises analog gain to 32× after the register list, because the list's own gain of 1× is almost black.

The rest of the numbers, including the chip id and the link frequency, are in [DETAILS.md](../../DETAILS.md).
