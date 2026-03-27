#!/bin/bash
# Build FRANK C64 - Commodore 64 emulator (Frodo4) for RP2040/RP2350
# Copyright (c) 2024-2026 Mikhail Matveev <xtreme@rh1.tech>
#
# Usage: ./build.sh [pico|pico2]
#   pico  = RP2040 (default)
#   pico2 = RP2350

PICO_BOARD="${1:-pico}"

rm -rf ./build
mkdir build
cd build
cmake -DPICO_BOARD="$PICO_BOARD" -DBOARD_VARIANT=M2 -DVIDEO_TYPE=HDMI -DCPU_SPEED=252 -DUSB_HID_ENABLED=0 -DDEBUG_LOGS_ENABLED=ON ..
make -j4
