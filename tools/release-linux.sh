#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright AIFX contributors.
#
# Build all AIFX plugins for Linux x86_64 and package them into a release
# tarball. The Docker-based build in tools/build-linux-plugin.sh is used so
# the host machine only needs Docker installed.
#
# Usage: tools/release-linux.sh [VERSION]
#   VERSION defaults to "dev" if not supplied.

set -e

VERSION="${1:-dev}"
ARCH="x86_64"

# Plugin targets matching each plugins/<dir>/CMakeLists.txt add_library() name.
# Each entry is "<plugin-dir>:<target>" so we can pass both to build-plugin.
TARGETS=(
    "depth_da3:DepthAnything3"
    "depth_crafter:DepthCrafter"
    "normal_crafter:NormalCrafter"
    "segmentation_sam3:SegmentationSAM3"
    "matte_mama:MatteMaMa"
    "matte_ma2:MatteMA2"
    "upscale_seedvr2:UpscaleSeedVR2"
)

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

if [[ "$OSTYPE" != "linux"* ]]; then
    echo "[ERROR] release-linux.sh must be run on Linux." >&2
    exit 1
fi

if ! command -v docker >/dev/null 2>&1; then
    echo "[ERROR] docker is required but not found in PATH." >&2
    exit 1
fi

echo "==> Building all ${#TARGETS[@]} AIFX plugins for Linux ${ARCH}..."
for spec in "${TARGETS[@]}"; do
    plugin_dir="${spec%%:*}"
    target="${spec##*:}"
    echo ""
    echo "==> $target ($plugin_dir)"
    ./tools/build-linux-plugin.sh "plugins/$plugin_dir" "$target"
done

echo ""
echo "==> Verifying bundles..."
for spec in "${TARGETS[@]}"; do
    target="${spec##*:}"
    # The Linux build produces bundles directly under build/linux/Release/.
    bundle="build/linux/Release/${target}.ofx.bundle"
    bin="${bundle}/Contents/Linux-${ARCH}/${target}.ofx"
    if [[ ! -f "$bin" ]]; then
        echo "[ERROR] Missing bundle binary: $bin" >&2
        exit 1
    fi
    arch_check="$(file "$bin" | grep -o 'ELF.*x86-64' || true)"
    if [[ -z "$arch_check" ]]; then
        echo "[ERROR] $target binary is not x86_64 ELF: $(file "$bin")" >&2
        exit 1
    fi
    echo "    [OK] $target"
done

echo ""
echo "==> Packaging..."
TOP="AIFX-${VERSION}-linux-${ARCH}"
STAGE="dist/staging/${TOP}"
rm -rf dist/staging
mkdir -p "$STAGE"

for spec in "${TARGETS[@]}"; do
    target="${spec##*:}"
    cp -R "build/linux/Release/${target}.ofx.bundle" "$STAGE/"
done

cat > "$STAGE/README.txt" <<EOF
AIFX ${VERSION} -- Linux ${ARCH}
====================================================

This archive contains seven OpenFX plugin bundles built for Linux ${ARCH}.

To install:

  1. Copy every *.ofx.bundle directory in this archive into the standard
     OFX plugin directory for Linux:

       /usr/OFX/Plugins/             (all users on this machine)
       \$HOME/.OFX/Plugins/           (this user only)

  2. Restart your OFX host (Nuke, Fusion, Flame, Resolve, ...).
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

mkdir -p dist
TAR_PATH="dist/aifx-${VERSION}-linux-${ARCH}.tar.gz"
( cd dist/staging && tar -czf "../$(basename "$TAR_PATH")" "$TOP" )
rm -rf dist/staging

echo ""
echo "==> Release artifact:"
ls -lh "$TAR_PATH"
echo ""
echo "==> SHA-256 for release notes:"
sha256sum "$TAR_PATH"
echo ""
echo "==> Next steps (once macOS + Windows tarballs also exist):"
echo "    1. Tag the commit:    git tag -a v${VERSION} -m \"AIFX v${VERSION}\""
echo "    2. Push the tag:      git push origin v${VERSION}"
echo "    3. Create release:    gh release create v${VERSION} \"$TAR_PATH\" --prerelease"
echo "       (or do it via the GitHub web UI)"
