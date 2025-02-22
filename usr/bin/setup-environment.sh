#!/bin/bash
set -e

echo "🔧 Installing development tools for ANT-OS..."
sudo apt update
sudo apt install -y git make gcc build-essential python3 python3-pip

echo "✅ Development environment ready!"
