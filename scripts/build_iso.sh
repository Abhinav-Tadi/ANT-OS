#!/bin/bash

ISO_NAME="/home/abhinav/ANT-OS/ANT-OS.iso"
BUILD_DIR="/home/abhinav/ANT-OS-ISO"
GRUB_CFG="/home/abhinav/ANT-OS/boot/grub/grub.cfg"

# Ensure required packages are installed
sudo apt update
sudo apt install -y grub-pc-bin grub-efi-amd64-bin xorriso mtools

# Create necessary directories
mkdir -p $BUILD_DIR/boot/grub

# Copy kernel & initrd
cp /home/abhinav/ANT-OS/boot/vmlinuz $BUILD_DIR/boot/vmlinuz
cp /home/abhinav/ANT-OS/boot/initrd.img $BUILD_DIR/boot/initrd.img

# Copy GRUB configuration
cp $GRUB_CFG $BUILD_DIR/boot/grub/grub.cfg

# Create bootable ISO
grub-mkrescue -o $ISO_NAME $BUILD_DIR --modules="linux normal iso9660 biosdisk memdisk search tar ls"

echo "✅ ANT-OS ISO created successfully: $ISO_NAME"
