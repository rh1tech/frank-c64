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
#   - Board variants: M1, M2
#   - Video types: VGA, HDMI
#   - Audio types: I2S, PWM
#   - MOS2 modes: OFF, ON
#   - CPU speeds: 378, 428, 504 MHz (428 only for VGA)
#
# Total: 40 firmware variants
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
FAILED=0

echo "Building $TOTAL_BUILDS firmware variants..."
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
                        # CMake output: m{1,2}p2-murmc64-{VIDEO}-{CPU}MHz-F66-{AUDIO}-v{VERSION}.{m1p2,m2p2}
                        BUILD_FILE="${MOS2_EXT}-murmc64-${VIDEO}-${CPU}MHz-F66-${AUDIO}-v${VERSION_DOT}.${MOS2_EXT}"
                    else
                        OUTPUT_NAME="murmc64_m${BOARD_NUM}_${VIDEO_LC}_${AUDIO_LC}_${CPU}mhz_${VERSION}.uf2"
                        # CMake output: m{1,2}p2-murmc64-{VIDEO}-{CPU}MHz-F66-{AUDIO}-v{VERSION}.uf2
                        BUILD_FILE="${MOS2_EXT}-murmc64-${VIDEO}-${CPU}MHz-F66-${AUDIO}-v${VERSION_DOT}.uf2"
                    fi

                    echo ""
                    echo "[$BUILD_COUNT/$TOTAL_BUILDS] Building: $OUTPUT_NAME"
                    echo "  Board: $BOARD | Video: $VIDEO | Audio: $AUDIO | CPU: ${CPU} MHz | MOS2: $MOS2"

                    # Clean and create build directory
                    rm -rf build
                    mkdir build
                    cd build

                    # Configure with CMake
                    CMAKE_MOS2_FLAG=""
                    if [[ "$MOS2" == "ON" ]]; then
                        CMAKE_MOS2_FLAG="-DMOS2=ON"
                    fi

                    BUILD_LOG="$SCRIPT_DIR/build_${OUTPUT_NAME}.log"

                    if ! cmake .. \
                        -DPICO_PLATFORM=rp2350 \
                        -DBOARD_VARIANT="$BOARD" \
                        -DVIDEO_TYPE="$VIDEO" \
                        -DAUDIO_TYPE="$AUDIO" \
                        -DCPU_SPEED="$CPU" \
                        -DUSB_HID_ENABLED=1 \
                        -DDEBUG_LOGS_ENABLED=OFF \
                        -DFIRMWARE_VERSION="v${VERSION_DOT}" \
                        $CMAKE_MOS2_FLAG \
                        > "$BUILD_LOG" 2>&1; then
                        echo "  ✗ CMake configuration failed"
                        tail -30 "$BUILD_LOG"
                        FAILED=$((FAILED + 1))
                        cd "$SCRIPT_DIR"
                        continue
                    fi

                    # Build
                    if make -j$(nproc) >> "$BUILD_LOG" 2>&1; then
                        # Output goes to bin/Release/ per CMakeLists.txt
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
ls -la "$RELEASE_DIR"/murmc64_*_${VERSION}.* 2>/dev/null | awk '{print "  " $9 " (" $5 " bytes)"}'
echo ""
echo "Version: ${VERSION_DOT}"
echo "Build matrix: 2 boards × 2 video × 2 audio × 2 MOS2 × 3 speeds (428 MHz VGA only)"
