#!/bin/bash
# Usage: ./make.sh
# Configures, builds, and packages CollabSync in one shot.
# Requires sudo once for the plugin copy step (JUCE copies to /Library).

set -e

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$PROJECT_DIR/build"

echo "==> Configuring..."
cmake -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
    -Wno-dev \
    > /dev/null

mkdir -p "$PROJECT_DIR/dist"

echo "==> Building (sudo required for plugin install to /Library)..."
sudo cmake --build "$BUILD_DIR" --parallel

# Give ownership of build artifacts back to the current user
sudo chown -R "$(id -un)" "$BUILD_DIR" "$PROJECT_DIR/dist"

echo "==> Packaging..."
bash "$PROJECT_DIR/scripts/package.sh"

echo ""
echo "Done."
