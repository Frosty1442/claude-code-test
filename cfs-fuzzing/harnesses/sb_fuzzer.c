/**
 * @file sb_fuzzer.c
 * @brief AFL++ Fuzzing Harness for cFS Software Bus (SB)
 *
 * This harness demonstrates comprehensive fuzzing of the cFS Software Bus
 * message processing subsystem using AFL++ in persistent mode with sanitizers.
 *
 * Target APIs:
 *  - CFE_SB_ReceiveBuffer(): Receive messages from pipe
 *  - CFE_SB_TransmitBuffer(): Transmit messages
 *  - CFE_SB_Subscribe(): Subscribe to message IDs
 *
 * Build:
 *   gcc -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer \
 *       --coverage -o sb_fuzzer sb_fuzzer.c -I../cFS/cfe/modules/core_api/fsw/inc
 *
 * AFL++ Usage:
 *   afl-fuzz -i corpus/sb_messages -o findings -M fuzzer01 -- ./sb_fuzzer @@
 *
 * @note This is a proof-of-concept demonstrating the fuzzing approach.
 *       A complete implementation would link against actual cFS libraries.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

/*
 * cFS Software Bus Packet Structure (simplified for demonstration)
 * Based on CCSDS Space Packet Protocol
 */
typedef struct {
    uint16_t stream_id;      // CCSDS Stream ID (Version | Type | SecHdr | APID)
    uint16_t sequence;       // Sequence flags and count
    uint16_t length;         // Packet data length - 1
    uint16_t function_code;  // Command function code
    uint8_t  checksum;       // Packet checksum
    uint8_t  data[1024];     // Packet data payload
} CFE_SB_Packet_t;

typedef struct {
    uint32_t msg_id;
    uint16_t size;
    uint8_t *data_ptr;
} CFE_SB_Buffer_t;

/*
 * Simulated Software Bus functions that would normally be from cFS
 * In production, these would link to actual cFE libraries
 */
int32_t SB_ProcessPacket(const uint8_t *packet_data, size_t packet_len) {
    if (packet_data == NULL || packet_len == 0) {
        return -1;  // Invalid input
    }

    // Simulate parsing CCSDS packet header
    if (packet_len < sizeof(CFE_SB_Packet_t)) {
        return -2;  // Packet too small
    }

    CFE_SB_Packet_t *pkt = (CFE_SB_Packet_t *)packet_data;

    // Validate stream ID
    uint16_t app_id = pkt->stream_id & 0x07FF;
    if (app_id == 0 || app_id > 2047) {
        return -3;  // Invalid application ID
    }

    // Validate packet length
    uint16_t declared_len = pkt->length + 1;
    if (declared_len > packet_len - 6) {
        // Length mismatch - potential overflow!
        // ASAN should detect if we access beyond bounds
        return -4;
    }

    // Simulate processing packet data
    // This is where bugs might occur in real cFS code
    for (int i = 0; i < declared_len && i < sizeof(pkt->data); i++) {
        // Checksum calculation (simplified)
        pkt->checksum ^= pkt->data[i];
    }

    // Simulate command dispatch based on function code
    switch (pkt->function_code) {
        case 0x00:  // NOOP
            break;
        case 0x01:  // RESET
            // Reset counters
            break;
        case 0x02:  // START
            // Intentional bug for fuzzer to find
            if (pkt->data[0] == 'B' && pkt->data[1] == 'U' &&
                pkt->data[2] == 'G') {
                // Trigger crash - fuzzer should discover this
                char *crash = NULL;
                *crash = 0x42;  // NULL pointer dereference
            }
            break;
        default:
            return -5;  // Unknown command
    }

    return 0;  // Success
}

/*
 * AFL++ Persistent Mode Harness
 * Processes multiple inputs per fork for 10,000x performance gain
 */
int main(int argc, char **argv) {
    uint8_t buffer[4096];
    ssize_t bytes_read;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <input_file>\\n", argv[0]);
        fprintf(stderr, "  Or use with AFL++: afl-fuzz -i corpus -o findings -- %s @@\\n", argv[0]);
        return 1;
    }

#ifdef __AFL_HAVE_MANUAL_CONTROL
    // AFL++ persistent mode - 10,000 iterations per fork
    while (__AFL_LOOP(10000)) {
#endif
        // Read fuzzed input
        FILE *fp = fopen(argv[1], "rb");
        if (!fp) {
            perror("fopen");
            return 1;
        }

        bytes_read = fread(buffer, 1, sizeof(buffer), fp);
        fclose(fp);

        if (bytes_read > 0) {
            // Fuzz target: Process Software Bus packet
            int32_t result = SB_ProcessPacket(buffer, bytes_read);

            // Log processing result (for debugging, removed in production fuzzing)
            #ifdef DEBUG_FUZZER
            if (result < 0) {
                fprintf(stderr, "Packet processing error: %d\\n", result);
            }
            #endif
        }

#ifdef __AFL_HAVE_MANUAL_CONTROL
    }
#endif

    return 0;
}

/*
 * Fuzzing Corpus Generator
 *
 * This function generates seed inputs for the fuzzing campaign
 * to be placed in corpus/sb_messages/
 */
void generate_seed_corpus(const char *output_dir) {
    CFE_SB_Packet_t pkt;
    FILE *fp;
    char filename[256];

    // Seed 1: Valid telemetry packet
    memset(&pkt, 0, sizeof(pkt));
    pkt.stream_id = 0x0800;     // Telemetry packet
    pkt.sequence = 0xC000;      // First packet in sequence
    pkt.length = 15;            // 16 bytes of data
    pkt.function_code = 0x00;   // NOOP
    snprintf(filename, sizeof(filename), "%s/valid_telemetry.bin", output_dir);
    fp = fopen(filename, "wb");
    fwrite(&pkt, 1, 6 + pkt.length + 1, fp);
    fclose(fp);

    // Seed 2: Valid command packet
    memset(&pkt, 0, sizeof(pkt));
    pkt.stream_id = 0x1800;     // Command packet
    pkt.sequence = 0xC000;
    pkt.length = 7;             // 8 bytes of data
    pkt.function_code = 0x01;   // RESET
    snprintf(filename, sizeof(filename), "%s/valid_command.bin", output_dir);
    fp = fopen(filename, "wb");
    fwrite(&pkt, 1, 6 + pkt.length + 1, fp);
    fclose(fp);

    // Seed 3: Maximum size packet (boundary condition)
    memset(&pkt, 0, sizeof(pkt));
    pkt.stream_id = 0x0800;
    pkt.length = 1023;  // Max size
    pkt.function_code = 0x00;
    snprintf(filename, sizeof(filename), "%s/max_size.bin", output_dir);
    fp = fopen(filename, "wb");
    fwrite(&pkt, 1, sizeof(pkt), fp);
    fclose(fp);

    // Seed 4: Minimum size packet
    memset(&pkt, 0, sizeof(pkt));
    pkt.stream_id = 0x0800;
    pkt.length = 0;     // Minimum size
    snprintf(filename, sizeof(filename), "%s/min_size.bin", output_dir);
    fp = fopen(filename, "wb");
    fwrite(&pkt, 1, 6, fp);
    fclose(fp);

    printf("Generated seed corpus in %s\\n", output_dir);
}
