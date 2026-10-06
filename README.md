# Camemake cameras on Raspberry Pi 5

These are the drivers that produced a real picture on a Raspberry Pi 5. Each one is a Camemake MIPI module with its own flex for that Pi. Pick the folder that matches the module you bought, run two commands, and open the picture in a web browser.

The Raspberry Pi camera app does not list these modules. The picture is the browser page on port 8090.

Camemake publishes these drivers so you can start. They are a starting point, not a full camera application, and Camemake does not provide deep support for them.

## What you need

- A Raspberry Pi 5 with the 64-bit Raspberry Pi OS
- A power supply that can actually feed the Pi. A weak supply drops frames.
- The Camemake module made for Raspberry Pi, not the universal flex and not a USB module
- The flex plugged into the socket printed **CAM/DISP 0**

Hold the Pi so you can read the logo. CAM/DISP 0 and CAM/DISP 1 are the two small connectors on the edge closest to you, between the micro-HDMI sockets and the Ethernet port. Use the one printed CAM/DISP 0. It is the connector closer to the Ethernet port. These drivers do not use CAM/DISP 1.

Only one camera can be installed at a time. The boot file has room for one of these overlays.

## Plug the camera in

1. Shut the Pi down and unplug the power.
2. Find the CAM/DISP 0 socket. Lift the black latch with a fingernail. It is small. Lift it evenly.
3. Slide the flex in until it stops. The metal contacts face the circuit board. The printed side faces the latch.
4. Push the latch closed so the flex does not pull out.
5. Power the Pi back on.

If a later check says the camera is missing, power off and seat the flex again. Do not force the latch.

## Install the driver

On the Pi, open a terminal and run:

```bash
sudo apt update
sudo apt full-upgrade
sudo reboot
```

After the reboot:

```bash
git clone https://github.com/Camemake/RPI5_MIPI_DRIVERS.git
cd RPI5_MIPI_DRIVERS
bash install.sh imx586
sudo reboot
```

Change `imx586` to the name of your camera. The names are in the table below. `install.sh` builds the driver for the kernel that is running, copies it into place, and writes the boot file. It asks for your password. A copy of the old boot file is kept next to it, with `.bak-camemake` on the end.

## See the picture

After that second reboot:

```bash
cd RPI5_MIPI_DRIVERS
bash view.sh imx586
```

The terminal prints an address like `http://192.168.1.50:8090/`. Open that address in a browser on a computer connected to the same network. Leave the terminal window open. Ctrl+C stops the preview.

The page shows the picture at half the sensor size, with a simple color balance. It is a viewer so you can see that the camera works. It is not a recording application, and it does not adjust exposure on its own except for the fixed settings already in each driver.

## Which camera

| Camera | Shop page | Command |
| --- | --- | --- |
| SC450AI 4MP | [SC450AI for Raspberry Pi](https://www.camemake.eu/shop/cm-mipi-sc450ai-rpi-sc450ai-4mp-ff-for-raspberry-pi-1096) | `bash install.sh sc450ai` |
| OX05B1S 5MP | [OX05B1S for Raspberry Pi](https://www.camemake.eu/shop/cm-mipi-ox05b1s-rpi-ox05b1s-5mp-ff-for-raspberry-pi-1088) | `bash install.sh ox05b1s` |
| GC4023 2.5MP | [GC4023 for Raspberry Pi](https://www.camemake.eu/shop/cm-mipi-gc4023-rpi-gc4023-2-5mp-ff-for-raspberry-pi-1095) | `bash install.sh gc4023` |
| HM2170 2MP | [HM2170 for Raspberry Pi](https://www.camemake.eu/shop/cm-mipi-hm2170-rpi-hm2170-2mp-ff-for-raspberry-pi-1082) | `bash install.sh hm2170` |
| SC233HGS 2MP | [SC233HGS for Raspberry Pi](https://www.camemake.eu/shop/cm-mipi-sc233hgs-rpi-sc233hgs-2mp-ff-for-raspberry-pi-1092) | `bash install.sh sc233hgs` |
| IMX675 5MP | [IMX675 for Raspberry Pi](https://www.camemake.eu/shop/cm-mipi-imx675-rpi-imx675-5mp-ff-for-raspberry-pi-1105) | `bash install.sh imx675` |
| IMX586 48MP | [IMX586 for Raspberry Pi](https://www.camemake.eu/shop/cm-mipi-imx586-rpi-imx586-48mp-ff-for-raspberry-pi-1104) | `bash install.sh imx586` |

All of the Raspberry Pi modules are listed at [Raspberry Pi MIPI camera modules](https://www.camemake.eu/raspberry-pi-camera-modules-rpi).

The OX05B1S preview looks magenta. That sensor sees infrared as well as color, and this simple viewer does not separate the infrared. A magenta picture means the camera is streaming.

The IMX586 browser page updates at about 2 pictures a second. Each frame is about 15 MB, and the viewer draws every one. The driver is using the 4000×3000 binned mode, which is the mode that was tested. The sensor can also expose an 8000×6000 array. That mode is not in this package.

## If the page stays black

Run this and read the last lines that mention your camera:

```bash
dmesg | grep -i -E "sc450ai|ox05b1s|gc4023|hm2170|sc233hgs|imx675|imx586"
```

- A line with the chip id means the Pi sees the module. Run `bash view.sh` with the same name.
- No line, or an error about the chip id, means the flex, the connector, or the overlay is wrong. Power off, reseat the flex in CAM/DISP 0, and confirm `/boot/firmware/config.txt` contains `camera_auto_detect=0` and one `dtoverlay=` line for your camera.
- `Wrong width or height 640x480` means the preview did not set the picture size. Run `bash view.sh` again. Do not start the preview by opening `/dev/video0` on its own.

A new driver starts at the next reboot. Do not unload the camera driver while a picture is running.

## What is in each folder

| File | What it is |
| --- | --- |
| `install.sh` | Builds the driver, installs it, and sets the boot file |
| `view.sh` | Starts the browser preview |
| `cameras/<name>/<name>.c` | The kernel driver |
| `cameras/<name>/regs.inc` | The register list the driver writes |
| `cameras/<name>/<name>-overlay.dts` | The Pi 5 device-tree overlay for CAM/DISP 0 |
| `cameras/<name>/live.py` | The preview page |
| `DETAILS.md` | Chip id, lanes, link frequency, and the other numbers |

The kernel modules are GPL-2.0-only, the same license as the Linux kernel.

Tested on a Raspberry Pi 5 running Raspberry Pi OS 64-bit, kernel `6.18.50+rpt-rpi-2712`. `install.sh` builds against the kernel that is running, so the headers for that kernel have to be installed. The script tries to install them.
