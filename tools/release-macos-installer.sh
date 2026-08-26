#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright AIFX contributors.
#
# Build the AIFX macOS installer .dmg.
#
# Inputs:
#   - VERSION arg (required)
#   - dist/aifx-<VERSION>-macos-universal.tar.gz from tools/release-macos.sh
#     (or a freshly built build/Release/*.ofx.bundle tree; we prefer the tarball
#     for reproducibility, fall back to build/ for dev iteration)
#
# Steps:
#   1. Stage the seven .ofx.bundle payloads into
#      installer/macos/Resources/Bundles/
#   2. swift build -c release of installer/macos/  (produces an executable)
#   3. Wrap the executable into "AIFX Installer.app" with the embedded Bundles
#      and the Info.plist baked with VERSION
#   4. Sign + notarise (if AIFX_SIGN_IDENTITY and/or AIFX_NOTARY_PROFILE set,
#      or if a single Developer ID Application identity is present in the
#      keychain). Skip cleanly otherwise — users get a Gatekeeper warning.
#   5. Wrap in a .dmg
#   6. Print SHA-256 and next-step gh release upload command
#
# Output:
#   dist/aifx-<VERSION>-macos-installer.dmg

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

# Version comes from the repo-root VERSION file, the same source CMake stamps
# into every bundle. An explicit argument is still allowed but must agree.
REPO_VERSION="$(tr -d '[:space:]' < "$REPO_ROOT/VERSION")"
VERSION="${1:-$REPO_VERSION}"
if [[ "$VERSION" != "$REPO_VERSION" ]]; then
    echo "[ERROR] Requested version '$VERSION' does not match VERSION file '$REPO_VERSION'." >&2
    echo "        Update the repo-root VERSION file first (see RELEASING.md)." >&2
    exit 1
fi

INSTALLER_DIR="installer/macos"
STAGE_BUNDLES_DIR="$INSTALLER_DIR/Resources/Bundles"
APP_NAME="AIFX Installer"
APP_DIR="$INSTALLER_DIR/build/${APP_NAME}.app"
DIST_DIR="dist"
DMG_PATH="$DIST_DIR/aifx-${VERSION}-macos-installer.dmg"
TARBALL="$DIST_DIR/aifx-${VERSION}-macos-universal.tar.gz"

if [[ "$OSTYPE" != "darwin"* ]]; then
    echo "[ERROR] This script only runs on macOS." >&2
    exit 1
fi

mkdir -p "$DIST_DIR"

# --- 1. Stage the seven bundle payloads ---------------------------------------

echo "==> [1/5] Staging .ofx.bundle payloads…"
rm -rf "$STAGE_BUNDLES_DIR"
mkdir -p "$STAGE_BUNDLES_DIR"

if [[ -f "$TARBALL" ]]; then
    echo "    using existing tarball: $TARBALL"
    TMP_EXTRACT="$(mktemp -d)"
    tar -xzf "$TARBALL" -C "$TMP_EXTRACT"
    # The tarball contains AIFX-<VERSION>-macos-universal/*.ofx.bundle.
    TOP="$(find "$TMP_EXTRACT" -mindepth 1 -maxdepth 1 -type d | head -1)"
    if [[ -z "$TOP" ]]; then
        echo "[ERROR] Could not find the top dir inside $TARBALL" >&2
        exit 1
    fi
    find "$TOP" -maxdepth 1 -name '*.ofx.bundle' -type d -exec cp -R {} "$STAGE_BUNDLES_DIR/" \;
    rm -rf "$TMP_EXTRACT"
else
    echo "    tarball missing; staging from build/Release/*.ofx.bundle"
    if [[ ! -d build/Release ]]; then
        echo "[ERROR] No build/Release and no $TARBALL. Run tools/release-macos.sh $VERSION first." >&2
        exit 1
    fi
    find build/Release -maxdepth 1 -name '*.ofx.bundle' -type d -exec cp -R {} "$STAGE_BUNDLES_DIR/" \;
fi

# Expected bundle set comes from plugins/manifest.txt (single source of truth).
source "$(dirname "${BASH_SOURCE[0]}")/plugin-manifest.sh"
EXPECTED=("${AIFX_PLUGIN_TARGETS[@]}")
for t in "${EXPECTED[@]}"; do
    if [[ ! -d "$STAGE_BUNDLES_DIR/${t}.ofx.bundle" ]]; then
        echo "[ERROR] Missing staged bundle: ${t}.ofx.bundle" >&2
        exit 1
    fi
done
echo "    staged: ${EXPECTED[*]}"

# --- 2. Build the SwiftUI executable -----------------------------------------

echo "==> [2/5] Building installer executable (swift build -c release)…"
(
    cd "$INSTALLER_DIR"
    swift build -c release --arch arm64 --arch x86_64
)
EXE_PATH="$INSTALLER_DIR/.build/apple/Products/Release/AIFXInstaller"
if [[ ! -f "$EXE_PATH" ]]; then
    # Fallback for single-arch dev builds.
    EXE_PATH="$INSTALLER_DIR/.build/release/AIFXInstaller"
fi
if [[ ! -f "$EXE_PATH" ]]; then
    echo "[ERROR] Could not find the built executable under $INSTALLER_DIR/.build" >&2
    exit 1
fi
echo "    built: $EXE_PATH"

# --- 3. Wrap into AIFX Installer.app -----------------------------------------

echo "==> [3/5] Wrapping into ${APP_NAME}.app…"
rm -rf "$APP_DIR"
mkdir -p "$APP_DIR/Contents/MacOS" "$APP_DIR/Contents/Resources/Bundles"
cp "$EXE_PATH" "$APP_DIR/Contents/MacOS/AIFXInstaller"
chmod +x "$APP_DIR/Contents/MacOS/AIFXInstaller"
cp -R "$STAGE_BUNDLES_DIR/." "$APP_DIR/Contents/Resources/Bundles/"
# Bake VERSION into Info.plist
sed -e "s|@VERSION@|${VERSION}|g" \
    "$INSTALLER_DIR/app/Info.plist.in" \
    > "$APP_DIR/Contents/Info.plist"
# App icon (Reepost logo).
if [[ -f "$INSTALLER_DIR/app/AppIcon.icns" ]]; then
    cp "$INSTALLER_DIR/app/AppIcon.icns" "$APP_DIR/Contents/Resources/AppIcon.icns"
fi

# Quick sanity: every bundle has its config/defaults.json (we'll mutate these
# at install time; failing to ship them means rendering will break).
for t in "${EXPECTED[@]}"; do
    f="$APP_DIR/Contents/Resources/Bundles/${t}.ofx.bundle/Contents/Resources/config/defaults.json"
    if [[ ! -f "$f" ]]; then
        echo "[ERROR] Missing $f — bundle wasn't built with the config step." >&2
        exit 1
    fi
done

# --- 4. Sign + notarise (optional) -------------------------------------------

echo "==> [4/5] Signing…"
SIGN_IDENTITY="${AIFX_SIGN_IDENTITY:-}"
if [[ -z "$SIGN_IDENTITY" ]]; then
    # Try to auto-detect a single Developer ID Application cert.
    LINE="$(security find-identity -p codesigning -v 2>/dev/null \
            | grep 'Developer ID Application' | head -1 || true)"
    if [[ -n "$LINE" ]]; then
        SIGN_IDENTITY="$(echo "$LINE" | sed -E 's/.*"(Developer ID Application: [^"]+)".*/\1/')"
    fi
fi

if [[ -n "$SIGN_IDENTITY" ]]; then
    echo "    identity: $SIGN_IDENTITY"
    # Sign embedded .ofx.bundle binaries first (sign deepest first),
    # then the installer executable, then the .app itself.
    for t in "${EXPECTED[@]}"; do
        BIN="$APP_DIR/Contents/Resources/Bundles/${t}.ofx.bundle/Contents/MacOS/${t}.ofx"
        if [[ -f "$BIN" ]]; then
            codesign --force --options runtime --timestamp \
                --sign "$SIGN_IDENTITY" "$BIN"
        fi
    done
    codesign --force --options runtime --timestamp \
        --entitlements "$INSTALLER_DIR/app/Entitlements.plist" \
        --sign "$SIGN_IDENTITY" \
        "$APP_DIR/Contents/MacOS/AIFXInstaller"
    codesign --force --options runtime --timestamp \
        --entitlements "$INSTALLER_DIR/app/Entitlements.plist" \
        --sign "$SIGN_IDENTITY" \
        "$APP_DIR"
    codesign --verify --strict --verbose=2 "$APP_DIR" >/dev/null 2>&1 \
        && echo "    [OK] codesign verify passed" \
        || { echo "[ERROR] codesign verify failed" >&2; exit 1; }

    NOTARY_PROFILE="${AIFX_NOTARY_PROFILE:-}"
    if [[ -n "$NOTARY_PROFILE" ]]; then
        echo "    submitting for notarisation (profile: $NOTARY_PROFILE)…"
        ZIPPED="$(mktemp -d)/AIFX-Installer-${VERSION}.zip"
        ditto -c -k --keepParent "$APP_DIR" "$ZIPPED"
        xcrun notarytool submit "$ZIPPED" \
            --keychain-profile "$NOTARY_PROFILE" --wait
        xcrun stapler staple "$APP_DIR"
        rm -f "$ZIPPED"
        echo "    [OK] notarised + stapled"
    else
        echo "    [WARNING] AIFX_NOTARY_PROFILE not set — NOT notarised."
        echo "    A Developer ID signature alone is not enough: macOS still blocks the app"
        echo "    with \"Apple could not verify ... is free of malware\" until the build has"
        echo "    been notarised and stapled. Set the profile up once with:"
        echo "      xcrun notarytool store-credentials AIFX-NOTARY \\"
        echo "        --apple-id <apple-id> --team-id <team-id> --password <app-specific-pw>"
        echo "    then re-run with: AIFX_NOTARY_PROFILE=AIFX-NOTARY $0 $VERSION"
        echo ""
        if [[ "${AIFX_ALLOW_UNSIGNED:-0}" != "1" ]]; then
            echo "[ERROR] Refusing to build a non-notarised installer for release." >&2
            echo "        Re-run with AIFX_ALLOW_UNSIGNED=1 for a local test build." >&2
            exit 1
        fi
        echo "    AIFX_ALLOW_UNSIGNED=1 set — continuing without notarisation."
    fi
else
    echo "    [WARNING] No Developer ID Application identity found in keychain."
    echo "    Building UNSIGNED — this DMG will be BLOCKED on end-user machines."
    echo ""
    echo "    On macOS 15 (Sequoia) and later, Gatekeeper refuses an unnotarised app with"
    echo "      \"Apple could not verify ... is free of malware\""
    echo "    and the old right-click → Open bypass NO LONGER WORKS. Nor does the"
    echo "    'Open Anyway' button: it is a transient row that only appears for about an"
    echo "    hour after a blocked launch, and on Sequoia it is not offered at all for an"
    echo "    app with no bundle signature (which is what this build is:"
    echo "    'spctl -a' reports 'no usable signature')."
    echo ""
    echo "    The only reliable route left is the terminal, on every workstation:"
    echo "      cp -R '/Volumes/AIFX <version>/AIFX Installer.app' /Applications/"
    echo "      find '/Applications/AIFX Installer.app' -exec xattr -d com.apple.quarantine {} \\; 2>/dev/null"
    echo "    (not 'xattr -dr' -- macOS 26 removed the -r flag; it clears nothing)"
    echo "    (the copy is required -- the DMG volume is read-only, so xattr fails there)"
    echo ""
    echo "    Ship a signed + notarised DMG instead: set AIFX_SIGN_IDENTITY (or install a"
    echo "    Developer ID Application certificate) and AIFX_NOTARY_PROFILE. See"
    echo "    RELEASING.md > 'Signing the macOS installer'."
    echo ""
    if [[ "${AIFX_ALLOW_UNSIGNED:-0}" != "1" ]]; then
        echo "[ERROR] Refusing to build an unsigned installer for release." >&2
        echo "        Re-run with AIFX_ALLOW_UNSIGNED=1 for a local test build." >&2
        exit 1
    fi
    echo "    AIFX_ALLOW_UNSIGNED=1 set — continuing with an unsigned build."
fi

# --- 5. Package as DMG --------------------------------------------------------

echo "==> [5/5] Building ${DMG_PATH}…"
rm -f "$DMG_PATH"
DMG_STAGE="$(mktemp -d)"
cp -R "$APP_DIR" "$DMG_STAGE/"
# Intentionally NO /Applications symlink: this is a one-shot installer
# (it copies the seven .ofx.bundle directories into ~/Library/OFX/Plugins/
# and exits). Suggesting a drag-to-Applications install would mislead users
# into thinking the app is meant to live there. Right action is just
# double-click from the mounted DMG.
# -fs HFS+ : hdiutil defaults to APFS on a modern build host, and an APFS disk
# image will not mount on macOS 10.12 or earlier -- with no error dialog, the
# Finder simply does nothing when you double-click it. HFS+ mounts on every
# macOS that can run an OFX host, and a read-only installer image gains nothing
# from APFS. UDZO (zlib) is the standard read-only compressed format.
hdiutil create -volname "AIFX ${VERSION}" \
    -srcfolder "$DMG_STAGE" -ov -fs HFS+ -format UDZO "$DMG_PATH" >/dev/null
rm -rf "$DMG_STAGE"

# Sign, then notarise, the disk image ITSELF.
#
# The .app inside was already notarised and stapled above, which is what lets it
# launch. But the image is a separate code object: Gatekeeper assesses it when
# the user opens it, and a signed-but-not-notarised image still trips an
# assessment prompt. Notarising and stapling the DMG means the ticket travels
# with the file, so the whole chain -- image, then app -- passes offline, on a
# machine that has never seen it before.
#
# Failures here are fatal rather than swallowed: shipping a half-signed image is
# how an unshippable artifact reached users in the first place.
if [[ -n "$SIGN_IDENTITY" ]]; then
    echo "    signing disk image…"
    codesign --force --timestamp --sign "$SIGN_IDENTITY" "$DMG_PATH"

    if [[ -n "${AIFX_NOTARY_PROFILE:-}" ]]; then
        echo "    submitting disk image for notarisation…"
        xcrun notarytool submit "$DMG_PATH" \
            --keychain-profile "$AIFX_NOTARY_PROFILE" --wait
        xcrun stapler staple "$DMG_PATH"
        echo "    [OK] disk image notarised + stapled"

        # Prove it end to end: this is the exact assessment the user's Mac runs
        # when they double-click the image.
        if spctl -a -vv -t open --context context:primary-signature "$DMG_PATH" 2>&1 | grep -q "accepted"; then
            echo "    [OK] spctl accepts the disk image"
        else
            echo "[ERROR] spctl still rejects the disk image after notarisation:" >&2
            spctl -a -vv -t open --context context:primary-signature "$DMG_PATH" >&2 2>&1 || true
            exit 1
        fi
    fi
fi

echo ""
echo "==> Release artifact:"
ls -lh "$DMG_PATH"

echo ""
echo "==> SHA-256 for release notes:"
shasum -a 256 "$DMG_PATH"

echo ""
echo "==> Attach to the GitHub release:"
echo "    gh release upload v${VERSION} \"$DMG_PATH\""
