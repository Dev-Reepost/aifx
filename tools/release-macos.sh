#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright AIFX contributors.
#
# Build all AIFX plugins as macOS universal binaries (arm64 + x86_64) and
# package them into a release tarball.
#
# Usage: tools/release-macos.sh [VERSION]
#   VERSION defaults to "dev" if not supplied.

set -e

VERSION="${1:-dev}"

# Plugin targets matching each plugins/<dir>/CMakeLists.txt add_library() name.
TARGETS=(
    DepthAnything3
    DepthCrafter
    NormalCrafter
    SegmentationSAM3
    MatteMaMa
    MatteMA2
    UpscaleSeedVR2
)

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

if [[ "$OSTYPE" != "darwin"* ]]; then
    echo "[ERROR] release-macos.sh must be run on macOS." >&2
    exit 1
fi

echo "==> Building all ${#TARGETS[@]} AIFX plugins as universal binaries..."
for target in "${TARGETS[@]}"; do
    echo ""
    echo "==> $target"
    ./tools/build-macos-universal-plugin.sh -p "$target" -t "$target"
done

echo ""
echo "==> Completing bundles (copying Info.plist + Resources from arm64 stage)..."
for target in "${TARGETS[@]}"; do
    src="build/arm64/Release/${target}.ofx.bundle"
    dst="build/Release/${target}.ofx.bundle"
    if [[ ! -d "$src/Contents/Resources" ]]; then
        echo "[ERROR] Missing arm64 staging bundle: $src" >&2
        exit 1
    fi
    cp -R "$src/Contents/Info.plist" "$dst/Contents/Info.plist"
    cp -R "$src/Contents/Resources"  "$dst/Contents/Resources"
done

echo ""
echo "==> Verifying bundles..."
for target in "${TARGETS[@]}"; do
    bin="build/Release/${target}.ofx.bundle/Contents/MacOS/${target}.ofx"
    if [[ ! -f "$bin" ]]; then
        echo "[ERROR] Missing bundle binary: $bin" >&2
        exit 1
    fi
    archs="$(lipo -archs "$bin")"
    if [[ "$archs" != *"arm64"* ]] || [[ "$archs" != *"x86_64"* ]]; then
        echo "[ERROR] $target is not universal: $archs" >&2
        exit 1
    fi
    echo "    [OK] $target ($archs)"
done

echo ""
echo "==> Packaging..."
TOP="AIFX-${VERSION}-macos-universal"
STAGE="dist/staging/${TOP}"
rm -rf dist
mkdir -p "$STAGE"

cp -R build/Release/*.ofx.bundle "$STAGE/"

cat > "$STAGE/README.txt" <<EOF
AIFX ${VERSION} -- macOS Universal (arm64 + x86_64)
====================================================

This archive contains seven OpenFX plugin bundles built for macOS as
universal binaries (Apple Silicon + Intel).

To install:

  1. Copy every *.ofx.bundle directory in this archive into the standard
     OFX plugin directory for macOS:

       ~/Library/OFX/Plugins/        (this user only -- recommended)
       /Library/OFX/Plugins/         (all users on this machine)

  2. If macOS quarantines a bundle (downloaded-from-internet warning):

       xattr -dr com.apple.quarantine '<path>/<Plugin>.ofx.bundle'

  3. Restart your OFX host (Flame, Nuke, Resolve, Fusion, ...).
     Plugins appear under the AIFX category in the effect/filter browser.

The plugins require a running ComfyUI server with matching custom nodes.
See the documentation:

  https://dev-reepost.github.io/aifx/comfyui-server-setup/

Plugin code:   BSD-3-Clause.
Model weights: per upstream, see each plugin page.
Funding:       CNC (Centre national du cinema et de l'image animee).

Status: pre-release. Plugins are functional but undergoing testing in
production hosts.

EOF

TAR_PATH="dist/aifx-${VERSION}-macos-universal.tar.gz"
( cd dist/staging && tar -czf "../$(basename "$TAR_PATH")" "$TOP" )
rm -rf dist/staging

echo ""
echo "==> Release artifact:"
ls -lh "$TAR_PATH"
echo ""
echo "==> SHA-256 for release notes:"
shasum -a 256 "$TAR_PATH"
echo ""
echo "==> Next steps:"
echo "    1. Tag the commit:    git tag -a v${VERSION} -m \"AIFX v${VERSION}\""
echo "    2. Push the tag:      git push origin v${VERSION}"
echo "    3. Create release:    gh release create v${VERSION} \"$TAR_PATH\" --prerelease"
echo "       (or do it via the GitHub web UI)"
