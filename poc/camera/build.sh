#!/bin/bash
#
# AXION POC Native Build Script
# Builds the ACAP as a native binary for AXIS OS 12 compatibility
#

set -e

echo "====== Building AXION POC ACAP (Native Binary) ======"

# Check if Docker is running (needed for SDK access)
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
echo "Target: AXIS OS 12 (Native Binary)"
echo ""

# Get absolute path to current directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Clean previous build artifacts
echo "Cleaning previous build artifacts..."
rm -f "$SCRIPT_DIR"/*.eap
rm -f "$SCRIPT_DIR"/app/*.eap
rm -f "$SCRIPT_DIR"/app/*.o
rm -f "$SCRIPT_DIR"/app/axion_poc

# Build using ACAP Native SDK container (for compilation only)
echo ""
echo "Compiling native binary using ACAP SDK container..."
docker run --rm \
    -v "$SCRIPT_DIR":/opt/app \
    -w /opt/app/app \
    axisecp/acap-native-sdk:${SDK_VERSION}-${ARCH}-ubuntu${UBUNTU_VERSION} \
    sh -c ". /opt/axis/acapsdk/environment-setup* && acap-build . -a settings/settings.json -a settings/mqtt.json"

# Check if build succeeded
if [ -f "$SCRIPT_DIR"/app/*.eap ]; then
    # Move .eap file to camera directory root
    mv "$SCRIPT_DIR"/app/*.eap "$SCRIPT_DIR"/

    echo ""
    echo "====== Build Complete ======"
    echo "Package Type: Native Binary (AXIS OS 12 compatible)"
    ls -lh "$SCRIPT_DIR"/*.eap

    # Show package size comparison
    EAP_SIZE=$(ls -lh "$SCRIPT_DIR"/*.eap | awk '{print $5}')
    echo ""
    echo "Package size: $EAP_SIZE (native binary - much smaller than Docker-based ~100MB)"
    echo ""
    echo "Compatibility:"
    echo "  - AXIS OS 12: YES (native execution)"
    echo "  - AXIS OS 11: YES (backwards compatible)"
    echo ""
    echo "To deploy to camera:"
    echo "  cd .."
    echo "  ./deploy.sh <camera-ip> <username> <password>"
else
    echo ""
    echo "====== Build Failed ======"
    echo "Error: No .eap file generated"
    echo "Check the build output above for errors"
    exit 1
fi
