# AuraX2 Firmware

ESP32-S3 firmware for AuraX LED controllers.

This repository contains the PlatformIO firmware project, embedded web UI, WLED migration helpers, AXP/PIX playback support, LED drivers, effects, sync, and OTA-ready build configuration.

## Build

```bash
pio run -e esp32s3dev_8MB
```

The current WLED-compatible 8 MB target uses the 2 MB OTA partition layout from `partitions_8MB.csv`.
