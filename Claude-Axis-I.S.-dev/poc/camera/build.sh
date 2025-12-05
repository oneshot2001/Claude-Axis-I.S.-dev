#!/bin/bash
#
# Axis I.S. POC Native Build Script
# Builds the ACAP as a native binary for AXIS OS 12 compatibility
#
# This script builds Paho MQTT and libjpeg from source within the Docker
# container to ensure proper cross-compilation for aarch64.
#

set -e

echo "====== Building Axis I.S. POC ACAP (Native Binary) ======"

# Check if Docker is running
if ! docker info > /dev/null 2>&1; then
    echo "Error: Docker is not running"
    echo "Docker is required to access the ACAP Native SDK compilation environment"
    exit 1
fi

# Build arguments
ARCH="${1:-aarch64}"
SDK_VERSION="${2:-12.2.0}"
UBUNTU_VERSION="24.04"

echo "Architecture: $ARCH"
echo "SDK Version: $SDK_VERSION"
echo "Ubuntu Version: $UBUNTU_VERSION"
echo "Target: AXIS OS 12 (Native Binary)"
echo ""

# Get absolute path to current directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Clean previous build artifacts
echo "Cleaning previous build artifacts..."
rm -f "$SCRIPT_DIR"/*.eap
rm -f "$SCRIPT_DIR"/app/*.eap
rm -f "$SCRIPT_DIR"/app/*.o
rm -f "$SCRIPT_DIR"/app/axis_is_poc

# Build using Dockerfile (includes Paho MQTT and libjpeg compilation)
echo ""
echo "Building ACAP with Dockerfile (includes dependency compilation)..."
echo "This may take several minutes on first build..."
echo ""

# Build the Docker image
docker build \
    --build-arg ARCH=${ARCH} \
    --build-arg SDK_VERSION=${SDK_VERSION} \
    --build-arg UBUNTU_VERSION=${UBUNTU_VERSION} \
    --platform linux/amd64 \
    -t axis-is-poc-builder \
    "$SCRIPT_DIR"

# Extract the .eap file from the final image using docker save
# Since we use FROM scratch, we need to extract from the image layers
echo ""
echo "Extracting .eap package..."
rm -rf "$SCRIPT_DIR/output_tmp"
mkdir -p "$SCRIPT_DIR/output_tmp"
docker save axis-is-poc-builder | tar -xf - -C "$SCRIPT_DIR/output_tmp"

# Find and extract the .eap file from the layer
LAYER_TAR=$(find "$SCRIPT_DIR/output_tmp" -name "*.tar" | head -1)
if [ -n "$LAYER_TAR" ]; then
    tar -xf "$LAYER_TAR" -C "$SCRIPT_DIR/output_tmp" '*.eap' 2>/dev/null || true
fi

# Also check blobs directory for OCI format
for blob in "$SCRIPT_DIR/output_tmp/blobs/sha256/"*; do
    if [ -f "$blob" ]; then
        tar -xf "$blob" -C "$SCRIPT_DIR/output_tmp" '*.eap' 2>/dev/null || true
    fi
done

# Move .eap file to camera directory root
if ls "$SCRIPT_DIR"/output_tmp/*.eap 1>/dev/null 2>&1; then
    mv "$SCRIPT_DIR"/output_tmp/*.eap "$SCRIPT_DIR"/
    rm -rf "$SCRIPT_DIR/output_tmp"

    echo ""
    echo "====== Build Complete ======"
    echo "Package Type: Native Binary (AXIS OS 12 compatible)"
    ls -lh "$SCRIPT_DIR"/*.eap

    # Show package info
    EAP_FILE=$(ls "$SCRIPT_DIR"/*.eap | head -1)
    EAP_SIZE=$(ls -lh "$EAP_FILE" | awk '{print $5}')
    echo ""
    echo "Package: $(basename "$EAP_FILE")"
    echo "Size: $EAP_SIZE"
    echo ""
    echo "Features:"
    echo "  - Core: VDO streaming, Larod inference, DLPU coordination"
    echo "  - Detection Module: YOLOv5n object detection"
    echo "  - Frame Publisher: On-demand JPEG frame transmission"
    echo "  - MQTT: Camera-to-cloud communication"
    echo ""
    echo "Compatibility:"
    echo "  - AXIS OS 12: YES (native execution)"
    echo "  - AXIS OS 11: YES (backwards compatible)"
    echo ""
    echo "To deploy to camera:"
    echo "  ./deploy.sh <camera-ip> <username> <password>"
else
    rm -rf "$SCRIPT_DIR/output_tmp"
    echo ""
    echo "====== Build Failed ======"
    echo "Error: No .eap file generated"
    echo "Check the build output above for errors"
    exit 1
fi
