#!/bin/bash
# Copyright (c) 2024-2026 Mikhail Matveev <xtreme@rh1.tech>
#
# release.sh - Build release variants of FRANK C64
#
# Build matrix:
#   - Chips: RP2040 (pico), RP2350 (pico2)
#   - Board variants: M1, M2, PC (Olimex PICO-PC), Z0 (Waveshare RP2350-PiZero)
#   - Video types: VGA, HDMI
#   - Audio types: I2S, PWM (PC supports PWM only)
#   - CPU speeds: 378, 428, 504 MHz (428 only for VGA)
#
# Output: UF2 files for direct flashing via BOOTSEL mode
#
# All builds include USB HID support (keyboard/mouse via USB).
# PS/2 keyboard and NES gamepad are also supported simultaneously.
# USB Serial debug output is DISABLED in release builds.
#
# Output format: frank-c64_BOARD_CHIP_VIDEO_AUDIO_CPUmhz_A_BB.uf2
#   BOARD = m1, m2, pc, or z0
#   CHIP  = rp2040 or rp2350
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

# Accept version from command line: ./release.sh 1.05 or ./release.sh 1 05
if [[ $# -ge 1 ]]; then
    INPUT_VERSION="$1"
    # Also accept two separate arguments: ./release.sh 1 05
    if [[ $# -ge 2 && ! "$1" == *"."* ]]; then
        INPUT_VERSION="$1.$2"
    fi

    # Parse version
    if [[ "$INPUT_VERSION" == *"."* ]]; then
        MAJOR="${INPUT_VERSION%%.*}"
        MINOR="${INPUT_VERSION##*.}"
    else
        echo -e "${RED}Error: Invalid version format. Use MAJOR.MINOR (e.g., 1.05)${NC}"
        exit 1
    fi

    MINOR=$((10#$MINOR))
    MAJOR=$((10#$MAJOR))
else
    # Interactive mode: read last version and prompt
    if [[ -f "$VERSION_FILE" ]]; then
        read -r LAST_MAJOR LAST_MINOR < "$VERSION_FILE"
    else
        LAST_MAJOR=1
        LAST_MINOR=0
    fi

    NEXT_MINOR=$((LAST_MINOR + 1))
    NEXT_MAJOR=$LAST_MAJOR
    if [[ $NEXT_MINOR -ge 100 ]]; then
        NEXT_MAJOR=$((NEXT_MAJOR + 1))
        NEXT_MINOR=0
    fi

    echo ""
    echo -e "${CYAN}┌─────────────────────────────────────────────────────────────────┐${NC}"
    echo -e "${CYAN}│                    FRANK C64 Release Builder                    │${NC}"
    echo -e "${CYAN}└─────────────────────────────────────────────────────────────────┘${NC}"
    echo ""
    echo -e "Last version: ${YELLOW}${LAST_MAJOR}.$(printf '%02d' $LAST_MINOR)${NC}"
    echo ""

    DEFAULT_VERSION="${NEXT_MAJOR}.$(printf '%02d' $NEXT_MINOR)"
    read -p "Enter version [default: $DEFAULT_VERSION]: " INPUT_VERSION
    INPUT_VERSION=${INPUT_VERSION:-$DEFAULT_VERSION}

    if [[ "$INPUT_VERSION" == *"."* ]]; then
        MAJOR="${INPUT_VERSION%%.*}"
        MINOR="${INPUT_VERSION##*.}"
    else
        read -r MAJOR MINOR <<< "$INPUT_VERSION"
    fi

    MINOR=$((10#$MINOR))
    MAJOR=$((10#$MAJOR))
fi

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
PICO_BOARDS=(pico pico2)
BOARD_VARIANTS=(M1 M2 PC Z0)
VIDEO_TYPES=(VGA HDMI)
AUDIO_TYPES=(I2S PWM)
CPU_SPEEDS=(378 428 504)

# Board slug (for output filename)
declare -A BOARD_SLUG=( [M1]=m1 [M2]=m2 [PC]=pc [Z0]=z0 )
# CMake build name prefix per board+chip
declare -A BOARD_CMAKE_PREFIX=(
    [M1_pico]=m1p1 [M2_pico]=m2p1 [PC_pico]=PCp1 [Z0_pico]=z0p1
    [M1_pico2]=m1p2 [M2_pico2]=m2p2 [PC_pico2]=PCp2 [Z0_pico2]=z0p2
)
# Chip slug for output filename
declare -A CHIP_SLUG=( [pico]=rp2040 [pico2]=rp2350 )
# PC (Olimex PICO-PC) only supports PWM audio (no I2S pins)
declare -A BOARD_PWM_ONLY=( [M1]=0 [M2]=0 [PC]=1 [Z0]=0 )

# Calculate total builds (428 MHz only valid for VGA, PC has PWM only)
# Per chip: M1/M2/Z0 = 10 each, PC = 5 → 35 per chip
# 2 chips × 35 = 70
TOTAL_BUILDS=70

BUILD_COUNT=0
FAILED_BUILDS=0

echo ""
echo -e "${YELLOW}Building $TOTAL_BUILDS firmware variants...${NC}"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

for PICO_BOARD in "${PICO_BOARDS[@]}"; do
for BOARD in "${BOARD_VARIANTS[@]}"; do
    for VIDEO in "${VIDEO_TYPES[@]}"; do
        for AUDIO in "${AUDIO_TYPES[@]}"; do
            for CPU in "${CPU_SPEEDS[@]}"; do

                # Skip invalid: CPU=428 only for VGA
                if [[ "$CPU" == "428" && "$VIDEO" != "VGA" ]]; then
                    continue
                fi

                # Skip I2S for boards that only support PWM
                if [[ "$AUDIO" == "I2S" && "${BOARD_PWM_ONLY[$BOARD]}" == "1" ]]; then
                    continue
                fi

                BUILD_COUNT=$((BUILD_COUNT + 1))

                BOARD_PREFIX="${BOARD_CMAKE_PREFIX[${BOARD}_${PICO_BOARD}]}"

                # Lowercase video/audio for filename
                VIDEO_LC=$(echo "$VIDEO" | tr '[:upper:]' '[:lower:]')
                AUDIO_LC=$(echo "$AUDIO" | tr '[:upper:]' '[:lower:]')

                OUTPUT_NAME="frank-c64_${BOARD_SLUG[$BOARD]}_${CHIP_SLUG[$PICO_BOARD]}_${VIDEO_LC}_${AUDIO_LC}_${CPU}mhz_${VERSION}.uf2"

                echo ""
                echo -e "${CYAN}[$BUILD_COUNT/$TOTAL_BUILDS] Building: $OUTPUT_NAME${NC}"
                echo -e "  Chip: ${CHIP_SLUG[$PICO_BOARD]} | Board: $BOARD | Video: $VIDEO | Audio: $AUDIO | CPU: ${CPU} MHz"

                # Clean and create build directory
                rm -rf build
                mkdir build
                cd build

                cmake .. \
                    -DPICO_BOARD="$PICO_BOARD" \
                    -DBOARD_VARIANT="$BOARD" \
                    -DVIDEO_TYPE="$VIDEO" \
                    -DAUDIO_TYPE="$AUDIO" \
                    -DCPU_SPEED="$CPU" \
                    -DUSB_HID_ENABLED=1 \
                    -DDEBUG_LOGS_ENABLED=OFF \
                    -DFIRMWARE_VERSION="v${VERSION_DOT}" \
                    > /dev/null 2>&1

                # Build
                if make -j8 > /dev/null 2>&1; then
                    BIN_DIR="$SCRIPT_DIR/bin/Release"

                    # CMake output differs by chip:
                    #   pico:  {prefix}-frank-c64-{VIDEO}-{CPU}MHz-{AUDIO}-v{VERSION}.uf2
                    #   pico2: {prefix}-frank-c64-{VIDEO}-{CPU}MHz-F66-{AUDIO}-v{VERSION}.uf2
                    if [[ "$PICO_BOARD" == "pico" ]]; then
                        SRC_FILE="$BIN_DIR/${BOARD_PREFIX}-frank-c64-${VIDEO}-${CPU}MHz-${AUDIO}-v${VERSION_DOT}.uf2"
                    else
                        SRC_FILE="$BIN_DIR/${BOARD_PREFIX}-frank-c64-${VIDEO}-${CPU}MHz-F66-${AUDIO}-v${VERSION_DOT}.uf2"
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

for BOARD in M1 M2 PC Z0; do
    for CHIP in rp2040 rp2350; do
        SLUG="${BOARD_SLUG[$BOARD]}"
        ZIP_NAME="frank-c64_${SLUG}_${CHIP}_${VERSION}.zip"
        rm -f "$ZIP_NAME"
        FILES=$(ls frank-c64_${SLUG}_${CHIP}_*_${VERSION}.* 2>/dev/null)
        if [[ -n "$FILES" ]]; then
            zip -q "$ZIP_NAME" $FILES
            FILE_COUNT=$(echo "$FILES" | wc -w | tr -d ' ')
            echo -e "  ${GREEN}✓${NC} $ZIP_NAME ($FILE_COUNT files)"
            rm -f $FILES
        fi
    done
done

cd "$SCRIPT_DIR"

echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo -e "Release archives in: ${CYAN}$RELEASE_DIR/${NC}"
echo ""
ls -la "$RELEASE_DIR"/frank-c64_*_${VERSION}.zip 2>/dev/null | awk '{print "  " $NF " (" $5 " bytes)"}'
echo ""
echo -e "Version: ${CYAN}${VERSION_DOT}${NC}"
echo ""
echo "Build matrix: 2 chips (RP2040, RP2350) × 4 boards (M1, M2, PC, Z0) × 2 video × 2 audio × 3 speeds (428 VGA only, PC PWM only)"
