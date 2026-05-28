# AuraX Finder for Android

Minimal Android app for finding AuraX devices on the current local network and
opening the ESP web UI in an embedded WebView.

Discovery uses two paths:

- UDP port `4210`: sends `AURAX?` and listens for `AURAX <hostname> <ip> ...`
  announcements from firmware.
- HTTP scan: probes private IPv4 `/24` networks visible on the phone and checks
  `http://<ip>/status`.

The app does not require internet. The phone only has to be on the same network
as AuraX devices, or running the hotspot that AuraX devices are connected to.

## Build

Open `android/AuraXFinder` in Android Studio and run:

```text
Build > Build Bundle(s) / APK(s) > Build APK(s)
```

For command line builds, install Android Studio or Android SDK + JDK, then run:

```bash
gradle :app:assembleDebug
```

The first Gradle sync downloads the Android Gradle Plugin from Google/Maven
repositories.

## GitHub APK artifact

The repository also contains `.github/workflows/android-finder.yml`. Run the
`Android Finder` workflow manually, or push changes under `android/AuraXFinder`,
and download the `AuraXFinder-debug-apk` artifact from the workflow run.
