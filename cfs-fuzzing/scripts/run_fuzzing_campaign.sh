#!/bin/bash
#
# Launch AFL++ Fuzzing Campaign for cFS
#
# This script launches parallel fuzzing instances:
#  - 1 main fuzzer (deterministic mutations)
#  - 3 secondary fuzzers (random mutations)
#
# Usage:
#   ./run_fuzzing_campaign.sh [duration_hours]
#

set -e

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

# Configuration
HARNESS="../build/sb_fuzzer"
CORPUS="../corpus/sb_messages"
OUTPUT="../findings"
DURATION_HOURS=${1:-24}  # Default: 24 hours

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}cFS AFL++ Fuzzing Campaign${NC}"
echo -e "${GREEN}========================================${NC}"

# Pre-flight checks
if [ ! -f "$HARNESS" ]; then
    echo -e "${RED}Error: Harness not found at $HARNESS${NC}"
    echo -e "Run: ${YELLOW}./build_harnesses.sh${NC}"
    exit 1
fi

if [ ! -d "$CORPUS" ]; then
    echo -e "${RED}Error: Corpus not found at $CORPUS${NC}"
    echo -e "Run: ${YELLOW}./generate_corpus.sh${NC}"
    exit 1
fi

# AFL++ system configuration
echo -e "\\n${YELLOW}Configuring system for AFL++...${NC}"

# Check core pattern
if [ "$(cat /proc/sys/kernel/core_pattern)" != "core" ]; then
    echo -e "${YELLOW}⚠ Attempting to set core_pattern (may require sudo)${NC}"
    echo "core" | sudo tee /proc/sys/kernel/core_pattern > /dev/null 2>&1 || true
fi

# Check CPU scaling
if [ -d "/sys/devices/system/cpu/cpu0/cpufreq" ]; then
    echo -e "${YELLOW}⚠ Consider setting CPU governor to 'performance'${NC}"
    echo -e "  Run: sudo cpupower frequency-set -g performance"
fi

# Display configuration
echo -e "\\n${GREEN}Campaign Configuration:${NC}"
echo -e "  Target: $HARNESS"
echo -e "  Corpus: $CORPUS ($(ls $CORPUS | wc -l) seed files)"
echo -e "  Output: $OUTPUT"
echo -e "  Duration: $DURATION_HOURS hours"
echo -e "  Parallel fuzzers: 4 (1 main + 3 secondary)"

# Create output directory
mkdir -p "$OUTPUT"

# Check if AFL++ is available
if ! command -v afl-fuzz &> /dev/null; then
    echo -e "\\n${RED}Error: AFL++ not found in PATH${NC}"
    echo -e "Install AFL++:"
    echo -e "  git clone https://github.com/AFLplusplus/AFLplusplus"
    echo -e "  cd AFLplusplus && make && sudo make install"
    exit 1
fi

echo -e "\\n${GREEN}AFL++ Version:${NC}"
afl-fuzz -h 2>&1 | head -3

# Set AFL++ environment variables
export AFL_SKIP_CPUFREQ=1  # Skip CPU frequency check
export AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1  # For Docker/containers

echo -e "\\n${YELLOW}Launching fuzzing campaign...${NC}"
echo -e "${YELLOW}Press Ctrl+C in each terminal to stop fuzzers${NC}"

# Function to launch fuzzer in background
launch_fuzzer() {
    local fuzzer_name=$1
    local mode=$2  # -M for main, -S for secondary
    local extra_args=$3

    echo -e "\\n${GREEN}Starting $fuzzer_name...${NC}"

    # Launch in background with nohup for persistence
    nohup afl-fuzz $mode $fuzzer_name \\
        -i $CORPUS \\
        -o $OUTPUT \\
        $extra_args \\
        -- $HARNESS @@ \\
        > $OUTPUT/${fuzzer_name}.log 2>&1 &

    local pid=$!
    echo -e "${GREEN}✓ $fuzzer_name launched (PID: $pid)${NC}"
    echo $pid >> $OUTPUT/fuzzer_pids.txt
}

# Clear old PID file
rm -f $OUTPUT/fuzzer_pids.txt

# Launch main fuzzer (deterministic mutations)
launch_fuzzer "fuzzer_main" "-M" ""

# Wait a bit for main fuzzer to initialize
sleep 2

# Launch secondary fuzzers
launch_fuzzer "fuzzer_02" "-S" ""
launch_fuzzer "fuzzer_03" "-S" ""
launch_fuzzer "fuzzer_04" "-S" ""

echo -e "\\n${GREEN}========================================${NC}"
echo -e "${GREEN}Fuzzing Campaign Active${NC}"
echo -e "${GREEN}========================================${NC}"
echo -e "\\nMonitoring commands:"
echo -e "  ${YELLOW}afl-whatsup $OUTPUT${NC}        # Campaign statistics"
echo -e "  ${YELLOW}tail -f $OUTPUT/fuzzer_main.log${NC}  # Main fuzzer log"
echo -e "  ${YELLOW}watch -n 5 'ls -lh $OUTPUT/*/crashes'${NC}  # Watch for crashes"

echo -e "\\nTo stop all fuzzers:"
echo -e "  ${YELLOW}kill \$(cat $OUTPUT/fuzzer_pids.txt)${NC}"

echo -e "\\nFuzzing will run for approximately $DURATION_HOURS hours"
echo -e "Check progress periodically with: ${YELLOW}afl-whatsup $OUTPUT${NC}"

# Optional: Auto-stop after duration
if [ "$DURATION_HOURS" != "continuous" ]; then
    DURATION_SECONDS=$((DURATION_HOURS * 3600))
    echo -e "\\nCampaign will auto-stop in $DURATION_HOURS hours..."

    # Background job to stop fuzzing after duration
    (
        sleep $DURATION_SECONDS
        echo -e "\\n${YELLOW}Duration reached. Stopping fuzzers...${NC}"
        if [ -f "$OUTPUT/fuzzer_pids.txt" ]; then
            kill $(cat $OUTPUT/fuzzer_pids.txt) 2>/dev/null || true
            echo -e "${GREEN}Fuzzing campaign stopped${NC}"
        fi
    ) &
fi

echo -e "\\n${GREEN}Campaign launched successfully!${NC}"
echo -e "Fuzzer logs available in: $OUTPUT/*.log"
