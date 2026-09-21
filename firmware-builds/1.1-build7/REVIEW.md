# Integration review - 2026-09-21

## Scope and result

Reviewed the complete integration diff and the surrounding startup, reconnect,
fallback AP, ESP-NOW interface refresh, status, captive portal and OTA paths.
Compared reconnect code with the tested iPhone source and the installed Arduino
2.0.6 WiFiGeneric.cpp event implementation. This is not an exhaustive audit of
all historical firmware code or a guarantee of compatibility with every phone.

## Fixed finding

Arduino's AUTH_EXPIRE path does not update WiFiSTAClass status. The original
candidate checked WL_CONNECTED before considering its disconnect event, so it
could suppress retries and continue treating STA services as connected. Added
the associated-event check to processManagedReconnect and maintainWifi. A host
test reproduces stale WL_CONNECTED after AUTH_EXPIRE: it fails on build 6 and
passes on build 7. No changes to retry timing or AP policy were needed.

## Existing risks left unchanged

1. `WifiControl::startFallbackAp` logs the result of startSoftApRadio but marks
   the AP active even when that call fails. On a radio initialization failure,
   the firmware can report AP mode without a working AP. This predates the
   integration. A retry policy should be hardware-tested separately to avoid
   introducing mode changes during playback or disconnecting AP clients.
2. `WifiControl::checkFirmwareManifest` performs synchronous HTTPS on the Wi-Fi
   control task. DNS/TLS/HTTP delays can temporarily delay web requests and sync
   processing. Audio-active checks already reject this operation. This predates
   the integration; a worker/task redesign was not mixed into the Wi-Fi fix.

## Verification boundaries

Host tests execute the actual retry/event functions with mocked clock, radio and
service operations. They cannot prove real cross-task scheduling, RF behavior,
driver failure recovery, hotspot compatibility or over-the-air upgrade success.
The LED renderer, storage and partition files match the pre-integration backup;
existing audio tests were rerun rather than merely relying on that equality.

## Device acceptance before broad release

- Upgrade one test unit via its existing OTA UI without erase; check retained
  SSID, LED pins, current limit, programs and saved effect settings.
- Repeated cold starts on an iPhone hotspot with 2.4 GHz compatibility enabled.
- Switch the hotspot off/on within the retry window, then leave it off until AP
  fallback; confirm the AP remains accessible without scanning/channel churn.
- Confirm an already running stored program/effect is not stopped by Wi-Fi loss.
- Check captive entry versus direct browser/Finder firmware upload behavior.
- Run real multi-device audio and sync while monitoring disconnect counters and
  free heap in /status. Host simulation is not a substitute for this RF test.
