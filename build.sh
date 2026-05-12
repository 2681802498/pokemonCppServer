#!/bin/bash

# Build script for Pokemon C++ Game Server

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

echo "=== Pokemon C++ Game Server Build ==="
echo ""

# Create build directory
if [ ! -d "$BUILD_DIR" ]; then
    echo "Creating build directory..."
    mkdir -p "$BUILD_DIR"
fi

cd "$BUILD_DIR"

# Run CMake
echo "Generating CMake build files..."
cmake ..

# Build
echo "Building project..."
make -j4

# Check if build was successful
if [ -f "bin/pokemon_server" ]; then
    echo ""
    echo "=== Build Successful ==="
    echo "Executable: $BUILD_DIR/bin/pokemon_server"
    echo ""
    echo "To run the server:"
    echo "  cd $BUILD_DIR"
    echo "  ./bin/pokemon_server"
else
    echo "Build failed!"
    exit 1
fi
