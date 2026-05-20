# Releasing AIFX

How to cut a new release with prebuilt bundles attached.

## Versioning

[Semantic versioning](https://semver.org). Bump the version number in the
git tag (`vMAJOR.MINOR.PATCH`) and in `CHANGELOG.md`.

## macOS — Universal (arm64 + x86_64)

On any Mac with Conan 2 and CMake 3.28+ installed:

```bash
VERSION=0.1.0
cd /path/to/aifx

# 1. Build each plugin as a universal binary.
for target in DepthAnything3 DepthCrafter NormalCrafter \
              SegmentationSAM3 MatteMaMa MatteMA2 UpscaleSeedVR2; do
  ./tools/build-macos-universal-plugin.sh -p "$target" -t "$target"
done

# 2. Each plugin's POST_BUILD step only ran in the per-arch builds. The
#    final universal bundles at build/Release/*.ofx.bundle/ have only the
#    binary. Copy Info.plist and Resources/ from the arm64 staging build:
for target in DepthAnything3 DepthCrafter NormalCrafter \
              SegmentationSAM3 MatteMaMa MatteMA2 UpscaleSeedVR2; do
  src="build/arm64/Release/${target}.ofx.bundle"
  dst="build/Release/${target}.ofx.bundle"
  cp    "$src/Contents/Info.plist" "$dst/Contents/Info.plist"
  cp -R "$src/Contents/Resources"  "$dst/Contents/Resources"
done

# 3. Package.
TOP="AIFX-${VERSION}-macos-universal"
STAGE="dist/staging/${TOP}"
mkdir -p "$STAGE"
cp -R build/Release/*.ofx.bundle "$STAGE/"
# Include the install README from the source tree:
cp dist/macos-readme.txt "$STAGE/README.txt"   # or write inline as below

# 4. Tarball.
( cd dist/staging && tar -czf "../aifx-${VERSION}-macos-universal.tar.gz" "${TOP}" )

# 5. SHA-256 for the release notes.
shasum -a 256 "dist/aifx-${VERSION}-macos-universal.tar.gz"
```

The same flow is encoded in `tools/release-macos.sh` (see below).

## Linux — x86_64

Not yet automated. Once a Linux build machine is set up:

```bash
./tools/build-linux-universal-plugin.sh -p <Target> -t <Target>
# (or per-plugin via build-linux-plugin.sh)
```

Package as `aifx-<version>-linux-x86_64.tar.gz` following the same layout.

## Windows — x86_64

Not yet automated. Once a Windows build machine is set up, run
`tools/build-windows-plugin.ps1` for each plugin in PowerShell.
Package as `aifx-<version>-windows-x86_64.zip` (Windows users expect a zip,
not a tarball).

## Creating the GitHub Release

1. Tag the commit and push:

   ```bash
   git tag -a v${VERSION} -m "AIFX v${VERSION}"
   git push origin v${VERSION}
   ```

2. On GitHub: **Releases → Draft a new release** for tag `v${VERSION}`.

3. Title: `AIFX v${VERSION}`.

4. Body: pull from `CHANGELOG.md` for that version, plus the SHA-256 sums
   of every attached artifact.

5. Attach the tarballs / zips you built.

6. Until V1.0.0 is reached, tick **"Set as a pre-release"**.

7. Publish.

## Or via `gh` CLI

```bash
gh release create v${VERSION} \
  dist/aifx-${VERSION}-macos-universal.tar.gz \
  --title "AIFX v${VERSION}" \
  --notes-file release-notes.md \
  --prerelease
```
