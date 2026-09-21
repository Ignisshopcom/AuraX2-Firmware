# AuraX firmware 1.1 build 6

Integrates the Wi-Fi reconnect implementation from `AuraX2_iPhone_test` into
the audio-capable 1.1 build 5 source. The user reported improved iPhone hotspot
stability with that candidate.

- Disable overlapping Arduino auto-reconnect and periodic WiFi.begin calls.
- Process disconnect events on the existing control task, with bounded backoff,
  first-framework-retry grace, and a missing-event watchdog.
- Preserve the 30-second connection window and stable AP fallback without scans.
- Expose reconnect counters and disconnect reasons through `/status`.
- Preserve audio, ESP-NOW, LED drivers, storage, partition layout, OTA handlers,
  and the captive-portal-only upload notice from build 5.

## Verification

- PlatformIO `seeed_xiao_esp32s3`: successful build.
- RAM: 51,404 bytes; application flash: 1,328,397 / 2,097,152 bytes.
- Host tests execute the actual event/reconnect functions with hardware stubs.
- Imported connection logic matches the user-tested iPhone candidate verbatim.
- No device flashing, live iPhone test, or public OTA manifest update in this task.

## Artifact

`AuraX2-S3-1.1-build6.bin` is an application/OTA image, not a full-flash image.
Do not erase flash or upload a filesystem image for this update.

Size: 1,328,784 bytes.

SHA-256: `E0EB6D1F7B3A25B3ED2D594BE6A7F752F28C16003EE59B91DA395EB1D1DA75D2`

Pre-integration backup commit: `ae25c2e505b75486d49366a5b559387e35841eae`.

Backup tag: `backup/aurax-1.1-build5-before-iphone-20260921`.
