#!/bin/bash

# Define base paths
BASE_DIR="/home/abhinav/ANT-OS"
LOG_FILE="$BASE_DIR/move_files.log"
BACKUP_FILE="$BASE_DIR/move_backup.log"

# Define expected folders for different file types
declare -A expected_folders=(
    ["png"]="usr/share/icons/"
    ["svg"]="usr/share/icons/"
    ["sh"]="usr/bin/"
    ["so"]="lib/"
    ["conf"]="etc/"
    ["log"]="var/log/"
)

# Function to move files based on type
move_files() {
    echo "Starting file reorganization..." | tee "$LOG_FILE"
    > "$BACKUP_FILE"

    find "$BASE_DIR" -type f | while read -r file; do
        ext="${file##*.}"
        dest="${expected_folders[$ext]}"

        if [[ -n "$dest" ]]; then
            new_path="$BASE_DIR/$dest$(basename "$file")"

            mkdir -p "$(dirname "$new_path")"
            mv "$file" "$new_path"
            echo "$new_path → $file" >> "$BACKUP_FILE"
            echo "Moved: $file → $new_path" | tee -a "$LOG_FILE"
        fi
    done

    echo "File reorganization complete!" | tee -a "$LOG_FILE"
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
            mv "$moved_file" "$original_location"
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

# Run the move process
move_files

