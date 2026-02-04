#!/bin/bash
# generate_tables.sh - Run this from the sketch folder
PYTHON_SCRIPT="../../generator/generate_tables.py"
FONT_FILE="/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"

if [ ! -f "$PYTHON_SCRIPT" ]; then
    echo "Error: generator script not found at $PYTHON_SCRIPT"
    exit 1
fi

python3 "$PYTHON_SCRIPT" --out arduino_tables --emit-c --font-file "$FONT_FILE" --font-size 20 --gen-float --gen-atan --gen-stereo
