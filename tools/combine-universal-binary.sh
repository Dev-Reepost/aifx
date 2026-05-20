#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright OpenFX and contributors to the OpenFX project.
#
# Combine arm64 and x86_64 OFX Plugin Binaries into Universal Binary
#
# This script takes separate arm64 and x86_64 plugin binaries and combines
# them into a universal binary using lipo.

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

show_usage() {
    cat << EOF
Usage: $0 <plugin-name> <arm64-binary> <x86_64-binary> [options]

Combine separate arm64 and x86_64 plugin binaries into a universal binary.

Arguments:
  plugin-name       Name of the plugin (e.g., SAMSegmentation)
  arm64-binary      Path to arm64 .ofx binary
  x86_64-binary     Path to x86_64 .ofx binary

Options:
  -o, --output DIR  Output directory (default: build/Release)
  -i, --install     Install to ~/Library/OFX/Plugins after combining
  -h, --help        Show this help message

Examples:
  # Basic usage
  $0 SAMSegmentation \\
      build/Release/Release/SAMSegmentation.ofx.bundle/Contents/MacOS/SAMSegmentation.ofx \\
      /tmp/x86_64/SAMSegmentation.ofx

  # With custom output and install
  $0 SAMSegmentation arm64.ofx x86_64.ofx -o ~/Desktop -i

Output:
  Creates: <output-dir>/<plugin-name>-universal.ofx.bundle/

Requirements:
  - Must be run on macOS
  - Both binaries must exist and be valid Mach-O files
  - arm64 binary must include the full .ofx.bundle structure
EOF
}

# Check for minimum arguments
if [[ $# -lt 3 ]]; then
    log_error "Insufficient arguments"
    show_usage
    exit 1
fi

# Parse arguments
PLUGIN_NAME="$1"
ARM64_BINARY="$2"
X86_64_BINARY="$3"
shift 3

OUTPUT_DIR="build/Release"
INSTALL_PLUGIN=false

while [[ $# -gt 0 ]]; do
    case $1 in
        -o|--output)
            OUTPUT_DIR="$2"
            shift 2
            ;;
        -i|--install)
            INSTALL_PLUGIN=true
            shift
            ;;
        -h|--help)
            show_usage
            exit 0
            ;;
        *)
            log_error "Unknown option: $1"
            show_usage
            exit 1
            ;;
    esac
done

# Verify we're on macOS
if [[ "$OSTYPE" != "darwin"* ]]; then
    log_error "This script only works on macOS"
    exit 1
fi

log_info "Combining universal binary for: $PLUGIN_NAME"

# Verify binaries exist
if [[ ! -f "$ARM64_BINARY" ]]; then
    log_error "arm64 binary not found: $ARM64_BINARY"
    exit 1
fi

if [[ ! -f "$X86_64_BINARY" ]]; then
    log_error "x86_64 binary not found: $X86_64_BINARY"
    exit 1
fi

log_success "Found arm64 binary: $ARM64_BINARY"
log_success "Found x86_64 binary: $X86_64_BINARY"

# Verify architectures
log_info "Verifying binary architectures..."

ARM64_CHECK=$(lipo -info "$ARM64_BINARY" 2>&1)
if ! echo "$ARM64_CHECK" | grep -q "arm64"; then
    log_error "First binary is not arm64!"
    echo "$ARM64_CHECK"
    exit 1
fi

X86_64_CHECK=$(lipo -info "$X86_64_BINARY" 2>&1)
if ! echo "$X86_64_CHECK" | grep -q "x86_64"; then
    log_error "Second binary is not x86_64!"
    echo "$X86_64_CHECK"
    exit 1
fi

log_success "Architecture verification passed"
log_info "  arm64:  $ARM64_CHECK"
log_info "  x86_64: $X86_64_CHECK"

# Find the arm64 bundle directory (we'll use its structure)
ARM64_DIR=$(dirname "$ARM64_BINARY")
ARM64_BUNDLE=$(dirname "$(dirname "$ARM64_DIR")")

if [[ ! -d "$ARM64_BUNDLE" ]]; then
    log_error "Could not find arm64 bundle directory"
    log_error "Expected bundle at: $ARM64_BUNDLE"
    exit 1
fi

log_info "Using bundle structure from: $ARM64_BUNDLE"

# Create output bundle
OUTPUT_BUNDLE="$OUTPUT_DIR/${PLUGIN_NAME}-universal.ofx.bundle"
log_info "Creating universal bundle: $OUTPUT_BUNDLE"

# Copy the arm64 bundle structure
rm -rf "$OUTPUT_BUNDLE"
mkdir -p "$OUTPUT_DIR"
cp -r "$ARM64_BUNDLE" "$OUTPUT_BUNDLE"

# Create universal binary
UNIVERSAL_BINARY="$OUTPUT_BUNDLE/Contents/MacOS/${PLUGIN_NAME}.ofx"
log_info "Creating universal binary with lipo..."

lipo -create \
    "$ARM64_BINARY" \
    "$X86_64_BINARY" \
    -output "$UNIVERSAL_BINARY"

log_success "Universal binary created!"

# Verify universal binary
log_info "Verifying universal binary..."
UNIVERSAL_INFO=$(lipo -info "$UNIVERSAL_BINARY")
echo "$UNIVERSAL_INFO"

if ! echo "$UNIVERSAL_INFO" | grep -q "arm64"; then
    log_error "Universal binary missing arm64!"
    exit 1
fi

if ! echo "$UNIVERSAL_INFO" | grep -q "x86_64"; then
    log_error "Universal binary missing x86_64!"
    exit 1
fi

log_success "Universal binary contains both architectures"

# Show detailed info
log_info "Detailed architecture info:"
lipo -detailed_info "$UNIVERSAL_BINARY"

# Install if requested
if [[ "$INSTALL_PLUGIN" == true ]]; then
    INSTALL_DIR="$HOME/Library/OFX/Plugins"
    log_info "Installing to $INSTALL_DIR..."

    mkdir -p "$INSTALL_DIR"

    # Remove existing plugin
    if [[ -d "$INSTALL_DIR/${PLUGIN_NAME}-universal.ofx.bundle" ]]; then
        log_warning "Removing existing plugin"
        rm -rf "$INSTALL_DIR/${PLUGIN_NAME}-universal.ofx.bundle"
    fi

    cp -r "$OUTPUT_BUNDLE" "$INSTALL_DIR/"
    log_success "Installed to: $INSTALL_DIR/${PLUGIN_NAME}-universal.ofx.bundle"
fi

# Summary
echo
log_success "Universal binary build complete!"
echo
log_info "Bundle location: $OUTPUT_BUNDLE"
log_info "Binary: $UNIVERSAL_BINARY"
echo
log_info "Architectures: $(lipo -info "$UNIVERSAL_BINARY")"

if [[ "$INSTALL_PLUGIN" == false ]]; then
    echo
    log_info "To install:"
    log_info "  cp -r \"$OUTPUT_BUNDLE\" ~/Library/OFX/Plugins/"
    log_info "Or run: $0 ... --install"
fi
