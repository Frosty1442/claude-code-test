#!/bin/bash
#
# Generate Seed Corpus for cFS Fuzzing Campaign
#

set -e

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${GREEN}Generating seed corpus for cFS fuzzing...${NC}"

# Create corpus directories
mkdir -p ../corpus/sb_messages
mkdir -p ../corpus/table_files
mkdir -p ../corpus/commands

# Generate Software Bus message samples
echo -e "${YELLOW}Creating Software Bus message samples...${NC}"

# Valid telemetry packet (CCSDS format)
printf '\x08\x00\xC0\x00\x00\x0F\x00\x00\x01\x02\x03\x04' > ../corpus/sb_messages/telemetry_valid.bin

# Valid command packet
printf '\x18\x00\xC0\x00\x00\x07\x01\x00\xAA\xBB\xCC\xDD' > ../corpus/sb_messages/command_valid.bin

# Maximum size packet (boundary condition)
printf '\x08\x00\xC0\x00\x03\xFF' > ../corpus/sb_messages/max_size.bin
dd if=/dev/zero bs=1024 count=1 >> ../corpus/sb_messages/max_size.bin 2>/dev/null

# Minimum size packet
printf '\x08\x00\xC0\x00\x00\x00' > ../corpus/sb_messages/min_size.bin

# Zero-length packet (edge case)
printf '\x08\x00\xC0\x00\xFF\xFF' > ../corpus/sb_messages/zero_length.bin

# Malformed stream ID
printf '\x00\x00\xC0\x00\x00\x07\x00\x00' > ../corpus/sb_messages/invalid_streamid.bin

# Corrupted sequence counter
printf '\x08\x00\xFF\xFF\x00\x07\x00\x00' > ../corpus/sb_messages/bad_sequence.bin

# Test packet that triggers bug (BUG command)
printf '\x18\x00\xC0\x00\x00\x0A\x02\x00BUG\x00\x00\x00\x00\x00' > ../corpus/sb_messages/trigger_bug.bin

echo -e "${GREEN}✓ Created $(ls ../corpus/sb_messages/ | wc -l) Software Bus seed files${NC}"

# Generate Table file samples
echo -e "${YELLOW}Creating Table file samples...${NC}"

# Valid table header
printf 'CFETBL\x00\x00\x00\x00\x00\x01\x00\x00\x00\x20' > ../corpus/table_files/valid_table.tbl
dd if=/dev/urandom bs=32 count=1 >> ../corpus/table_files/valid_table.tbl 2>/dev/null

# Empty table
printf 'CFETBL\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00' > ../corpus/table_files/empty_table.tbl

# Large table
printf 'CFETBL\x00\x00\x00\x00\x04\x00\x00\x00\x10\x00' > ../corpus/table_files/large_table.tbl
dd if=/dev/urandom bs=1024 count=1 >> ../corpus/table_files/large_table.tbl 2>/dev/null

# Corrupted magic number
printf 'BADTBL\x00\x00\x00\x00\x00\x01\x00\x00\x00\x20' > ../corpus/table_files/bad_magic.tbl

echo -e "${GREEN}✓ Created $(ls ../corpus/table_files/ | wc -l) Table file seed files${NC}"

# Generate command packet samples
echo -e "${YELLOW}Creating Command packet samples...${NC}"

# NOOP command
printf '\x18\x00\xC0\x00\x00\x01\x00\x00' > ../corpus/commands/noop.bin

# RESET command
printf '\x18\x00\xC0\x00\x00\x01\x01\x00' > ../corpus/commands/reset.bin

# START command
printf '\x18\x00\xC0\x00\x00\x01\x02\x00' > ../corpus/commands/start.bin

echo -e "${GREEN}✓ Created $(ls ../corpus/commands/ | wc -l) Command seed files${NC}"

# Summary
echo -e "\\n${GREEN}========================================${NC}"
echo -e "${GREEN}Corpus Generation Complete${NC}"
echo -e "${GREEN}========================================${NC}"
echo -e "Total seed files: $(find ../corpus -type f | wc -l)"
echo -e "\\nCorpus directories:"
echo -e "  - Software Bus messages: ../corpus/sb_messages/"
echo -e "  - Table files: ../corpus/table_files/"
echo -e "  - Commands: ../corpus/commands/"
echo -e "\\nNext step: ${YELLOW}./run_fuzzing_campaign.sh${NC}"
