#!/bin/bash
#
# release-ci.sh - Non-interactive release build for CI/CD
#
# Usage: ./release-ci.sh <major> <minor>
#   e.g. ./release-ci.sh 1 04
#
# Same as release.sh but takes version as arguments instead of interactive input.
#
# Build matrix:
#   - Chips: RP2040 (pico), RP2350 (pico2)
#   - Board variants: M1, M2, PC (Olimex PICO-PC), Z0 (Waveshare RP2350-PiZero)
#   - Video types: VGA, HDMI
#   - Audio types: I2S, PWM (PC supports PWM only)
#   - CPU speeds: 378, 428, 504 MHz (428 only for VGA)
#
# Total: 70 firmware variants
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Version from arguments
if [[ $# -lt 2 ]]; then
    echo "Usage: $0 <major> <minor>"
    echo "  e.g. $0 1 04"
    exit 1
fi

MAJOR=$((10#$1))
MINOR=$((10#$2))

# Validate
if [[ $MAJOR -lt 1 ]]; then
    echo "Error: Major version must be >= 1"
    exit 1
fi
if [[ $MINOR -lt 0 || $MINOR -ge 100 ]]; then
    echo "Error: Minor version must be 0-99"
    exit 1
fi

# Format version string
VERSION="${MAJOR}_$(printf '%02d' $MINOR)"
VERSION_DOT="${MAJOR}.$(printf '%02d' $MINOR)"
echo "Building release version: ${VERSION_DOT}"

# Save new version
echo "$MAJOR $MINOR" > version.txt

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
FAILED=0

echo "Building $TOTAL_BUILDS firmware variants..."
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

                # CMake output differs by chip:
                #   pico:  {prefix}-frank-c64-{VIDEO}-{CPU}MHz-{AUDIO}-v{VERSION}.uf2
                #   pico2: {prefix}-frank-c64-{VIDEO}-{CPU}MHz-F66-{AUDIO}-v{VERSION}.uf2
                if [[ "$PICO_BOARD" == "pico" ]]; then
                    BUILD_FILE="${BOARD_PREFIX}-frank-c64-${VIDEO}-${CPU}MHz-${AUDIO}-v${VERSION_DOT}.uf2"
                else
                    BUILD_FILE="${BOARD_PREFIX}-frank-c64-${VIDEO}-${CPU}MHz-F66-${AUDIO}-v${VERSION_DOT}.uf2"
                fi

                echo ""
                echo "[$BUILD_COUNT/$TOTAL_BUILDS] Building: $OUTPUT_NAME"
                echo "  Chip: ${CHIP_SLUG[$PICO_BOARD]} | Board: $BOARD | Video: $VIDEO | Audio: $AUDIO | CPU: ${CPU} MHz"

                # Clean and create build directory
                rm -rf build
                mkdir build
                cd build

                BUILD_LOG="$SCRIPT_DIR/build_${OUTPUT_NAME}.log"

                if ! cmake .. \
                    -DPICO_BOARD="$PICO_BOARD" \
                    -DBOARD_VARIANT="$BOARD" \
                    -DVIDEO_TYPE="$VIDEO" \
                    -DAUDIO_TYPE="$AUDIO" \
                    -DCPU_SPEED="$CPU" \
                    -DUSB_HID_ENABLED=1 \
                    -DDEBUG_LOGS_ENABLED=OFF \
                    -DFIRMWARE_VERSION="v${VERSION_DOT}" \
                    > "$BUILD_LOG" 2>&1; then
                    echo "  ✗ CMake configuration failed"
                    tail -30 "$BUILD_LOG"
                    FAILED=$((FAILED + 1))
                    cd "$SCRIPT_DIR"
                    continue
                fi

                # Build
                if make -j$(nproc) >> "$BUILD_LOG" 2>&1; then
                    BIN_DIR="$SCRIPT_DIR/bin/Release"

                    SRC_FILE="$BIN_DIR/$BUILD_FILE"

                    if [[ -f "$SRC_FILE" ]]; then
                        cp "$SRC_FILE" "$RELEASE_DIR/$OUTPUT_NAME"
                        echo "  ✓ Success → release/$OUTPUT_NAME"
                    else
                        echo "  ✗ Output file not found: $SRC_FILE"
                        echo "  Expected: $SRC_FILE"
                        echo "  Available files:"
                        ls -la "$BIN_DIR"/ 2>/dev/null | tail -10
                        FAILED=$((FAILED + 1))
                    fi
                    rm -f "$BUILD_LOG"
                else
                    echo "  ✗ Build failed — last 40 lines:"
                    tail -40 "$BUILD_LOG"
                    FAILED=$((FAILED + 1))
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

if [[ $FAILED -gt 0 ]]; then
    echo "Release build completed with $FAILED failures!"
    exit 1
else
    echo "Release build complete! All $BUILD_COUNT builds successful."
fi

echo ""
echo "Release files in: $RELEASE_DIR/"
ls -la "$RELEASE_DIR"/frank-c64_*_${VERSION}.* 2>/dev/null | awk '{print "  " $9 " (" $5 " bytes)"}'
echo ""
echo "Version: ${VERSION_DOT}"
echo "Build matrix: 2 chips (RP2040, RP2350) × 4 boards (M1, M2, PC, Z0) × 2 video × 2 audio × 3 speeds (428 VGA only, PC PWM only)"
