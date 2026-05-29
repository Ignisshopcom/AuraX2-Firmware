# AuraX Finder for iOS

Native SwiftUI companion app for finding AuraX devices on the local network and opening the ESP web UI in an embedded WebView.

Discovery currently uses:

- Bonjour/mDNS service `_aurax._tcp`
- direct fallback probe of `http://192.168.4.1/status` for AuraX AP mode

The app does not require internet while running. iOS will ask for Local Network permission the first time discovery touches the LAN.

## Build

Open `ios/AuraXFinder/AuraXFinder.xcodeproj` in Xcode and run the `AuraXFinder` scheme.

GitHub Actions can build the simulator app without Apple signing:

```bash
xcodebuild \
  -project ios/AuraXFinder/AuraXFinder.xcodeproj \
  -scheme AuraXFinder \
  -configuration Debug \
  -sdk iphonesimulator \
  -destination "generic/platform=iOS Simulator" \
  CODE_SIGNING_ALLOWED=NO \
  build
```

An installable `.ipa` for real iPhones requires an Apple Developer account, a signing certificate, and a provisioning profile. Add those as GitHub Secrets before enabling device/TestFlight export.
