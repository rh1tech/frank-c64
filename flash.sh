#!/bin/bash
# Flash FRANK C64 to connected Pico device
# Copyright (c) 2024-2026 Mikhail Matveev <xtreme@rh1.tech>

if [[ -n "$1" ]]; then
    FIRMWARE="$1"
else
    # Auto-detect: find the most recent frank-c64 ELF in build/
    FIRMWARE=$(ls -t ./build/*frank-c64*.elf 2>/dev/null | head -1)
    if [[ -z "$FIRMWARE" ]]; then
        FIRMWARE=$(ls -t ./build/*frank-c64*.uf2 2>/dev/null | head -1)
    fi
fi

if [[ -z "$FIRMWARE" || ! -f "$FIRMWARE" ]]; then
    echo "Error: No firmware file found"
    echo "Usage: $0 [firmware.elf|firmware.uf2]"
    echo "Or build first, then run without arguments to auto-detect."
    exit 1
fi

echo "Flashing: $FIRMWARE"
picotool load -f "$FIRMWARE" && picotool reboot -f
