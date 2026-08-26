#!/usr/bin/env bash
# AIFX — verify the identity and OpenFX ABI of built or installed .ofx bundles.
# SPDX-License-Identifier: BSD-3-Clause
#
# WHY THIS EXISTS
#
# A .ofx bundle used to be anonymous: every Info.plist claimed "1.0.0" and the
# binary carried no build stamp, so a plugin sitting in /Library/OFX/Plugins was
# indistinguishable from any other build. That is how a pre-0.1.5 macOS bundle —
# compiled without OFX_SUPPORTS_OPENGLRENDER, hence two vtable slots short of
# the OfxSupport archive it was linked against — survived on a Flare workstation
# and kept segfaulting the host the moment a node was instanced, while the same
# source tree ran fine on Linux.
#
# Builds from 0.2.1 onward stamp their identity in three places (see
# kAifxBuildMarker in plugins/common/comfyui_base_plugin.cpp):
#
#   * the .ofx binary                       — authoritative, survives repackaging
#   * Contents/Resources/aifx-build.txt     — human-readable, `cat`-able
#   * Contents/Info.plist (macOS)           — CFBundleVersion
#
# This script reads them, cross-checks them against each other and (when run
# from a source tree) against the current VERSION + ABI tag, and — for legacy
# bundles that predate the stamp — falls back to measuring the emitted
# ComfyUI::BasePlugin vtable so the known-bad builds are still caught.
#
# Usage:
#   tools/verify-ofx-abi.sh <bundle-or-directory>...   # explicit targets
#   tools/verify-ofx-abi.sh                            # scan the OFX dirs
#   tools/verify-ofx-abi.sh --expect <tag> <target>... # pin the expected ABI tag
#   tools/verify-ofx-abi.sh --quiet <target>...        # only report problems
#
# Exit codes: 0 = all good, 1 = at least one bundle rejected, 2 = usage error.
#
# Deliberately dependency-light (bash + nm/strings) so it can be run on a
# Flame/Flare workstation that has no build toolchain.

set -uo pipefail

RED=$'\033[0;31m'; GREEN=$'\033[0;32m'; YELLOW=$'\033[1;33m'; BLUE=$'\033[0;34m'; NC=$'\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

QUIET=false
EXPECT_TAG=""
EXPECT_VERSION=""
TARGETS=()

usage() {
    sed -n '2,/^set -uo/p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//;$d'
    exit "${1:-2}"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --expect)         EXPECT_TAG="${2:-}";     shift 2 ;;
        --expect-version) EXPECT_VERSION="${2:-}"; shift 2 ;;
        --quiet|-q)       QUIET=true;              shift ;;
        --help|-h)        usage 0 ;;
        -*)               echo "Unknown option: $1" >&2; usage 2 ;;
        *)                TARGETS+=("$1");         shift ;;
    esac
done

# Fall back to the source tree's own expectations when not pinned explicitly.
# Absent a source tree (field use on a workstation) we only report, never
# compare against a version we cannot know.
if [[ -z "$EXPECT_VERSION" && -f "$REPO_ROOT/VERSION" ]]; then
    EXPECT_VERSION="$(tr -d '[:space:]' < "$REPO_ROOT/VERSION")"
fi
if [[ -z "$EXPECT_TAG" && -f "$REPO_ROOT/CMakeLists.txt" ]]; then
    # The top-level CMakeLists forces OpenCL/CUDA off and inherits OpenFX's
    # OpenGL default (ON); mirror that rather than hardcoding a literal here.
    if grep -q 'set(OFX_SUPPORTS_OPENCLRENDER OFF' "$REPO_ROOT/CMakeLists.txt"; then
        EXPECT_TAG="v1-gl1-cl0-cu0"
    fi
fi

# Default scan set: the per-user and system OFX plugin directories for this OS.
if [[ ${#TARGETS[@]} -eq 0 ]]; then
    case "$(uname -s)" in
        Darwin) TARGETS=("$HOME/Library/OFX/Plugins" "/Library/OFX/Plugins") ;;
        Linux)  TARGETS=("$HOME/OFX/Plugins" "/usr/OFX/Plugins") ;;
        *)      TARGETS=("${COMMONPROGRAMFILES:-}/OFX/Plugins" "${LOCALAPPDATA:-}/OFX/Plugins") ;;
    esac
fi

say()  { [[ "$QUIET" == true ]] || echo "$@"; }

# Locate the .ofx binary inside a bundle, whatever the per-OS arch directory is
# (Contents/MacOS, Contents/Linux-x86-64, Contents/Win64, ...).
find_binary() {
    find "$1/Contents" -type f -name '*.ofx' 2>/dev/null | head -1
}

# Read the AIFX-BUILD marker out of a binary. Empty if the build predates it.
read_marker() {
    strings - "$1" 2>/dev/null | grep -m1 '^AIFX-BUILD|' || true
}

marker_field() { sed -n "s/.*|$2=\([^|]*\).*/\1/p" <<<"$1"; }

# Legacy fallback: measure the emitted ComfyUI::BasePlugin vtable.
#
# The two virtuals OFX_SUPPORTS_OPENGLRENDER adds to OFX::ImageEffect
# (contextAttached/contextDetached) are inherited by BasePlugin, so a plugin TU
# compiled without the macro emits a vtable exactly 2 slots (16 bytes) shorter.
# nm has no symbol size on Mach-O, so measure the distance to the next symbol in
# address order — good enough to separate 232 from 248.
#
# Echoes the byte size, or nothing if it cannot be determined.
measure_vtable() {
    local bin="$1" arch_flag=()
    command -v nm >/dev/null 2>&1 || return 0
    # Universal binaries need an explicit slice or nm concatenates both.
    if [[ "$(uname -s)" == "Darwin" ]] && file "$bin" 2>/dev/null | grep -q 'universal binary'; then
        arch_flag=(-arch arm64)
    fi
    local addrs
    addrs=$(nm "${arch_flag[@]}" -n "$bin" 2>/dev/null \
        | grep -A1 '_*_ZTVN7ComfyUI10BasePluginE$' \
        | awk '{print $1}' | grep -E '^[0-9a-f]+$' | head -2)
    [[ $(wc -l <<<"$addrs") -eq 2 ]] || return 0
    local a1 a2
    a1=$(sed -n 1p <<<"$addrs"); a2=$(sed -n 2p <<<"$addrs")
    echo $(( 0x$a2 - 0x$a1 ))
}

# The known-good vtable size for a correctly built AIFX plugin. Derived from a
# reference build rather than from theory, and only ever consulted for legacy
# bundles that carry no AIFX-BUILD marker — current builds are validated by the
# marker, and the compile-time guard in plugins/common/aifx_abi_check.h makes a
# short vtable unbuildable in the first place.
LEGACY_GOOD_VTABLE=248

failures=0
checked=0
warned=0

# A bundle is only ever judged if we can positively identify it as AIFX -- by
# its build marker, or (for legacy builds) by the presence of ComfyUI::BasePlugin.
# Third-party and half-installed bundles share these directories; reporting them
# as failures would train people to ignore the output.
check_bundle() {
    local bundle="$1"
    local name; name="$(basename "$bundle" .ofx.bundle)"
    local bin;  bin="$(find_binary "$bundle")"

    if [[ -z "$bin" ]]; then
        say "${YELLOW}SKIP${NC}  $name — no .ofx binary under Contents/ (incomplete or foreign bundle)"
        return
    fi

    local marker; marker="$(read_marker "$bin")"

    if [[ -z "$marker" ]]; then
        # Pre-0.2.1 build, or not AIFX at all. The vtable probe answers which.
        local vt; vt="$(measure_vtable "$bin")"
        if [[ -z "$vt" ]]; then
            say "${YELLOW}SKIP${NC}  $name — not an AIFX bundle (no build marker, no BasePlugin)"
            return
        fi
        checked=$((checked + 1))
        if [[ "$vt" -lt "$LEGACY_GOOD_VTABLE" ]]; then
            echo "${RED}FAIL${NC}  $name — legacy build with a SHORT vtable (${vt}B, expected >= ${LEGACY_GOOD_VTABLE}B)"
            echo "        Built without OFX_SUPPORTS_OPENGLRENDER: the host dispatches actions"
            echo "        into the wrong virtual and segfaults the moment a node is instanced."
            echo "        Fix: delete this bundle and reinstall AIFX ${EXPECT_VERSION:-(current)}."
            echo "        Path: $bundle"
            failures=$((failures + 1))
            return
        fi
        warned=$((warned + 1))
        say "${YELLOW}WARN${NC}  $name — pre-0.2.1 build (no marker); vtable ${vt}B looks sane"
        return
    fi

    checked=$((checked + 1))

    local ver tag ofx
    ver="$(marker_field "$marker" version)"
    tag="$(marker_field "$marker" ofxabi)"
    ofx="$(marker_field "$marker" openfx)"

    local problems=()
    [[ -n "$EXPECT_TAG"     && "$tag" != "$EXPECT_TAG"     ]] && problems+=("ABI tag $tag != expected $EXPECT_TAG")
    [[ -n "$EXPECT_VERSION" && "$ver" != "$EXPECT_VERSION" ]] && problems+=("version $ver != expected $EXPECT_VERSION")

    # Cross-check the bundle's own copies against the binary. A disagreement
    # means the bundle was assembled from mixed build outputs.
    local stamp="$bundle/Contents/Resources/aifx-build.txt"
    if [[ -f "$stamp" ]]; then
        local sver; sver="$(marker_field "$(cat "$stamp")" version)"
        [[ "$sver" == "$ver" ]] || problems+=("Resources stamp says $sver, binary says $ver")
    fi
    local plist="$bundle/Contents/Info.plist"
    if [[ -f "$plist" ]]; then
        local pver
        pver="$(grep -A1 '<key>CFBundleVersion</key>' "$plist" | sed -n 's/.*<string>\(.*\)<\/string>.*/\1/p')"
        [[ -z "$pver" || "$pver" == "$ver" ]] || problems+=("Info.plist says $pver, binary says $ver")
    fi

    if [[ ${#problems[@]} -gt 0 ]]; then
        echo "${RED}FAIL${NC}  $name — $ver ($tag)"
        printf '        %s\n' "${problems[@]}"
        echo "        Path: $bundle"
        failures=$((failures + 1))
        return
    fi

    say "${GREEN}OK${NC}    $name — $ver ($tag, openfx ${ofx:0:7})"
}

for target in "${TARGETS[@]}"; do
    [[ -n "$target" && -e "$target" ]] || continue
    if [[ "$target" == *.ofx.bundle ]]; then
        check_bundle "$target"
    else
        say "${BLUE}==${NC} $target"
        shopt -s nullglob
        for b in "$target"/*.ofx.bundle; do check_bundle "$b"; done
        shopt -u nullglob
    fi
done

if [[ "$checked" -eq 0 ]]; then
    echo "${YELLOW}No AIFX bundles found in: ${TARGETS[*]}${NC}"
    exit 0
fi

if [[ "$failures" -gt 0 ]]; then
    echo
    echo "${RED}${failures} of ${checked} AIFX bundle(s) rejected.${NC}"
    exit 1
fi

say
if [[ "$warned" -gt 0 ]]; then
    say "${GREEN}${checked} AIFX bundle(s) verified${NC} (${warned} pre-0.2.1, identity unknown)."
else
    say "${GREEN}${checked} AIFX bundle(s) verified.${NC}"
fi
exit 0
