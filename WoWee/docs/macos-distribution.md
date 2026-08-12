# macOS signing and notarization

WoWee distributes two macOS apps in one DMG: `Wowee.app` and the independent
`Wowee Asset Extractor.app` launcher. The main app also contains the
`asset_extract` executable. Every Mach-O component is signed inside-out with a
Developer ID Application identity and the hardened runtime before the DMG is
signed, submitted to Apple's notary service, and stapled.

The bundle keeps executables and dylibs in `Contents/MacOS` and
`Contents/Frameworks`. Assets, expansion data, and helper scripts belong in
`Contents/Resources`; placing unsigned data under `Contents/MacOS` prevents
Apple from sealing the bundle correctly.

## GitHub Actions secrets

Configure these repository secrets before running a trusted build or creating
a `v*` release tag:

- `APPLE_DEVELOPER_ID_CERT_P12_BASE64`: base64-encoded Developer ID identity
  and private key in PKCS#12 format
- `APPLE_DEVELOPER_ID_CERT_PASSWORD`: PKCS#12 export password
- `APPLE_DEVELOPER_ID_IDENTITY`: full `Developer ID Application: ...` identity
- `APPLE_NOTARY_KEY_P8_BASE64`: base64-encoded App Store Connect API private key
- `APPLE_NOTARY_KEY_ID`: App Store Connect API key ID
- `APPLE_NOTARY_ISSUER_ID`: App Store Connect issuer ID

The API key needs only the App Store Connect `Developer` role for notarization.
The private `.p8` key is downloadable once, so retain an encrypted backup.

Pull requests without secrets still receive an ad-hoc bundle build for compile
and packaging coverage. Manual trusted builds and tagged release builds fail
closed if signing or notarization credentials are absent.

## Verification

The workflows verify every bundled Mach-O component and run the following trust
checks after notarization:

```bash
codesign --verify --deep --strict --verbose=2 Wowee.app
xcrun stapler validate Wowee.dmg
spctl --assess --type open --context context:primary-signature \
  --verbose=2 Wowee.dmg
```

A successful DMG assessment reports `source=Notarized Developer ID`.
