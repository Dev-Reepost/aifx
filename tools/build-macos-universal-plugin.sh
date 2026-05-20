#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright OpenFX and contributors to the OpenFX project.
#
# Build macOS Universal Binary OFX Plugin (arm64 + x86_64)
#
# This script builds a plugin separately for arm64 and x86_64,
# then combines them into a macOS universal binary using lipo.
# Universal binaries only work on macOS.

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Global variables
PLUGIN_NAME="SAMSegmentation"
TARGET_NAME="SAMSegmentation"
CLEAN_BUILD=false
INSTALL_PLUGIN=false
INSTALL_DIR=""
ARM64_DIR="build/arm64"
X86_64_DIR="build/x86_64"
RELEASE_DIR="build/Release"
ARM64_BINARY=""
X86_64_BINARY=""
UNIVERSAL_BINARY=""

# ============================================================================
# Logging Functions
# ============================================================================

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1" >&2
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1" >&2
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1" >&2
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1" >&2
}

# ============================================================================
# Helper Functions
# ============================================================================

show_usage() {
    cat << EOF
Usage: $0 [options]

Build a universal binary (arm64 + x86_64) OFX plugin for macOS.

Options:
  -h, --help              Show this help message
  -p, --plugin NAME       Plugin name (default: SAMSegmentation)
  -t, --target NAME       CMake target name (default: SAMSegmentation)
  -c, --clean             Clean build directories before building
  -i, --install           Install to ~/Library/OFX/Plugins after building
  --install-dir DIR       Install to custom directory

Examples:
  $0                                  # Build SAMSegmentation as universal
  $0 -i                               # Build and install to ~/Library/OFX/Plugins
  $0 -p MyPlugin -t MyPluginTarget    # Build custom plugin
  $0 --clean --install                # Clean build and install
  $0 --install-dir ~/OFX/Plugins      # Install to custom directory

Requirements:
  - Must be run on macOS (arm64 recommended)
  - Requires Conan 2.x and CMake 3.28+
  - Dependencies must be available for both architectures

Note: This script builds each architecture separately and combines them.
      It requires Conan packages to be available for both arm64 and x86_64.
EOF
}

verify_macos() {
    if [[ "$OSTYPE" != "darwin"* ]]; then
        log_error "This script only works on macOS"
        exit 1
    fi
}

get_openfx_root() {
    local script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    OFX_ROOT="$(cd "$script_dir/../.." && pwd)"
    cd "$OFX_ROOT"
}

parse_arguments() {
    while [[ $# -gt 0 ]]; do
        case $1 in
            -h|--help)
                show_usage
                exit 0
                ;;
            -p|--plugin)
                PLUGIN_NAME="$2"
                shift 2
                ;;
            -t|--target)
                TARGET_NAME="$2"
                shift 2
                ;;
            -c|--clean)
                CLEAN_BUILD=true
                shift
                ;;
            -i|--install)
                INSTALL_PLUGIN=true
                shift
                ;;
            --install-dir)
                INSTALL_DIR="$2"
                INSTALL_PLUGIN=true
                shift 2
                ;;
            *)
                log_error "Unknown option: $1"
                show_usage
                exit 1
                ;;
        esac
    done

    # Set default install directory if not specified
    if [[ "$INSTALL_PLUGIN" == true && -z "$INSTALL_DIR" ]]; then
        INSTALL_DIR="$HOME/Library/OFX/Plugins"
    fi
}

clean_build_directories() {
    if [[ "$CLEAN_BUILD" == true ]]; then
        log_warning "Cleaning build directories..."
        rm -rf "$ARM64_DIR" "$X86_64_DIR"
        log_success "Build directories cleaned"
    fi
}

# ============================================================================
# Build Functions
# ============================================================================

install_conan_dependencies() {
    local arch=$1
    local build_dir=$2
    local arch_name=$3

    log_info "Installing Conan dependencies for $arch_name..."

    if ! conan install . \
        -s build_type=Release \
        -s arch="$arch" \
        -pr:b=default \
        --build=missing \
        -o "&:build_comfyui_plugins=True" \
        -of="$build_dir" 2>&1; then

        log_error "Failed to install $arch_name dependencies via Conan"
        log_warning "This likely means Conan packages don't have $arch_name prebuilt binaries"
        log_warning "Attempting to build from source (this may take a while)..."

        conan install . \
            -s build_type=Release \
            -s arch="$arch" \
            -pr:b=default \
            --build="*" \
            -o "&:build_comfyui_plugins=True" \
            -of="$build_dir"
    fi
}

configure_cmake() {
    local arch=$1
    local build_dir=$2
    local arch_name=$3

    log_info "Configuring CMake for $arch_name..."

    local toolchain=$(find "$build_dir" -name "conan_toolchain.cmake" | head -1)

    cmake -S . -B "$build_dir" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_OSX_ARCHITECTURES="$arch" \
        -DBUILD_COMFYUI_PLUGINS=ON \
        -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
        -DCMAKE_POLICY_DEFAULT_CMP0091=NEW
}

build_target() {
    local build_dir=$1
    local arch_name=$2

    log_info "Building $TARGET_NAME for $arch_name..."
    cmake --build "$build_dir" --config Release --target "$TARGET_NAME" --parallel
}

build_architecture() {
    local arch=$1
    local build_dir=$2
    local arch_name=$3

    log_info "Step: Building $arch_name version..."
    mkdir -p "$build_dir"

    install_conan_dependencies "$arch" "$build_dir" "$arch_name"
    configure_cmake "$arch" "$build_dir" "$arch_name"
    build_target "$build_dir" "$arch_name"

    log_success "$arch_name build completed"
}

# ============================================================================
# Binary Management Functions
# ============================================================================

find_binary() {
    local build_dir=$1
    local arch_name=$2

    local binary=$(find "$build_dir" -name "${TARGET_NAME}.ofx" -type f | head -1)

    if [[ -z "$binary" ]]; then
        log_error "Could not find $arch_name binary for $TARGET_NAME"
        exit 1
    fi

    log_success "Found $arch_name binary: $binary"
    echo "$binary"
}

verify_architecture() {
    local binary=$1
    local expected_arch=$2
    local arch_name=$3

    local arch_info=$(lipo -info "$binary")

    if ! echo "$arch_info" | grep -q "architecture: $expected_arch"; then
        log_error "$arch_name binary is not actually $expected_arch!"
        echo "$arch_info"
        exit 1
    fi
}

locate_binaries() {
    log_info "Step: Locating plugin binaries..."

    ARM64_BINARY=$(find_binary "$ARM64_DIR" "arm64")
    X86_64_BINARY=$(find_binary "$X86_64_DIR" "x86_64")

    log_success "Both binaries located"
}

verify_binaries() {
    log_info "Verifying binary architectures..."

    verify_architecture "$ARM64_BINARY" "arm64" "arm64"
    verify_architecture "$X86_64_BINARY" "x86_64" "x86_64"

    log_success "Both binaries verified"
}

# ============================================================================
# Bundle Creation Functions
# ============================================================================

create_universal_binary() {
    log_info "Step: Creating universal binary..."

    # Create output bundle structure
    local bundle_dir="$RELEASE_DIR/${TARGET_NAME}.ofx.bundle/Contents/MacOS"
    mkdir -p "$bundle_dir"

    UNIVERSAL_BINARY="$bundle_dir/${TARGET_NAME}.ofx"

    # Use lipo to create universal binary
    lipo -create \
        "$ARM64_BINARY" \
        "$X86_64_BINARY" \
        -output "$UNIVERSAL_BINARY"

    log_success "Universal binary created: $UNIVERSAL_BINARY"
}

verify_universal_binary() {
    log_info "Verifying universal binary..."
    lipo -info "$UNIVERSAL_BINARY"
    log_success "Universal binary verified"
}

copy_bundle_resources() {
    local arm64_bundle_dir=$(dirname "$(dirname "$ARM64_BINARY")")

    # Copy Info.plist
    if [[ -f "$arm64_bundle_dir/Info.plist" ]]; then
        cp "$arm64_bundle_dir/Info.plist" "$RELEASE_DIR/${TARGET_NAME}.ofx.bundle/Contents/"
        log_success "Copied Info.plist"
    fi

    # Copy Resources directory if it exists (important for ComfyUI plugins)
    if [[ -d "$arm64_bundle_dir/Resources" ]]; then
        cp -r "$arm64_bundle_dir/Resources" "$RELEASE_DIR/${TARGET_NAME}.ofx.bundle/Contents/"
        log_success "Copied Resources directory"
    fi
}

# ============================================================================
# Installation Functions
# ============================================================================

install_plugin() {
    if [[ "$INSTALL_PLUGIN" != true ]]; then
        return
    fi

    log_info "Step: Installing plugin to $INSTALL_DIR..."

    mkdir -p "$INSTALL_DIR"

    # Remove existing plugin if present
    if [[ -d "$INSTALL_DIR/${TARGET_NAME}.ofx.bundle" ]]; then
        log_warning "Removing existing plugin at $INSTALL_DIR/${TARGET_NAME}.ofx.bundle"
        rm -rf "$INSTALL_DIR/${TARGET_NAME}.ofx.bundle"
    fi

    # Copy the bundle
    cp -r "$RELEASE_DIR/${TARGET_NAME}.ofx.bundle" "$INSTALL_DIR/"

    log_success "Plugin installed to: $INSTALL_DIR/${TARGET_NAME}.ofx.bundle"
}

# ============================================================================
# Summary Functions
# ============================================================================

print_summary() {
    log_success "Build complete!"
    echo
    log_info "Universal plugin bundle: $RELEASE_DIR/${TARGET_NAME}.ofx.bundle"

    if [[ "$INSTALL_PLUGIN" == false ]]; then
        log_info "To install for Flame/host apps:"
        log_info "  cp -r \"$RELEASE_DIR/${TARGET_NAME}.ofx.bundle\" \"$HOME/Library/OFX/Plugins/\""
        log_info "Or run: $0 --install"
    fi

    echo
    log_info "Binary architectures:"
    lipo -detailed_info "$UNIVERSAL_BINARY"
}

# ============================================================================
# Main Function
# ============================================================================

main() {
    parse_arguments "$@"
    verify_macos
    get_openfx_root

    log_info "Building universal binary for: $PLUGIN_NAME"
    log_info "OpenFX root: $OFX_ROOT"

    clean_build_directories

    # Build both architectures
    build_architecture "armv8" "$ARM64_DIR" "arm64"
    build_architecture "x86_64" "$X86_64_DIR" "x86_64"

    # Locate and verify binaries
    locate_binaries
    verify_binaries

    # Create universal binary
    create_universal_binary
    verify_universal_binary
    copy_bundle_resources

    # Install if requested
    install_plugin

    # Print summary
    print_summary
}

# Run main function
main "$@"
