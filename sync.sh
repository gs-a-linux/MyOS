#!/bin/bash
while true; do
    # Wait for any modify, create, delete, or move event in the current folder
    inotifywait -r -e modify,create,delete,move --exclude '\.git' ./
    
    git add .
    git commit -m "Auto-sync update: $(date '+%Y-%m-%d %H:%M:%S')"
    git push origin main
    
    echo "Changes synced to GitHub!"
done
