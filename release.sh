#!/bin/bash
# Copyright (c) 2024-2026 Mikhail Matveev <xtreme@rh1.tech>
#
# release.sh - Build release variants of MurmC64
#
# Build matrix:
#   - Board variants: M1, M2
#   - Video types: VGA, HDMI
#   - Audio types: I2S, PWM
#   - MOS2 modes: OFF, ON
#   - CPU speeds: 378, 428, 504 MHz (428 only for VGA)
#
# Output formats:
#   - UF2 files for direct flashing via BOOTSEL mode
#   - m1p2/m2p2 files for Murmulator OS (when MOS2=ON)
#
# All builds include USB HID support (keyboard/mouse via USB).
# PS/2 keyboard and NES gamepad are also supported simultaneously.
# USB Serial debug output is DISABLED in release builds.
#
# Output format: murmc64_mX_VIDEO_AUDIO_CPUmhz_A_BB.{uf2,m1p2,m2p2}
#   X     = Board variant (1 or 2)
#   VIDEO = vga or hdmi
#   AUDIO = i2s or pwm
#   CPU   = CPU speed in MHz
#   A     = Major version
#   BB    = Minor version (zero-padded)
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Version file
VERSION_FILE="version.txt"

# Read last version or initialize
if [[ -f "$VERSION_FILE" ]]; then
    read -r LAST_MAJOR LAST_MINOR < "$VERSION_FILE"
else
    LAST_MAJOR=1
    LAST_MINOR=0
fi

# Calculate next version (for default suggestion)
NEXT_MINOR=$((LAST_MINOR + 1))
NEXT_MAJOR=$LAST_MAJOR
if [[ $NEXT_MINOR -ge 100 ]]; then
    NEXT_MAJOR=$((NEXT_MAJOR + 1))
    NEXT_MINOR=0
fi

# Interactive version input
echo ""
echo -e "${CYAN}┌─────────────────────────────────────────────────────────────────┐${NC}"
echo -e "${CYAN}│                    MurmC64 Release Builder                      │${NC}"
echo -e "${CYAN}└─────────────────────────────────────────────────────────────────┘${NC}"
echo ""
echo -e "Last version: ${YELLOW}${LAST_MAJOR}.$(printf '%02d' $LAST_MINOR)${NC}"
echo ""

DEFAULT_VERSION="${NEXT_MAJOR}.$(printf '%02d' $NEXT_MINOR)"
read -p "Enter version [default: $DEFAULT_VERSION]: " INPUT_VERSION
INPUT_VERSION=${INPUT_VERSION:-$DEFAULT_VERSION}

# Parse version (handle both "1.00" and "1 00" formats)
if [[ "$INPUT_VERSION" == *"."* ]]; then
    MAJOR="${INPUT_VERSION%%.*}"
    MINOR="${INPUT_VERSION##*.}"
else
    read -r MAJOR MINOR <<< "$INPUT_VERSION"
fi

# Remove leading zeros for arithmetic, then re-pad
MINOR=$((10#$MINOR))
MAJOR=$((10#$MAJOR))

# Validate
if [[ $MAJOR -lt 1 ]]; then
    echo -e "${RED}Error: Major version must be >= 1${NC}"
    exit 1
fi
if [[ $MINOR -lt 0 || $MINOR -ge 100 ]]; then
    echo -e "${RED}Error: Minor version must be 0-99${NC}"
    exit 1
fi

# Format version string
VERSION="${MAJOR}_$(printf '%02d' $MINOR)"
VERSION_DOT="${MAJOR}.$(printf '%02d' $MINOR)"
echo ""
echo -e "${GREEN}Building release version: ${VERSION_DOT}${NC}"

# Save new version
echo "$MAJOR $MINOR" > "$VERSION_FILE"

# Create release directory
RELEASE_DIR="$SCRIPT_DIR/release"
mkdir -p "$RELEASE_DIR"

# Build matrix configuration
BOARD_VARIANTS=(M1 M2)
VIDEO_TYPES=(VGA HDMI)
AUDIO_TYPES=(I2S PWM)
MOS2_MODES=(OFF ON)
CPU_SPEEDS=(378 428 504)

# Calculate total builds (428 MHz only valid for VGA)
# VGA: 2 boards × 2 audio × 2 MOS2 × 3 speeds = 24
# HDMI: 2 boards × 2 audio × 2 MOS2 × 2 speeds = 16
TOTAL_BUILDS=40

BUILD_COUNT=0
FAILED_BUILDS=0

echo ""
echo -e "${YELLOW}Building $TOTAL_BUILDS firmware variants...${NC}"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

for BOARD in "${BOARD_VARIANTS[@]}"; do
    for VIDEO in "${VIDEO_TYPES[@]}"; do
        for AUDIO in "${AUDIO_TYPES[@]}"; do
            for MOS2 in "${MOS2_MODES[@]}"; do
                for CPU in "${CPU_SPEEDS[@]}"; do

                    # Skip invalid: CPU=428 only for VGA
                    if [[ "$CPU" == "428" && "$VIDEO" != "VGA" ]]; then
                        continue
                    fi

                    BUILD_COUNT=$((BUILD_COUNT + 1))

                    # Board variant number and MOS2 extension
                    if [[ "$BOARD" == "M1" ]]; then
                        BOARD_NUM=1
                        MOS2_EXT="m1p2"
                    else
                        BOARD_NUM=2
                        MOS2_EXT="m2p2"
                    fi

                    # Lowercase video/audio for filename
                    VIDEO_LC=$(echo "$VIDEO" | tr '[:upper:]' '[:lower:]')
                    AUDIO_LC=$(echo "$AUDIO" | tr '[:upper:]' '[:lower:]')

                    # Output filename
                    if [[ "$MOS2" == "ON" ]]; then
                        OUTPUT_NAME="murmc64_m${BOARD_NUM}_${VIDEO_LC}_${AUDIO_LC}_${CPU}mhz_${VERSION}.${MOS2_EXT}"
                    else
                        OUTPUT_NAME="murmc64_m${BOARD_NUM}_${VIDEO_LC}_${AUDIO_LC}_${CPU}mhz_${VERSION}.uf2"
                    fi

                    echo ""
                    echo -e "${CYAN}[$BUILD_COUNT/$TOTAL_BUILDS] Building: $OUTPUT_NAME${NC}"
                    echo -e "  Board: $BOARD | Video: $VIDEO | Audio: $AUDIO | CPU: ${CPU} MHz | MOS2: $MOS2"

                    # Clean and create build directory
                    rm -rf build
                    mkdir build
                    cd build

                    # Configure with CMake
                    CMAKE_MOS2_FLAG=""
                    if [[ "$MOS2" == "ON" ]]; then
                        CMAKE_MOS2_FLAG="-DMOS2=ON"
                    fi

                    cmake .. \
                        -DPICO_PLATFORM=rp2350 \
                        -DBOARD_VARIANT="$BOARD" \
                        -DVIDEO_TYPE="$VIDEO" \
                        -DAUDIO_TYPE="$AUDIO" \
                        -DCPU_SPEED="$CPU" \
                        -DUSB_HID_ENABLED=1 \
                        -DDEBUG_LOGS_ENABLED=OFF \
                        -DFIRMWARE_VERSION="v${VERSION_DOT}" \
                        $CMAKE_MOS2_FLAG \
                        > /dev/null 2>&1

                    # Build
                    if make -j8 > /dev/null 2>&1; then
                        # Output goes to bin/Release/ per CMakeLists.txt
                        BIN_DIR="$SCRIPT_DIR/bin/Release"

                        # Determine source file based on CMakeLists.txt naming:
                        # m{1,2}p2-murmc64-{VIDEO}-{CPU}MHz-F66-{AUDIO}-v{VERSION}[.m{1,2}p2][.uf2]
                        # MOS2 builds rename .uf2 to remove extension
                        if [[ "$MOS2" == "ON" ]]; then
                            SRC_FILE="$BIN_DIR/${MOS2_EXT}-murmc64-${VIDEO}-${CPU}MHz-F66-${AUDIO}-v${VERSION_DOT}.${MOS2_EXT}"
                        else
                            SRC_FILE="$BIN_DIR/${MOS2_EXT}-murmc64-${VIDEO}-${CPU}MHz-F66-${AUDIO}-v${VERSION_DOT}.uf2"
                        fi

                        if [[ -f "$SRC_FILE" ]]; then
                            cp "$SRC_FILE" "$RELEASE_DIR/$OUTPUT_NAME"
                            echo -e "  ${GREEN}✓ Success${NC} → release/$OUTPUT_NAME"
                        else
                            echo -e "  ${RED}✗ Output file not found: $SRC_FILE${NC}"
                            FAILED_BUILDS=$((FAILED_BUILDS + 1))
                        fi
                    else
                        echo -e "  ${RED}✗ Build failed${NC}"
                        FAILED_BUILDS=$((FAILED_BUILDS + 1))
                    fi

                    cd "$SCRIPT_DIR"

                done
            done
        done
    done
done

# Clean up build directory
rm -rf build

echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
if [[ $FAILED_BUILDS -eq 0 ]]; then
    echo -e "${GREEN}Release build complete! All $BUILD_COUNT builds successful.${NC}"
else
    echo -e "${YELLOW}Release build complete. ${GREEN}$((BUILD_COUNT - FAILED_BUILDS))${NC} successful, ${RED}$FAILED_BUILDS${NC} failed.${NC}"
fi

# Create platform ZIP archives
echo ""
echo -e "${CYAN}=== Creating platform archives ===${NC}"

cd "$RELEASE_DIR"

# M1 archive
M1_ZIP="murmc64_m1_${VERSION}.zip"
rm -f "$M1_ZIP"
M1_FILES=$(ls murmc64_m1_*_${VERSION}.* 2>/dev/null)
if [[ -n "$M1_FILES" ]]; then
    zip -q "$M1_ZIP" $M1_FILES
    M1_COUNT=$(echo "$M1_FILES" | wc -w | tr -d ' ')
    echo -e "  ${GREEN}✓${NC} $M1_ZIP ($M1_COUNT files)"
    rm -f $M1_FILES
fi

# M2 archive
M2_ZIP="murmc64_m2_${VERSION}.zip"
rm -f "$M2_ZIP"
M2_FILES=$(ls murmc64_m2_*_${VERSION}.* 2>/dev/null)
if [[ -n "$M2_FILES" ]]; then
    zip -q "$M2_ZIP" $M2_FILES
    M2_COUNT=$(echo "$M2_FILES" | wc -w | tr -d ' ')
    echo -e "  ${GREEN}✓${NC} $M2_ZIP ($M2_COUNT files)"
    rm -f $M2_FILES
fi

cd "$SCRIPT_DIR"

echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo -e "Release archives in: ${CYAN}$RELEASE_DIR/${NC}"
echo ""
ls -la "$RELEASE_DIR"/murmc64_m?_${VERSION}.zip 2>/dev/null | awk '{print "  " $NF " (" $5 " bytes)"}'
echo ""
echo -e "Version: ${CYAN}${VERSION_DOT}${NC}"
echo ""
echo "Build matrix: 2 boards × 2 video × 2 audio × 2 MOS2 × 3 speeds (428 MHz VGA only)"
