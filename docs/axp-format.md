# AuraX `.axp` Program Format

`.axp` is an AuraX-only compressed program format. It exists next to the legacy
Photon-compatible `.pix` export; the `.pix` encoder stays unchanged.

## Goals

- keep playback hot path identical to `.pix` by decoding into PSRAM at load time
- reduce LittleFS storage use by removing 4096-byte padding and compressing columns
- allow each picture command to fall back to raw data when compression would grow it
- add a lossless LZSS block codec in version 2 for color-rich images where RLE is weak

## Layout

All integers are little-endian 32-bit words unless noted otherwise.

```
DW 0   magic "AXP1" = 0x31505841
DW 1   version = 1 or 2
DW 2   command count
DW 3   number of LEDs
DW 4   end behavior
DW 5   total decoded image bytes

Command table, 10 DW per command:
DW 0   start time ms
DW 1   end time ms
DW 2   width = LEDs per column
DW 3   height = number of columns
DW 4   line frequency Hz
DW 5   encoded data offset from file start
DW 6   encoded data size
DW 7   decoded offset in PSRAM buffer
DW 8   codec: 0 raw, 1 column stream, 2 LZSS block
DW 9   last command flag

Encoded command data follows immediately after the command table.
```

## Column Stream Codec

The column stream stores columns sequentially. Each column starts with a one-byte
method:

- `0`: raw column, followed by `width * 4` bytes
- `1`: RLE column, followed by `uint16 run_count`, then runs of
  `uint16 count + uint32 pixel`
- `2`: repeat previous decoded column

If the encoded command would be larger than raw image bytes, the exporter stores
that command as codec `0`.

## LZSS Block Codec

Version 2 adds codec `2`, a lossless byte-level LZSS stream decoded into the same
raw image buffer as `.pix` before playback starts. Playback FPS is unchanged
because the hot path still reads raw `[dim, B, G, R]` columns from PSRAM.

The stream is grouped by flag bytes, LSB first:

- flag bit `0`: one literal byte follows
- flag bit `1`: one little-endian `uint16` match token follows

Match token layout:

```
bits 0..11   offset - 1   (1..4096 bytes back)
bits 12..15  length - 3   (3..18 bytes)
```

The exporter tests raw, column stream, and LZSS per unique image block, then
stores whichever is smallest.
