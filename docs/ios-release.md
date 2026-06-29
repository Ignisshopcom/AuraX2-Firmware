# AuraX Finder iOS Release

This repo can build the iOS companion app in two modes:

- Simulator app: no Apple signing required.
- Signed iPhone `.ipa`: requires Apple Developer Program membership and GitHub secrets.

The iOS app lives in `ios/AuraXFinder` and uses bundle ID:

```text
com.aurax.finder
```

## Apple Developer Setup

1. Enroll in the Apple Developer Program at:
   `https://developer.apple.com/programs/`
2. Open App Store Connect:
   `https://appstoreconnect.apple.com/`
3. Create a new app for bundle ID `com.aurax.finder`.
4. In Certificates, Identifiers & Profiles, create:
   - An App ID / Identifier for `com.aurax.finder`
   - An Apple Distribution certificate
   - An App Store provisioning profile for `com.aurax.finder`
5. Export the distribution certificate as `.p12`.
6. Download the provisioning profile as `.mobileprovision`.
7. Create an App Store Connect API key for TestFlight upload.

## GitHub Secrets

Add these in GitHub:

`Settings` -> `Secrets and variables` -> `Actions` -> `New repository secret`

Required to build a signed `.ipa`:

```text
APPLE_TEAM_ID
IOS_CERTIFICATE_P12_BASE64
IOS_CERTIFICATE_PASSWORD
IOS_PROVISIONING_PROFILE_BASE64
IOS_PROVISIONING_PROFILE_NAME
```

Optional, only needed for TestFlight upload:

```text
APPSTORE_API_KEY_ID
APPSTORE_API_ISSUER_ID
APPSTORE_API_PRIVATE_KEY
IOS_KEYCHAIN_PASSWORD
```

`IOS_PROVISIONING_PROFILE_NAME` must match the profile name shown in Apple Developer, not the file name.

## Encoding Files For GitHub Secrets

PowerShell:

```powershell
[Convert]::ToBase64String([IO.File]::ReadAllBytes("C:\path\to\certificate.p12")) | Set-Clipboard
[Convert]::ToBase64String([IO.File]::ReadAllBytes("C:\path\to\profile.mobileprovision")) | Set-Clipboard
```

Paste the first output into `IOS_CERTIFICATE_P12_BASE64`.
Paste the second output into `IOS_PROVISIONING_PROFILE_BASE64`.

For `APPSTORE_API_PRIVATE_KEY`, paste the full `.p8` text, including the `BEGIN PRIVATE KEY` and `END PRIVATE KEY` lines.

## Build From GitHub

1. Go to `Actions`.
2. Open `iOS Finder`.
3. Click `Run workflow`.
4. Enable `Build signed iPhone .ipa`.
5. Enable `Upload signed .ipa to App Store Connect / TestFlight` only after App Store Connect API secrets are set.

The `.ipa` artifact is named:

```text
AuraXFinder-iPhone-ipa
```

