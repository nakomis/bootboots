#!/bin/bash
#
# BootBoots ESP32-S3 Build and Flash Script
#
# Builds the catcam firmware and flashes it via USB.
# Use this when OTA is not available (device in boot loop, etc.)
#
# Usage:
#   ./build_and_flash.sh              # Auto-detect USB port
#   ./build_and_flash.sh /dev/cu.usbmodem14201
#
# Prerequisites:
#   1. Close serial monitor if running
#   2. Hold BOOT button, plug USB into OTG port, release BOOT
#

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CATCAM_DIR="$(dirname "$SCRIPT_DIR")"

# Build environment
BUILD_ENV="esp32s3cam"
BAUD_RATE=921600

# Flash settings for ESP32-S3 N16R8
FLASH_MODE="dout"
FLASH_FREQ="80m"
FLASH_SIZE="16MB"
FLASH_OFFSET="0x110000"  # OTA0 partition

echo -e "${BLUE}============================================================${NC}"
echo -e "${BLUE}  BootBoots ESP32-S3 Build & Flash${NC}"
echo -e "${BLUE}============================================================${NC}"
echo

# Find serial port
SERIAL_PORT="${1:-}"
if [ -z "$SERIAL_PORT" ]; then
    # Auto-detect USB modem port (ESP32-S3 in download mode)
    if [[ "$OSTYPE" == "darwin"* ]]; then
        SERIAL_PORT=$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)
    else
        SERIAL_PORT=$(ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null | head -1)
    fi
fi

if [ -z "$SERIAL_PORT" ] || [ ! -e "$SERIAL_PORT" ]; then
    echo -e "${YELLOW}No USB port detected. Please put ESP32-S3 in download mode:${NC}"
    echo "  1. Hold the BOOT button"
    echo "  2. Plug USB into the OTG port (while holding BOOT)"
    echo "  3. Release the BOOT button"
    echo
    if [[ "$OSTYPE" == "darwin"* ]]; then
        echo "Available ports:"
        ls /dev/cu.usb* 2>/dev/null || echo "  (none found)"
    else
        echo "Available ports:"
        ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null || echo "  (none found)"
    fi
    echo
    read -p "Enter serial port (or press ENTER to retry auto-detect): " SERIAL_PORT

    if [ -z "$SERIAL_PORT" ]; then
        if [[ "$OSTYPE" == "darwin"* ]]; then
            SERIAL_PORT=$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)
        else
            SERIAL_PORT=$(ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null | head -1)
        fi
    fi

    if [ -z "$SERIAL_PORT" ] || [ ! -e "$SERIAL_PORT" ]; then
        echo -e "${RED}Error: No serial port found${NC}"
        exit 1
    fi
fi

echo -e "${GREEN}Configuration:${NC}"
echo "  Serial Port: $SERIAL_PORT"
echo "  Build Env:   $BUILD_ENV"
echo "  Flash Addr:  $FLASH_OFFSET"
echo

# Check for required tools
ESPTOOL="$HOME/.platformio/packages/tool-esptoolpy/esptool.py"
if [ ! -f "$ESPTOOL" ]; then
    echo -e "${RED}Error: esptool.py not found at $ESPTOOL${NC}"
    exit 1
fi

# Step 1: Build firmware
echo -e "${BLUE}[1/2] Building firmware...${NC}"
cd "$CATCAM_DIR"
if ! pio run -e "$BUILD_ENV"; then
    echo -e "${RED}Build failed${NC}"
    exit 1
fi

FIRMWARE_BIN="$CATCAM_DIR/.pio/build/$BUILD_ENV/firmware.bin"
FIRMWARE_SIZE=$(stat -f%z "$FIRMWARE_BIN" 2>/dev/null || stat -c%s "$FIRMWARE_BIN" 2>/dev/null)
VERSION=$(grep 'FIRMWARE_VERSION' "$CATCAM_DIR/include/version.h" | cut -d'"' -f2)
echo -e "${GREEN}Built: v$VERSION ($FIRMWARE_SIZE bytes)${NC}"
echo

# Step 2: Flash firmware
echo -e "${BLUE}[2/2] Flashing to $FLASH_OFFSET...${NC}"

if ! python3 "$ESPTOOL" \
    --chip esp32s3 \
    --port "$SERIAL_PORT" \
    --baud "$BAUD_RATE" \
    write_flash -z \
    --flash_mode "$FLASH_MODE" \
    --flash_freq "$FLASH_FREQ" \
    --flash_size "$FLASH_SIZE" \
    "$FLASH_OFFSET" "$FIRMWARE_BIN"; then
    echo -e "${RED}Flash failed${NC}"
    exit 1
fi

echo
echo -e "${GREEN}============================================================${NC}"
echo -e "${GREEN}  Done! Flashed v$VERSION${NC}"
echo -e "${GREEN}============================================================${NC}"
echo
echo -e "${YELLOW}Unplug from OTG port, plug into UART port for serial:${NC}"
echo "  pio device monitor -b 115200"
echo
