# sc233hgs registers

Register map for the SC233HGS driver in this folder. It covers the fields the driver reads and writes on the streamed 1920×1200 mode. Analog rows that stay as a block are listed in write order at the bottom and are not decoded bit by bit.

Access is I2C address `0x30`, a 16-bit register address, then one data byte. The chip id is two bytes at `0x3107`: `0xcb` then `0x61`.

## Frame timing

The streamed mode is 1920×1200, RAW10, BGGR, 4 lanes. The receiver logged `Using a link rate of 540 Mbps`, so the link frequency programmed here is 270 MHz. The module crystal is 27 MHz.

| Field | Value | Where it comes from |
| --- | --- | --- |
| Output width | 1920 | Mode `0x301f=0x48`. The table does not write a width register |
| Output height | 1200 | Same mode byte |
| Packed stride | 2400 bytes | 1920 pixels × 10 bits / 8. Fourcc `pBAA` |
| HTS register | `0x0465` (1125) | `0x320c=0x04`, `0x320d=0x65` |
| Line length the driver uses | 2250 | Twice the HTS register. `hblank` is 2250 − 1920 = 330 |
| VTS, first write | `0x0640` (1600) | `0x320e=0x06`, `0x320f=0x40`. The later write replaces it |
| VTS, the value that runs | `0x0c80` (3200) | `0x320e=0x0c`, `0x320f=0x80` at the end of the table |
| Vertical blank | 2000 | 3200 − 1200 |
| Exposure in the table | 64 lines | `0x3e00=0x00`, `0x3e01=0x40`, `0x3e02=0x00` |
| Exposure the driver applies | 192 lines | `0x3e00=0x00`, `0x3e01=0xc0`, `0x3e02=0x00` |
| Fine gain in the table | `0x3e09=0x40` | Left in place until the driver overwrites it |
| Fine gain the driver applies | `0x3e09=0x80` | Coarse gain `0x3e08` stays `0x00` |
| Frame request | every 50 ms | `0x2100` low for 2 ms, then high |

The 64-line table exposure is almost black. 192 lines and fine gain `0x80` is the pair that produced a picture. Exposure has to stay below the running VTS of 3200 lines.

## Control registers

### `0x2100` frame edge

This is the register that starts a frame. `0x0100` is not the stream bit on this part.

The sensor emits one frame when `0x2100` goes `0`, stays there, then goes `1`. The low level has to last about a millisecond. A back-to-back write does not count as an edge. The driver holds it low for 2 ms and repeats the edge every 50 ms.

The table's first row writes `0x00`. After the table, the exposure writes, and the gain write, the driver writes `0x01`.

### `0x3107` chip id

Read two bytes. `0x3107` is `0xcb` and `0x3108` is `0x61`. The driver compares the pair with `0xcb61`.

### `0x3018` lane count, `0x301f` mode

`0x3018=0x7a` is the 4-lane setting in this table. `0x301f=0x48` selects the 1920×1200 list. Both stay as written for this mode.

### `0x3019` MIPI clock

The table writes `0x00`, which leaves the clock running. On the SC450AI the same address turns the MIPI clock off when written `0xff`. This driver leaves `0x3019` at `0x00`.

### `0x320c` / `0x320d` horizontal total

Big-endian line length, `0x0465` = 1125. This driver treats one pixel line as twice that register, 2250. Change the pair together. The blank the driver reports is `2 × HTS − 1920`.

### `0x320e` / `0x320f` vertical total

Big-endian frame length in lines. The table writes `0x0640` (1600) and later `0x0c80` (3200). The second write is the one that runs. Keep both rows, in that order. Vertical blank is `VTS − 1200`.

### `0x3222` trigger source

The table does not write this register. Leave bit 0 clear. This cable has no frame-sync wire, and setting that bit waits for a pulse the board does not send. Frames come from the `0x2100` edge instead.

### `0x3221` mirror, flip, and HDR enable

The table does not write this register. Read it, change the bits below, and write the whole byte back.

| Bits | Mask | Function |
| --- | --- | --- |
| 7 | `0x80` | Knee-point HDR. 1 enables it. The streamed mode leaves this clear |
| 6:5 | `0x60` | Vertical flip |
| 2:1 | `0x06` | Horizontal mirror |

Bit 7 is the HDR switch on the global-shutter parts. Bits 6:5 and 2:1 are the flip and mirror used by the SC132GS and SC2336 drivers. This driver has not written the register, so the picture that streamed is linear.

### Exposure, `0x3e00` / `0x3e01` / `0x3e02`

One value, in units of 1/16 line:

```
value = ((0x3e00 & 0x0f) << 12) | (0x3e01 << 4) | (0x3e02 >> 4)
lines = value / 16
```

The driver writes it as:

```
0x3e00 = (lines_x16 >> 12) & 0x0f
0x3e01 = (lines_x16 >> 4) & 0xff
0x3e02 = (lines_x16 & 0x0f) << 4
```

`0x3e02` keeps the fraction in bits 7:4. The table's `0x40` in `0x3e01`, with the other two bytes 0, is 64 lines. The driver then writes `0x3e01=0xc0` and `0x3e02=0x00`, which is 192 lines. `0x3e00` stays 0 until the line count needs bits above `0x3e01`.

### Knee-point HDR

HDR on this sensor is one frame with two slopes. The bright part of the scene is compressed after a knee, and the dark part stays on the steeper slope. There is no second exposure on another MIPI virtual channel. The stagger addresses used by the rolling-shutter SmartSens parts, `0x3e04`, `0x3e05`, `0x3e22`, `0x3e10`, `0x3e11`, `0x3e12`, and `0x3e13`, are not this HDR path.

The knee has to stay shorter than the total exposure. The exposure that produced a picture is 192 lines, and the frame length is 3200 lines.

| Register | This mode | Role |
| --- | --- | --- |
| `0x3221[7]` | not written, bit clear | HDR enable. Set the bit to turn the knee on |
| `0x3e00` / `0x3e01` / `0x3e02` | 192 lines after the table | Total exposure, both slopes. Same packing as the linear exposure |
| `0x3385` | `0x59` (89) | First knee byte in this table |
| `0x3387` | `0x79` (121) | Second knee byte in this table |
| `0x3e03` | `0x0b` | Manual exposure and manual gain, and the gain map those bytes use |
| `0x3e08` | `0x00` | Coarse analog gain. The driver leaves the table value |
| `0x3e09` | `0x80` after the table | Fine analog gain. The table value is `0x40` |
| `0x3e06` | not written | Coarse digital gain |
| `0x3e07` | not written | Fine digital gain |
| `0x33fa` | `0x02` | Analog setting that follows the gain |
| `0x3317` | `0x30` | Analog setting that follows the gain |
| `0x3234` | not written | External knee trigger. This cable has no Trigger1 pin, so leave it |

`0x3386` is not in this list. The older SC130GS describes the knee as the 16-bit pair `0x3385`/`0x3386`, counted in units of two lines. This init list writes `0x3385` and `0x3387` and skips `0x3386`, which is the same pair the SC132GS and SC035GS lists write. Keep those two bytes, and count them in the same line units as `{0x3e01, 0x3e02[7:4]}`. On this module that field is one line per count: `0x3e01=0x40` with the other exposure bytes 0 is 64 lines.

`0x3e03=0x0b` leaves bits [1:0] set. Bit 0 is manual exposure and bit 1 is manual gain, so the exposure and gain registers are the ones that apply. The rest of the byte stays `0x0b`. Writing `0x03` selects the other gain map.

Fine analog `0x40` is the 1× code in the map this byte selects. The driver replaces it with `0x80`. Coarse analog stays `0x00`. Digital gain is the pair this driver does not write. On the SC132GS, 1× digital gain is `0x3e06=0x00` and `0x3e07=0x80`, and the coarse analog code for the first gain step is `0x03` rather than `0x00`. This table's coarse byte is `0x00`, so that sibling's coarse ladder is not what is loaded here. Check the picture before copying those coarse codes across.

`0x33fa` and `0x3317` move with gain on the SC132GS: below 2× they are `0x01` and `0xf0`, and at 2× and above they are `0x02` and `0x0a`. This table's `0x3317` is `0x30`, so the second of those constants is not the value loaded here. If fine gain moves a long way from `0x80`, write this pair again and look at the frame.

`0x3234[7]` would enable an external trigger and `0x3234[0]` would take the knee from a Trigger1 pin. This connector does not wire that pin. The knee stays in `0x3385` and `0x3387`, and frames still come from the `0x2100` edge.

The bytes `0x3385` and `0x3387` are already in the mode table. HDR stays off until `0x3221[7]` is set.

### PLL hold, `0x36e9` and `0x37f9`

Both are written `0x80` before the PLL coefficient bytes, then `0x24` after those bytes. `0x80` holds the PLL. `0x24` is the value that runs at 27 MHz in and 270 MHz link. Keep that order. The coefficient bytes are `0x36ea`–`0x36ed` and `0x37fa`–`0x37fd`.

### `0x4837`

The table writes `0x1e` for this 270 MHz link. Leave it with the mode.

## Write order

201 rows. A later row to the same address replaces the earlier one. Rows with no note are mode-table bytes and stay in this order.

After the last row the driver waits 8 ms, writes the exposure and gain below, then writes `0x2100=0x01`.

- `0x2100` = `0x00` — frame edge low while the table loads
- `0x36e9` = `0x80` — PLL hold, first write
- `0x37f9` = `0x80` — PLL hold, first write
- `0x300c` = `0x24`
- `0x3018` = `0x7a` — 4-lane setting for this mode
- `0x3019` = `0x00` — MIPI clock left running
- `0x301f` = `0x48` — 1920×1200 mode
- `0x3044` = `0x10`
- `0x3062` = `0x00`
- `0x320c` = `0x04` — HTS high, 1125
- `0x320d` = `0x65` — HTS low
- `0x320e` = `0x06` — VTS high, first value 1600
- `0x320f` = `0x40` — VTS low, first value
- `0x321f` = `0x0b`
- `0x322f` = `0x00`
- `0x3231` = `0x00`
- `0x3250` = `0x03`
- `0x32e2` = `0x00`
- `0x3301` = `0x18`
- `0x3304` = `0x68`
- `0x3306` = `0x48`
- `0x3309` = `0x88`
- `0x330b` = `0xf0`
- `0x330d` = `0x30`
- `0x3314` = `0xc0`
- `0x3315` = `0x30`
- `0x3317` = `0x30` — gain-dependent analog, table value
- `0x3318` = `0x1f`
- `0x331d` = `0x32`
- `0x331f` = `0x02`
- `0x3320` = `0xc1`
- `0x333b` = `0x30`
- `0x3352` = `0x1a`
- `0x3356` = `0x1c`
- `0x3363` = `0x0f`
- `0x3385` = `0x59` — first knee byte, 89
- `0x3387` = `0x79` — second knee byte, 121
- `0x33b0` = `0x00`
- `0x33ef` = `0x04`
- `0x33f8` = `0x02`
- `0x33fa` = `0x02` — gain-dependent analog, table value
- `0x341c` = `0x06`
- `0x341e` = `0x24`
- `0x341f` = `0x27`
- `0x34ad` = `0x05`
- `0x34af` = `0x02`
- `0x34de` = `0x02`
- `0x34f2` = `0x00`
- `0x3619` = `0x20`
- `0x361a` = `0x90`
- `0x3630` = `0x80`
- `0x3633` = `0x44`
- `0x3637` = `0x3e`
- `0x363a` = `0x00`
- `0x363b` = `0x17`
- `0x363c` = `0x0f`
- `0x363d` = `0x1f`
- `0x363e` = `0xa8`
- `0x363f` = `0x86`
- `0x3648` = `0x4a`
- `0x364a` = `0x22`
- `0x3660` = `0x02`
- `0x3661` = `0x05`
- `0x3662` = `0x0b`
- `0x3663` = `0x17`
- `0x3664` = `0x17`
- `0x3665` = `0x08`
- `0x3666` = `0x18`
- `0x3667` = `0x38`
- `0x3668` = `0x78`
- `0x3670` = `0xf0`
- `0x3671` = `0xc8`
- `0x3672` = `0xa8`
- `0x367c` = `0x43`
- `0x367d` = `0x43`
- `0x367e` = `0x43`
- `0x36c0` = `0x08`
- `0x36c1` = `0x38`
- `0x36c6` = `0x08`
- `0x36c7` = `0x38`
- `0x36ea` = `0x09` — PLL coefficient
- `0x36eb` = `0x0c` — PLL coefficient
- `0x36ec` = `0x53` — PLL coefficient
- `0x36ed` = `0x24` — PLL coefficient
- `0x3718` = `0x04`
- `0x3719` = `0x06`
- `0x3722` = `0x08`
- `0x3728` = `0xa0`
- `0x372a` = `0x10`
- `0x3778` = `0x08`
- `0x3779` = `0x18`
- `0x377a` = `0xd8`
- `0x3794` = `0x00`
- `0x3795` = `0x00`
- `0x3796` = `0x00`
- `0x3798` = `0x10`
- `0x3799` = `0x10`
- `0x379a` = `0x10`
- `0x37c4` = `0x08`
- `0x37c5` = `0x38`
- `0x37d2` = `0x08`
- `0x37d3` = `0x38`
- `0x37d4` = `0x08`
- `0x37d5` = `0x38`
- `0x37fa` = `0x09` — PLL coefficient
- `0x37fb` = `0xe5` — PLL coefficient
- `0x37fc` = `0x01` — PLL coefficient
- `0x37fd` = `0x34` — PLL coefficient
- `0x3900` = `0x1d`
- `0x3901` = `0x06`
- `0x3903` = `0x40`
- `0x3904` = `0x0a`
- `0x3905` = `0x4d`
- `0x391f` = `0x44`
- `0x3933` = `0x00`
- `0x3934` = `0x20`
- `0x3935` = `0x00`
- `0x3936` = `0x3c`
- `0x3937` = `0x7e`
- `0x3938` = `0x64`
- `0x3939` = `0x1f`
- `0x393a` = `0xaa`
- `0x393b` = `0x00`
- `0x393c` = `0x20`
- `0x39c1` = `0x6c`
- `0x3c0a` = `0x2a`
- `0x3e00` = `0x00` — exposure bits 15:12, table value, 64 lines together with the next two
- `0x3e01` = `0x40` — exposure bits 11:4
- `0x3e02` = `0x00` — exposure fraction in bits 7:4
- `0x3e03` = `0x0b` — manual exposure, manual gain, gain map for the knee mode
- `0x3e08` = `0x00` — coarse analog gain
- `0x3e09` = `0x40` — fine analog gain, table value
- `0x3e15` = `0x00`
- `0x3e1c` = `0x00`
- `0x3e8e` = `0x00`
- `0x4330` = `0x50`
- `0x4331` = `0x20`
- `0x4333` = `0x02`
- `0x4360` = `0x07`
- `0x4362` = `0xf0`
- `0x4364` = `0xf0`
- `0x4365` = `0x18`
- `0x4366` = `0x38`
- `0x4368` = `0x58`
- `0x436a` = `0x78`
- `0x436b` = `0x18`
- `0x436c` = `0x38`
- `0x4370` = `0x20`
- `0x4371` = `0x10`
- `0x4372` = `0x18`
- `0x4373` = `0x38`
- `0x4402` = `0x03`
- `0x4403` = `0x0b`
- `0x4404` = `0x21`
- `0x4405` = `0x2b`
- `0x440c` = `0x36`
- `0x440d` = `0x36`
- `0x440e` = `0x29`
- `0x440f` = `0x44`
- `0x4412` = `0x01`
- `0x450d` = `0x10`
- `0x4837` = `0x1e` — MIPI timing for the 270 MHz link
- `0x4b0a` = `0x92`
- `0x5000` = `0x38`
- `0x5002` = `0x00`
- `0x502e` = `0x21`
- `0x5034` = `0x01`
- `0x5104` = `0x14`
- `0x5105` = `0x10`
- `0x5106` = `0x04`
- `0x5107` = `0x59`
- `0x510d` = `0x44`
- `0x5302` = `0x00`
- `0x5787` = `0x0a`
- `0x5788` = `0x0a`
- `0x5789` = `0x08`
- `0x578a` = `0x0a`
- `0x578b` = `0x0a`
- `0x578c` = `0x08`
- `0x578d` = `0x40`
- `0x5790` = `0x08`
- `0x5791` = `0x04`
- `0x5792` = `0x04`
- `0x5793` = `0x08`
- `0x5794` = `0x04`
- `0x5795` = `0x04`
- `0x57aa` = `0x2a`
- `0x57ab` = `0x7f`
- `0x57ac` = `0x00`
- `0x57ad` = `0x00`
- `0x36e9` = `0x24` — PLL run, replaces the `0x80` hold
- `0x37f9` = `0x24` — PLL run, replaces the `0x80` hold
- `0x304a` = `0x02`
- `0x4408` = `0x00`
- `0x4409` = `0x00`
- `0x440a` = `0x03`
- `0x440b` = `0xff`
- `0x4424` = `0x01`
- `0x4407` = `0xa0`
- `0x320e` = `0x0c` — VTS high, running value 3200
- `0x320f` = `0x80` — VTS low, running value

Driver writes after that list, before the first frame edge:

- `0x3e00` = `0x00`
- `0x3e01` = `0xc0` — 192 lines with the bytes around it
- `0x3e02` = `0x00`
- `0x3e09` = `0x80` — fine analog gain
- `0x2100` = `0x01` — frame edge high
