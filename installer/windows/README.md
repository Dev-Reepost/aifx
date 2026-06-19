# AIFX Windows installer

A native [Inno Setup 6](https://jrsoftware.org/isdl.php) wizard that installs
the seven AIFX OpenFX plugin bundles and bakes the operator's site
configuration into each bundle's `defaults.json`. It is the Windows counterpart
of the macOS `.dmg` wizard in [`../macos/`](../macos/).

## Files

| File | Role |
|------|------|
| `aifx-installer.iss` | Inno Setup script: wizard pages, install logic, custom site-config page. |
| `patch-defaults.ps1` | Rewrites each installed bundle's `Contents\Resources\config\defaults.json` with the collected config. Runs post-install via PowerShell. Written for Windows PowerShell 5.1 (what end users have). |
| `Bundles/` | *(generated, gitignored)* the seven `.ofx.bundle` payloads, staged by the release script. |
| `Output/` | *(generated, gitignored)* default ISCC output dir for manual compiles. |

## Building

From the repo root, on Windows:

```powershell
tools\release-windows-installer.ps1 -Version <version>
```

This stages the bundles (preferring `dist\aifx-<version>-windows-x86_64.zip`,
falling back to `build\windows\*.ofx.bundle`), verifies each carries its binary
and `defaults.json`, then compiles the installer to
`dist\aifx-<version>-windows-setup.exe`. Requires `ISCC.exe` (Inno Setup 6) —
`winget install JRSoftware.InnoSetup`.

Optional code-signing: set `$env:AIFX_SIGN_THUMBPRINT` (a cert in the local
store) or `$env:AIFX_SIGN_PFX` + `$env:AIFX_SIGN_PFX_PASSWORD`; the script signs
with `signtool` and an RFC3161 timestamp.

## The config schema

`patch-defaults.ps1` writes exactly the keys the plugin reads at runtime
(`plugins/common/comfyui_base_plugin.cpp`), preserving everything else
(the per-plugin `project` block, the `macos`/`linux` mount entries,
`enableCache`, `asyncMode`, `placeholderMode`):

| Wizard field | JSON key |
|---|---|
| Server address | `server.serverAddress` |
| Server port | `server.serverPort` |
| ComfyUI server's view (UNC) | `storage.serverMountPath` |
| This PC's view (Local Storage Mount) | `storage.localMountPath.windows` |
| Job timeout | `controls.timeout` |
| Enable processing by default | `controls.enableProcessing` |

See [`docs/configuration.md`](../../docs/configuration.md) for the full schema.
