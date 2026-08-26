# AIFX macOS installer

A native SwiftUI wizard that installs the seven AIFX OpenFX plugin bundles and
bakes the operator's site configuration into each bundle's `defaults.json`. It
is the macOS counterpart of the Windows `.exe` wizard in
[`../windows/`](../windows/), shipped as a `.dmg`.

## Files

| File | Role |
|------|------|
| `Package.swift` | Swift Package Manager manifest for the installer executable. |
| `Sources/AIFXInstaller/AIFXInstaller.swift` | The wizard: site-config form, the `defaults.json` rewrite, and the copy/patch/de-quarantine install logic. |
| `app/Info.plist.in` | App bundle `Info.plist` template; the version is baked in at build time. |
| `app/Entitlements.plist` | Hardened-runtime entitlements used when signing/notarising. |
| `app/AppIcon.icns` | App icon. |
| `Resources/Bundles/` | *(generated, gitignored)* the seven `.ofx.bundle` payloads, staged by the release script and embedded in the app. |
| `build/` | *(generated, gitignored)* the assembled `AIFX Installer.app`. |

## Building

From the repo root, on macOS:

```bash
tools/release-macos-installer.sh <version>
```

This stages the bundles (preferring `dist/aifx-<version>-macos-universal.tar.gz`
from `tools/release-macos.sh`, falling back to `build/Release/*.ofx.bundle`),
runs `swift build -c release`, wraps the executable into `AIFX Installer.app`
with the embedded bundles and a version-stamped `Info.plist`, and packages it as
`dist/aifx-<version>-macos-installer.dmg`.

Optional signing + notarisation: set `AIFX_SIGN_IDENTITY` (a *Developer ID
Application* identity) and/or `AIFX_NOTARY_PROFILE` (a stored
`notarytool` credential). With neither set the script skips both cleanly and
end users get a Gatekeeper warning on first launch (right-click → **Open**, or
`find '/path/to/AIFX Installer.app' -exec xattr -d com.apple.quarantine {} \;`).

## The config schema

The wizard writes exactly the keys the plugin reads at runtime
(`plugins/common/comfyui_base_plugin.cpp`), preserving everything else (the
per-plugin `project` block, the other `localMountPath` OS entries, `enableCache`,
`asyncMode`, `placeholderMode`):

| Wizard field | JSON key |
|---|---|
| Server address | `server.serverAddress` |
| Server port | `server.serverPort` |
| This Mac's view of the share | `storage.localMountPath.macos` |
| ComfyUI server's view (UNC) | `storage.serverMountPath` |
| Job timeout (seconds) | `controls.timeout` |
| Enable processing by default | `controls.enableProcessing` |

Ticking **Keep each plugin's bundled defaults** skips the rewrite entirely and
installs the stock `defaults.json` (configure in the host UI instead).

See [`docs/configuration.md`](../../docs/configuration.md) for the full schema.
