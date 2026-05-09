#!/bin/sh
#
# Helper script to build OpenSSL for Zig build system
#

set -e

OPENSSL_SRC_DIR="deps/openssl-3.2.0"
OPENSSL_BUILD_DIR="$OPENSSL_SRC_DIR/build"

if [ ! -d "$OPENSSL_SRC_DIR" ]; then
    echo "Error: OpenSSL source directory not found: $OPENSSL_SRC_DIR"
    exit 1
fi

echo "Building OpenSSL..."

# Create build directory
mkdir -p "$OPENSSL_BUILD_DIR"

# Configure and build OpenSSL
cd "$OPENSSL_BUILD_DIR"

if [ ! -f "Makefile" ]; then
    echo "Configuring OpenSSL..."
    ../config no-shared no-tests
fi

echo "Compiling OpenSSL..."
make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)

echo "OpenSSL build completed successfully!"
echo "Libraries are in: $OPENSSL_BUILD_DIR"
