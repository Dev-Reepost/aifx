#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright AIFX contributors.
#
# Build all AIFX plugins for Linux x86_64 and package them into a release
# tarball. The build runs natively on the host using Conan + CMake, so the
# machine needs a C++ toolchain, conan, and cmake >= 3.28 -- but no Docker.
#
# (To cross-build Linux binaries from macOS instead, use the Docker-based
# tools/build-linux-plugin.sh.)
#
# Usage: tools/release-linux.sh [VERSION]
#   VERSION defaults to the repo-root VERSION file.

set -euo pipefail

# Version comes from the repo-root VERSION file, the same source CMake stamps
# into every bundle. Passing an explicit VERSION is still allowed but it must
# agree -- otherwise the tarball name and the version baked into the binaries
# disagree, and the artifact becomes unidentifiable once it leaves this machine.
REPO_VERSION="$(tr -d '[:space:]' < "$(dirname "${BASH_SOURCE[0]}")/../VERSION")"
VERSION="${1:-$REPO_VERSION}"
if [[ "$VERSION" != "$REPO_VERSION" ]]; then
    echo "[ERROR] Requested version '$VERSION' does not match VERSION file '$REPO_VERSION'." >&2
    echo "        Update the repo-root VERSION file first (see RELEASING.md)." >&2
    exit 1
fi
ARCH="x86_64"
# OFX bundle spec arch directory (Contents/Linux-x86-64) -- note the hyphen,
# distinct from the underscore used in archive names (aifx-...-x86_64.tar.gz).
# The plugins' POST_BUILD emits the spec name; keep this in sync with it.
OFX_ARCH_DIR="Linux-x86-64"
REQUIRED_CMAKE="3.28"

# Plugin set comes from plugins/manifest.txt (single source of truth).
source "$(dirname "${BASH_SOURCE[0]}")/plugin-manifest.sh"
TARGETS=("${AIFX_PLUGIN_TARGETS[@]}")

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

BUILD_DIR="build/linux"

if [[ "$OSTYPE" != "linux"* ]]; then
    echo "[ERROR] release-linux.sh must be run on Linux." >&2
    echo "        To cross-build from macOS, use tools/build-linux-plugin.sh (Docker)." >&2
    exit 1
fi

# Prefer a pip-installed cmake (~/.local/bin) over an older system one.
export PATH="$HOME/.local/bin:$PATH"

# --- Toolchain checks --------------------------------------------------------
for tool in conan cmake; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "[ERROR] '$tool' is required but not found in PATH." >&2
        [[ "$tool" == "cmake" ]] && echo "        Install a recent one with: pip install --user 'cmake>=${REQUIRED_CMAKE}'" >&2
        exit 1
    fi
done

if ! command -v g++ >/dev/null 2>&1 && ! command -v clang++ >/dev/null 2>&1; then
    echo "[ERROR] No C++ compiler (g++ or clang++) found in PATH." >&2
    exit 1
fi

# Static C++ runtime must be present. Each .ofx links libstdc++/libgcc
# statically (-static-libstdc++ -static-libgcc, set in plugins/CMakeLists.txt)
# so the plugin carries its own std::filesystem/std::string and cannot bind to
# the host's. DaVinci Resolve loads libProResRAW.so — which re-exports an older
# libstdc++ — into the global symbol scope, and a dynamically-linked plugin then
# crashes (SIGSEGV) the first time it touches std::filesystem. Static linkage
# needs the static archives, which on Rocky/RHEL ship in the (CRB-repo)
# 'libstdc++-static' package and are NOT installed by default.
CXX_FOR_CHECK="$(command -v g++ || command -v clang++)"
if ! echo 'int main(){}' | "$CXX_FOR_CHECK" -static-libstdc++ -static-libgcc -x c++ - -o /dev/null 2>/dev/null; then
    echo "[ERROR] The static C++ runtime (libstdc++.a) is missing — '-static-libstdc++' fails to link." >&2
    echo "        On Rocky/RHEL 9:  sudo dnf config-manager --set-enabled crb && sudo dnf install libstdc++-static" >&2
    echo "        On Ubuntu/Debian: it ships with the g++ dev package (build-essential)." >&2
    echo "        This is required: without it the .ofx links libstdc++ dynamically and crashes in DaVinci Resolve." >&2
    exit 1
fi

# cmake must be >= REQUIRED_CMAKE.
CMAKE_VERSION="$(cmake --version | head -1 | grep -oE '[0-9]+\.[0-9]+(\.[0-9]+)?')"
if [[ "$(printf '%s\n%s\n' "$REQUIRED_CMAKE" "$CMAKE_VERSION" | sort -V | head -1)" != "$REQUIRED_CMAKE" ]]; then
    echo "[ERROR] cmake >= ${REQUIRED_CMAKE} required, but found ${CMAKE_VERSION} ($(command -v cmake))." >&2
    echo "        Install a recent one with: pip install --user 'cmake>=${REQUIRED_CMAKE}'" >&2
    exit 1
fi
echo "==> Using cmake ${CMAKE_VERSION} ($(command -v cmake)), conan $(conan --version | grep -oE '[0-9.]+')"

# --- Build -------------------------------------------------------------------
echo ""
echo "==> [1/3] Installing Conan dependencies..."
# -o '*:shared=False' forces static linkage of all deps regardless of the
# build host's Conan profile (CLI -o is highest precedence), so the shipped
# .ofx is self-contained and portable; expat stays shared (OpenFX HostSupport
# only, never in the bundle). Mirrors tools/build-plugin.sh.
conan install . \
    -s build_type=Release \
    -pr:b=default \
    -o '*:shared=False' \
    -o 'expat/*:shared=True' \
    --build=missing \
    -of="$BUILD_DIR"

echo ""
echo "==> [2/3] Configuring CMake..."
TOOLCHAIN="$(find "$BUILD_DIR" -name 'conan_toolchain.cmake' | head -1)"
if [[ -z "$TOOLCHAIN" ]]; then
    echo "[ERROR] conan_toolchain.cmake not found under $BUILD_DIR after conan install." >&2
    exit 1
fi
cmake -S . -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_COMFYUI_PLUGINS=ON \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
    -DCMAKE_POLICY_DEFAULT_CMP0091=NEW

echo ""
echo "==> [3/3] Building all ${#TARGETS[@]} plugin targets..."
cmake --build "$BUILD_DIR" --config Release --target "${TARGETS[@]}" --parallel

# Drop the legacy top-level <bundle>/Resources/ directory. Resources are packaged
# under Contents/Resources/ (OFX spec) and only ever read from there; the old
# top-level copy is a stale leftover from incremental builds predating that
# layout change, and it shipped duplicate (out-of-date) config/workflow files in
# earlier releases. Remove it from each bundle before packaging.
for target in "${TARGETS[@]}"; do
    rm -rf "$BUILD_DIR/${target}.ofx.bundle/Resources"
done

# --- Verify ------------------------------------------------------------------
echo ""
echo "==> Verifying bundles..."
for target in "${TARGETS[@]}"; do
    # The native build produces bundles directly under build/linux/.
    bundle="$BUILD_DIR/${target}.ofx.bundle"
    bin="${bundle}/Contents/${OFX_ARCH_DIR}/${target}.ofx"
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

# Identity + ABI gate. Every bundle must carry a build stamp matching this
# source tree before it is allowed into a tarball: an unidentifiable bundle on a
# workstation is what turned a fixed crash into a month-long field bug.
echo ""
echo "==> Verifying build identity + OFX ABI..."
_bundles=()
for target in "${TARGETS[@]}"; do _bundles+=("$BUILD_DIR/${target}.ofx.bundle"); done
./tools/verify-ofx-abi.sh --expect-version "$VERSION" "${_bundles[@]}"

# --- Package -----------------------------------------------------------------
echo ""
echo "==> Packaging..."
TOP="AIFX-${VERSION}-linux-${ARCH}"
STAGE="dist/staging/${TOP}"
rm -rf dist/staging
mkdir -p "$STAGE"

for target in "${TARGETS[@]}"; do
    cp -R "$BUILD_DIR/${target}.ofx.bundle" "$STAGE/"
done

# Ship the installer script alongside the bundles. Run from the extracted
# archive, it installs the sibling *.ofx.bundle dirs and bakes site config into
# each plugin's defaults.json -- the terminal counterpart to the macOS wizard.
cp "$REPO_ROOT/tools/install-posix.sh" "$STAGE/install.sh"
chmod +x "$STAGE/install.sh"

cat > "$STAGE/README.txt" <<EOF
AIFX ${VERSION} -- Linux ${ARCH}
====================================================

This archive contains seven OpenFX plugin bundles built for Linux ${ARCH}.

Recommended -- run the installer:

  ./install.sh              Installs into /usr/OFX/Plugins for all users.
                            That is the only OFX directory Flame scans, so it
                            is the only location offered; sudo is requested
                            once. Use --prefix <dir> to override.

  It walks you through the install location and your ComfyUI site config
  (server URL, mount paths), writes that config into each plugin, and copies
  the bundles into place. For unattended rollout use the flags it prints with
  --help (e.g. ./install.sh --yes --server <host> --local-mount <path>).

Manual install (if you'd rather wire it by hand):

  1. Copy every *.ofx.bundle directory in this archive into the standard
     OFX plugin directory for Linux:

       /usr/OFX/Plugins/             (all users on this machine)
       \$HOME/OFX/Plugins/            (this user only)

     ...then configure each plugin (server URL, mount paths) in your host UI.

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
