# Technical details

These numbers are from a Raspberry Pi 5, CAM/DISP 0. The module crystal is 27 MHz, except the OV5640, whose crystal is 24 MHz. The Pi kernel has no built-in driver for these sensors. The OV9281 is the exception: the kernel's `ov9282` driver matches it and wants a 24 MHz clock, so this package keeps that driver from binding and uses the 27 MHz driver here. The OV5640 driver is the mainline sensor driver, built out of tree, with the power-down pin held low. `rpicam-hello` does not list them. Raw V4L2 is the path that works.

The receiver is `rp1-cfe`. A fresh receiver comes up at 640×480. The preview sets the sensor pad, `csi2:0`, and `csi2:4` to the mode below, including `field:none colorspace:raw`, then enables the link `csi2:4 -> rp1-cfe-csi2_ch0`. The capture node is `rp1-cfe-csi2_ch0`. The media device number changes after a reboot. `view.sh` finds it.

The link number below is the V4L2 link frequency programmed into the receiver. The line in `dmesg` that says `Using a link rate of N Mbps` is twice that frequency. It is the rate we asked for.

Packed RAW10 fourcc letters are case-sensitive. `pgAA` is GRBG. `pGAA` is GBRG, and the receiver rejects it. Mono RAW10 is `Y10P`.

The boot file needs `camera_auto_detect=0` and exactly one of these overlays. The overlay enables `i2c0if`, `i2c0mux`, and `cam0_clk` at the module crystal rate, puts the sensor on `i2c_csi_dsi0`, and connects it to `csi0`. Supplies are `avdd` from `cam0_reg`, and `dovdd` and `dvdd` from `cam_dummy_reg`. The clock name is `xclk`. The OV5640 overlay does not use `cam0_reg`: that GPIO is the sensor's power-down pin and has to stay low.

Register address is 16 bits. The value is 8 bits, except on the AR0234CS, where the value is 16 bits.

The sensor page is the maker's own description of the chip. The row below is the mode this package streamed on a Raspberry Pi 5.

| Sensor | Maker's page | Tested mode | Lanes | Link |
| --- | --- | --- | --- | --- |
| SmartSens SC450AI | [SC450AI](https://www.smartsenstech.com/en/mpage?id=142) | 2688×1520 RAW10 BGGR | 4 | 180 MHz |
| OMNIVISION OX05B1S | [OX05B1S](https://www.ovt.com/products/ox05b/) | 2592×1944 RAW10 GRBG | 4 | 450 MHz |
| GalaxyCore GC4023 | [GalaxyCore](https://www.gcoreinc.com/) | 2560×1440 RAW10 RGGB | 2 | 351 MHz |
| Himax HM2170 | [HM2170](https://www.himax.com.tw/products/cmos-image-sensor/image-sensors/) | 1928×1088 RAW10 GRBG | 2 | 540 MHz |
| SmartSens SC233HGS | [SC233HGS](https://www.smartsenstech.com/en/mpage?id=212) | 1920×1200 RAW10 BGGR | 4 | 270 MHz |
| Sony IMX675 | [IMX675](https://www.sony-semicon.com/en/news/2022/2022072001.html) | 2608×1960 RAW10 GRBG | 2 | 800 MHz |
| Sony IMX586 | [IMX586](https://www.sony.com/en/SonyInfo/News/Press/201807/18-060E/) | 4000×3000 RAW10 RGGB | 4 | 450 MHz |
| OMNIVISION OV9281 | [OV9281](https://www.ovt.com/products/ov9281/) | 1280×800 mono RAW10 | 2 | 450 MHz |
| onsemi AR0234CS | [AR0234CS](https://www.onsemi.com/products/sensors/image-sensors/ar0234cs) | 1920×1200 RAW10 GRBG | 2 | 506.25 MHz |
| Sony IMX908 | Contact Camemake | 3856×2176 RAW10 RGGB | 2 | 720 MHz |
| OmniVision OV5640 | Contact Camemake | 640×480 UYVY | 2 | 192 MHz |

## SC450AI

Sensor: [SmartSens SC450AI](https://www.smartsenstech.com/en/mpage?id=142)

Shop: [CM_MIPI_SC450AI_RPI](https://www.camemake.eu/shop/cm-mipi-sc450ai-rpi-sc450ai-4mp-ff-for-raspberry-pi-1096)

- Compatible `smartsens,sc450ai`, I2C `0x30`, chip id at `0x3107` is `0xbd2f`
- 2688×1520, RAW10, BGGR, fourcc `pBAA`, 3360 bytes per line
- 4 lanes, link 180 MHz, log line `360 Mbps`
- The register list was written for a 27 MHz clock

## OX05B1S

Sensor: [OMNIVISION OX05B1S](https://www.ovt.com/products/ox05b/)

Shop: [CM_MIPI_OX05B1S_RPI](https://www.camemake.eu/shop/cm-mipi-ox05b1s-rpi-ox05b1s-5mp-ff-for-raspberry-pi-1088)

- Compatible `ovti,ox05b1s`, I2C `0x36`, chip id at `0x300a` is `0x580542`
- 2592×1944, RAW10, GRBG, fourcc `pgAA`
- The receiver pads each line from 3240 bytes to 3248. `sizeimage` is 6314112. A tight 3240-byte row tears the picture.
- 4 lanes. The table writes `0x3010=0x41`. Link 450 MHz, log line `900 Mbps`
- The register list was captured at 12 MHz and was not rewritten. The module crystal is 27 MHz, and 450 MHz is the rate that streamed.
- This is an RGB-IR sensor. The 2×2 preview is magenta.

## GC4023

Sensor: [GalaxyCore](https://www.gcoreinc.com/). The public GalaxyCore site is the company page.

Shop: [CM_MIPI_GC4023_RPI](https://www.camemake.eu/shop/cm-mipi-gc4023-rpi-gc4023-2-5mp-ff-for-raspberry-pi-1095)

- Compatible `galaxycore,gc4023`, I2C `0x29`. The chip does not answer at `0x31`. Chip id at `0x03f0` is `0x4023`
- 2560×1440, RAW10, RGGB, fourcc `pRAA`, 3200 bytes per line, `sizeimage` 4608000
- 2 lanes. The table writes `0x0114=0x01`. Link 351 MHz, log line `702 Mbps`
- Stream on is `0x0100=0x09`. Standby is `0x0100=0x00`

## HM2170

Sensor: [Himax HM2170](https://www.himax.com.tw/products/cmos-image-sensor/image-sensors/)

Shop: [CM_MIPI_HM2170_RPI](https://www.camemake.eu/shop/cm-mipi-hm2170-rpi-hm2170-2mp-ff-for-raspberry-pi-1082)

- Compatible `himax,hm2170`, I2C `0x25`. Address `0x24` does not answer.
- Chip id is 3 bytes at `0x0000`: `0x21 0x70` plus a revision. The tested module is revision D (`0x05`). Revision below 4 uses a different register list.
- 1928×1088, RAW10, GRBG, fourcc `pgAA`
- The receiver pads each line from 2410 bytes to 2416. `sizeimage` is 2628608
- 2 lanes. Stream on is `0x0100=0x01`. A table entry `0xffff` is a delay in milliseconds.
- The register list was written for a 19.2 MHz clock and a 384 MHz link. This module's crystal is 27 MHz, so the link used here is 540 MHz (`384 * 27 / 19.2`). Log line `1080 Mbps`

## SC233HGS

Sensor: [SmartSens SC233HGS](https://www.smartsenstech.com/en/mpage?id=212). The same part is in SmartSens's [global-shutter list](https://www.smartsenstech.com/en/gs_products).

Shop: [CM_MIPI_SC233HGS_RPI](https://www.camemake.eu/shop/cm-mipi-sc233hgs-rpi-sc233hgs-2mp-ff-for-raspberry-pi-1092)

- Compatible `smartsens,sc233hgs`, I2C `0x30`, chip id at `0x3107` is `0xcb61`
- 1920×1200, RAW10, BGGR, fourcc `pBAA`, 2400 bytes per line, `sizeimage` 2880000
- 4 lanes, link 270 MHz, log line `540 Mbps`
- This connector has no external frame-sync wire. With trigger mode left off, the sensor emits one frame each time register `0x2100` goes low for about a millisecond and then high. The driver repeats that edge. `0x0100` is not the stream bit on this part.
- The table's exposure is very short. After the table the driver writes a longer exposure (`0x3e01=0xc0`) and gain (`0x3e09=0x80`).

## IMX675

Sensor: [Sony IMX675](https://www.sony-semicon.com/en/news/2022/2022072001.html). Sony's product brief is [IMX675-AAQR](https://www.sony-semicon.com/files/62/flyer_security/IMX675-AAQR_AAQR1_AATN_Flyer.pdf).

Shop: [CM_MIPI_IMX675_RPI](https://www.camemake.eu/shop/cm-mipi-imx675-rpi-imx675-5mp-ff-for-raspberry-pi-1105)

- Compatible `sony,imx675`, I2C `0x1a`. The module id byte at `0x3a00` is `0x96`
- 2608×1960, RAW10, GRBG, fourcc `pgAA`
- The receiver pads each line from 3260 bytes to 3264. `sizeimage` is 6397440
- 2 lanes. The table writes `0x3040=0x01`. On this family, `0x03` would be 4 lanes.
- `0x3014=0x03` selects a 27 MHz input clock. `0x3023=0x00` is RAW10
- Link 800 MHz, log line `1600 Mbps`
- Standby is `0x3000=0x01` and master stop is `0x3002=0x01`. After the table settles, stream on is `0x3000=0` and then `0x3002=0`

## IMX586

Sensor: [Sony IMX586](https://www.sony.com/en/SonyInfo/News/Press/201807/18-060E/). Sony describes the 8000×6000 array. This package streams the 4000×3000 binned mode.

Shop: [CM_MIPI_IMX586_RPI](https://www.camemake.eu/shop/cm-mipi-imx586-rpi-imx586-48mp-ff-for-raspberry-pi-1104)

- Compatible `sony,imx586`, I2C `0x1a`. Chip id at `0x0016` / `0x0017` is `0x0586`
- 4000×3000, RAW10, RGGB, fourcc `pRAA`
- The receiver pads each line from 5000 bytes to 5008. `sizeimage` is 15024000
- `0x0901=0x22` is the 2×2 bin of the 8000×6000 array. This package does not include the full 8000×6000 mode.
- 4 lanes. The table writes `0x0114=0x03`
- The table was written for a 24 MHz clock (`0x0136=0x18`). This module's crystal is 27 MHz, so the driver writes `0x0136=0x1b` and `0x0137=0x00` and leaves the PLL ratios alone.
- Output bit rate is `27000000 / 6 * 400 / 2` = 900 Mbps. Link frequency is half of that, 450 MHz. Log line `900 Mbps`
- Stream on is `0x0100=0x01` after the table settles. Standby is `0x0100=0x00`
- The table's analog gain `0x0000` is 1× and the picture is a few codes above black. After the table the driver writes `0x0204=0x03` and `0x0205=0xe0` (32×, `1024 / (1024 - 992)`) and the same pair to `0x0216` / `0x0217`. It also writes `0x0601=0` so a color-bar test cannot stay on.
- The browser preview of this mode is about 2 frames per second.

## OV9281

Sensor: [OMNIVISION OV9281](https://www.ovt.com/products/ov9281/). OMNIVISION lists 1280×800 at 120 fps on a 24 MHz clock. This package streams that size on the module's 27 MHz crystal.

Shop: [CM_MIPI_OV9281_RPI](https://www.camemake.eu/shop/cm-mipi-ov9281-rpi-ov9281-1mp-ff-for-raspberry-pi-1107)

- Compatible `ovti,ov9281`, I2C `0x60`, chip id at `0x300a` is `0x9281`
- 1280×800, mono RAW10, fourcc `Y10P`, 1600 bytes per line, `sizeimage` 1280000
- 2 lanes. `0x3662=0x05` is RAW10. Link 450 MHz, log line `900 Mbps`
- The register list was written for a 24 MHz clock and 800 Mbps per lane, which is a 400 MHz link. The module crystal is 27 MHz, so the same PLL ratios run at 450 MHz (`400 * 27 / 24`)
- The Pi kernel's `ov9282` driver wants 24 MHz. `install.sh` stops that module from loading, and this overlay replaces the Pi's OV9281 overlay. That Pi overlay targets the other camera socket
- Stream on is `0x0100=0x01` after the table settles. Standby is `0x0100=0x00`. Soft reset is `0x0103=0x01`, with a 10 ms wait before the rest of the table
- A 3 second capture held 354 frames. The preview is gray

## AR0234CS

Sensor: [onsemi AR0234CS](https://www.onsemi.com/products/sensors/image-sensors/ar0234cs). onsemi lists 1920×1200 at 120 fps. This package streams that size on the module's 27 MHz crystal. A 3 second capture held 160 frames.

Shop: [CM_MIPI_AR0234CS_RPI](https://www.camemake.eu/shop/cm-mipi-ar0234cs-rpi-ar0234cs-2mp-ff-for-raspberry-pi-1106)

- Compatible `onnn,ar0234cs`, I2C `0x10`, chip id at `0x3000` is `0x0a56`. The mono part reads `0x1a56` and this driver rejects it
- Registers are 16-bit address and 16-bit value
- 1920×1200, RAW10, GRBG, fourcc `pgAA`, 2400 bytes per line, `sizeimage` 2880000
- 2 lanes. The table writes `0x31ae=0x0202`. Link 506.25 MHz, log line `1012 Mbps`
- The 10-bit PLL list was written for a 24 MHz clock: pre-divider `0x302e=0x08`, multiplier `0x3030=0x96`, VCO 450 MHz. The module crystal is 27 MHz, so the same ratios run at 506.25 MHz (`450 * 27 / 24`). Leave the pre-divider at 8. Holding the link at 450 MHz by writing pre-divider 9 produced no frames
- Soft reset is `0x301a=0x0001`, then 200 ms, then standby `0x301a=0x2058`. Stream on is `0x301a=0x205c`. Writing only `0x301c=0x01` did not start the stream
- Frame length `0x300a=0x04c4` is 1220 lines. Line length `0x300c=0x0264` is 612. Exposure `0x3012=0x02dc` is 732 lines. Analog gain `0x3060=0x0e` is just above 1×
- The browser preview is the full 1920×1200 frame, about 3 pictures a second

## IMX908

Sensor: Sony IMX908. Contact Camemake for the module.

- Compatible `sony,imx908`, I2C `0x1a`. Chip id at `0x4c0c` is `0x038c`, little-endian. That register answers after standby is cancelled and about 24 ms have passed
- 3856×2176, RAW10, RGGB, fourcc `pRAA`
- The receiver pads each line from 4820 bytes to 4832. `sizeimage` is 10514432
- 2 lanes. The driver writes `0x3040=0x01`. On this connector, 4-lane mode (`0x3040=0x03`) fails CRC and the packets arrive as data type `0x37`
- The clock is the crystal on the module. `0x3014=0x03` selects 27 MHz. Do not drive a clock into that pin
- Link 720 MHz, from `0x3015=0x03`. Log line `1440 Mbps`. Sony's RAW10 value `0x359c=0x2c` is written with the mode
- Stream on is standby cancel `0x3000=0x00`, a 24 ms wait, then master start `0x3002=0x00`. Standby is `0x3000=0x01` and master stop is `0x3002=0x01`
- Analog gain `0x3070=80` is 24 dB. A 1 second capture held 21 frames. The browser preview is half of 3856×2176, about 3 pictures a second

## OV5640

Sensor: OmniVision OV5640. Contact Camemake for the module.

- Compatible `ovti,ov5640`, I2C `0x3c`, chip id at `0x300a` is `0x5640`. A note that says write `0x40` and read `0x42` is 7-bit `0x20`. This module does not answer there
- 640×480, UYVY, fourcc `UYVY`, 1280 bytes per line, `sizeimage` 614400
- 2 lanes, non-continuous clock. Link 192 MHz, log line `384 Mbps`. The driver's 384 MHz menu entry is the rate before this mode is applied
- The crystal on the module is 24 MHz and it feeds MCLK. `cam0_clk` is 24000000. Do not drive GPIO 34 as a clock
- Connector pin 6 is PWDN and it is GPIO 34. PWDN is active high. The internal pull-up holds the chip down, and every address NACKs while the pin is high. `cam0_reg` is that same GPIO, so the overlay disables it and holds the pin low. RESETB is pulled up on the module and is not on the connector. GPIO 35 is open
- The register lists in the driver are the mainline mode tables. The stored 12 MHz DVP list is not this mode. Every write is in `cameras/ov5640/REGISTERS.md`
- The browser preview is the full 640×480 frame, about 30 pictures a second
