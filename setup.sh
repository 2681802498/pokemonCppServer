#!/bin/bash

# Setup script for Pokemon C++ Game Server
# This script installs all required dependencies

set -e

echo "=== Pokemon C++ Game Server Setup ==="

# Detect OS
if [[ "$OSTYPE" == "darwin"* ]]; then
    echo "Detected macOS"
    # Check if Homebrew is installed
    if ! command -v brew &> /dev/null; then
        echo "Installing Homebrew..."
        /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
    fi
    
    echo "Installing dependencies via Homebrew..."
    brew install cmake protobuf grpc hiredis nlohmann-json
    
elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
    echo "Detected Linux"
    # Check for package manager
    if command -v apt-get &> /dev/null; then
        echo "Installing dependencies via apt..."
        sudo apt-get update
        sudo apt-get install -y \
            cmake \
            protobuf-compiler \
            protobuf-compiler-grpc \
            libgrpc++-dev \
            libhiredis-dev \
            nlohmann-json3-dev \
            build-essential
    elif command -v dnf &> /dev/null; then
        echo "Installing dependencies via dnf..."
        sudo dnf install -y \
            cmake \
            protobuf-devel \
            grpc-devel \
            grpc-plugins \
            hiredis-devel \
            nlohmann_json-devel
    elif command -v yum &> /dev/null; then
        echo "Installing dependencies via yum..."
        sudo yum install -y \
            cmake \
            protobuf-devel \
            grpc-devel \
            hiredis-devel
    else
        echo "Unsupported package manager"
        exit 1
    fi
else
    echo "Unsupported OS"
    exit 1
fi

echo "Dependencies installed successfully!"
echo ""
echo "Next steps:"
echo "1. Build the project:"
echo "   mkdir -p build && cd build"
echo "   cmake .."
echo "   make -j4"
echo ""
echo "2. Run with local Redis:"
echo "   redis-server &"
echo "   ./bin/pokemon_server"
echo ""
echo "3. Or run with Docker Compose:"
echo "   docker-compose up --build"
