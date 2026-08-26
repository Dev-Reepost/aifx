# Releasing AIFX

How to cut a new release with prebuilt bundles attached.

A release is: an updated `CHANGELOG.md`, a bumped download link on the landing
page, a git tag `vMAJOR.MINOR.PATCH`, and a GitHub pre-release with one or more
platform artifacts attached. Each platform is built by its own automated script;
you only run the scripts for the platforms whose binaries changed (see
[Per-platform vs. full releases](#per-platform-vs-full-releases)).

## Versioning

[Semantic versioning](https://semver.org). The version lives in the repo-root
[`VERSION`](VERSION) file — **that is the single source of truth**, and the
first step of any release is to edit it.

From there CMake stamps it into every artifact automatically:

- the `.ofx` binary itself (`AIFX-BUILD|version=…`, see `kAifxBuildMarker`)
- `Contents/Resources/aifx-build.txt` in each bundle
- `Contents/Info.plist` (`CFBundleVersion`) on macOS

and every `tools/release-*` script reads the same file, refusing to run if an
explicit version argument disagrees with it.

Four things still have to be updated by hand and must agree with `VERSION`: the
git tag (`vMAJOR.MINOR.PATCH`), the top `CHANGELOG.md` entry, the
**Download vX.Y.Z** link in `docs/index.html`, and the `data-version` attribute
on the `#aifx-smart-download` button in the same file (the OS-detecting hero
button builds its installer URLs from it).

### Why the artifacts carry their version

Before 0.2.1 every bundle claimed `1.0.0` and the binary carried no stamp, so a
plugin installed on a workstation was indistinguishable from any other build.
A pre-0.1.5 macOS bundle — compiled without `OFX_SUPPORTS_OPENGLRENDER`, two
vtable slots short of the `OfxSupport` archive it was linked against — therefore
survived unnoticed in `/Library/OFX/Plugins` and kept segfaulting Flare on
instancing long after the fix had shipped. Identify a bundle in the field with:

```bash
cat "<bundle>.ofx.bundle/Contents/Resources/aifx-build.txt"
# or, authoritatively, from the binary:
strings "<bundle>.ofx.bundle/Contents/MacOS/<name>.ofx" | grep AIFX-BUILD
```

## Build-time and artifact-time guards

Three layers, each catching what the previous one cannot:

| Layer | Where | Catches |
|-------|-------|---------|
| `static_assert` | [`plugins/common/aifx_abi_check.h`](plugins/common/aifx_abi_check.h) | A translation unit whose view of the OpenFX Support ABI (vtable length, render-argument layout) disagrees with the `OfxSupport` archive. **Fails the compile.** |
| Portability + identity check | `verify_binary_portability()` in [`tools/build-plugin.sh`](tools/build-plugin.sh) | Non-portable load paths/RPATHs, and a bundle whose build stamp is missing or inconsistent. **Fails the build.** |
| Release gate | `tools/verify-ofx-abi.sh`, called by every `release-*` script | A staged bundle whose stamped version doesn't match the release. **Blocks packaging.** |

`tools/verify-ofx-abi.sh` also runs standalone on a workstation with no build
tree — it is the tool to reach for when a host crashes on a plugin:

```bash
tools/verify-ofx-abi.sh                      # scan this OS's OFX plugin dirs
tools/verify-ofx-abi.sh /Library/OFX/Plugins # or a specific location
```

For bundles predating the stamp it falls back to measuring the emitted
`ComfyUI::BasePlugin` vtable, so the known-bad pre-0.1.5 builds are still
identified.

## Signing the macOS installer

`tools/release-macos-installer.sh` refuses to produce an unsigned or
non-notarised `.dmg`. On macOS 15 (Sequoia) and later, Gatekeeper blocks such an
app with *"Apple could not verify 'AIFX Installer.app' is free of malware"* and
the old right-click → **Open** bypass no longer works, so an unsigned DMG is not
a shippable artifact.

### One-time setup

**1. Create a Developer ID Application certificate.** Requires a *paid* Apple
Developer Program membership and the Account Holder or Admin role — a free Apple
ID cannot issue one. With Xcode installed, the short path is:

> Xcode → Settings (⌘,) → **Accounts** → sign in → select the team →
> **Manage Certificates…** → **+** → **Developer ID Application**

If that entry is greyed out or missing, the membership is not active yet
(enrolment can take a day or two) or the role is insufficient.

Without Xcode, generate a CSR in **Keychain Access → Certificate Assistant →
Request a Certificate From a Certificate Authority** (save to disk), upload it at
[developer.apple.com → Certificates, IDs & Profiles → Certificates → **+** →
Developer ID Application](https://developer.apple.com/account/resources/certificates/list),
then double-click the downloaded `.cer` to install it.

Verify:

```bash
security find-identity -p codesigning -v | grep "Developer ID Application"
# 1) ABC123… "Developer ID Application: Your Name (TEAMID1234)"
```

**2. Store notarisation credentials.** You need the 10-character Team ID (the
parenthesised part of the identity above, or Membership details in the portal)
and an **app-specific password** — generate one at
[account.apple.com → Sign-In and Security → App-Specific Passwords](https://account.apple.com/account/manage).
Your normal Apple ID password will not work.

```bash
xcrun notarytool store-credentials AIFX-NOTARY \
  --apple-id <your-apple-id-email> \
  --team-id <TEAMID1234> \
  --password <xxxx-xxxx-xxxx-xxxx>
```

This writes the credentials into the login keychain once; nothing else needs the
password again.

### Cutting a signed release

```bash
AIFX_NOTARY_PROFILE=AIFX-NOTARY tools/release-macos-installer.sh
```

The script signs the seven embedded `.ofx` binaries, then the installer
executable, then the `.app` (hardened runtime + timestamp), notarises and staples
the app, builds the DMG, then signs, notarises and staples the **image itself**.
Both are needed: the stapled app is what launches, the stapled image is what the
Finder assesses when the user double-clicks. The final step runs the same
`spctl` assessment the user's Mac will run and fails the build if it is rejected.

Notarisation takes a few minutes; `--wait` blocks until Apple answers.

`AIFX_SIGN_IDENTITY` overrides certificate auto-detection. For a local test
build only, `AIFX_ALLOW_UNSIGNED=1` bypasses both gates — never for a public
release.

### After a signed release supersedes an unsigned one

Replace the published asset and drop the workaround from the docs:

```bash
gh release upload v<version> dist/aifx-<version>-macos-installer.dmg --clobber
```

Then remove the "Known issue — macOS blocks the installer" section from the
release notes, the `#aifx-macos-note` block in `docs/index.html`, and the
quarantine step in `docs/installation.md`.

## Artifacts

Each platform produces one archive (the macOS installer `.dmg` is an optional
extra), built by an automated script and named by convention:

| Platform | Script | Artifact |
|----------|--------|----------|
| Linux x86_64 | `tools/release-linux.sh <version>` | `dist/aifx-<version>-linux-x86_64.tar.gz` |
| macOS universal (arm64 + x86_64) | `tools/release-macos.sh <version>` | `dist/aifx-<version>-macos-universal.tar.gz` |
| macOS installer (optional) | `tools/release-macos-installer.sh <version>` | `dist/aifx-<version>-macos-installer.dmg` |
| Windows x86_64 | `tools\release-windows.ps1 -Version <version>` | `dist\aifx-<version>-windows-x86_64.zip` |
| Windows installer (optional) | `tools\release-windows-installer.ps1 -Version <version>` | `dist\aifx-<version>-windows-setup.exe` |

Every script builds all seven plugins (the set comes from
`plugins/manifest.txt`, the single source of truth), stages the
`*.ofx.bundle` directories with a `README.txt`, packages the archive, and prints
its SHA-256. Each must run **on the target OS** — there is no cross-compilation.

## Build prerequisites

All platforms need **CMake 3.28+**, **Conan 2.1+**, a **C++17 compiler**, and
**Git**. `tools/setup-env.sh` (macOS/Linux) bootstraps Conan and the default
profile; on a fresh machine run it once from the repo root.

Platform-specific:

- **Linux** — the **static C++ runtime** (`libstdc++.a`) is required. Each
  `.ofx` links libstdc++/libgcc statically (`-static-libstdc++ -static-libgcc`,
  set in `plugins/CMakeLists.txt`) so the plugin carries its own
  `std::filesystem` and cannot bind to the host's. Without it the build fails to
  link (and a dynamically-linked plugin would crash in DaVinci Resolve — see the
  0.1.8 changelog entry). It ships with the g++ dev package on Debian/Ubuntu
  (`build-essential`); on Rocky/RHEL it is a separate CRB-repo package:

  ```bash
  sudo dnf config-manager --set-enabled crb && sudo dnf install libstdc++-static
  ```

  `tools/setup-env.sh` probes for this and offers to install it;
  `tools/release-linux.sh` preflight-checks it before building.
- **macOS** — Xcode command line tools (Apple Clang). The universal build
  compiles each plugin for arm64 and x86_64 and `lipo`s them together.
- **Windows** — Visual Studio 2022 with the C++ workload. The plugins link the
  standard MSVC runtime, so target machines need the **Microsoft Visual C++
  Redistributable (x64)**. The optional `.exe` installer additionally needs
  **Inno Setup 6** (`ISCC.exe`) on the build machine —
  `winget install JRSoftware.InnoSetup` or
  [jrsoftware.org/isdl.php](https://jrsoftware.org/isdl.php).

## Build the artifacts

Run the script for each platform you are shipping, on that OS. Examples for
`VERSION=0.1.8`:

```bash
# Linux  (on a Linux build box)
tools/release-linux.sh 0.1.8

# macOS  (on a Mac)
tools/release-macos.sh 0.1.8
tools/release-macos-installer.sh 0.1.8     # optional .dmg wizard

# Windows  (in PowerShell)
tools\release-windows.ps1 -Version 0.1.8
tools\release-windows-installer.ps1 -Version 0.1.8   # optional .exe wizard
```

The Windows installer script reuses `dist\aifx-<version>-windows-x86_64.zip`
when present (run `release-windows.ps1` first for a reproducible build),
otherwise it stages from `build\windows\*.ofx.bundle`.

Each prints the artifact path and its SHA-256 — keep those for the release notes.

## Verify the artifacts (Linux)

Linux is the platform most exposed to ELF symbol interposition by the host (the
reason for the static-runtime linkage). After building, confirm each `.ofx` is
self-contained — no dynamic C++ runtime, and `std::filesystem` bound locally:

```bash
for t in DepthAnything3 DepthCrafter NormalCrafter SegmentationSAM3 \
         MatteMaMa MatteMA2 UpscaleSeedVR2; do
  ofx="build/linux/${t}.ofx.bundle/Contents/Linux-x86-64/${t}.ofx"
  echo "== $t =="
  readelf -d  "$ofx" | grep -i 'libstdc++\|libgcc_s' && echo "  !! dynamic C++ runtime (BAD)" || echo "  ok: no dynamic C++ runtime"
  readelf -sW "$ofx" | grep -i '_M_split_cmptsEv$'   # want LOCAL + a real address, never UND
done
```

A correct build shows **no** `libstdc++.so.6`/`libgcc_s` in `NEEDED` and
`_M_split_cmpts` as a `LOCAL` defined symbol (not `UND`).

## Update the changelog and landing page

1. In `CHANGELOG.md`, add a `## [X.Y.Z] - YYYY-MM-DD` section above the previous
   release (move items out of `[Unreleased]`). Lead with a one-paragraph summary;
   group entries under `### Fixed` / `### Changed` / `### Added`. Note explicitly
   if the release ships only some platforms.
2. In `docs/index.html`, bump the **Download vX.Y.Z** hero link.

## Tag, push, and publish

```bash
VERSION=0.1.8

# 1. Commit the changelog + landing bump (plus the code under release).
git add CHANGELOG.md docs/index.html
git commit -m "docs(changelog): record ${VERSION}; bump landing"

# 2. Tag and push both the branch and the tag.
git tag -a "v${VERSION}" -m "AIFX v${VERSION}"
git push origin main
git push origin "v${VERSION}"

# 3. Create the GitHub pre-release with whatever artifacts you built.
gh release create "v${VERSION}" \
  "dist/aifx-${VERSION}-linux-x86_64.tar.gz" \
  --title "AIFX v${VERSION} (Linux)" \
  --notes-file release-notes.md \
  --prerelease
```

Release conventions:

- **Title** names the platforms shipped, e.g. `AIFX v0.1.8 (Linux)` or
  `AIFX v0.1.7 (Windows + Linux)`.
- **Notes** are drawn from the `CHANGELOG.md` entry, plus the SHA-256 of every
  attached artifact.
- **Always tick pre-release** (`--prerelease`) until v1.0.0.
- Attach every artifact in one `gh release create`, or add more later with
  `gh release upload v${VERSION} <file>`.

To do it from the web UI instead: **Releases → Draft a new release**, pick the
existing tag, set the title/notes, attach the archives, tick *Set as a
pre-release*, and publish.

## Per-platform vs. full releases

A release does **not** have to ship all three platforms. Several past releases
were single-platform (e.g. v0.1.4–v0.1.6 Linux-only) because a fix only affected
one OS. When a change is gated to one platform (the static-runtime linkage, for
instance, is `if(UNIX AND NOT APPLE)` in CMake), the other platforms' binaries
are byte-for-byte equivalent to the prior release — rebuilding them is optional.
Just make the scope explicit in the release title and the changelog so users know
which OSes actually changed.
