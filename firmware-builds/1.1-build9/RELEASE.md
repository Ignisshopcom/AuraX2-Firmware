# AuraX 1.1 build 9 - Timed Photon program bridge

Based on build 8 (7011f40aab01dfa41a879fe0f207d9eb93984c7d).
The previous artifact remains unchanged and available for rollback.

## Changes

- Send Photon START/STOP before ESP-NOW retries and local program loading.
- START carries elapsed time from the existing AuraX request epoch, including
  browser-supplied request age, instead of a constant zero timestamp.
- Recompute elapsed time immediately before sending to each selected group.
- Use an incrementing 8-bit packet ID; group selection stays in destination
  UDP port 5000 + group (5001..5010). One datagram per selected group.
- Keep existing SYNC, station-connected and relay guards. No Photon messages
  from effect selection, low battery or received ESP-NOW commands. No new
  retries, heartbeat, networking task or acknowledgment wait.
- Wi-Fi reconnect, storage, LED drivers and AuraX ESP-NOW code are unchanged.

## Evidence and wire format

Source: supplied Wifi_commands.xlsx and passive traffic captured 2026-09-22.
The workbook labels the first byte `id` and the four time bytes
`master current prg time`. Actual Photon broadcasts increment the first byte
while retaining destination port 5001. Prefix and time are little-endian.

The timing unit is inferred from 27 consecutive Photon packets: elapsed time
increased by 780258 ticks over 7.794815 seconds, approximately 10 us per tick.
Observed packets repeated approximately every 300 ms. This build does not add
periodic Photon broadcasts or assume their receiver behavior is documented.

Captured fixture: `2C A4 E1 14 0A 00 01 00` on port 5001 is packet ID 44,
elapsed time 660705 ticks, program prefix 001. Host tests reproduce it exactly.

START: ID (1 byte), A4, elapsed 10-us ticks (uint32 LE), prefix (uint16 LE).
STOP: ID (1 byte), A3. Counter wraps modulo 256. Future request epochs clamp
elapsed time to zero; values beyond uint32 range saturate.

The age field compensates time already spent before UDP transmission. It cannot
by itself measure later network transit or receiver processing. Physical
catch-up accuracy remains unverified: the current Photon program has no
recognizable opening cue for a time-offset comparison. No claim of frame-exact
or guaranteed synchronization is made.

## Verification

- Production Photon sender host tests and call-order checks: PASS.
- Managed Wi-Fi reconnect regression tests: PASS.
- PlatformIO seeed_xiao_esp32s3 compilation: PASS.
- RAM: 51404 bytes. Application flash: 1329145 / 2097152 bytes.
- No device flashing or public OTA publication performed for build 9.

Application-only OTA image: `AuraX2-S3-1.1-build9.bin`.
Size: 1329536 bytes.
SHA-256: F0A213C960E079EFAB209C97CA12B8F758694F370F5463B235ECAFB8A91BB11D.

Do not erase flash or upload a filesystem image for this update.
