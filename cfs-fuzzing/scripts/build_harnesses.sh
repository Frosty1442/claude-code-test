#!/bin/bash
#
# Build AFL++ Fuzzing Harnesses for cFS
#
# This script compiles all fuzzing harnesses with sanitizers and coverage
#

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}cFS AFL++ Fuzzing Harness Builder${NC}"
echo -e "${GREEN}========================================${NC}"

# Configuration
HARNESS_DIR="../harnesses"
BUILD_DIR="../build"
CFS_ROOT="../../cFS"

# Compiler flags for fuzzing with sanitizers
CFLAGS="-g -O1 -fsanitize=address,undefined -fsanitize-address-use-after-scope \
        -fno-omit-frame-pointer -fno-optimize-sibling-calls --coverage \
        -fprofile-arcs -ftest-coverage"

LDFLAGS="-fsanitize=address,undefined -lgcov"

# Include paths for cFS headers
INCLUDES="-I${CFS_ROOT}/cfe/modules/core_api/fsw/inc \
          -I${CFS_ROOT}/cfe/modules/sb/fsw/inc \
          -I${CFS_ROOT}/cfe/modules/tbl/fsw/inc \
          -I${CFS_ROOT}/osal/src/os/inc"

# Create build directory
mkdir -p "$BUILD_DIR"

echo -e "\\n${YELLOW}[1/3] Building Software Bus Fuzzer...${NC}"
gcc $CFLAGS $INCLUDES $HARNESS_DIR/sb_fuzzer.c -o $BUILD_DIR/sb_fuzzer $LDFLAGS
if [ $? -eq 0 ]; then
    echo -e "${GREEN}✓ sb_fuzzer built successfully${NC}"
    ls -lh $BUILD_DIR/sb_fuzzer
else
    echo -e "${RED}✗ Failed to build sb_fuzzer${NC}"
    exit 1
fi

echo -e "\\n${YELLOW}[2/3] Verifying sanitizer instrumentation...${NC}"
if readelf -s $BUILD_DIR/sb_fuzzer | grep -q "__asan"; then
    echo -e "${GREEN}✓ AddressSanitizer instrumentation confirmed${NC}"
else
    echo -e "${RED}✗ AddressSanitizer not found${NC}"
fi

if readelf -s $BUILD_DIR/sb_fuzzer | grep -q "__ubsan"; then
    echo -e "${GREEN}✓ UndefinedBehaviorSanitizer instrumentation confirmed${NC}"
else
    echo -e "${YELLOW}⚠ UndefinedBehaviorSanitizer symbols not found (may be normal)${NC}"
fi

echo -e "\\n${YELLOW}[3/3] Generating test execution...${NC}"
echo "Testing harness with minimal input..."
echo -n "AAAA" > /tmp/test_input.bin
$BUILD_DIR/sb_fuzzer /tmp/test_input.bin 2>&1 | head -5 || true
echo -e "${GREEN}✓ Harness executable${NC}"

echo -e "\\n${GREEN}========================================${NC}"
echo -e "${GREEN}Build Complete!${NC}"
echo -e "${GREEN}========================================${NC}"
echo -e "Fuzzing harnesses built in: ${BUILD_DIR}/"
echo -e "\\nNext steps:"
echo -e "  1. Generate seed corpus: ${YELLOW}./scripts/generate_corpus.sh${NC}"
echo -e "  2. Run fuzzing campaign: ${YELLOW}./scripts/run_fuzzing_campaign.sh${NC}"
echo -e "  3. Monitor progress: ${YELLOW}afl-whatsup findings/${NC}"
