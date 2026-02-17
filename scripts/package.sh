#!/bin/bash
set -e

# Build in Release mode and generate .deb package
echo "Building in Release mode..."
./scripts/build.sh --release

echo "Generating .deb package..."
cd build
cpack

echo ""
echo "Package created:"
ls -lh *.deb
