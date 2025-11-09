#!/bin/bash
#
# Build CubeSat Peripheral Fuzzing Harnesses
# Compiles GPS, ADCS, and Power fuzzing targets with sanitizers
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HARNESS_DIR="$SCRIPT_DIR/../harnesses/peripherals"
BUILD_DIR="$SCRIPT_DIR/../build/peripherals"

# Colors
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

echo -e "${GREEN}======================================${NC}"
echo -e "${GREEN}Building CubeSat Peripheral Fuzzers${NC}"
echo -e "${GREEN}======================================${NC}"
echo

# Create build directory
mkdir -p "$BUILD_DIR"

# Compiler flags for fuzzing
CFLAGS="-g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -Wall"
LDFLAGS="-lm"

# Build GPS NMEA fuzzer
echo -e "${YELLOW}[1/2] Building GPS NMEA fuzzer...${NC}"
gcc $CFLAGS \
    "$HARNESS_DIR/gps_nmea_fuzzer.c" \
    -o "$BUILD_DIR/gps_nmea_fuzzer" \
    $LDFLAGS

if [ $? -eq 0 ]; then
    echo -e "${GREEN}✓ GPS fuzzer built successfully${NC}"
    ls -lh "$BUILD_DIR/gps_nmea_fuzzer"
else
    echo -e "${RED}✗ GPS fuzzer build failed${NC}"
    exit 1
fi

echo

# Build ADCS sensor fuzzer
echo -e "${YELLOW}[2/2] Building ADCS sensor fuzzer...${NC}"
gcc $CFLAGS \
    "$HARNESS_DIR/adcs_sensor_fuzzer.c" \
    -o "$BUILD_DIR/adcs_sensor_fuzzer" \
    $LDFLAGS

if [ $? -eq 0 ]; then
    echo -e "${GREEN}✓ ADCS fuzzer built successfully${NC}"
    ls -lh "$BUILD_DIR/adcs_sensor_fuzzer"
else
    echo -e "${RED}✗ ADCS fuzzer build failed${NC}"
    exit 1
fi

echo
echo -e "${GREEN}======================================${NC}"
echo -e "${GREEN}Verifying Instrumentation${NC}"
echo -e "${GREEN}======================================${NC}"
echo

# Verify ASAN instrumentation
echo -e "${YELLOW}Checking for AddressSanitizer symbols...${NC}"
ASAN_SYMBOLS=$(readelf -s "$BUILD_DIR/gps_nmea_fuzzer" | grep -i asan | wc -l)
echo -e "GPS Fuzzer: ${GREEN}$ASAN_SYMBOLS ASAN symbols${NC}"

ASAN_SYMBOLS=$(readelf -s "$BUILD_DIR/adcs_sensor_fuzzer" | grep -i asan | wc -l)
echo -e "ADCS Fuzzer: ${GREEN}$ASAN_SYMBOLS ASAN symbols${NC}"

echo

# Verify UBSAN instrumentation
echo -e "${YELLOW}Checking for UndefinedBehaviorSanitizer symbols...${NC}"
UBSAN_SYMBOLS=$(readelf -s "$BUILD_DIR/gps_nmea_fuzzer" | grep -i ubsan | wc -l)
echo -e "GPS Fuzzer: ${GREEN}$UBSAN_SYMBOLS UBSAN symbols${NC}"

UBSAN_SYMBOLS=$(readelf -s "$BUILD_DIR/adcs_sensor_fuzzer" | grep -i ubsan | wc -l)
echo -e "ADCS Fuzzer: ${GREEN}$UBSAN_SYMBOLS UBSAN symbols${NC}"

echo
echo -e "${GREEN}======================================${NC}"
echo -e "${GREEN}Quick Sanity Tests${NC}"
echo -e "${GREEN}======================================${NC}"
echo

# Test GPS fuzzer with valid input
echo -e "${YELLOW}Testing GPS fuzzer with valid NMEA sentence...${NC}"
echo '$GPGGA,123519.000,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47' > /tmp/test_gps.nmea
if "$BUILD_DIR/gps_nmea_fuzzer" /tmp/test_gps.nmea 2>&1 | grep -q "ERROR"; then
    echo -e "${RED}✗ GPS fuzzer test failed${NC}"
else
    echo -e "${GREEN}✓ GPS fuzzer test passed${NC}"
fi

# Test ADCS fuzzer with valid input
echo -e "${YELLOW}Testing ADCS fuzzer with valid sensor packet...${NC}"
if [ -f "$SCRIPT_DIR/../corpus/adcs/valid_mag.bin" ]; then
    if "$BUILD_DIR/adcs_sensor_fuzzer" "$SCRIPT_DIR/../corpus/adcs/valid_mag.bin" 2>&1 | grep -q "ERROR"; then
        echo -e "${RED}✗ ADCS fuzzer test failed${NC}"
    else
        echo -e "${GREEN}✓ ADCS fuzzer test passed${NC}"
    fi
fi

rm -f /tmp/test_gps.nmea

echo
echo -e "${GREEN}======================================${NC}"
echo -e "${GREEN}Build Complete!${NC}"
echo -e "${GREEN}======================================${NC}"
echo
echo "Fuzzers built:"
echo "  - $BUILD_DIR/gps_nmea_fuzzer"
echo "  - $BUILD_DIR/adcs_sensor_fuzzer"
echo
echo "To run fuzzing campaigns:"
echo "  afl-fuzz -i corpus/gps -o findings/gps -- $BUILD_DIR/gps_nmea_fuzzer @@"
echo "  afl-fuzz -i corpus/adcs -o findings/adcs -- $BUILD_DIR/adcs_sensor_fuzzer @@"
echo
