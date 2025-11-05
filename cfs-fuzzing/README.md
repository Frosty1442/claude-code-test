# AFL++ Fuzzing Infrastructure for NASA Core Flight System (cFS)

## Project Overview

This project implements comprehensive fuzz testing infrastructure for NASA's Core Flight System (cFS) using AFL++, AddressSanitizer (ASAN), UndefinedBehaviorSanitizer (UBSAN), and coverage-guided fuzzing techniques.

### Research Summary

Based on NASA's successful fuzzing research (IRAD project by Jose Martinez-Pedraza, Goddard GSFC):
- NASA successfully applied AFL and libFuzzer to the open-source cFS
- Implemented automated fuzzing process to periodically test cFS
- Fuzzing effectively mimics radiation-induced bit flips and uncovers hidden bugs
- Tools used: AFL, libFuzzer (both used by Google's OSS-Fuzz)

## Architecture Analysis

### cFS Core Components

**1. cFE (Core Flight Executive)** - `/cFS/cfe/modules/`
   - **Software Bus (SB)**: Inter-process message routing system
     - Key files: `cfe_sb_api.c`, `cfe_sb_dispatch.c`, `cfe_sb_buf.c`
     - High-value APIs: `CFE_SB_ReceiveBuffer()`, `CFE_SB_TransmitBuffer()`, `CFE_SB_Subscribe()`
   - **Table Services (TBL)**: Configuration table parsing and validation
     - Key files: `cfe_tbl_api.c`, `cfe_tbl_task_cmds.c`, `cfe_tbl_internal.c`
     - Fuzzing targets: Table file parsing, validation routines
   - **Event Services (EVS)**: Event logging and filtering
   - **File Services (FS)**: File I/O operations
   - **Executive Services (ES)**: Application management

**2. OSAL (Operating System Abstraction Layer)** - `/cFS/osal/`
   - Platform-independent OS interface
   - Critical for portability across missions

**3. PSP (Platform Support Package)** - `/cFS/psp/`
   - Hardware-specific implementations

## Fuzzing Strategy

### Compiler Flags & Instrumentation

**Primary Configuration (GCC + Sanitizers):**
```cmake
-g                                      # Debug symbols
-O1                                     # Light optimization
-fsanitize=address                      # AddressSanitizer
-fsanitize=undefined                    # UndefinedBehaviorSanitizer
-fsanitize-address-use-after-scope      # Enhanced ASAN
-fno-omit-frame-pointer                 # Better stack traces
--coverage                              # gcov coverage
-fprofile-arcs -ftest-coverage          # Arc profiling
```

**Linker Flags:**
```
-fsanitize=address,undefined -lgcov
```

### AFL++ Configuration

**Environment Variables:**
```bash
export AFL_USE_ASAN=1              # Enable Address Sanitizer
export AFL_USE_UBSAN=1             # Enable UB Sanitizer
export AFL_LLVM_LAF_ALL=1          # Comparison splitting
export AFL_LLVM_CMPLOG=1           # Input-to-state correlation
export AFL_INST_RATIO=100          # Full coverage instrumentation
```

**Compiler Selection Hierarchy:**
1. **Primary**: `afl-clang-lto` (LTO mode for LLVM 11+)
2. **Fallback**: `afl-clang-fast` (LLVM mode for Clang 3.8+)
3. **Alternative**: `afl-gcc-fast` (GCC plugin mode for GCC 5+)

### Build System Integration

**CMake Configuration:**
The fuzzing build modifies `/cFS/sample_defs/global_build_options.cmake` to inject:
- Sanitizer compilation flags
- Coverage instrumentation
- Debug symbols
- Optimized stack trace generation

**Key Configuration Files:**
- `fuzzing-toolchain.cmake`: AFL++ toolchain specification
- `global_build_options.cmake`: Mission-wide compiler flags
- `arch_build_custom.cmake`: Architecture-specific options

## High-Value Fuzzing Targets

### 1. Software Bus (SB) Message Processing

**Target Functions:**
```c
CFE_Status_t CFE_SB_ReceiveBuffer(CFE_SB_Buffer_t **BufPtr,
                                  CFE_SB_PipeId_t PipeId,
                                  int32 TimeOut)
CFE_Status_t CFE_SB_TransmitBuffer(CFE_SB_Buffer_t *BufPtr,
                                   bool IsOrigination)
```

**Fuzzing Rationale:**
- Handles all inter-process communication
- Parses CCSDS Space Packet Protocol headers
- Vulnerable to malformed message injection
- Critical for mission safety

**Input Corpus:**
- Valid CCSDS packets with proper headers
- Messages with boundary-case sizes
- Corrupted message IDs and routing info

### 2. Table Services (TBL) File Parsing

**Target Functions:**
```c
CFE_Status_t CFE_TBL_Load(CFE_TBL_Handle_t TblHandle,
                          CFE_TBL_SrcEnum_t SrcType,
                          const void *SrcDataPtr)
CFE_Status_t CFE_TBL_Validate(CFE_TBL_Handle_t TblHandle)
```

**Fuzzing Rationale:**
- Parses configuration tables from files
- File format vulnerabilities common attack vector
- Improper validation can corrupt mission data

**Input Corpus:**
- Valid cFE table binary files
- Malformed table headers
- Oversized/undersized table data

### 3. Command Ingest (ci_lab)

**Target**: Command packet reception and dispatch

**Fuzzing Rationale:**
- Processes external commands
- Command injection vulnerability surface
- Improper bounds checking can lead to crashes

## Fuzzing Harness Architecture

### Persistent Mode Harness Template

```c
#include <stdint.h>
#include <stddef.h>
#include "cfe.h"

// Persistent mode for 10,000x performance improvement
__AFL_FUZZ_INIT();

int main(int argc, char **argv) {
    // Initialize cFS subsystem
    CFE_ES_Main(argc, argv);

    #ifdef __AFL_HAVE_MANUAL_CONTROL
    __AFL_INIT();
    #endif

    unsigned char *buf = __AFL_FUZZ_TESTCASE_BUF;

    while (__AFL_LOOP(10000)) {
        int len = __AFL_FUZZ_TESTCASE_LEN;

        // Fuzz target function
        CFE_SB_ReceiveBuffer((CFE_SB_Buffer_t **)buf, pipe_id, 0);

        // Reset state for next iteration
        // ... cleanup code ...
    }

    return 0;
}
```

### Coverage-Guided Fuzzing

**Benefits:**
- Persistent mode: 10,000x faster than fork-exec
- Shared memory testcase delivery
- ASAN detects: buffer overflows, use-after-free, double-free
- UBSAN detects: integer overflows, null pointer dereferences
- gcov tracks: line coverage, branch coverage, function coverage

## Fuzzing Campaign Execution

### Parallel Fuzzing Setup

```bash
#!/bin/bash
# Launch 1 main + 3 secondary fuzzers

# Main fuzzer (deterministic mutations)
afl-fuzz -i corpus/sb_messages -o findings -M fuzzer01 \\
    -- ./cfs_sb_harness @@

# Secondary fuzzer with CMPLOG
afl-fuzz -i corpus/sb_messages -o findings -S fuzzer02 \\
    -c ./cfs_sb_harness_cmplog -- ./cfs_sb_harness @@

# Additional secondary fuzzers (random mutations)
afl-fuzz -i corpus/sb_messages -o findings -S fuzzer03 \\
    -- ./cfs_sb_harness @@
afl-fuzz -i corpus/sb_messages -o findings -S fuzzer04 \\
    -- ./cfs_sb_harness @@
```

### Monitoring Progress

```bash
# Real-time campaign statistics
afl-whatsup findings/

# Expected metrics:
# - execs/sec: 1000+ (persistent mode)
# - coverage: increasing over time
# - unique crashes: logged in findings/*/crashes/
# - hangs: logged in findings/*/hangs/
```

### Crash Analysis

```bash
# Reproduce crash with ASAN
AFL_USE_ASAN=1 ./cfs_sb_harness findings/fuzzer01/crashes/id:000000*

# Minimize crashing input
afl-tmin -i findings/fuzzer01/crashes/id:000000* \\
         -o minimized_crash.bin \\
         -- ./cfs_sb_harness @@

# Generate stack trace
gdb --args ./cfs_sb_harness minimized_crash.bin
(gdb) run
(gdb) bt full
```

## Expected Results

### Success Criteria

✅ **Infrastructure Setup**
- AFL++ core binaries operational
- cFS repository cloned with all submodules
- Build configuration supports sanitizers + coverage
- Fuzzing harnesses compile successfully

✅ **Fuzzing Campaign**
- Minimum 24-48 hours runtime per component
- Coverage metrics tracked via gcov/lcov
- Crash deduplication via AFL++
- ASAN reports for memory safety issues

✅ **Documentation**
- Build process fully documented
- Harness creation guidelines
- Reproduction steps for findings
- Integration with CI/CD (optional)

### Performance Targets

- **Executions/sec**: 1,000+ (persistent mode)
- **Code coverage**: 60%+ of target modules
- **Campaign duration**: 24-48 hours minimum
- **Parallel fuzzers**: 4-8 instances

## Corpus Preparation

### Software Bus Message Corpus

```
corpus/sb_messages/
├── valid_telemetry_packet.bin      # Standard telemetry
├── valid_command_packet.bin        # Standard command
├── max_size_packet.bin             # Boundary case
├── min_size_packet.bin             # Boundary case
├── zero_length_packet.bin          # Edge case
└── malformed_header.bin            # Invalid case
```

### Table Services Corpus

```
corpus/table_files/
├── valid_config_table.tbl
├── large_table.tbl
├── empty_table.tbl
├── corrupted_header.tbl
└── invalid_checksum.tbl
```

## Sanitizer Output Analysis

### AddressSanitizer Report Example

```
=================================================================
==12345==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x60300000eff4
    #0 0x7f8a2b4c5678 in CFE_SB_ProcessMessage cfe_sb_api.c:1234
    #1 0x7f8a2b4c5890 in CFE_SB_ReceiveBuffer cfe_sb_api.c:1339

0x60300000eff4 is located 4 bytes after 16-byte region
allocated by thread T0 here:
    #0 0x7f8a2c123456 in malloc
    #1 0x7f8a2b4c5432 in CFE_SB_AllocateBuffer cfe_sb_buf.c:89
```

### UndefinedBehaviorSanitizer Report Example

```
cfe_tbl_internal.c:456:23: runtime error: signed integer overflow:
2147483647 + 1 cannot be represented in type 'int'
    #0 0x7f8a2b123456 in CFE_TBL_ValidateTable cfe_tbl_internal.c:456
```

## Coverage Analysis

### Generating Coverage Reports

```bash
# Run fuzzing campaign with coverage
make ENABLE_UNIT_TESTS=true prep
make test

# Generate lcov report
lcov --capture --directory build-fuzz --output-file coverage.info
genhtml coverage.info --output-directory coverage_html

# View results
firefox coverage_html/index.html
```

## Integration with CI/CD

### GitHub Actions Example

```yaml
name: cFS Fuzzing
on: [push, pull_request]
jobs:
  fuzz:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
        with:
          submodules: recursive
      - name: Install AFL++
        run: |
          git clone https://github.com/AFLplusplus/AFLplusplus
          cd AFLplusplus && make && sudo make install
      - name: Build cFS with fuzzing
        run: |
          make SIMULATION=native prep
          make
      - name: Run short fuzzing campaign
        run: |
          timeout 1h afl-fuzz -i corpus -o findings -- ./harness @@
      - name: Upload crashes
        uses: actions/upload-artifact@v2
        with:
          name: crashes
          path: findings/*/crashes/
```

## References

### NASA cFS Documentation
- [cFE User's Guide](https://github.com/nasa/cFS/blob/gh-pages/cfe-usersguide.pdf)
- [OSAL API Guide](https://github.com/nasa/cFS/blob/gh-pages/osal-apiguide.pdf)
- [cFE App Developer's Guide](https://github.com/nasa/cFE/blob/main/docs/cFE%20Application%20Developers%20Guide.md)

### AFL++ Documentation
- [AFL++ Official Docs](https://aflplus.plus/docs/)
- [Fuzzing in Depth](https://aflplus.plus/docs/fuzzing_in_depth/)
- [Best Practices](https://aflplus.plus/docs/best_practices/)

### Security & Fuzzing Research
- NASA IRAD Fuzzing Project (Martinez-Pedraza, 2021)
- [Google OSS-Fuzz](https://google.github.io/oss-fuzz/)
- [LLVM libFuzzer](https://llvm.org/docs/LibFuzzer.html)

## Project Structure

```
cfs-fuzzing/
├── README.md                    # This file
├── harnesses/                   # Fuzzing harness source code
│   ├── sb_fuzzer.c             # Software Bus fuzzer
│   ├── tbl_fuzzer.c            # Table Services fuzzer
│   └── cmd_fuzzer.c            # Command Ingest fuzzer
├── corpus/                      # Seed input files
│   ├── sb_messages/
│   ├── table_files/
│   └── commands/
├── findings/                    # AFL++ output directory
│   └── (generated during fuzzing)
├── scripts/                     # Automation scripts
│   ├── build_harnesses.sh
│   ├── run_fuzzing_campaign.sh
│   └── analyze_crashes.sh
└── docs/                        # Additional documentation
    └── FUZZING_RESULTS.md

## License

This fuzzing infrastructure is provided for security research and testing purposes.
NASA's Core Flight System is licensed under Apache 2.0.

## Contributors

- Comprehensive fuzzing infrastructure designed based on NASA's successful AFL/libFuzzer research
- Build system integration with CMake and sanitizers
- Persistent-mode harness templates for high-performance fuzzing
