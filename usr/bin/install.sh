#!/bin/bash
set -e

echo "🚀 Installing ANT-OS branding and custom scripts..."

# Define source and destination directories
BRANDING_SRC="$(pwd)/branding"
SCRIPTS_SRC="$(pwd)/scripts"
CONFIG_SRC="$(pwd)/config"
BRANDING_DEST="/usr/share/ant-os/branding"
SCRIPTS_DEST="/usr/local/bin"
CONFIG_DEST="/etc/ant-os"

# Create necessary directories
sudo mkdir -p "$BRANDING_DEST" "$SCRIPTS_DEST" "$CONFIG_DEST"

# Copy branding files
sudo cp -r "$BRANDING_SRC"/* "$BRANDING_DEST"

# Copy custom scripts
sudo cp -r "$SCRIPTS_SRC/os-creaters" "$SCRIPTS_DEST"
sudo chmod +x "$SCRIPTS_DEST/os-creaters"

# Copy system config files
sudo cp -r "$CONFIG_SRC"/* "$CONFIG_DEST"

echo "✅ ANT-OS installation complete!"
