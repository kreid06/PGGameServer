#!/bin/bash
cd "$(dirname "$0")"  # Change to script directory
if [ ! -f .env ]; then
    echo "ERROR: .env file not found!"
    echo "Please copy .env.example to .env and configure:"
    echo "cp .env.example .env"
    exit 1
fi
./build/game_dashboard
