# AuraX firmware 1.1 build 5

Candidate build for ESP32-S3.

## Change

- A firmware-upload notice appears only when the UI was opened by the operating
  system's captive portal.
- The notice links to `http://192.168.4.1` and explains that firmware should be
  uploaded from Safari or Chrome if the captive portal cannot open a file picker.
- Opening the UI directly in a browser or AuraX Finder keeps the original
  behavior: **Upload firmware file** immediately opens the `.bin` file picker.
- Wi-Fi connection behavior and the OTA upload handler are unchanged.

## Verification

- Direct browser route: file picker opens immediately; no notice is displayed.
- Captive-portal route: notice is hidden until **Upload firmware file** is pressed.
- Notice layout checked at desktop and 390 x 844 mobile viewport.
- PlatformIO `seeed_xiao_esp32s3` build completed successfully.

SHA-256: `E1078E86885CDF2910A7E31AF8D67466A3408DC2AA097045EFB4468B342BEEEF`
