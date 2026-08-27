#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright AIFX contributors.
#
# AIFX terminal installer for Linux and macOS. It walks the operator through the
# same choices as the macOS SwiftUI wizard (installer/macos) —
#   1. install location (per-user vs system-wide)
#   2. site configuration (ComfyUI server URL + the two mount-path views)
#   3. install (rewrite each bundle's defaults.json, copy into place)
# — but as a script, so it also drops cleanly into headless / multi-workstation
# rollout (Ansible, SSH, render-farm provisioning) via its --non-interactive
# flags. Linux has no portable native GUI toolkit and our audience already lives
# in a terminal, so a robust script beats a GUI there.
#
# On macOS it is also the way around Gatekeeper. The installer .app is not
# signed with an Apple Developer ID, so macOS 15+ refuses to launch it at all
# ("Apple could not verify ... is free of malware") and offers no bypass. A
# script run from a terminal is never gatekeeper-assessed, so this path works
# with no certificate and no per-machine xattr dance.
#
# Targets bash 3.2 — macOS still ships that at /bin/bash. No mapfile, no
# associative arrays, no ${var,,}.
#
# Ship this file *inside* the release tarball, alongside the seven
# *.ofx.bundle directories: run it from the extracted archive with no path
# arguments and it installs the sibling bundles.
#
# Usage:
#   ./install.sh                       # interactive, system-wide (sudo)
#   ./install.sh --user                # interactive, per-user (no sudo)
#   ./install.sh --yes \                # fully non-interactive
#       --server comfyui.example.local --port 8188 \
#       --local-mount /mnt/silo/AIFX \
#       --server-mount '\\COMFYUI-HOST\silo\AIFX' \
#       --timeout 600
#   ./install.sh --keep-defaults --yes # copy only, leave bundled config
#   ./install.sh --prefix /opt/OFX/Plugins   # explicit target dir
#
# The defaults.json keys written here mirror exactly what the current plugin
# code reads (plugins/common/comfyui_base_plugin.cpp): server.serverAddress,
# server.serverPort, storage.localMountPath.linux, storage.serverMountPath,
# controls.timeout, controls.enableProcessing. Keep this in sync with that
# reader and with config/defaults-base.json.

set -euo pipefail

# --- Defaults (match the macOS wizard's SiteConfig where they overlap) -------
# System-wide by default. Autodesk Flame and Flare only scan the machine-wide
# OFX directory, so a per-user install leaves them blind to the plugins -- the
# operator gets a clean "installed" and an empty effects browser. Nuke, Resolve
# and Fusion read both, so system is the only default that works everywhere.
# --user opts out for machines where sudo is not available.
SCOPE="system"               # user | system
PREFIX=""                    # explicit override of the install dir
SERVER_ADDRESS="127.0.0.1"
SERVER_PORT=8188
LOCAL_MOUNT=""                            # THIS box's view of the share (per-OS default below)
SERVER_MOUNT='\\COMFYUI-HOST\share'       # the ComfyUI server's view (UNC)
TIMEOUT=600
ENABLE_PROCESSING=false
KEEP_DEFAULTS=false
ASSUME_YES=false
REQUIRED_GLIBC="2.34"

# --- Platform ---------------------------------------------------------------
#
# One script for Linux and macOS. On macOS it also replaces the SwiftUI wizard
# for anyone who cannot get past Gatekeeper: the installer .app is not signed
# with an Apple Developer ID, so macOS refuses to launch it, whereas a script
# invoked from a terminal is never gatekeeper-assessed. Same prompts, same
# defaults.json keys, no code-signing required.
case "$(uname -s)" in
    Darwin)
        AIFX_OS="macos"
        OS_LABEL="macOS"
        # Standard OFX plugin directories on macOS (see docs/installation.md).
        # NOTE: Autodesk Flame and Flare only scan the system-wide directory.
        SYSTEM_DIR="/Library/OFX/Plugins"
        USER_DIR="${HOME}/Library/OFX/Plugins"
        DEFAULT_LOCAL_MOUNT="/Volumes/comfyui-share"
        ;;
    Linux)
        AIFX_OS="linux"
        OS_LABEL="Linux"
        # Standard OFX plugin directories on Linux (see docs/installation.md).
        # NOTE: Flame scans /usr/OFX/Plugins only.
        SYSTEM_DIR="/usr/OFX/Plugins"
        USER_DIR="${HOME}/OFX/Plugins"
        DEFAULT_LOCAL_MOUNT="/mnt/comfyui-share"
        ;;
    *)
        echo "install-posix.sh supports Linux and macOS; this is $(uname -s)." >&2
        echo "On Windows use tools/release-windows-installer.ps1's .exe wizard." >&2
        exit 1
        ;;
esac
: "${LOCAL_MOUNT:=$DEFAULT_LOCAL_MOUNT}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

c_bold=$'\033[1m'; c_dim=$'\033[2m'; c_cyan=$'\033[36m'
c_green=$'\033[32m'; c_yellow=$'\033[33m'; c_red=$'\033[31m'; c_off=$'\033[0m'
step() { printf '\n%s==> %s%s\n' "$c_cyan" "$1" "$c_off"; }
ok()   { printf '    %s[OK]%s %s\n' "$c_green" "$c_off" "$1"; }
warn() { printf '    %s[!]%s  %s\n' "$c_yellow" "$c_off" "$1" >&2; }
die()  { printf '%s[ERROR]%s %s\n' "$c_red" "$c_off" "$1" >&2; exit 1; }

usage() {
    sed -n '17,33p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit "${1:-0}"
}

# --- Arg parsing -------------------------------------------------------------
while [[ $# -gt 0 ]]; do
    case "$1" in
        --system)        SCOPE="system"; shift ;;
        --user)          SCOPE="user"; shift ;;
        --prefix)        PREFIX="${2:?--prefix needs a directory}"; shift 2 ;;
        --server)        SERVER_ADDRESS="${2:?}"; shift 2 ;;
        --port)          SERVER_PORT="${2:?}"; shift 2 ;;
        --local-mount)   LOCAL_MOUNT="${2:?}"; shift 2 ;;
        --server-mount)  SERVER_MOUNT="${2:?}"; shift 2 ;;
        --timeout)       TIMEOUT="${2:?}"; shift 2 ;;
        --enable)        ENABLE_PROCESSING=true; shift ;;
        --keep-defaults) KEEP_DEFAULTS=true; shift ;;
        -y|--yes)        ASSUME_YES=true; shift ;;
        -h|--help)       usage 0 ;;
        *)               die "Unknown argument: $1  (try --help)" ;;
    esac
done

# --- Locate the bundles ------------------------------------------------------
# Bundles sit next to this script when run from the extracted tarball.
# NOT mapfile: that is a bash 4 builtin and macOS still ships bash 3.2 at
# /bin/bash, where it silently does not exist. This loop is portable to both.
BUNDLES=()
while IFS= read -r _b; do BUNDLES+=("$_b"); done < <(
    find "$SCRIPT_DIR" -maxdepth 1 -type d -name '*.ofx.bundle' | sort)
[[ ${#BUNDLES[@]} -gt 0 ]] || die "No *.ofx.bundle directories found next to this script ($SCRIPT_DIR).
        Run install.sh from inside the extracted release archive."

printf '%sAIFX %s installer%s  —  %d plugin bundle(s) found\n' "$c_bold" "$OS_LABEL" "$c_off" "${#BUNDLES[@]}"

# --- Preflight: glibc floor (Linux only) -------------------------------------
# The prebuilt .ofx require glibc >= 2.34 (Ubuntu 22.04+, Debian 12+, RHEL 9+).
# Warn rather than abort: the user may know better, and it's only the prebuilt
# binaries that care — not the install mechanics.
detect_glibc() {
    local v=""
    if command -v getconf >/dev/null 2>&1; then
        v="$(getconf GNU_LIBC_VERSION 2>/dev/null | awk '{print $2}')"
    fi
    if [[ -z "$v" ]] && command -v ldd >/dev/null 2>&1; then
        v="$(ldd --version 2>/dev/null | head -1 | grep -oE '[0-9]+\.[0-9]+$' || true)"
    fi
    printf '%s' "$v"
}
GLIBC_VERSION=""
[[ "$AIFX_OS" == "linux" ]] && GLIBC_VERSION="$(detect_glibc)"
if [[ "$AIFX_OS" != "linux" ]]; then
    :   # macOS: no glibc floor to check.
elif [[ -n "$GLIBC_VERSION" ]]; then
    if [[ "$(printf '%s\n%s\n' "$REQUIRED_GLIBC" "$GLIBC_VERSION" | sort -V | head -1)" != "$REQUIRED_GLIBC" ]]; then
        warn "This host has glibc ${GLIBC_VERSION}; the prebuilt plugins need >= ${REQUIRED_GLIBC}."
        warn "They will fail to load. Build from source instead — see docs/installation.md."
        $ASSUME_YES || { read -rp "    Continue anyway? [y/N] " a; [[ "$a" =~ ^[Yy] ]] || exit 1; }
    else
        ok "glibc ${GLIBC_VERSION} (>= ${REQUIRED_GLIBC})"
    fi
else
    warn "Could not detect glibc version; the prebuilt plugins need >= ${REQUIRED_GLIBC}."
fi

# --- Preflight: JSON editor --------------------------------------------------
# We patch defaults.json with python3 (already a project dependency via
# tools/merge-defaults.py, and present on essentially every modern distro).
# Only required when actually rewriting config.
if ! $KEEP_DEFAULTS && ! command -v python3 >/dev/null 2>&1; then
    warn "python3 not found — cannot rewrite site config into defaults.json."
    warn "Falling back to copy-only; configure each plugin in your host UI instead."
    KEEP_DEFAULTS=true
fi

# --- Resolve the install directory -------------------------------------------
if [[ -n "$PREFIX" ]]; then
    DEST="$PREFIX"
elif [[ "$SCOPE" == "system" ]]; then
    DEST="$SYSTEM_DIR"
else
    DEST="$USER_DIR"
fi

# --- Interactive prompts -----------------------------------------------------
prompt() {  # prompt VAR "Label" "current"
    local __var="$1" __label="$2" __cur="$3" __in
    read -rp "    ${__label} [${__cur}]: " __in || true
    printf -v "$__var" '%s' "${__in:-$__cur}"
}

if ! $ASSUME_YES; then
    step "Install location"
    printf '    1) System-wide %s%s%s   (needs sudo -- required for Flame / Flare)\n' "$c_dim" "$SYSTEM_DIR" "$c_off"
    printf '    2) Per-user    %s%s%s   (no sudo -- Nuke, Resolve, Fusion only:\n' "$c_dim" "$USER_DIR" "$c_off"
    printf '       %sAutodesk hosts do not scan this path%s)\n' "$c_dim" "$c_off"
    [[ -n "$PREFIX" ]] && printf '    (overridden by --prefix %s)\n' "$PREFIX"
    if [[ -z "$PREFIX" ]]; then
        read -rp "    Choose [1]: " loc || true
        if [[ "${loc:-1}" == "2" ]]; then SCOPE="user"; DEST="$USER_DIR"; else SCOPE="system"; DEST="$SYSTEM_DIR"; fi
    fi

    if ! $KEEP_DEFAULTS; then
        step "Site configuration  ${c_dim}(written into each plugin's defaults.json)${c_off}"
        prompt SERVER_ADDRESS "ComfyUI server address (host or IP)" "$SERVER_ADDRESS"
        prompt SERVER_PORT    "ComfyUI server port"                 "$SERVER_PORT"
        prompt LOCAL_MOUNT    "Shared folder as THIS $OS_LABEL machine sees it" "$LOCAL_MOUNT"
        prompt SERVER_MOUNT   "Shared folder as the ComfyUI SERVER sees it (UNC)" "$SERVER_MOUNT"
        prompt TIMEOUT        "Per-job timeout (seconds)"           "$TIMEOUT"
        read -rp "    Enable processing by default? [y/N] " e || true
        [[ "$e" =~ ^[Yy] ]] && ENABLE_PROCESSING=true || ENABLE_PROCESSING=false
    fi
fi

[[ "$SERVER_PORT" =~ ^[0-9]+$ && "$SERVER_PORT" -gt 0 && "$SERVER_PORT" -lt 65536 ]] \
    || die "Invalid port: $SERVER_PORT"
[[ "$TIMEOUT" =~ ^[0-9]+$ && "$TIMEOUT" -ge 10 ]] || die "Invalid timeout: $TIMEOUT"

# --- Summary + confirm -------------------------------------------------------
step "Ready to install"
printf '    Destination:  %s%s%s\n' "$c_bold" "$DEST" "$c_off"
if $KEEP_DEFAULTS; then
    printf '    Site config:  %skeeping each plugin'\''s bundled defaults%s\n' "$c_dim" "$c_off"
else
    printf '    Server:       %s:%s\n' "$SERVER_ADDRESS" "$SERVER_PORT"
    printf '    Local mount:  %s\n' "$LOCAL_MOUNT"
    printf '    Server mount: %s\n' "$SERVER_MOUNT"
    printf '    Timeout:      %ss\n' "$TIMEOUT"
    printf '    Processing:   %s\n' "$($ENABLE_PROCESSING && echo 'enabled by default' || echo 'off until you flip it in the host')"
fi

if ! $ASSUME_YES; then
    read -rp $'\n    Proceed? [Y/n] ' go || true
    [[ "${go:-y}" =~ ^[Yy]?$ ]] || { echo "Aborted."; exit 1; }
fi

# --- sudo wrapper for system-wide installs -----------------------------------
SUDO=""
if [[ "$SCOPE" == "system" || ! -w "$(dirname "$DEST")" ]]; then
    if [[ "$(id -u)" -ne 0 ]]; then
        command -v sudo >/dev/null 2>&1 || die "Need root to write $DEST and sudo is not available. Re-run as root."
        SUDO="sudo"
    fi
fi

# --- defaults.json patcher ---------------------------------------------------
# Edits in place, preserving every other key (project block, asyncMode, etc.).
patch_defaults() {
    local file="$1"
    AIFX_SERVER="$SERVER_ADDRESS" AIFX_PORT="$SERVER_PORT" \
    AIFX_LOCAL="$LOCAL_MOUNT" AIFX_SERVERMOUNT="$SERVER_MOUNT" \
    AIFX_TIMEOUT="$TIMEOUT" AIFX_ENABLE="$ENABLE_PROCESSING" \
    AIFX_OS="$AIFX_OS" \
    python3 - "$file" <<'PY'
import json, os, sys
path = sys.argv[1]
with open(path) as f:
    d = json.load(f)
d.setdefault("server", {})["serverAddress"] = os.environ["AIFX_SERVER"]
d["server"]["serverPort"] = int(os.environ["AIFX_PORT"])
storage = d.setdefault("storage", {})
lmp = storage.setdefault("localMountPath", {})
if not isinstance(lmp, dict):           # tolerate the legacy scalar form
    lmp = {}; storage["localMountPath"] = lmp
lmp[os.environ["AIFX_OS"]] = os.environ["AIFX_LOCAL"]
storage["serverMountPath"] = os.environ["AIFX_SERVERMOUNT"]
controls = d.setdefault("controls", {})
controls["timeout"] = int(os.environ["AIFX_TIMEOUT"])
controls["enableProcessing"] = os.environ["AIFX_ENABLE"] == "true"
with open(path, "w") as f:
    json.dump(d, f, indent=2)
    f.write("\n")
PY
}

# --- Install -----------------------------------------------------------------
step "Installing into $DEST"
$SUDO mkdir -p "$DEST"

for bundle in "${BUNDLES[@]}"; do
    name="$(basename "$bundle")"
    target="$DEST/$name"

    if ! $KEEP_DEFAULTS; then
        # Patch a private copy so we never mutate the read-only archive payload,
        # then move it into place. The defaults.json lives under Contents/.
        tmp="$(mktemp -d)"
        cp -R "$bundle" "$tmp/$name"
        cfg="$tmp/$name/Contents/Resources/config/defaults.json"
        if [[ -f "$cfg" ]]; then
            patch_defaults "$cfg" || { warn "$name: could not patch defaults.json — copying as-is"; }
        else
            warn "$name: no defaults.json — copying as-is"
        fi
        $SUDO rm -rf "$target"
        $SUDO cp -R "$tmp/$name" "$target"
        rm -rf "$tmp"
    else
        $SUDO rm -rf "$target"
        $SUDO cp -R "$bundle" "$target"
    fi
    # macOS tags anything that arrived via a browser with com.apple.quarantine,
    # and the flag is inherited by every file extracted from the archive. An OFX
    # host dlopen()ing a quarantined bundle can be refused, so strip it. No-op
    # on Linux, where the attribute does not exist.
    # NOT `xattr -dr`: macOS 26 dropped the -r flag, so that form exits 64 and
    # silently clears nothing. find(1) does the recursion instead. The per-file
    # "No such xattr" for unmarked files is expected, hence 2>/dev/null.
    if [[ "$AIFX_OS" == "macos" ]]; then
        $SUDO find "$target" -exec xattr -d com.apple.quarantine {} \; 2>/dev/null || true
    fi

    ok "$name"
done

# --- Done --------------------------------------------------------------------
step "Done"
printf '    Installed %d plugin(s) into %s\n' "${#BUNDLES[@]}" "$DEST"
printf '    %sRestart your OFX host%s (Resolve, Nuke, Fusion, Natron, Flame).\n' "$c_bold" "$c_off"
printf '    The plugins appear under the %sAIFX%s category.\n' "$c_bold" "$c_off"
if [[ "$DEST" != "$SYSTEM_DIR" && "$DEST" != "$USER_DIR" ]]; then
    printf '    %sNote:%s a non-standard path — make sure OFX_PLUGIN_PATH includes %s.\n' "$c_yellow" "$c_off" "$DEST"
fi
$KEEP_DEFAULTS && printf '    Configure each plugin (server URL, mount paths) in your host UI on first use.\n'
echo
