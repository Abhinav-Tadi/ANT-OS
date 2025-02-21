#!/bin/bash
set -e

echo "🔧 Setting up ISO build environment..."
sudo apt update
sudo apt install -y live-build debootstrap squashfs-tools xorriso grub-pc-bin grub-efi-amd64-bin mtools

# Define build directory
BUILD_DIR="$HOME/ant-os-build"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Copy ANT-OS files
cp -r ~/ANT-OS/* "$BUILD_DIR"

# Start ISO build process
lb config
lb build

echo "✅ ANT-OS ISO successfully created in $BUILD_DIR"
