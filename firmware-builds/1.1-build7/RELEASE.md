# AuraX firmware 1.1 build 7

Final integration artifact, superseding build 6.

Includes the user-tested `AuraX2_iPhone_test` managed reconnect logic, with one
additional review fix: Arduino 2.0.6 leaves its public status unchanged on
AUTH_EXPIRE. A disconnected station can therefore still report WL_CONNECTED.
Reconnect and service maintenance now also consult the associated-state event
snapshot rather than treating that stale status as a live connection.

The new regression test failed before this fix and passes after it.

## Verification

- S3 PlatformIO build: PASS. RAM 51,404 bytes; flash 1,328,421 / 2,097,152 bytes.
- Production reconnect host tests: PASS, including AUTH_EXPIRE, bounded retry
  delays, first framework retry grace, DHCP waiting, watchdog, AP and clock wrap.
- Audio stream validation and packet timing tests: PASS.
- Ten simulated group receivers, loss, ordering, STOP and recovery: PASS.
- All ten audio effects, parameter extremes, frequency isolation, smoothing,
  intermediate frames, LED counts 0..1000 and identical group output: PASS.
- UI, captive upload notice, OTA upload handler, drivers, storage, playback,
  ESP-NOW implementation and partition layout unchanged from the build 5 backup.

This is not a hardware certification: no device was flashed in this task and
no new live iPhone hotspot test was performed. The public OTA manifest was not
changed. See `REVIEW.md` for residual risks and recommended device checks.

## OTA image

File: `AuraX2-S3-1.1-build7.bin`

Size: 1,328,816 bytes.

SHA-256: `B6D25DAE40287D80063F8C1345F17E779F862F3C3DDBF9BCF865E70140C02141`

Application-only OTA image; do not use as a full-flash image at address zero.
No erase or filesystem upload is needed for this update on the existing layout.

Pre-integration backup: `ae25c2e505b75486d49366a5b559387e35841eae`;
tag `backup/aurax-1.1-build5-before-iphone-20260921`.
