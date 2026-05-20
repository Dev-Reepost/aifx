#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright OpenFX and contributors to the OpenFX project.

# Build OpenFX ComfyUI plugins for Linux (both ARM64 and x86_64) using Docker
# This script runs on macOS and builds Linux binaries in Docker containers

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
OUTPUT_DIR=""
INSTALL=false
CLEAN=false
DOCKER_IMAGE="openfx-linux-builder:latest"
ARM64_BINARY=""
X86_64_BINARY=""

# ============================================================================
# Logging Functions
# ============================================================================

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

# ============================================================================
# Helper Functions
# ============================================================================

show_usage() {
    cat << EOF
Usage: $0 [OPTIONS]

Build OpenFX ComfyUI plugins for Linux (ARM64 + x86_64) using Docker

Options:
  -p, --plugin NAME     Plugin name (default: SAMSegmentation)
  -t, --target NAME     CMake target name (default: SAMSegmentation)
  -o, --output DIR      Output directory (default: build/linux-universal)
  --install             Copy to output directory after build
  -c, --clean           Clean build directories before building
  -h, --help            Show this help message

Example:
  $0 -p SAMSegmentation -t SAMSegmentation --install
EOF
}

get_openfx_root() {
    local script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    OFX_ROOT="$(cd "$script_dir/../.." && pwd)"

    # Set default output directory if not specified
    if [[ -z "$OUTPUT_DIR" ]]; then
        OUTPUT_DIR="$OFX_ROOT/build/linux-universal"
    fi
}

parse_arguments() {
    while [[ $# -gt 0 ]]; do
        case $1 in
            -p|--plugin)
                PLUGIN_NAME="$2"
                shift 2
                ;;
            -t|--target)
                TARGET_NAME="$2"
                shift 2
                ;;
            -o|--output)
                OUTPUT_DIR="$2"
                shift 2
                ;;
            --install)
                INSTALL=true
                shift
                ;;
            -c|--clean)
                CLEAN=true
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
}

verify_docker() {
    if ! docker info >/dev/null 2>&1; then
        log_error "Docker is not running. Please start Docker and try again."
        exit 1
    fi
}

ensure_docker_image() {
    if ! docker image inspect "$DOCKER_IMAGE" >/dev/null 2>&1; then
        log_info "Building Docker image: $DOCKER_IMAGE"
        local script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
        docker build -t "$DOCKER_IMAGE" -f "$script_dir/Dockerfile.linux" "$script_dir"
        log_success "Docker image built"
    else
        log_info "Using existing Docker image: $DOCKER_IMAGE"
    fi
}

clean_build_directories() {
    if [[ "$CLEAN" == true ]]; then
        log_warning "Cleaning build directories..."
        rm -rf "$OFX_ROOT/build/linux-arm64" "$OFX_ROOT/build/linux-x86_64"
        log_success "Build directories cleaned"
    fi
}

prepare_output_directory() {
    mkdir -p "$OUTPUT_DIR"
}

# ============================================================================
# Docker Build Functions
# ============================================================================

build_in_docker() {
    local arch=$1
    local build_dir=$2
    local platform_flag=$3
    local arch_name=$4

    log_info "Step: Building $arch_name version..."

    if [[ -n "$platform_flag" ]]; then
        log_warning "This may take 10-15 minutes due to emulation..."
    fi

    local docker_cmd="docker run --rm"
    if [[ -n "$platform_flag" ]]; then
        docker_cmd="$docker_cmd --platform $platform_flag"
    fi

    $docker_cmd \
        -v "$OFX_ROOT:/build" \
        -w /build \
        "$DOCKER_IMAGE" \
        bash -c "
            set -e
            echo '[INFO] Installing Conan dependencies for $arch_name...'
            conan install . \
                -s build_type=Release \
                -s arch=$arch \
                -pr:b=default \
                --build=missing \
                -o "&:build_comfyui_plugins=True" \
                -of=$build_dir

            echo '[INFO] Configuring CMake for $arch_name...'
            TOOLCHAIN=\$(find $build_dir -name 'conan_toolchain.cmake' | head -1)
            cmake -S . -B $build_dir \
                -DCMAKE_BUILD_TYPE=Release \
                -DBUILD_COMFYUI_PLUGINS=ON \
                -DCMAKE_TOOLCHAIN_FILE=\"\$TOOLCHAIN\" \
                -DCMAKE_POLICY_DEFAULT_CMP0091=NEW

            echo '[INFO] Building $TARGET_NAME for $arch_name...'
            cmake --build $build_dir --config Release --target $TARGET_NAME --parallel

            echo '[SUCCESS] $arch_name build completed'
        "

    if [[ $? -ne 0 ]]; then
        log_error "$arch_name build failed"
        exit 1
    fi

    log_success "$arch_name build completed"
}

build_arm64() {
    build_in_docker "armv8" "build/linux-arm64" "" "ARM64"
}

build_x86_64() {
    # Setup QEMU for x86_64 emulation if needed
    if ! docker run --rm --privileged multiarch/qemu-user-static --reset -p yes >/dev/null 2>&1; then
        log_warning "Could not set up QEMU emulation. Trying direct execution..."
    fi

    build_in_docker "x86_64" "build/linux-x86_64" "linux/amd64" "x86_64"
}

# ============================================================================
# Binary Management Functions
# ============================================================================

find_binary() {
    local build_dir=$1
    local arch_name=$2

    local binary=$(find "$OFX_ROOT/$build_dir" -name "${PLUGIN_NAME}.ofx" -type f | head -1)

    if [[ -z "$binary" ]]; then
        log_error "$arch_name binary not found: ${PLUGIN_NAME}.ofx"
        exit 1
    fi

    log_success "Found $arch_name binary: $binary"
    echo "$binary"
}

verify_architecture() {
    local binary=$1
    local expected_arch=$2
    local arch_name=$3

    local arch_info=$(file "$binary")

    if ! echo "$arch_info" | grep -q "$expected_arch"; then
        log_error "$arch_name binary is not actually $expected_arch!"
        echo "$arch_info"
        exit 1
    fi

    echo "$arch_info"
}

locate_binaries() {
    log_info "Step: Locating plugin binaries..."

    ARM64_BINARY=$(find_binary "build/linux-arm64" "ARM64")
    X86_64_BINARY=$(find_binary "build/linux-x86_64" "x86_64")

    log_success "Both binaries located"
}

verify_binaries() {
    log_info "Verifying binary architectures..."

    local arm64_info=$(verify_architecture "$ARM64_BINARY" "ARM aarch64" "ARM64")
    local x86_64_info=$(verify_architecture "$X86_64_BINARY" "x86-64" "x86_64")

    log_success "Both binaries verified"

    # Show binary info
    log_info "Binary information:"
    echo "ARM64:   $arm64_info"
    echo "x86_64:  $x86_64_info"
}

# ============================================================================
# Packaging Functions
# ============================================================================

package_plugins() {
    if [[ "$INSTALL" != true ]]; then
        return
    fi

    log_info "Step: Packaging plugins to $OUTPUT_DIR..."

    # Create architecture-specific directories
    mkdir -p "$OUTPUT_DIR/arm64"
    mkdir -p "$OUTPUT_DIR/x86_64"

    # Find and copy bundle directories
    local arm64_bundle=$(dirname "$(dirname "$(dirname "$ARM64_BINARY")")")
    local x86_64_bundle=$(dirname "$(dirname "$(dirname "$X86_64_BINARY")")")

    local bundle_name=$(basename "$arm64_bundle")

    # Copy bundles
    cp -r "$arm64_bundle" "$OUTPUT_DIR/arm64/"
    cp -r "$x86_64_bundle" "$OUTPUT_DIR/x86_64/"

    log_success "ARM64 plugin: $OUTPUT_DIR/arm64/$bundle_name"
    log_success "x86_64 plugin: $OUTPUT_DIR/x86_64/$bundle_name"
}

# ============================================================================
# Summary Functions
# ============================================================================

print_summary() {
    log_success "Build complete!"
    echo ""
    echo "Linux ARM64 binary:  $ARM64_BINARY"
    echo "Linux x86_64 binary: $X86_64_BINARY"
    echo ""
    echo "To deploy on Linux:"
    echo "  - ARM64 systems: Copy from $OUTPUT_DIR/arm64/ to /usr/OFX/Plugins or ~/.OFX/Plugins"
    echo "  - x86_64 systems: Copy from $OUTPUT_DIR/x86_64/ to /usr/OFX/Plugins or ~/.OFX/Plugins"
}

# ============================================================================
# Main Function
# ============================================================================

main() {
    parse_arguments "$@"
    get_openfx_root

    log_info "Building Linux universal plugin: $PLUGIN_NAME"
    log_info "OpenFX root: $OFX_ROOT"

    verify_docker
    ensure_docker_image
    clean_build_directories
    prepare_output_directory

    # Build both architectures
    build_arm64
    build_x86_64

    # Locate and verify binaries
    locate_binaries
    verify_binaries

    # Package if requested
    package_plugins

    # Print summary
    print_summary
}

# Run main function
main "$@"
