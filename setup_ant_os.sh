#!/bin/bash

# Define base directory
BASE_DIR="/home/abhinav/ANT-OS"
LOG_FILE="$BASE_DIR/setup.log"
BACKUP_FILE="$BASE_DIR/move_backup.log"

# List of files to move (src → dest)
declare -A file_moves=(
    ["$BASE_DIR/branding/grub/background.png"]="/usr/share/backgrounds/background.png"
    ["$BASE_DIR/branding/plymouth/mint-logo.png"]="/usr/share/plymouth/themes/mint-logo.png"
    ["$BASE_DIR/linuxmint-source/mintupdate/usr/bin/mintupdate"]="/usr/bin/mintupdate"
    ["$BASE_DIR/boot/grub/grub.cfg"]="/boot/grub/grub.cfg"  # Fixed Path
    ["$BASE_DIR/config/system.dconf"]="/etc/dconf/system.dconf"
)

# List of essential packages
required_packages=("grub" "plymouth" "cinnamon" "mintupdate")

# List of empty folders to populate with sample files
declare -A empty_folders=(
    ["/etc"]="mint.conf plymouth.conf x-cinnamon-portals.conf"
    ["/usr/share/themes"]="default-theme.conf"
    ["/usr/share/icons"]="icon-theme.conf"
    ["/var/log"]="system.log"
)

# Function to move misplaced files
move_files() {
    echo "Moving misplaced files..." | tee -a "$LOG_FILE"
    > "$BACKUP_FILE"

    for src in "${!file_moves[@]}"; do
        dest="${file_moves[$src]}"

        if [ -f "$src" ]; then
            mkdir -p "$(dirname "$dest")"
            mv "$src" "$dest" || echo "Error moving file: mv '$src' '$dest'" >> setup.log
            echo "$dest → $src" >> "$BACKUP_FILE"
            echo "Moved: $src → $dest" | tee -a "$LOG_FILE"
        else
            echo "Warning: $src not found!" | tee -a "$LOG_FILE"
        fi
    done
}

# Function to fill empty folders with sample files
populate_empty_folders() {
    echo "Filling empty folders with sample files..." | tee -a "$LOG_FILE"

    for folder in "${!empty_folders[@]}"; do
        mkdir -p "$folder"

        for file in ${empty_folders[$folder]}; do
            sample_file="$folder/$file"

            if [ ! -f "$sample_file" ]; then
                echo "# Sample file for $file" > "$sample_file"
                echo "Created: $sample_file" | tee -a "$LOG_FILE"
            fi
        done
    done
}

# Function to verify missing dependencies
check_dependencies() {
    echo "Checking required system dependencies..." | tee -a "$LOG_FILE"

    for package in "${required_packages[@]}"; do
        if ! dpkg -l | grep -q "$package"; then
            echo "Installing missing package: $package" | tee -a "$LOG_FILE"
            sudo apt install -y "$package" || echo "Failed to install: $package" >> "$LOG_FILE"
        else
            echo "Found: $package" | tee -a "$LOG_FILE"
        fi
    done
}

# Function to undo file moves
undo_moves() {
    if [ ! -f "$BACKUP_FILE" ]; then
        echo "No previous moves to undo!" | tee -a "$LOG_FILE"
        exit 1
    fi

    echo "Undoing last move..." | tee -a "$LOG_FILE"

    while IFS=" → " read -r moved_file original_location; do
        if [ -f "$moved_file" ]; then
            mkdir -p "$(dirname "$original_location")"
            mv "$moved_file" "$original_location" || echo "Error moving file: mv '$moved_file' '$original_location'" >> setup.log
            echo "Restored: $moved_file → $original_location" | tee -a "$LOG_FILE"
        else
            echo "Warning: $moved_file not found!" | tee -a "$LOG_FILE"
        fi
    done < "$BACKUP_FILE"

    rm -f "$BACKUP_FILE"
    echo "Undo complete!" | tee -a "$LOG_FILE"
}

# Check if user wants to undo
if [ "$1" == "--undo" ]; then
    undo_moves
    exit 0
fi

# Run the setup process
echo "Starting ANT-OS setup..." | tee "$LOG_FILE"
move_files
populate_empty_folders
check_dependencies
echo "ANT-OS setup complete!" | tee -a "$LOG_FILE"
