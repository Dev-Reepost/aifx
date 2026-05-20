#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright OpenFX and contributors to the OpenFX project.

# Build OpenFX ComfyUI plugins for Linux using Docker
# This script runs on macOS and builds Linux binaries in a Docker container

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

# Script directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
OFX_ROOT="$( cd "$SCRIPT_DIR/../.." && pwd )"

# Docker image name
DOCKER_IMAGE="openfx-linux-builder:latest"

# Default values
PLUGIN_NAME="SAMSegmentation"
TARGET_NAME="SAMSegmentation"
OUTPUT_DIR="$OFX_ROOT/build/linux"
INSTALL=false

# Parse arguments
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
        -h|--help)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Build OpenFX ComfyUI plugins for Linux using Docker"
            echo ""
            echo "Options:"
            echo "  -p, --plugin NAME     Plugin name (default: SAMSegmentation)"
            echo "  -t, --target NAME     CMake target name (default: SAMSegmentation)"
            echo "  -o, --output DIR      Output directory (default: build/linux)"
            echo "  --install             Copy to output directory after build"
            echo "  -h, --help            Show this help message"
            echo ""
            echo "Example:"
            echo "  $0 -p SAMSegmentation -t SAMSegmentation --install"
            exit 0
            ;;
        *)
            log_error "Unknown option: $1"
            exit 1
            ;;
    esac
done

log_info "Building Linux plugin: $PLUGIN_NAME"
log_info "OpenFX root: $OFX_ROOT"

# Check if Docker is running
if ! docker info >/dev/null 2>&1; then
    log_error "Docker is not running. Please start Docker and try again."
    exit 1
fi

# Build Docker image if it doesn't exist
if ! docker image inspect "$DOCKER_IMAGE" >/dev/null 2>&1; then
    log_info "Building Docker image: $DOCKER_IMAGE"
    docker build -t "$DOCKER_IMAGE" -f "$SCRIPT_DIR/Dockerfile.linux" "$SCRIPT_DIR"
    log_success "Docker image built"
else
    log_info "Using existing Docker image: $DOCKER_IMAGE"
fi

# Create output directory
mkdir -p "$OUTPUT_DIR"

# Run build in Docker container
log_info "Step 1/2: Building plugin in Docker container..."

docker run --rm \
    -v "$OFX_ROOT:/build" \
    -w /build \
    "$DOCKER_IMAGE" \
    bash -c "
        set -e
        echo '[INFO] Installing Conan dependencies...'
        conan install . \
            -s build_type=Release \
            -pr:b=default \
            --build=missing \
            -o "&:build_comfyui_plugins=True" \
            -of=build/linux

        echo '[INFO] Configuring CMake...'
        TOOLCHAIN=\$(find build/linux -name 'conan_toolchain.cmake' | head -1)
        cmake -S . -B build/linux \
            -DCMAKE_BUILD_TYPE=Release \
            -DBUILD_COMFYUI_PLUGINS=ON \
            -DCMAKE_TOOLCHAIN_FILE=\"\$TOOLCHAIN\" \
            -DCMAKE_POLICY_DEFAULT_CMP0091=NEW

        echo '[INFO] Building $TARGET_NAME...'
        cmake --build build/linux --config Release --target $TARGET_NAME --parallel

        echo '[SUCCESS] Build completed'
    "

if [ $? -ne 0 ]; then
    log_error "Build failed"
    exit 1
fi

log_success "Plugin built successfully"

# Find the built plugin
log_info "Step 2/2: Locating plugin binary..."

PLUGIN_BINARY=$(find "$OFX_ROOT/build/linux" -name "${PLUGIN_NAME}.ofx" -type f | head -1)

if [ -z "$PLUGIN_BINARY" ]; then
    log_error "Plugin binary not found: ${PLUGIN_NAME}.ofx"
    exit 1
fi

log_success "Found plugin: $PLUGIN_BINARY"

# Verify it's a Linux binary
if ! file "$PLUGIN_BINARY" | grep -q "ELF.*LSB"; then
    log_error "Binary is not a Linux ELF executable"
    file "$PLUGIN_BINARY"
    exit 1
fi

log_success "Verified Linux ELF binary"

# Show binary info
log_info "Binary information:"
file "$PLUGIN_BINARY"

# Install if requested
if [ "$INSTALL" = true ]; then
    log_info "Installing plugin to $OUTPUT_DIR..."

    # Find the .ofx.bundle directory
    PLUGIN_BUNDLE=$(dirname "$(dirname "$(dirname "$PLUGIN_BINARY")")")
    BUNDLE_NAME=$(basename "$PLUGIN_BUNDLE")

    # Copy bundle
    cp -r "$PLUGIN_BUNDLE" "$OUTPUT_DIR/"
    log_success "Plugin installed to: $OUTPUT_DIR/$BUNDLE_NAME"
fi

log_success "Build complete!"
echo ""
echo "Linux plugin binary: $PLUGIN_BINARY"
echo "To test on Linux: Copy to /usr/OFX/Plugins or ~/.OFX/Plugins"
