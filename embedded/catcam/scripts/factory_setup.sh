#!/bin/bash
#
# BootBoots ESP32 Factory Setup Script
#
# This script sets up a factory-new ESP32-CAM with the bootloader-based OTA system.
# It builds and flashes both the bootloader (factory partition) and the catcam app (OTA0 partition).
#
# Usage:
#   ./factory_setup.sh [serial_port] [baud_rate]
#
# Examples:
#   ./factory_setup.sh /dev/cu.usbserial-1430
#   ./factory_setup.sh /dev/ttyUSB0 460800
#

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Default values
DEFAULT_BAUD=460800
SERIAL_PORT="${1:-}"
BAUD_RATE="${2:-$DEFAULT_BAUD}"

# Script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CATCAM_DIR="$(dirname "$SCRIPT_DIR")"
BOOTLOADER_DIR="$(dirname "$CATCAM_DIR")/bootloader"

echo -e "${BLUE}============================================================${NC}"
echo -e "${BLUE}  BootBoots ESP32 Factory Setup${NC}"
echo -e "${BLUE}============================================================${NC}"
echo

# Check if serial port is provided
if [ -z "$SERIAL_PORT" ]; then
    echo -e "${RED}Error: Serial port not specified${NC}"
    echo
    echo "Usage: $0 <serial_port> [baud_rate]"
    echo
    echo "Examples:"
    echo "  $0 /dev/cu.usbserial-1430"
    echo "  $0 /dev/ttyUSB0 460800"
    echo
    echo "Available ports:"
    if [[ "$OSTYPE" == "darwin"* ]]; then
        ls /dev/cu.* 2>/dev/null || echo "  No serial ports found"
    else
        ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null || echo "  No serial ports found"
    fi
    exit 1
fi

# Verify serial port exists
if [ ! -e "$SERIAL_PORT" ]; then
    echo -e "${RED}Error: Serial port $SERIAL_PORT does not exist${NC}"
    exit 1
fi

echo -e "${GREEN}Configuration:${NC}"
echo "  Serial Port: $SERIAL_PORT"
echo "  Baud Rate: $BAUD_RATE"
echo "  Catcam Dir: $CATCAM_DIR"
echo "  Bootloader Dir: $BOOTLOADER_DIR"
echo

# Check for required tools
echo -e "${BLUE}Checking prerequisites...${NC}"

if ! command -v pio &> /dev/null; then
    echo -e "${RED}Error: PlatformIO CLI (pio) not found${NC}"
    echo "Install with: pip install platformio"
    exit 1
fi

ESPTOOL="$HOME/.platformio/packages/tool-esptoolpy/esptool.py"
if [ ! -f "$ESPTOOL" ]; then
    echo -e "${RED}Error: esptool.py not found at $ESPTOOL${NC}"
    echo "PlatformIO may not be properly installed"
    exit 1
fi

echo -e "${GREEN}✓ Prerequisites OK${NC}"
echo

# Function to wait for user to put device in download mode
wait_for_download_mode() {
    echo -e "${YELLOW}Please put the ESP32 in download mode:${NC}"
    echo "  1. Hold the BOOT button"
    echo "  2. Press and release the RESET button"
    echo "  3. Release the BOOT button"
    echo
    read -p "Press ENTER when ready..."
    echo
}

# Step 1: Build Bootloader
echo -e "${BLUE}[1/5] Building bootloader...${NC}"
cd "$BOOTLOADER_DIR"
if ! pio run -e esp32s3cam; then
    echo -e "${RED}✗ Bootloader build failed${NC}"
    exit 1
fi

BOOTLOADER_2ND="$BOOTLOADER_DIR/.pio/build/esp32s3cam/bootloader.bin"
BOOTLOADER_PARTITIONS="$BOOTLOADER_DIR/.pio/build/esp32s3cam/partitions.bin"
BOOTLOADER_BIN="$BOOTLOADER_DIR/.pio/build/esp32s3cam/firmware.bin"
BOOTLOADER_SIZE=$(stat -f%z "$BOOTLOADER_BIN" 2>/dev/null || stat -c%s "$BOOTLOADER_BIN" 2>/dev/null)
echo -e "${GREEN}✓ Bootloader built: $(numfmt --to=iec-i --suffix=B $BOOTLOADER_SIZE 2>/dev/null || echo "${BOOTLOADER_SIZE} bytes")${NC}"
echo

# Step 2: Build Catcam App
echo -e "${BLUE}[2/5] Building catcam application...${NC}"
cd "$CATCAM_DIR"
if ! pio run -e esp32s3cam; then
    echo -e "${RED}✗ Catcam build failed${NC}"
fi

CATCAM_BIN="$CATCAM_DIR/.pio/build/esp32s3cam/firmware.bin"
CATCAM_SIZE=$(stat -f%z "$CATCAM_BIN" 2>/dev/null || stat -c%s "$CATCAM_BIN" 2>/dev/null)
echo -e "${GREEN}✓ Catcam built: $(numfmt --to=iec-i --suffix=B $CATCAM_SIZE 2>/dev/null || echo "${CATCAM_SIZE} bytes")${NC}"
echo

# Step 3: Flash Bootloader (2nd stage + partition table + factory firmware)
echo -e "${BLUE}[3/5] Flashing bootloader to factory partition...${NC}"
wait_for_download_mode

if ! python3 "$ESPTOOL" \
    --chip esp32s3 \
    --port "$SERIAL_PORT" \
    --baud "$BAUD_RATE" \
    --before default_reset \
    --after hard_reset \
    write_flash -z --flash_mode dout --flash_freq 80m --flash_size 16MB \
    0x0 "$BOOTLOADER_2ND" \
    0x8000 "$BOOTLOADER_PARTITIONS" \
    0x10000 "$BOOTLOADER_BIN"; then
    echo -e "${RED}✗ Bootloader flash failed${NC}"
    exit 1
fi

echo -e "${GREEN}✓ Bootloader flashed successfully${NC}"
echo

# Step 4: Flash Catcam App to OTA0 Partition
echo -e "${BLUE}[4/5] Flashing catcam app to OTA0 partition (0x110000)...${NC}"
wait_for_download_mode

if ! python3 "$ESPTOOL" \
    --chip esp32s3 \
    --port "$SERIAL_PORT" \
    --baud "$BAUD_RATE" \
    --before default_reset \
    --after hard_reset \
    write_flash -z --flash_mode dout --flash_freq 80m --flash_size 16MB \
    0x110000 "$CATCAM_BIN"; then
    echo -e "${RED}✗ Catcam flash failed${NC}"
    exit 1
fi

echo -e "${GREEN}✓ Catcam app flashed successfully${NC}"
echo

# Step 5: Verify Boot Sequence
echo -e "${BLUE}[5/5] Verifying boot sequence...${NC}"
echo
echo -e "${YELLOW}Connect to serial monitor to verify:${NC}"
echo "  pio device monitor --port $SERIAL_PORT --baud 115200"
echo
echo -e "${YELLOW}Expected output:${NC}"
echo "  1. [BOOTLOADER] messages from factory partition"
echo "  2. [CATCAM] Set boot partition to factory for next reboot"
echo "  3. Device running normally"
echo

# Summary
echo -e "${GREEN}============================================================${NC}"
echo -e "${GREEN}  ✓ Factory Setup Complete!${NC}"
echo -e "${GREEN}============================================================${NC}"
echo
echo -e "${GREEN}Partition Layout:${NC}"
echo "  Factory (1MB @ 0x10000):    Bootloader"
echo "  OTA0 (7MB @ 0x110000):      Catcam App v$(grep 'FIRMWARE_VERSION' "$CATCAM_DIR/include/version.h" | cut -d'"' -f2)"
echo "  SPIFFS (@ 0x810000):        File storage"
echo
echo -e "${GREEN}Next Steps:${NC}"
echo "  1. Monitor serial output to verify boot sequence"
echo "  2. Test OTA update via web interface"
echo "  3. Verify bootloader runs on every reboot"
echo
echo -e "${BLUE}Happy Flashing! 🚀${NC}"
echo
