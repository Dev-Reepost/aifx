#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright AIFX contributors.
#
# Build all AIFX plugins as macOS universal binaries (arm64 + x86_64) and
# package them into a release tarball.
#
# Usage: tools/release-macos.sh [VERSION]
#   VERSION defaults to the repo-root VERSION file.

set -e

# Version comes from the repo-root VERSION file, the same source CMake stamps
# into every bundle. Passing an explicit VERSION is still allowed (for a dry run
# or a re-tag) but it must agree -- otherwise the tarball name and the version
# baked into the binaries would disagree, and the artifact becomes unidentifiable
# the moment it leaves this machine.
REPO_VERSION="$(tr -d '[:space:]' < "$(dirname "${BASH_SOURCE[0]}")/../VERSION")"
VERSION="${1:-$REPO_VERSION}"
if [[ "$VERSION" != "$REPO_VERSION" ]]; then
    echo "[ERROR] Requested version '$VERSION' does not match VERSION file '$REPO_VERSION'." >&2
    echo "        Update the repo-root VERSION file first (see RELEASING.md)." >&2
    exit 1
fi

# Plugin set comes from plugins/manifest.txt (single source of truth).
source "$(dirname "${BASH_SOURCE[0]}")/plugin-manifest.sh"
TARGETS=("${AIFX_PLUGIN_TARGETS[@]}")

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

# Identity + ABI gate. Every bundle must carry a build stamp matching this
# source tree before it is allowed into a tarball: an unidentifiable bundle on a
# workstation is what turned a fixed crash into a month-long field bug.
echo ""
echo "==> Verifying build identity + OFX ABI..."
./tools/verify-ofx-abi.sh --expect-version "$VERSION" \
    $(printf 'build/Release/%s.ofx.bundle ' "${TARGETS[@]}")

echo ""
echo "==> Packaging..."
TOP="AIFX-${VERSION}-macos-universal"
STAGE="dist/staging/${TOP}"
# Only clear THIS script's staging area. `rm -rf dist` (what this used to do)
# also deleted the Linux and Windows artifacts sitting alongside -- building the
# macOS release destroyed the other platforms' output, which is never what the
# per-platform release flow in RELEASING.md intends. release-linux.sh and
# release-windows.ps1 already scope their cleanup this way.
rm -rf dist/staging
mkdir -p "$STAGE"

# Drop any legacy top-level <bundle>/Resources/ directory. Resources live under
# Contents/Resources/ (OFX spec) and are only read from there; a top-level copy
# is a stale incremental-build leftover and must not ship.
for bundle in build/Release/*.ofx.bundle; do
    rm -rf "$bundle/Resources"
done

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
Authors:       MaGMa for Reepost Studio
               https://www.reepoststudio.fr/
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
