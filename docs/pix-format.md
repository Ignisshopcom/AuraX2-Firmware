# Ignis .pix File Format Specification

**Format version:** 2
**Byte order:** Little-endian
**Base unit:** 32-bit word (DW = double word = 4 bytes)

---

## Overall Layout

```
┌─────────────────────────────────────────┐
│  Program Header (program parameters)    │
├─────────────────────────────────────────┤
│  Command 0  (picture_command)           │
│  Command 1  (picture_command)           │
│  ...                                    │
│  Command N  (picture_command, last=1)   │
├─────────────────────────────────────────┤
│  [padding to 4096-byte block boundary]  │
├─────────────────────────────────────────┤
│  Image data 0                           │
│  [padding to 4096-byte block boundary]  │
│  Image data 1                           │
│  [padding to 4096-byte block boundary]  │
│  ...                                    │
└─────────────────────────────────────────┘
```

Image data starts at the first 4096-byte block boundary after all commands.
Each image also starts at a 4096-byte block boundary.

---

## Encoding of Records

### Program parameter record

```
Byte 3      Byte 2      Byte 1      Byte 0
0xD1        <type>      <size_dw>   (lower byte of size_dw, typically 0x00)
<value DW 0>
...
<value DW size_dw-1>
```

| Field     | Bits  | Description                              |
|-----------|-------|------------------------------------------|
| label     | 31–24 | `0xD1` — identifies a program parameter  |
| type      | 23–16 | parameter type (see table below)         |
| size_dw   | 15–0  | number of value DWs that follow          |

### Command record

```
Byte 3      Byte 2      Byte 1      Byte 0
0xA1        <cmd_type>  <size_dw high>  <size_dw low>
  [parameter records ...]
```

| Field     | Bits  | Description                              |
|-----------|-------|------------------------------------------|
| label     | 31–24 | `0xA1` — identifies a command            |
| cmd_type  | 23–16 | command type (see table below)           |
| size_dw   | 15–0  | total number of DWs in command (incl. header) |

### Command parameter record

```
Byte 3      Byte 2      Byte 1      Byte 0
0xB1        <type>      <size_dw high>  <size_dw low>
<value DW>
```

| Field     | Bits  | Description                              |
|-----------|-------|------------------------------------------|
| label     | 31–24 | `0xB1` — identifies a command parameter  |
| type      | 23–16 | parameter type (see table below)         |
| size_dw   | 15–0  | number of value DWs that follow (typically 1) |

---

## Program Header Parameters

Order of parameters in the header is fixed (as written by the encoder):

| # | Type ID | Name              | Size (DW) | Description                                              |
|---|---------|-------------------|-----------|----------------------------------------------------------|
| 1 | `0x05`  | `prgFileVersion`  | 1         | Format version; currently `2`                            |
| 2 | `0x03`  | `prgName`         | 8         | Project name, UTF-8, zero-padded to 32 bytes             |
| 3 | `0x04`  | `numOfLeds`       | 1         | Number of LEDs on the stick                              |
| 4 | `0x06`  | `progEndBehavior` | 1         | Behavior after last command (see values below)           |
| 5 | `0x01`  | `headerSize_dw`   | 1         | Total header size in DWs (including this record itself)  |

### `progEndBehavior` values

| Value | Meaning            |
|-------|--------------------|
| `0`   | Exit to standby    |
| `1`   | Repeat program     |
| `2`   | Keep last frame    |

---

## Command Types

| ID | Name              | Description              |
|----|-------------------|--------------------------|
| 1  | `picture_command` | Display an image         |
| 2  | `fixcol_command`  | Fixed color (reserved)   |
| 3  | `inccol_command`  | Incremental color (reserved) |
| 4  | `repeat_command`  | Repeat (reserved)        |
| 5  | `end_command`     | End (reserved)           |
| 6  | `keeplast_command`| Keep last (reserved)     |

Only `picture_command` (1) is currently emitted by Ignis Studio.

---

## `picture_command` Parameters

Parameters appear in the following fixed order:

| # | Type ID | Name                  | Size (DW) | Description                                                   |
|---|---------|-----------------------|-----------|---------------------------------------------------------------|
| 1 | `0x05`  | `startTime`           | 1         | Start time in milliseconds. Stored as **signed int32** — negative value means "start before t=0" (i.e. immediately). Parse as `(int32_t)val < 0 ? 0 : val`. |
| 2 | `0x06`  | `endTime`             | 1         | End time in milliseconds                                      |
| 3 | `0x09`  | `dimming`             | 1         | Brightness 0–100                                             |
| 4 | `0x04`  | `offset`              | 1         | Byte offset of image data from start of file                  |
| 5 | `0x0A`  | `width`               | 1         | Image width **after** 90° rotation (= source image height)    |
| 6 | `0x0B`  | `height`              | 1         | Image height **after** 90° rotation (= source image width)    |
| 7 | `0x07`  | `frequency`           | 1         | Line scan frequency in Hz                                     |
| 8 | `0x08`  | `gap`                 | 1         | Gap between picture repetitions in pixels                     |
| 9 | `0x0C`  | `picture_frequency`   | 1         | Image display frequency (used in accelerometer mode)          |
|10 | `0x0D`  | `accelerometer`       | 1         | `0` = disabled, `1` = accelerometer mode enabled             |
|11 | `0x0E`  | `last_command`        | 1         | `1` if this is the last command in the program, else `0`      |

Total command size: 1 (header DW) + 11 × 2 (parameter header + value) = **23 DWs**.

---

## Image Data

### Pixel format

Each pixel is stored as one 32-bit little-endian word in **DimBGR** format:

```
Bits 31–24  Bits 23–16  Bits 15–8   Bits 7–0
    R            G           B         0xE0 (dim byte, fixed)
```

The dim byte `0xE0` is the APA102 LED controller brightness nibble (global dimming, maximum = `0xFF`).

Source RGBA channels are mapped as:
```
output_dw = (R << 24) | (G << 16) | (B << 8) | 0xE0
```
Alpha is discarded.

### Rotation

Before writing, the image is rotated **90° clockwise**:

```
for j in 0..width-1:          // column in source → row in output
    for i in height-1..0:     // bottom-to-top in source → left-to-right in output
        output[pos++] = source[i * width + j]
```

As a result, the dimensions stored in the command are swapped relative to the source image:
- `width`  in command = source image **height**
- `height` in command = source image **width**

### Size and alignment

```
image_size_bytes = source_width * source_height * 4
image_block_size = ceil(image_size_bytes / 4096) * 4096
```

Each image is padded with zeros to the next 4096-byte boundary.

---

## Label Summary

| Label  | Meaning                  |
|--------|--------------------------|
| `0xA1` | Command                  |
| `0xB1` | Command parameter        |
| `0xD1` | Program parameter        |

---

## Example: minimal file with one image

```
Offset  Content
------  -------

00 00   0xD1050001  prgFileVersion header (type=5, size=1)
00 04   0x00000002  version = 2

00 08   0xD1030008  prgName header (type=3, size=8)
00 0C   "my-project\0..."  (32 bytes, UTF-8, zero-padded)

00 2C   0xD1040001  numOfLeds header (type=4, size=1)
00 30   0x00000090  144 LEDs

00 34   0xD1060001  progEndBehavior header (type=6, size=1)
00 38   0x00000001  repeat

00 3C   0xD1010001  headerSize_dw header (type=1, size=1)
00 40   0x00000011  header ends at DW 17 (= 0x44 bytes)

00 44   0xA1010017  picture_command, size=23 DWs
00 48     0xB1050001  startTime
00 4C     0x00000000    = 0 ms
00 50     0xB1060001  endTime
00 54     0x000007D0    = 2000 ms
...
00 9C     0xB10E0001  last_command
00 A0     0x00000001    = 1 (last)

01 000  [image data, block-aligned at 0x1000]
```
