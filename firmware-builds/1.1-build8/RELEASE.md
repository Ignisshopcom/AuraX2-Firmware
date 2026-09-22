# AuraX 1.1 build 8 - Photon program bridge candidate

Adds Photon START/STOP UDP broadcasts to the existing locally initiated program
fanout. The previous build 7 remains available unchanged.

## Behavior

- AuraX sync class 1 maps to Photon wifi_group=1 and UDP destination port 5001.
  Classes 2..10 map to groups 2..10 and ports 5002..5010 respectively.
- Each selected class receives one directed-subnet-broadcast datagram. Port 5000
  is not used additionally, avoiding duplicate START on receivers listening to
  both ports. No acknowledgment waits, repeat loops or periodic packets.
- Requires enabled SYNC, a selected class, and a connected shared Wi-Fi LAN.
  Router client isolation/broadcast filtering may prevent delivery.
- AuraX program slot 1 starts Photon prefix 001 (e.g. 001_intro.prg); the Photon
  program must already exist. This does not upload or convert program files.
- Explicit STOP / power off fanout sends Photon STOP. Effect selection,
  effect stop, low-battery blackout and received ESP-NOW packets do not send it.
- Forwarded HTTP requests with relay=false do not send it again. An external
  controller should use one originating relay-enabled request to trigger this
  bridge, not relay-enabled START on every AuraX simultaneously.
- AP-only operation does not bridge to Photon. Existing AuraX ESP-NOW sync,
  reconnect, audio, LED and storage behavior is unchanged.

## Wire format and outstanding confirmation

Source: supplied Wifi_commands.xlsx, List1 B10:I10 (START), B13:C13 (STOP),
plus Photon developer's message confirming ports 5000 and 5000+wifi_group.
Workbook SHA-256: E1631916AF1B48B262AA719609F3A9B09EB83BEBABF81F71C564BD35EEA36F13.

START: group byte, A4, four zero time bytes, two program-prefix bytes.
STOP: group byte, A3. The module's textual +IPD header is not sent over UDP.
Timestamp zero follows the developer's earlier instructions for PC-originated
commands; frame-exact synchronization with Photon is not claimed.

The workbook does not specify prefix endianness. This candidate uses
little-endian, pending developer/device confirmation. For class 1/program 1:

```
START -> broadcast:5001  01 A4 00 00 00 00 01 00
STOP  -> broadcast:5001  01 A3
```

## Verification and artifact

- Host sender/protocol tests: PASS.
- Existing managed reconnect regression tests: PASS.
- S3 PlatformIO compilation: PASS.
- RAM: 51,404 bytes. Application flash: 1,328,917 / 2,097,152 bytes.
- No real Photon test, flashing or public OTA publication performed.

Application-only OTA image: `AuraX2-S3-1.1-build8.bin` (not a full-flash image).
Size: 1,329,312 bytes.
SHA-256: C75D3170C6AF8A4FE56D767EFAE145D1CFFDBC06916C6A1516A61686BAC655E7.

Pre-change commit: 11be973ffbfcf6e4b6bb075e362fea1476e71cc0 (build 7).
Do not erase flash or upload a filesystem image for this update.
